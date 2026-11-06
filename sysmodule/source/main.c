// km-fan — Kontrol Merkezi fan modülü (arka plan / sysmodule)
//
// Çalışma mantığı:
//  * /config/kontrol-merkezi/fan.ini'deki modu okur, dosya değişince yeniden okur.
//  * Otomatik modda fana hiç dokunmaz: fanı sistemin kendi termal servisi (tc) yönetir.
//  * Diğer modlarda 100 ms'de bir SoC sıcaklığını okur ve fan seviyesini yazar.
//    Seviye hiçbir zaman güvenlik eğrisinin altına inmez (bkz. ortak/fan_egri.h).
//  * Sıcaklık okunamazsa ya da fan yazılamazsa fanı BIRAKIR: yazmayı keser ve
//    kontrol sisteme döner. Modül hiçbir hatada konsolu çökertmez.
#include <switch.h>
#include <sys/stat.h>
#include <time.h>

#include "fan_egri.h"

#define INNER_HEAP_SIZE 0x20000
#define DONGU_NS        100000000ULL   // 100 ms
#define AYAR_KONTROL_NS 1000000000ULL  // ayar dosyasının değişme zamanına saniyede bir bak
#define AYAR_ZORLA_NS   5000000000ULL  // FAT zaman çözünürlüğü 2 sn: 5 sn'de bir zorla oku
#define HATA_LIMITI     5              // üst üste 5 okuma hatası = fanı bırak

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

static char  g_heap[INNER_HEAP_SIZE];
static bool  g_hazir = false; // kritik servisler açıldı mı

void __libnx_initheap(void) {
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = g_heap;
    fake_heap_end   = g_heap + sizeof(g_heap);
}

void __appInit(void) {
    // libnx'in varsayılanı hata olursa çöker; burada çökmek yerine işaretleyip
    // main()'den sessizce çıkıyoruz. Böylece fan sistemin kontrolünde kalır.
    if (R_FAILED(smInitialize())) return;

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    const bool fs  = R_SUCCEEDED(fsInitialize()) && R_SUCCEEDED(fsdevMountSdmc());
    const bool fan = R_SUCCEEDED(fanInitialize());
    const bool ts  = R_SUCCEEDED(tsInitialize());
    g_hazir = fs && fan && ts;

    smExit();
}

void __appExit(void) {
    tsExit();
    fanExit();
    fsdevUnmountAll();
    fsExit();
}

// ─── Günlük (sadece olaylar; her döngüde yazmaz, SD'yi yormaz) ───────────────
static void gunluk(const char* mesaj) {
    FILE* f = fopen(KMFAN_GUNLUK, "a");
    if (!f) return;
    fprintf(f, "%s\n", mesaj);
    fclose(f);
}

static time_t ayarZamani(void) {
    struct stat st;
    return stat(KMFAN_AYAR, &st) == 0 ? st.st_mtime : 0;
}

// ─── Sıcaklık ────────────────────────────────────────────────────────────────
static TsSession g_ts;
static bool g_tsAcik = false;

static bool socSicakligi(float* c) {
    if (hosversionAtLeast(10, 0, 0)) {
        if (!g_tsAcik) g_tsAcik = R_SUCCEEDED(tsOpenSession(&g_ts, TsDeviceCode_LocationExternal));
        if (!g_tsAcik) return false;
        if (R_FAILED(tsSessionGetTemperature(&g_ts, c))) {
            tsSessionClose(&g_ts);
            g_tsAcik = false;
            return false;
        }
    } else {
        s32 mc = 0;
        if (R_FAILED(tsGetTemperatureMilliC(TsLocation_External, &mc))) return false;
        *c = mc / 1000.0f;
    }
    // Mantıksız değerleri hata say (sensör takılması vb.)
    return *c > 0.0f && *c < 120.0f;
}

// ─── Fan ─────────────────────────────────────────────────────────────────────
static FanController g_fan;
static bool  g_fanAcik = false;
static float g_seviye  = -1.0f;

static void faniBirak(float t) {
    if (!g_fanAcik) return;
    // Sisteme devretmeden önce makul bir değere çek: dengeli eğri ya da güvenlik
    // tabanı (hangisi büyükse). Sistem bir sonraki ayarında kendi değerini yazar.
    if (t > 0.0f) {
        const KmFanAyar dengeli = { KMFAN_DENGELI, 0 };
        fanControllerSetRotationSpeedLevel(&g_fan, kmfanHedef(&dengeli, t));
    }
    fanControllerClose(&g_fan);
    g_fanAcik = false;
    g_seviye = -1.0f;
}

static void faniAyarla(float hedef, float t) {
    // Yumuşak geçiş: yükselirken hızlı, düşerken yavaş (gürültü dalgalanmasın)
    if (g_seviye < 0.0f) g_seviye = hedef;
    else if (hedef > g_seviye) g_seviye = (g_seviye + 0.10f < hedef) ? g_seviye + 0.10f : hedef;
    else g_seviye = (g_seviye - 0.02f > hedef) ? g_seviye - 0.02f : hedef;

    // Güvenlik tabanı yumuşatmayı beklemez
    const float taban = kmfanGuvenlik(t);
    if (g_seviye < taban) g_seviye = taban;

    if (!g_fanAcik) g_fanAcik = R_SUCCEEDED(fanOpenController(&g_fan, KMFAN_FAN_KODU));
    if (!g_fanAcik) return;
    if (R_FAILED(fanControllerSetRotationSpeedLevel(&g_fan, g_seviye))) faniBirak(-1.0f);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    if (!g_hazir) return 0; // servis yoksa hiçbir şey yapma, fan sistemde kalır

    remove(KMFAN_GUNLUK);
    gunluk("km-fan basladi");

    KmFanAyar ayar;
    kmfanAyarOku(&ayar);
    time_t sonZaman = ayarZamani();
    int oncekiMod = -1;
    int hata = 0;
    float sonT = -1.0f;
    u64 sonKontrol = armGetSystemTick();
    u64 sonZorla = sonKontrol;

    while (true) {
        const u64 simdi = armGetSystemTick();
        if (armTicksToNs(simdi - sonKontrol) >= AYAR_KONTROL_NS) {
            sonKontrol = simdi;
            const time_t z = ayarZamani();
            const bool zorla = armTicksToNs(simdi - sonZorla) >= AYAR_ZORLA_NS;
            if (z != sonZaman || zorla) {
                sonZaman = z;
                if (zorla) sonZorla = simdi;
                kmfanAyarOku(&ayar);
            }
        }
        if (ayar.mod != oncekiMod) {
            char m[64];
            snprintf(m, sizeof(m), "mod: %s (sabit %d)", kmfanModAdi(ayar.mod), ayar.sabitYuzde);
            gunluk(m);
            oncekiMod = ayar.mod;
        }

        if (ayar.mod == KMFAN_OTOMATIK) {
            faniBirak(sonT);
            svcSleepThread(AYAR_KONTROL_NS); // otomatikte saniyede bir uyan
            continue;
        }

        float t = 0.0f;
        if (!socSicakligi(&t)) {
            if (++hata == HATA_LIMITI) {
                gunluk("sicaklik okunamiyor, fan sisteme birakildi");
                faniBirak(sonT);
            }
            svcSleepThread(DONGU_NS * 5);
            continue;
        }
        if (hata >= HATA_LIMITI) gunluk("sicaklik tekrar okunuyor");
        hata = 0;
        sonT = t;

        faniAyarla(kmfanHedef(&ayar, t), t);
        svcSleepThread(DONGU_NS);
    }
    return 0;
}
