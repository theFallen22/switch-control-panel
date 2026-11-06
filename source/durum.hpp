// Kontrol Merkezi — sistem durumu, servisler, sys-clk profilleri ve yedekleme
// Switch Lite (Mariko / Hoag) için yazıldı. Konsol hiçbir zaman dock'ta olmadığı
// için profiller sadece "handheld" satırına yazılır; şarj profilleri sys-clk'in
// kendi geri düşme sırasıyla (şarj → handheld) aynı değerleri kullanır.
#pragma once

#include <tesla.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <strings.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
#include <sysclk.h>
#include <sysclk/client/ipc.h>
}
#include "ag_ayar.h"
#include "fan_egri.h"
#include "pwm_fan.h"

namespace km {

// ─── Sabitler ────────────────────────────────────────────────────────────────
inline constexpr const char* SURUM          = APP_VERSION;
inline constexpr const char* SYSCLK_CONFIG  = "sdmc:/config/sys-clk/config.ini";
inline constexpr const char* KM_DIZIN       = "sdmc:/config/kontrol-merkezi/";
inline constexpr const char* YEDEK_DIZIN    = "sdmc:/config/kontrol-merkezi/yedek/";
inline constexpr const char* DURUM_DOSYASI  = "sdmc:/config/kontrol-merkezi/durum.ini";
inline constexpr const char* BILDIRIM_DIZIN = "sdmc:/config/ultrahand/notifications/";
inline constexpr const char* OVERLAY_DIZIN  = "sdmc:/switch/.overlays/";
inline constexpr const char* AYAR_DOSYASI   = "sdmc:/config/kontrol-merkezi/ayarlar.ini";
inline constexpr const char* PIL_GUNLUGU    = "sdmc:/config/kontrol-merkezi/pil-gunlugu.csv";
inline constexpr const char* ALBUM_DIZIN    = "sdmc:/Nintendo/Album";
inline constexpr float LITE_PIL_WH          = 13.6f; // Switch Lite: 3570 mAh × ~3.8 V
inline constexpr int GECMIS_UZUNLUK         = 60;    // grafikler: son 60 sn
inline constexpr u64 QLAUNCH_TID            = 0x0100000000001000ULL;
inline constexpr int MAKS_YEDEK             = 10;

// "Güvenli" üst sınırlar: bunların üstü overclock sayılır ve onay ister.
// CPU 1224 = sdev değeri, GPU 460 = sys-clk handheld tavanı, MEM 1600 = resmi dock değeri.
inline constexpr u32 GUVENLI_MHZ[SysClkModule_EnumMax] = { 1224, 460, 1600 };

// sys-clk eski sürümse ya da frekans listesini veremezse kullanılan tablo (MHz)
inline const std::vector<u32> YEDEK_FREKANS[SysClkModule_EnumMax] = {
    { 612, 714, 816, 918, 1020, 1122, 1224, 1326, 1428, 1581, 1683, 1785 },
    { 76, 153, 230, 307, 384, 460, 537, 614, 691, 768, 844, 921 },
    { 665, 800, 1065, 1331, 1600 },
};

// ─── Hazır profiller ─────────────────────────────────────────────────────────
struct OnAyar {
    const char* ad;
    const char* aciklama;
    u32 mhz[SysClkModule_EnumMax]; // CPU, GPU, MEM (0 = stok)
};

enum ProfilTuru : int { PT_PIL = 0, PT_DENGELI = 1, PT_PERF = 2, PT_OZEL = 3, PT_BILINMIYOR = 4 };

inline const OnAyar ONAYARLAR[3] = {
    { "Pil Tasarrufu", "Hafif / 2D oyunlar, en uzun pil",          { 816, 307, 1065 } },
    { "Dengeli",       "Stok ayarlar, sys-clk dokunmaz",             { 0,   0,   0    } },
    { "Performans",    "Ağır oyunlar, güvenli sınırda hafif artış", { 1224, 460, 1600 } },
};

inline const char* profilAdi(int t) {
    switch (t) {
        case PT_PIL:     return "Pil Tasarrufu";
        case PT_DENGELI: return "Dengeli";
        case PT_PERF:    return "Performans";
        case PT_OZEL:    return "Özel";
        default:         return "-";
    }
}

// ─── Servisler ───────────────────────────────────────────────────────────────
struct Servisler {
    bool sysclk = false;    // sys-clk çalışıyor ve API uyumlu
    bool sysclkVar = false; // servis var ama API uyumsuz olabilir
    u32  sysclkApi = 0;
    char sysclkSurum[32] = {};
    bool psm = false, nifm = false, ns = false, lbl = false, pdm = false;
    bool pwm = false, pmshell = false;
};
inline Servisler srv;

inline void servisleriBaslat() {
    tsl::hlp::doWithSmSession([] {
        if (sysclkIpcRunning()) {
            srv.sysclkVar = true;
            if (R_SUCCEEDED(sysclkIpcInitialize())) {
                u32 api = 0;
                if (R_SUCCEEDED(sysclkIpcGetAPIVersion(&api))) srv.sysclkApi = api;
                if (api == SYSCLK_IPC_API_VERSION) {
                    srv.sysclk = true;
                    sysclkIpcGetVersionString(srv.sysclkSurum, sizeof(srv.sysclkSurum));
                } else {
                    sysclkIpcExit(); // farklı yapı boyutları = çöp veri; hiç dokunma
                }
            }
        }
        srv.psm  = R_SUCCEEDED(psmInitialize());
        srv.nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        srv.ns   = R_SUCCEEDED(nsInitialize());
        srv.lbl  = R_SUCCEEDED(lblInitialize());
        srv.pdm  = R_SUCCEEDED(pdmqryInitialize());
        srv.pwm  = pwmFanAc(KMFAN_FAN_KODU);
        srv.pmshell = R_SUCCEEDED(pmshellInitialize());
    });
}

inline void servisleriKapat() {
    if (srv.pmshell) pmshellExit();
    if (srv.pwm)    pwmFanKapat();
    if (srv.pdm)    pdmqryExit();
    if (srv.lbl)    lblExit();
    if (srv.ns)     nsExit();
    if (srv.nifm)   nifmExit();
    if (srv.psm)    psmExit();
    if (srv.sysclk) sysclkIpcExit();
    srv = Servisler{};
}

// ─── Küçük yardımcılar ───────────────────────────────────────────────────────
inline void bildir(const std::string& mesaj) {
    if (tsl::notification) tsl::notification->show(mesaj, 22);
}

inline std::string tidMetni(u64 tid) {
    char b[17];
    snprintf(b, sizeof(b), "%016lX", tid);
    return b;
}

inline bool dosyaVar(const std::string& yol) {
    struct stat st;
    return stat(yol.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline void dizinOlustur(const char* yol) {
    // yol "sdmc:/a/b/c/" biçiminde; her seviyeyi sırayla oluştur
    std::string s(yol);
    for (size_t i = strlen("sdmc:/"); i < s.size(); ++i) {
        if (s[i] == '/') mkdir(s.substr(0, i).c_str(), 0777);
    }
}

inline bool dosyaKopyala(const std::string& kaynak, const std::string& hedef) {
    FILE* in = fopen(kaynak.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(hedef.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    return ok;
}

inline std::string zamanDamgasi() {
    time_t t = time(nullptr);
    struct tm* lt = localtime(&t);
    char b[20];
    strftime(b, sizeof(b), "%Y%m%d-%H%M%S", lt);
    return b;
}

inline std::string mhzMetni(u32 mhz) {
    if (mhz == 0) return "Stok";
    return std::to_string(mhz) + " MHz";
}

// ─── Oyun adları ─────────────────────────────────────────────────────────────
inline std::map<u64, std::string> isimOnbellek;

inline std::string oyunAdi(u64 tid) {
    if (tid == 0 || tid == QLAUNCH_TID) return "Ana Menü";
    auto it = isimOnbellek.find(tid);
    if (it != isimOnbellek.end()) return it->second;

    std::string ad = tidMetni(tid);
    if (srv.ns) {
        auto* veri = static_cast<NsApplicationControlData*>(malloc(sizeof(NsApplicationControlData)));
        if (veri) {
            u64 boyut = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, veri, sizeof(*veri), &boyut))
                && boyut >= sizeof(NacpStruct)) {
                NacpLanguageEntry* dil = nullptr;
                // nacpGetLanguageEntry hem eski hem sıkıştırılmış (yeni FW) başlık formatını çözer
                if (R_SUCCEEDED(nacpGetLanguageEntry(&veri->nacp, &dil)) && dil && dil->name[0]) {
                    ad = dil->name;
                }
            }
            free(veri);
        }
    }
    isimOnbellek[tid] = ad;
    return ad;
}

// ─── Ayarlar ─────────────────────────────────────────────────────────────────
struct Ayarlar {
    bool isiUyarisi = true; // panel açıkken SoC eşiği aşarsa uyar
    u32  isiEsigi   = 70;   // °C
    bool ocOnayi    = true; // overclock frekanslar iki kez A ister
};
inline Ayarlar ayar;
inline constexpr u32 ESIK_SECENEKLERI[] = { 60, 65, 70, 75, 80 };

inline void ayarlariOku() {
    FILE* f = fopen(AYAR_DOSYASI, "r");
    if (!f) return;
    char satir[96];
    while (fgets(satir, sizeof(satir), f)) {
        char* eq = strchr(satir, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* k = satir;
        const long v = strtol(eq + 1, nullptr, 10);
        if (!strcmp(k, "isi_uyarisi")) ayar.isiUyarisi = v != 0;
        else if (!strcmp(k, "isi_esigi") && v >= 50 && v <= 90) ayar.isiEsigi = static_cast<u32>(v);
        else if (!strcmp(k, "oc_onayi")) ayar.ocOnayi = v != 0;
    }
    fclose(f);
}

inline void ayarlariYaz() {
    dizinOlustur(KM_DIZIN);
    FILE* f = fopen(AYAR_DOSYASI, "w");
    if (!f) return;
    fprintf(f, "[ayarlar]\nisi_uyarisi=%d\nisi_esigi=%u\noc_onayi=%d\n", ayar.isiUyarisi ? 1 : 0, ayar.isiEsigi, ayar.ocOnayi ? 1 : 0);
    fclose(f);
}

// ─── Geçmiş (grafikler için halka tampon) ────────────────────────────────────
struct Gecmis {
    float v[GECMIS_UZUNLUK] = {};
    int n = 0, bas = 0;
    void ekle(float x) {
        v[(bas + n) % GECMIS_UZUNLUK] = x;
        if (n < GECMIS_UZUNLUK) ++n; else bas = (bas + 1) % GECMIS_UZUNLUK;
    }
    float al(int i) const { return v[(bas + i) % GECMIS_UZUNLUK]; } // 0 = en eski
    float son() const { return n ? al(n - 1) : 0.f; }
    float enAz() const { float m = n ? al(0) : 0.f; for (int i = 1; i < n; ++i) m = std::min(m, al(i)); return m; }
    float enCok() const { float m = n ? al(0) : 0.f; for (int i = 1; i < n; ++i) m = std::max(m, al(i)); return m; }
    float ort() const { float t = 0; for (int i = 0; i < n; ++i) t += al(i); return n ? t / n : 0.f; }
};
inline Gecmis gecmisSicaklik; // °C
inline Gecmis gecmisGuc;      // W (pil çekişi, pozitif = tüketim)
inline Gecmis gecmisCpu;      // MHz
inline Gecmis gecmisGpu;      // MHz
inline Gecmis gecmisFan;      // %

// ─── Anlık sistem durumu ─────────────────────────────────────────────────────
struct Anlik {
    bool ctxOk = false;
    SysClkContext ctx{};

    bool psmOk = false;
    u32 batarya = 0;
    PsmChargerType sarj = PsmChargerType_Unconnected;

    bool netOk = false;
    NifmInternetConnectionType netTip{};
    NifmInternetConnectionStatus netDurum{};
    u32 sinyal = 0; // 0..3
    char ssid[0x22] = {};

    bool sdOk = false;
    s64 sdBos = 0, sdToplam = 0;

    bool pilOk = false;
    PsmBatteryChargeInfoFields pil{};

    bool icOk = false;
    s64 icBos = 0, icToplam = 0; // dahili hafıza (kullanıcı bölümü)

    u32 ip = 0;

    bool fanOk = false;
    double fanYuzde = 0.0;

    u64 tid = 0;     // şu an açık uygulama (yoksa 0)
    u64 sonOyun = 0; // en son görülen oyun
    int profil = 4;  // tid (yoksa sonOyun) için profil türü; saniyede bir yenilenir
};
inline Anlik anlik;
inline u64 sonGuncelleme = 0;
inline u32 guncellemeSayaci = 0;

// ─── Oynama istatistikleri (pdm) ─────────────────────────────────────────────
struct Oynama { bool ok = false; u64 dakika = 0; u32 acilis = 0; };
inline std::map<u64, Oynama> oynamaOnbellek;

inline Oynama oynamaBilgisi(u64 tid) {
    if (!tid || !srv.pdm) return {};
    auto it = oynamaOnbellek.find(tid);
    if (it != oynamaOnbellek.end()) return it->second;
    Oynama o;
    PdmPlayStatistics st{};
    if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(tid, false, &st))) {
        o.ok = true;
        o.dakika = st.playtime / 60000000000ULL;
        o.acilis = st.total_launches;
    }
    oynamaOnbellek[tid] = o;
    return o;
}

inline std::string sureMetni(u64 dakika) {
    if (dakika < 60) return std::to_string(dakika) + " dk";
    const u64 sa = dakika / 60, dk = dakika % 60;
    return dk ? std::to_string(sa) + " sa " + std::to_string(dk) + " dk" : std::to_string(sa) + " sa";
}

inline u64 sonOyunuOku() {
    FILE* f = fopen(DURUM_DOSYASI, "r");
    if (!f) return 0;
    char satir[64] = {};
    u64 tid = 0;
    while (fgets(satir, sizeof(satir), f)) {
        if (strncmp(satir, "son_oyun=", 9) == 0) tid = strtoull(satir + 9, nullptr, 16);
    }
    fclose(f);
    return tid;
}

inline std::string oyunAdi(u64 tid);

inline void sonOyunuYaz(u64 tid) {
    dizinOlustur(KM_DIZIN);
    const std::string ad = oyunAdi(tid); // dosyayı açmadan önce: ns çağrısı uzun sürebilir
    FILE* f = fopen(DURUM_DOSYASI, "w");
    if (!f) return;
    fprintf(f, "[durum]\nson_oyun=%016lX\nson_oyun_adi=%s\n", tid, ad.c_str());
    fclose(f);
}

inline u64 acikUygulama() {
    u64 pid = 0, tid = 0;
    if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && R_SUCCEEDED(pmdmntGetProgramId(&tid, pid))) return tid;
    return 0;
}

inline int profilTuru(u64 tid);

inline void guncelle(bool zorla = false) {
    const u64 simdi = armGetSystemTick();
    if (!zorla && armTicksToNs(simdi - sonGuncelleme) < 1000000000ULL) return; // saniyede bir
    sonGuncelleme = simdi;
    const bool agirTur = zorla || (guncellemeSayaci++ % 5 == 0);        // 5 sn'de bir

    if (srv.sysclk) anlik.ctxOk = R_SUCCEEDED(sysclkIpcGetCurrentContext(&anlik.ctx));

    if (srv.psm) {
        anlik.psmOk = R_SUCCEEDED(psmGetBatteryChargePercentage(&anlik.batarya));
        psmGetChargerType(&anlik.sarj);
    }

    if (srv.nifm) {
        u32 guc = 0;
        anlik.netOk = R_SUCCEEDED(nifmGetInternetConnectionStatus(&anlik.netTip, &guc, &anlik.netDurum));
        anlik.sinyal = anlik.netOk ? guc : 0;
        if (anlik.netOk && agirTur) {
            NifmNetworkProfileData profil{};
            if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&profil))) {
                const auto& w = profil.wireless_setting_data;
                size_t n = std::min<size_t>(w.ssid_len, sizeof(anlik.ssid) - 1);
                memcpy(anlik.ssid, w.ssid, n);
                anlik.ssid[n] = '\0';
            }
        }
        if (!anlik.netOk) anlik.ssid[0] = '\0';
    }

    if (srv.pwm) {
        anlik.fanOk = pwmFanYuzde(&anlik.fanYuzde);
        if (anlik.fanOk) gecmisFan.ekle(static_cast<float>(anlik.fanYuzde));
    }
    if (srv.psm && agirTur) anlik.pilOk = R_SUCCEEDED(psmGetBatteryChargeInfoFields(&anlik.pil));
    if (srv.nifm && agirTur) {
        if (!anlik.netOk || R_FAILED(nifmGetCurrentIpAddress(&anlik.ip))) anlik.ip = 0;
    }
    if (srv.ns && agirTur) {
        anlik.icOk = R_SUCCEEDED(nsGetFreeSpaceSize(NcmStorageId_BuiltInUser, &anlik.icBos))
                  && R_SUCCEEDED(nsGetTotalSpaceSize(NcmStorageId_BuiltInUser, &anlik.icToplam));
    }

    if (anlik.ctxOk) {
        const auto& c = anlik.ctx;
        const u32 soc = c.temps[SysClkThermalSensor_SOC];
        gecmisSicaklik.ekle(soc / 1000.0f);
        gecmisGuc.ekle(-c.power[SysClkPowerSensor_Now] / 1000.0f);
        gecmisCpu.ekle((c.realFreqs[SysClkModule_CPU] ? c.realFreqs[SysClkModule_CPU] : c.freqs[SysClkModule_CPU]) / 1000000.0f);
        gecmisGpu.ekle((c.realFreqs[SysClkModule_GPU] ? c.realFreqs[SysClkModule_GPU] : c.freqs[SysClkModule_GPU]) / 1000000.0f);

        // Isı uyarısı: dakikada en fazla bir kez
        static u64 sonUyari = 0;
        if (ayar.isiUyarisi && soc >= ayar.isiEsigi * 1000
            && (sonUyari == 0 || armTicksToNs(simdi - sonUyari) > 60000000000ULL)) {
            sonUyari = simdi;
            if (tsl::notification) tsl::notification->show("SoC " + std::to_string(soc / 1000) + "°C! Daha hafif bir profil dene.", 22);
        }
    }

    if (agirTur) {
        if (FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc")) {
            anlik.sdOk = R_SUCCEEDED(fsFsGetFreeSpace(fs, "/", &anlik.sdBos))
                      && R_SUCCEEDED(fsFsGetTotalSpace(fs, "/", &anlik.sdToplam));
        }
    }

    u64 tid = anlik.ctxOk ? anlik.ctx.applicationId : acikUygulama();
    if (tid == QLAUNCH_TID) tid = 0;
    anlik.tid = tid;
    if (tid && tid != anlik.sonOyun) {
        anlik.sonOyun = tid;
        sonOyunuYaz(tid);
    }
    const u64 hedef = tid ? tid : anlik.sonOyun;
    anlik.profil = (srv.sysclk && hedef) ? profilTuru(hedef) : 4;
}

// ─── Pil: kalan süre tahmini ─────────────────────────────────────────────────
// Pil sağlığı ve son ortalama tüketimden kaba bir tahmin. Şarjda ya da veri yoksa -1.
inline int kalanDakika() {
    if (!anlik.ctxOk || !anlik.psmOk || anlik.sarj != PsmChargerType_Unconnected) return -1;
    const int mw = std::abs(anlik.ctx.power[SysClkPowerSensor_Avg]);
    if (mw < 300) return -1;
    float saglik = anlik.pilOk ? anlik.pil.battery_age_percentage / 100000.0f : 1.0f;
    saglik = std::clamp(saglik, 0.5f, 1.0f);
    const float wh = LITE_PIL_WH * saglik * anlik.batarya / 100.0f;
    return static_cast<int>(wh / (mw / 1000.0f) * 60.0f);
}

// ─── Pil günlüğü (uyku tüketimini ölçmek için) ───────────────────────────────
// Panel her açıldığında yüzde + zaman kaydedilir. İki kayıt arasındaki düşüş,
// özellikle uykudan önce ve sonra açınca, uyku modundaki tüketimi gösterir.
struct PilKaydi { s64 zaman; u32 yuzde; bool sarj; };

inline std::vector<PilKaydi> pilGunlugunuOku() {
    std::vector<PilKaydi> v;
    FILE* f = fopen(PIL_GUNLUGU, "r");
    if (!f) return v;
    long long t; unsigned y; int s;
    while (fscanf(f, "%lld,%u,%d\n", &t, &y, &s) == 3) v.push_back({ static_cast<s64>(t), y, s != 0 });
    fclose(f);
    return v;
}

inline void pilKaydiEkle() {
    if (!srv.psm || !anlik.psmOk) return;
    auto v = pilGunlugunuOku();
    const s64 simdi = static_cast<s64>(time(nullptr));
    const bool sarj = anlik.sarj != PsmChargerType_Unconnected;
    // Gürültüyü önle: son kayıttan bu yana 5 dk geçmediyse ve yüzde aynıysa yazma
    if (!v.empty() && simdi - v.back().zaman < 300 && v.back().yuzde == anlik.batarya) return;
    v.push_back({ simdi, anlik.batarya, sarj });
    if (v.size() > 40) v.erase(v.begin(), v.end() - 40);
    dizinOlustur(KM_DIZIN);
    FILE* f = fopen(PIL_GUNLUGU, "w");
    if (!f) return;
    for (const auto& k : v) fprintf(f, "%lld,%u,%d\n", static_cast<long long>(k.zaman), k.yuzde, k.sarj ? 1 : 0);
    fclose(f);
}

// ─── Albüm (ekran görüntüleri ve videolar) ───────────────────────────────────
struct AlbumBilgi { u32 resim = 0, video = 0; s64 bayt = 0; };

inline void albumTara(const std::string& yol, AlbumBilgi& b, int derinlik = 0) {
    if (derinlik > 5) return;
    DIR* d = opendir(yol.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string tam = yol + "/" + e->d_name;
        if (e->d_type == DT_DIR) { albumTara(tam, b, derinlik + 1); continue; }
        const size_t n = strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".jpg")) ++b.resim;
        else if (n > 4 && !strcasecmp(e->d_name + n - 4, ".mp4")) ++b.video;
        else continue;
        struct stat st;
        if (stat(tam.c_str(), &st) == 0) b.bayt += st.st_size;
    }
    closedir(d);
}

inline std::string ipMetni(u32 ip) {
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return b;
}

inline std::string gbMetni(s64 bayt) {
    char b[24];
    const double gb = bayt / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 10) snprintf(b, sizeof(b), "%.0f GB", gb);
    else if (gb >= 1) snprintf(b, sizeof(b), "%.1f GB", gb);
    else snprintf(b, sizeof(b), "%.0f MB", bayt / (1024.0 * 1024.0));
    return b;
}

// ─── Fan modülü (km-fan) ─────────────────────────────────────────────────────
inline KmFanAyar fanAyari{ KMFAN_OTOMATIK, 60 };

inline void fanAyariOku() { kmfanAyarOku(&fanAyari); }

inline bool fanAyariYaz() {
    dizinOlustur(KM_DIZIN);
    return kmfanAyarYaz(&fanAyari) != 0;
}

inline bool fanModuluKurulu() { return dosyaVar(KMFAN_MODUL); }

inline bool fanModuluCalisiyor() {
    u64 pid = 0;
    return R_SUCCEEDED(pmdmntGetProcessId(&pid, KMFAN_TID)) && pid != 0;
}

inline bool fanModuluBaslat() {
    if (!srv.pmshell || !fanModuluKurulu()) return false;
    if (fanModuluCalisiyor()) return true;
    NcmProgramLocation konum{};
    konum.program_id = KMFAN_TID;
    konum.storageID = NcmStorageId_None;
    u64 pid = 0;
    return R_SUCCEEDED(pmshellLaunchProgram(0, &konum, &pid));
}

// Önce otomatiğe alır (modül fanı sisteme devreder), sonra kapatır
inline bool fanModuluDurdur() {
    if (!srv.pmshell || !fanModuluCalisiyor()) return true;
    fanAyari.mod = KMFAN_OTOMATIK;
    fanAyariYaz();
    svcSleepThread(1500000000ULL);
    return R_SUCCEEDED(pmshellTerminateProgram(KMFAN_TID));
}

inline bool fanAcilistaBasliyor() { return dosyaVar(KMFAN_BOOT_FLAG); }

inline void fanAcilistaBaslat(bool acik) {
    if (acik) {
        dizinOlustur("sdmc:/atmosphere/contents/" KMFAN_TID_STR "/flags/");
        if (FILE* f = fopen(KMFAN_BOOT_FLAG, "w")) fclose(f);
    } else {
        remove(KMFAN_BOOT_FLAG);
    }
}

// ─── Mac bağlantısı modülü (km-ag) ───────────────────────────────────────────
inline KmAgAyar agAyari{};

inline void agAyariOku() { kmagAyarOku(&agAyari); }

inline bool agAyariYaz() {
    dizinOlustur(KM_DIZIN);
    return kmagAyarYaz(&agAyari) != 0;
}

inline void yeniPin() {
    u32 r = 0;
    randomGet(&r, sizeof(r));
    snprintf(agAyari.pin, sizeof(agAyari.pin), "%06u", r % 1000000u);
}

inline bool agModuluKurulu() { return dosyaVar(KMAG_MODUL); }

inline bool agModuluCalisiyor() {
    u64 pid = 0;
    return R_SUCCEEDED(pmdmntGetProcessId(&pid, KMAG_TID)) && pid != 0;
}

inline bool agModuluBaslat() {
    if (!srv.pmshell || !agModuluKurulu()) return false;
    if (agModuluCalisiyor()) return true;
    NcmProgramLocation konum{};
    konum.program_id = KMAG_TID;
    konum.storageID = NcmStorageId_None;
    u64 pid = 0;
    return R_SUCCEEDED(pmshellLaunchProgram(0, &konum, &pid));
}

inline bool agAcilistaBasliyor() { return dosyaVar(KMAG_BOOT_FLAG); }

inline void agAcilistaBaslat(bool acik) {
    if (acik) {
        dizinOlustur("sdmc:/atmosphere/contents/" KMAG_TID_STR "/flags/");
        if (FILE* f = fopen(KMAG_BOOT_FLAG, "w")) fclose(f);
    } else {
        remove(KMAG_BOOT_FLAG);
    }
}

// ─── Yedekleme ───────────────────────────────────────────────────────────────
inline bool oturumdaYedeklendi = false;

inline std::vector<std::string> yedekleriListele() {
    std::vector<std::string> v;
    if (DIR* d = opendir(YEDEK_DIZIN)) {
        while (dirent* e = readdir(d)) {
            std::string ad = e->d_name;
            if (ad.size() > 4 && ad.compare(ad.size() - 4, 4, ".ini") == 0) v.push_back(ad);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), std::greater<>()); // en yeni en üstte (ad zaman damgası içeriyor)
    return v;
}

// Yedek dosya adı: <zaman>_<etiket>.ini — zaman başta olduğu için ad sıralaması = zaman sıralaması
inline bool yedekAl(const char* etiket = "oto") {
    if (!dosyaVar(SYSCLK_CONFIG)) return false;
    dizinOlustur(YEDEK_DIZIN);
    std::string hedef = std::string(YEDEK_DIZIN) + zamanDamgasi() + "_" + etiket + ".ini";
    if (!dosyaKopyala(SYSCLK_CONFIG, hedef)) return false;

    auto liste = yedekleriListele();
    for (size_t i = MAKS_YEDEK; i < liste.size(); ++i) remove((std::string(YEDEK_DIZIN) + liste[i]).c_str());
    return true;
}

inline bool yedekGeriYukle(const std::string& ad) {
    yedekAl("geri-yukleme-oncesi");
    // sys-clk dosyanın değişme zamanını izliyor, kopyalama sonrası kendisi yeniden okur
    return dosyaKopyala(std::string(YEDEK_DIZIN) + ad, SYSCLK_CONFIG);
}

// ─── sys-clk profilleri ──────────────────────────────────────────────────────
inline bool profilOku(u64 tid, SysClkTitleProfileList& out) {
    out = SysClkTitleProfileList{};
    return srv.sysclk && R_SUCCEEDED(sysclkIpcGetProfiles(tid, &out));
}

inline int profilTuru(const SysClkTitleProfileList& p) {
    // Handheld dışındaki satırlarda değer varsa (eski elle yazılmış ayar) "Özel" say
    for (int pr = 0; pr < SysClkProfile_EnumMax; ++pr) {
        if (pr == SysClkProfile_Handheld) continue;
        for (int m = 0; m < SysClkModule_EnumMax; ++m)
            if (p.mhzMap[pr][m]) return PT_OZEL;
    }
    const u32* h = p.mhzMap[SysClkProfile_Handheld];
    for (int i = 0; i < 3; ++i) {
        if (h[0] == ONAYARLAR[i].mhz[0] && h[1] == ONAYARLAR[i].mhz[1] && h[2] == ONAYARLAR[i].mhz[2]) return i;
    }
    return PT_OZEL;
}

inline int profilTuru(u64 tid) {
    SysClkTitleProfileList p;
    if (!profilOku(tid, p)) return PT_BILINMIYOR;
    return profilTuru(p);
}

// Her yazmadan önce oturum başına bir kez otomatik yedek alınır
inline Result profilYaz(u64 tid, const u32 mhz[SysClkModule_EnumMax]) {
    if (!srv.sysclk) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!oturumdaYedeklendi) oturumdaYedeklendi = yedekAl("oto");
    SysClkTitleProfileList p{};
    for (int m = 0; m < SysClkModule_EnumMax; ++m) p.mhzMap[SysClkProfile_Handheld][m] = mhz[m];
    const Result rc = sysclkIpcSetProfiles(tid, &p);
    sonGuncelleme = 0; // panel bir sonraki karede yenilensin
    return rc;
}

inline std::vector<u64> profilliOyunlar() {
    std::vector<u64> v;
    FILE* f = fopen(SYSCLK_CONFIG, "r");
    if (!f) return v;
    char satir[256];
    while (fgets(satir, sizeof(satir), f)) {
        // [0100ABCD12345000] biçimindeki bölümler
        if (satir[0] != '[') continue;
        char* son = strchr(satir, ']');
        if (!son || son - satir != 17) continue;
        *son = '\0';
        bool hex = true;
        for (int i = 1; i <= 16; ++i) hex &= isxdigit(static_cast<unsigned char>(satir[i])) != 0;
        if (!hex) continue;
        u64 tid = strtoull(satir + 1, nullptr, 16);
        SysClkTitleProfileList p;
        if (!profilOku(tid, p)) continue;
        bool dolu = false;
        for (u32 x : p.mhz) dolu |= x != 0;
        if (dolu && std::find(v.begin(), v.end(), tid) == v.end()) v.push_back(tid);
    }
    fclose(f);
    return v;
}

inline std::vector<u32> frekansListesi(SysClkModule m) {
    if (srv.sysclk) {
        u32 hz[SYSCLK_FREQ_LIST_MAX] = {};
        u32 adet = 0;
        if (R_SUCCEEDED(sysclkIpcGetFreqList(m, hz, SYSCLK_FREQ_LIST_MAX, &adet)) && adet > 0) {
            std::vector<u32> v;
            for (u32 i = 0; i < adet && i < SYSCLK_FREQ_LIST_MAX; ++i) {
                u32 mhz = hz[i] / 1000000;
                if (mhz && std::find(v.begin(), v.end(), mhz) == v.end()) v.push_back(mhz);
            }
            std::sort(v.begin(), v.end());
            if (!v.empty()) return v;
        }
    }
    return YEDEK_FREKANS[m];
}

// ─── Diğer overlay'ler ───────────────────────────────────────────────────────
// Dosya adı tam bilinmeyebilir (FPSLocker.ovl / fpslocker.ovl / Status-Monitor-Overlay.ovl ...)
inline std::string overlayBul(const char* anahtar) {
    std::string bulunan;
    if (DIR* d = opendir(OVERLAY_DIZIN)) {
        while (dirent* e = readdir(d)) {
            std::string ad = e->d_name, kucuk = ad;
            std::transform(kucuk.begin(), kucuk.end(), kucuk.begin(), [](unsigned char c) { return std::tolower(c); });
            if (kucuk.size() > 4 && kucuk.compare(kucuk.size() - 4, 4, ".ovl") == 0 && kucuk.find(anahtar) != std::string::npos) {
                bulunan = std::string("/switch/.overlays/") + ad;
                break;
            }
        }
        closedir(d);
    }
    return bulunan;
}

inline void overlayAc(const std::string& yol, const std::string& arguman = "") {
    tsl::setNextOverlay(yol, arguman);
    tsl::Overlay::get()->close();
}

// ─── Ultrahand bildirim API'si ───────────────────────────────────────────────
// Ultrahand, bu klasöre bırakılan .notify (JSON) dosyalarını bildirim olarak gösterir.
// Sonraki aşamadaki arka plan modülü (düşük pil vb.) de bu yolu kullanacak.
inline bool ultrahandBildirimiGonder(const std::string& baslik, const std::string& metin) {
    dizinOlustur(BILDIRIM_DIZIN);
    std::string yol = std::string(BILDIRIM_DIZIN) + "kontrol-merkezi-" + zamanDamgasi() + ".notify";
    FILE* f = fopen(yol.c_str(), "w");
    if (!f) return false;
    fprintf(f, "{\"title\":\"%s\",\"text\":\"%s\",\"duration\":4000,\"priority\":20}\n", baslik.c_str(), metin.c_str());
    fclose(f);
    return true;
}

// ─── Sürüm / donanım bilgisi ─────────────────────────────────────────────────
inline std::string hosSurumu() {
    u32 v = hosversionGet();
    char b[24];
    snprintf(b, sizeof(b), "%u.%u.%u", HOSVER_MAJOR(v), HOSVER_MINOR(v), HOSVER_MICRO(v));
    return b;
}

inline std::string amsSurumu() {
    u64 v = 0;
    if (R_FAILED(splGetConfig(static_cast<SplConfigItem>(65000), &v))) return "?";
    char b[24];
    snprintf(b, sizeof(b), "%lu.%lu.%lu", (v >> 56) & 0xFF, (v >> 48) & 0xFF, (v >> 40) & 0xFF);
    return b;
}

inline std::string modelAdi() {
    u64 hw = 0;
    if (R_FAILED(splGetConfig(SplConfigItem_HardwareType, &hw))) return "?";
    switch (hw) {
        case 0: return "Switch (Erista)";
        case 1: return "Copper (dev)";
        case 2: return "Switch Lite";
        case 3: return "Switch (Mariko)";
        case 4: return "Calcio";
        case 5: return "Switch OLED";
        default: return "Bilinmiyor";
    }
}

} // namespace km
