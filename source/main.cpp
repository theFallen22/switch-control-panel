// Kontrol Merkezi — Switch Lite için kişisel kontrol paneli (libultrahand overlay)
#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include "durum.hpp"

using namespace km;
using R = tsl::gfx::Renderer;

// ─── Renkler ve çizim yardımcıları ───────────────────────────────────────────
namespace renk {
    inline tsl::Color yesil()   { return tsl::healthyRamTextColor; }
    inline tsl::Color sari()    { return tsl::neutralRamTextColor; }
    inline tsl::Color kirmizi() { return tsl::badRamTextColor; }
    inline tsl::Color etiket()  { return tsl::sectionTextColor; }
    inline tsl::Color metin()   { return tsl::defaultTextColor; }
    inline tsl::Color soluk()   { return tsl::infoTextColor; }
    inline tsl::Color kart()    { return tsl::tableBGColor; }
    inline tsl::Color cizgi()   { return tsl::separatorColor; }
}

static tsl::Color sicaklikRengi(u32 milliC) {
    if (milliC < 55000) return renk::yesil();
    if (milliC < 65000) return renk::sari();
    return renk::kirmizi();
}

static tsl::Color bataryaRengi(u32 yuzde) {
    if (yuzde > 30) return renk::yesil();
    if (yuzde > 15) return renk::sari();
    return renk::kirmizi();
}

static tsl::Color dolulukRengi(double oran) {
    if (oran > 0.9) return renk::kirmizi();
    if (oran > 0.75) return renk::sari();
    return renk::yesil();
}

static std::string sicaklikMetni(u32 milliC) {
    char b[16];
    snprintf(b, sizeof(b), "%u.%u°C", milliC / 1000, (milliC % 1000) / 100);
    return b;
}

static void kart(R* r, s32 x, s32 y, s32 w, s32 h) {
    r->drawRoundedRect(x, y, w, h, 10, R::a(renk::kart()));
}

static void cubuk(R* r, s32 x, s32 y, s32 w, double oran, tsl::Color c) {
    oran = std::clamp(oran, 0.0, 1.0);
    r->drawRoundedRect(x, y, w, 8, 4, R::a(renk::cizgi()));
    r->drawRoundedRect(x, y, std::max<s32>(8, static_cast<s32>(w * oran)), 8, 4, R::a(c));
}

static const char* sarjMetni(PsmChargerType t) {
    switch (t) {
        case PsmChargerType_Unconnected:  return "Pilde";
        case PsmChargerType_EnoughPower:  return "Şarj oluyor";
        case PsmChargerType_LowPower:     return "Yavaş şarj";
        default:                          return "Desteklenmeyen şarj";
    }
}

static std::string kalanSureMetni() {
    const int dk = kalanDakika();
    if (dk < 0) return "";
    return "~" + sureMetni(static_cast<u64>(dk));
}

// ─── Gösterge paneli (ana ekranın üstündeki kartlar) ─────────────────────────
static constexpr s32 OYUN_H = 74, KART_H = 84, SAAT_H = 64, BOSLUK = 8;
static constexpr s32 PANEL_YUKSEKLIK = 4 + OYUN_H + BOSLUK + KART_H + BOSLUK + SAAT_H + BOSLUK + KART_H + 8;

static void paneliCiz(R* r, s32 x, s32 y, s32 w, s32 /*h*/) {
    const s32 yarim = (w - BOSLUK) / 2;
    const s32 sx = x + yarim + BOSLUK;
    const auto& a = anlik;
    char b[64];

    // 1) Oyun kartı: ad, profil, oynama süresi
    s32 cy = y + 4;
    kart(r, x, cy, w, OYUN_H);
    r->drawString(a.tid ? "Şu an açık" : "Oyun açık değil", false, x + 14, cy + 22, 15, R::a(renk::etiket()));
    r->drawString(a.tid ? oyunAdi(a.tid) : "Ana Menü", false, x + 14, cy + 47, 21, R::a(renk::metin()), w - 28);
    if (srv.sysclk && a.tid) {
        r->drawString(profilAdi(a.profil), false, x + w - 128, cy + 22, 15, R::a(tsl::onTextColor), 120);
    }
    if (a.tid) {
        const Oynama o = oynamaBilgisi(a.tid);
        if (o.ok) {
            snprintf(b, sizeof(b), "Toplam %s · %u kez açıldı", sureMetni(o.dakika).c_str(), o.acilis);
            r->drawString(b, false, x + 14, cy + 67, 14, R::a(renk::soluk()), w - 28);
        }
    } else if (a.sonOyun) {
        r->drawString("Son oyun: " + oyunAdi(a.sonOyun), false, x + 14, cy + 67, 14, R::a(renk::soluk()), w - 28);
    }

    // 2) Batarya | Sıcaklık
    cy += OYUN_H + BOSLUK;
    kart(r, x, cy, yarim, KART_H);
    kart(r, sx, cy, yarim, KART_H);

    r->drawString("Batarya", false, x + 14, cy + 22, 15, R::a(renk::etiket()));
    if (a.psmOk) {
        snprintf(b, sizeof(b), "%%%u", a.batarya);
        r->drawString(b, false, x + 14, cy + 54, 28, R::a(bataryaRengi(a.batarya)));
        std::string alt = sarjMetni(a.sarj);
        const std::string kalan = kalanSureMetni();
        if (!kalan.empty()) alt += " · " + kalan;
        else if (a.ctxOk) {
            snprintf(b, sizeof(b), " · %+.1f W", a.ctx.power[SysClkPowerSensor_Avg] / 1000.0f);
            alt += b;
        }
        r->drawString(alt, false, x + 14, cy + 75, 14, R::a(renk::soluk()), yarim - 20);
    } else {
        r->drawString("-", false, x + 14, cy + 54, 28, R::a(renk::soluk()));
    }

    r->drawString("Sıcaklık (SoC)", false, sx + 14, cy + 22, 15, R::a(renk::etiket()));
    if (a.ctxOk) {
        const u32 soc = a.ctx.temps[SysClkThermalSensor_SOC];
        r->drawString(sicaklikMetni(soc), false, sx + 14, cy + 54, 28, R::a(sicaklikRengi(soc)));
        if (a.fanOk) snprintf(b, sizeof(b), "Fan %%%.0f · PCB %u°", a.fanYuzde, a.ctx.temps[SysClkThermalSensor_PCB] / 1000);
        else snprintf(b, sizeof(b), "PCB %u° · Yüzey %u°", a.ctx.temps[SysClkThermalSensor_PCB] / 1000, a.ctx.temps[SysClkThermalSensor_Skin] / 1000);
        r->drawString(b, false, sx + 14, cy + 75, 14, R::a(renk::soluk()), yarim - 20);
    } else if (a.fanOk) {
        snprintf(b, sizeof(b), "Fan %%%.0f", a.fanYuzde);
        r->drawString(b, false, sx + 14, cy + 52, 19, R::a(renk::metin()));
    } else {
        r->drawString("sys-clk gerekli", false, sx + 14, cy + 52, 17, R::a(renk::soluk()));
    }

    // 3) Saat hızları + RAM yükü
    cy += KART_H + BOSLUK;
    kart(r, x, cy, w, SAAT_H);
    r->drawString("Saat hızları (MHz)", false, x + 14, cy + 22, 15, R::a(renk::etiket()));
    if (a.ctxOk) {
        const s32 sutun = (w - 28) / 4;
        static const char* kisa[SysClkModule_EnumMax] = { "CPU", "GPU", "MEM" };
        for (int m = 0; m < SysClkModule_EnumMax; ++m) {
            const u32 hz = a.ctx.realFreqs[m] ? a.ctx.realFreqs[m] : a.ctx.freqs[m];
            const u32 mhz = hz / 1000000;
            snprintf(b, sizeof(b), "%s %u", kisa[m], mhz);
            r->drawString(b, false, x + 14 + sutun * m, cy + 50, 18, R::a(mhz > GUVENLI_MHZ[m] ? renk::sari() : renk::metin()));
        }
        const u32 ram = a.ctx.ramLoad[SysClkRamLoad_All]; // binde
        snprintf(b, sizeof(b), "RAM %%%u", (ram + 5) / 10);
        r->drawString(b, false, x + 14 + sutun * 3, cy + 50, 18, R::a(ram > 900 ? renk::sari() : renk::metin()));
    } else {
        r->drawString(srv.sysclkVar ? "sys-clk sürümü uyumsuz" : "sys-clk çalışmıyor", false, x + 14, cy + 50, 17, R::a(renk::soluk()));
    }

    // 4) Wi-Fi | Depolama
    cy += SAAT_H + BOSLUK;
    kart(r, x, cy, yarim, KART_H);
    kart(r, sx, cy, yarim, KART_H);

    r->drawString("Wi-Fi", false, x + 14, cy + 22, 15, R::a(renk::etiket()));
    const bool bagli = a.netOk && a.netDurum == NifmInternetConnectionStatus_Connected;
    for (u32 i = 0; i < 3; ++i) {
        const s32 bh = 6 + 5 * i;
        r->drawRect(x + yarim - 40 + i * 9, cy + 24 - bh, 6, bh, R::a(bagli && a.sinyal > i ? renk::yesil() : renk::cizgi()));
    }
    if (bagli) {
        r->drawString(a.ssid[0] ? a.ssid : "Bağlı", false, x + 14, cy + 52, 19, R::a(renk::metin()), yarim - 24);
        r->drawString(a.ip ? ipMetni(a.ip) : "İnternet var", false, x + 14, cy + 75, 14, R::a(renk::soluk()));
    } else {
        r->drawString(a.netOk ? "Bağlanıyor" : "Kapalı", false, x + 14, cy + 52, 19, R::a(renk::soluk()));
    }

    r->drawString("SD kart", false, sx + 14, cy + 22, 15, R::a(renk::etiket()));
    if (a.sdOk && a.sdToplam > 0) {
        r->drawString(gbMetni(a.sdBos) + " boş", false, sx + 14, cy + 52, 19, R::a(renk::metin()), yarim - 20);
        const double dolu = 1.0 - static_cast<double>(a.sdBos) / a.sdToplam;
        cubuk(r, sx + 14, cy + 64, yarim - 28, dolu, dolulukRengi(dolu));
    } else {
        r->drawString("-", false, sx + 14, cy + 52, 19, R::a(renk::soluk()));
    }
}

// ─── Grafik (son 60 sn) ──────────────────────────────────────────────────────
static constexpr s32 GRAFIK_H = 122;

static void grafikCiz(R* r, s32 x, s32 y, s32 w, const Gecmis& g, const char* baslik,
                      const char* birim, int ondalik, tsl::Color renkCizgi, float sabitAlt = NAN, float sabitUst = NAN) {
    kart(r, x, y, w, GRAFIK_H - 6);
    char b[64];
    r->drawString(baslik, false, x + 14, y + 22, 15, R::a(renk::etiket()));
    if (g.n == 0) {
        r->drawString("Veri bekleniyor…", false, x + 14, y + 60, 15, R::a(renk::soluk()));
        return;
    }
    snprintf(b, sizeof(b), "%.*f %s", ondalik, g.son(), birim);
    r->drawString(b, false, x + w - 120, y + 22, 17, R::a(renkCizgi), 110);

    const s32 gx = x + 14, gy = y + 32, gw = w - 28, gh = 58;
    float alt = std::isnan(sabitAlt) ? g.enAz() : sabitAlt;
    float ust = std::isnan(sabitUst) ? g.enCok() : sabitUst;
    if (ust - alt < 1.0f) { ust += 0.5f; alt -= 0.5f; }

    r->drawRect(gx, gy + gh, gw, 1, R::a(renk::cizgi()));
    const float adim = static_cast<float>(gw) / (GECMIS_UZUNLUK - 1);
    const s32 bas = GECMIS_UZUNLUK - g.n; // sağa yasla: en yeni nokta sağda
    for (int i = 0; i < g.n; ++i) {
        const float oran = std::clamp((g.al(i) - alt) / (ust - alt), 0.0f, 1.0f);
        const s32 px = gx + static_cast<s32>((bas + i) * adim);
        const s32 ph = std::max<s32>(2, static_cast<s32>(oran * gh));
        r->drawRect(px, gy + gh - ph, std::max<s32>(2, static_cast<s32>(adim) - 1), ph, R::a(renkCizgi));
    }
    snprintf(b, sizeof(b), "en az %.*f · ort %.*f · en çok %.*f", ondalik, g.enAz(), ondalik, g.ort(), ondalik, g.enCok());
    r->drawString(b, false, x + 14, y + 108, 13, R::a(renk::soluk()), w - 28);
}

// ─── Bilgi metni çizen küçük eleman ──────────────────────────────────────────
static tsl::elm::CustomDrawer* bilgiKutusu(std::vector<std::string> satirlar) {
    return new tsl::elm::CustomDrawer([satirlar](R* r, s32 x, s32 y, s32 w, s32) {
        s32 cy = y + 22;
        for (const auto& s : satirlar) {
            r->drawString(s, false, x + 12, cy, 15, R::a(renk::soluk()), w - 24);
            cy += 22;
        }
    });
}

static u16 bilgiYuksekligi(size_t satir) { return static_cast<u16>(14 + satir * 22); }

static tsl::elm::ListItem* menuSatiri(const std::string& ad, std::function<void()> ac, const std::string& deger = "") {
    auto* item = new tsl::elm::ListItem(ad, deger);
    item->setClickListener([ac](u64 k) {
        if (!(k & KEY_A)) return false;
        ac();
        return true;
    });
    return item;
}

// Değeri saniyede bir yenilenen satırlar için ortak yardımcı
struct CanliSatir {
    tsl::elm::ListItem* item;
    std::function<std::string()> deger;
    std::string onceki;
};

static void canliYenile(std::vector<CanliSatir>& satirlar) {
    for (auto& s : satirlar) {
        std::string d = s.deger();
        if (d != s.onceki) { s.item->setValue(d); s.onceki = d; }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Frekans seçici
// ═════════════════════════════════════════════════════════════════════════════
static u32 taslak[SysClkModule_EnumMax] = {};

class FrekansSecGui : public tsl::Gui {
    SysClkModule m_modul;
    s32 m_onayBekleyen = -1;
public:
    explicit FrekansSecGui(SysClkModule m) : m_modul(m) {}

    tsl::elm::Element* createUI() override {
        const char* ad = m_modul == SysClkModule_CPU ? "CPU" : m_modul == SysClkModule_GPU ? "GPU" : "Bellek";
        auto* frame = new tsl::elm::OverlayFrame(std::string(ad) + " frekansı", "Özel profil");
        auto* list = new tsl::elm::List();

        auto* stok = new tsl::elm::ListItem("Stok (dokunma)", taslak[m_modul] == 0 ? ult::CHECKMARK_SYMBOL : "");
        stok->setClickListener([this](u64 k) {
            if (!(k & KEY_A)) return false;
            taslak[m_modul] = 0;
            tsl::goBack();
            return true;
        });
        list->addItem(stok);

        list->addItem(new tsl::elm::CategoryHeader("Güvenli aralık"));
        bool ocBasligi = false;
        for (u32 mhz : frekansListesi(m_modul)) {
            const bool oc = mhz > GUVENLI_MHZ[m_modul];
            if (oc && !ocBasligi) {
                list->addItem(new tsl::elm::CategoryHeader(ayar.ocOnayi ? "Overclock (iki kez A)" : "Overclock"));
                ocBasligi = true;
            }
            std::string deger = taslak[m_modul] == mhz ? ult::CHECKMARK_SYMBOL : (oc ? "OC" : "");
            auto* item = new tsl::elm::ListItem(std::to_string(mhz) + " MHz", deger);
            item->setClickListener([this, mhz, oc](u64 k) {
                if (!(k & KEY_A)) return false;
                if (oc && ayar.ocOnayi && m_onayBekleyen != static_cast<s32>(mhz)) {
                    m_onayBekleyen = static_cast<s32>(mhz);
                    bildir("Overclock! Sıcaklığı izle. Onay için tekrar A.");
                    return true;
                }
                taslak[m_modul] = mhz;
                tsl::goBack();
                return true;
            });
            list->addItem(item);
        }
        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Özel profil editörü
// ═════════════════════════════════════════════════════════════════════════════
class OzelProfilGui : public tsl::Gui {
    u64 m_tid;
    tsl::elm::ListItem* m_satir[SysClkModule_EnumMax] = {};
    std::string m_onceki[SysClkModule_EnumMax];
public:
    explicit OzelProfilGui(u64 tid) : m_tid(tid) {
        SysClkTitleProfileList p;
        if (profilOku(tid, p)) {
            for (int m = 0; m < SysClkModule_EnumMax; ++m) taslak[m] = p.mhzMap[SysClkProfile_Handheld][m];
        } else {
            std::fill(std::begin(taslak), std::end(taslak), 0);
        }
    }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Özel Profil", oyunAdi(m_tid));
        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("Frekanslar (elde)"));

        static const char* adlar[SysClkModule_EnumMax] = { "CPU", "GPU", "Bellek" };
        for (int m = 0; m < SysClkModule_EnumMax; ++m) {
            m_satir[m] = menuSatiri(adlar[m], [m] { tsl::changeTo<FrekansSecGui>(static_cast<SysClkModule>(m)); }, mhzMetni(taslak[m]));
            list->addItem(m_satir[m]);
        }

        list->addItem(new tsl::elm::CategoryHeader("Hazır profilden başla"));
        for (int i = 0; i < 3; ++i) {
            list->addItem(menuSatiri(ONAYARLAR[i].ad, [i] {
                for (int m = 0; m < SysClkModule_EnumMax; ++m) taslak[m] = ONAYARLAR[i].mhz[m];
            }, "kopyala"));
        }

        list->addItem(new tsl::elm::CategoryHeader("İşlem"));
        list->addItem(menuSatiri("Kaydet ve uygula", [this] {
            if (R_SUCCEEDED(profilYaz(m_tid, taslak))) {
                bildir("Özel profil kaydedildi");
                tsl::goBack();
            } else {
                bildir("Kaydedilemedi (sys-clk?)");
            }
        }));

        list->addItem(bilgiKutusu({
            "Şarj olurken de aynı değerler kullanılır.",
            "Sarı = overclock. Sıcaklığı Canlı izleme'den",
            "takip et.",
        }), bilgiYuksekligi(3));

        frame->setContent(list);
        return frame;
    }

    void update() override {
        for (int m = 0; m < SysClkModule_EnumMax; ++m) {
            std::string s = mhzMetni(taslak[m]);
            if (m_satir[m] && s != m_onceki[m]) {
                m_satir[m]->setValue(s);
                m_onceki[m] = s;
            }
        }
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Oyun profili seçimi
// ═════════════════════════════════════════════════════════════════════════════
class ProfilGui : public tsl::Gui {
    u64 m_tid;
    tsl::elm::ListItem* m_satir[4] = {};
    int m_aktif = -2;
    u64 m_sonKontrol = 0;

    void degerleriYenile() {
        const int t = profilTuru(m_tid);
        if (t == m_aktif) return;
        m_aktif = t;
        for (int i = 0; i < 3; ++i) {
            if (!m_satir[i]) continue;
            std::string ozet = ONAYARLAR[i].mhz[0]
                ? std::to_string(ONAYARLAR[i].mhz[0]) + "/" + std::to_string(ONAYARLAR[i].mhz[1]) + "/" + std::to_string(ONAYARLAR[i].mhz[2])
                : "Stok";
            m_satir[i]->setValue(t == i ? ult::CHECKMARK_SYMBOL : ozet, t != i);
        }
        if (m_satir[3]) m_satir[3]->setValue(t == PT_OZEL ? ult::CHECKMARK_SYMBOL : "Düzenle", t != PT_OZEL);
    }

public:
    explicit ProfilGui(u64 tid) : m_tid(tid) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(oyunAdi(m_tid), tidMetni(m_tid));
        auto* list = new tsl::elm::List();

        if (!srv.sysclk) {
            list->addItem(bilgiKutusu({
                srv.sysclkVar ? "sys-clk sürümü bu panelle uyumsuz." : "sys-clk çalışmıyor.",
                "Kurulum: /atmosphere/contents/00FF0000636C6BFF",
            }), bilgiYuksekligi(2));
            frame->setContent(list);
            return frame;
        }

        const Oynama o = oynamaBilgisi(m_tid);
        if (o.ok) {
            list->addItem(new tsl::elm::CategoryHeader("Oyun"));
            list->addItem(new tsl::elm::ListItem("Toplam oynama", sureMetni(o.dakika)));
            list->addItem(new tsl::elm::ListItem("Açılış sayısı", std::to_string(o.acilis)));
        }

        list->addItem(new tsl::elm::CategoryHeader("Hazır profiller  (CPU/GPU/MEM)"));
        for (int i = 0; i < 3; ++i) {
            m_satir[i] = menuSatiri(ONAYARLAR[i].ad, [this, i] {
                if (R_SUCCEEDED(profilYaz(m_tid, ONAYARLAR[i].mhz))) {
                    bildir(std::string(ONAYARLAR[i].ad) + " uygulandı");
                    m_aktif = -2;
                    degerleriYenile();
                } else {
                    bildir("Yazılamadı");
                }
            });
            list->addItem(m_satir[i]);
        }

        list->addItem(new tsl::elm::CategoryHeader("Özel"));
        m_satir[3] = menuSatiri("Özel profil", [this] { tsl::changeTo<OzelProfilGui>(m_tid); });
        list->addItem(m_satir[3]);

        list->addItem(new tsl::elm::CategoryHeader("Nasıl çalışır"));
        list->addItem(bilgiKutusu({
            "Profil sys-clk ayarına yazılır; oyun her",
            "açıldığında otomatik uygulanır, kapanınca",
            "stok değerlere dönülür.",
            "FPS hedefi için FPSLocker'ı kullan.",
            "İlk değişiklikten önce otomatik yedek alınır.",
        }), bilgiYuksekligi(5));

        degerleriYenile();
        frame->setContent(list);
        return frame;
    }

    void update() override {
        const u64 t = armGetSystemTick();
        if (armTicksToNs(t - m_sonKontrol) > 700000000ULL) {
            m_sonKontrol = t;
            degerleriYenile();
        }
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Profilli oyunlar listesi
// ═════════════════════════════════════════════════════════════════════════════
class OyunListesiGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Oyun Profilleri", "sys-clk config.ini");
        auto* list = new tsl::elm::List();

        auto oyunlar = profilliOyunlar();
        if (anlik.tid && std::find(oyunlar.begin(), oyunlar.end(), anlik.tid) == oyunlar.end())
            oyunlar.insert(oyunlar.begin(), anlik.tid);

        if (oyunlar.empty()) {
            list->addItem(bilgiKutusu({
                "Henüz profilli oyun yok.",
                "Bir oyun açıkken paneli aç ve",
                "'Bu oyunun profili'ni seç.",
            }), bilgiYuksekligi(3));
        } else {
            list->addItem(new tsl::elm::CategoryHeader(std::to_string(oyunlar.size()) + " oyun · profil / toplam süre"));
            for (u64 tid : oyunlar) {
                std::string ad = oyunAdi(tid);
                if (tid == anlik.tid) ad = "▶ " + ad;
                std::string deger = profilAdi(profilTuru(tid));
                const Oynama o = oynamaBilgisi(tid);
                if (o.ok && o.dakika) deger += " · " + sureMetni(o.dakika);
                list->addItem(menuSatiri(ad, [tid] { tsl::changeTo<ProfilGui>(tid); }, deger));
            }
        }
        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Canlı izleme (grafikler)
// ═════════════════════════════════════════════════════════════════════════════
class CanliIzlemeGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Canlı İzleme", "Son 60 saniye · saniyede bir");
        auto* list = new tsl::elm::List();

        if (!srv.sysclk) {
            list->addItem(bilgiKutusu({ "Grafikler için sys-clk gerekli." }), bilgiYuksekligi(1));
            frame->setContent(list);
            return frame;
        }

        list->addItem(new tsl::elm::CustomDrawer([](R* r, s32 x, s32 y, s32 w, s32) {
            grafikCiz(r, x, y + 4, w, gecmisSicaklik, "SoC sıcaklığı", "°C", 1,
                      sicaklikRengi(static_cast<u32>(gecmisSicaklik.son() * 1000)), 30.0f, 80.0f);
            grafikCiz(r, x, y + 4 + GRAFIK_H, w, gecmisGuc, "Pil tüketimi", "W", 1, renk::sari());
            grafikCiz(r, x, y + 4 + GRAFIK_H * 2, w, gecmisCpu, "CPU frekansı", "MHz", 0, tsl::onTextColor, 0.0f, 1800.0f);
            grafikCiz(r, x, y + 4 + GRAFIK_H * 3, w, gecmisGpu, "GPU frekansı", "MHz", 0, tsl::onTextColor, 0.0f, 921.0f);
            grafikCiz(r, x, y + 4 + GRAFIK_H * 4, w, gecmisFan, "Fan hızı", "%", 0, renk::yesil(), 0.0f, 100.0f);
        }), GRAFIK_H * 5 + 8);

        list->addItem(bilgiKutusu({
            "Veriler panel açıkken toplanır.",
            "Eksi tüketim = şarj oluyor.",
            "FPS için Hızlı işlemler > Performans HUD.",
        }), bilgiYuksekligi(3));

        frame->setContent(list);
        return frame;
    }

    void update() override { guncelle(); }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Batarya detayı + uyku tüketimi
// ═════════════════════════════════════════════════════════════════════════════
class BataryaGui : public tsl::Gui {
    std::vector<CanliSatir> m_canli;

    void ekle(tsl::elm::List* list, const std::string& ad, std::function<std::string()> deger) {
        auto* item = new tsl::elm::ListItem(ad, deger());
        m_canli.push_back({ item, std::move(deger), "" });
        list->addItem(item);
    }

public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Batarya", "Switch Lite · 3570 mAh");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Şu an"));
        ekle(list, "Doluluk", [] { return anlik.psmOk ? "%" + std::to_string(anlik.batarya) : std::string("-"); });
        ekle(list, "Durum", [] { return std::string(sarjMetni(anlik.sarj)); });
        ekle(list, "Kalan süre (tahmini)", [] {
            const std::string k = kalanSureMetni();
            return k.empty() ? std::string(anlik.sarj != PsmChargerType_Unconnected ? "şarjda" : "hesaplanıyor") : k;
        });
        ekle(list, "Anlık güç", [] {
            if (!anlik.ctxOk) return std::string("-");
            char b[24];
            snprintf(b, sizeof(b), "%+.2f W", anlik.ctx.power[SysClkPowerSensor_Now] / 1000.0f);
            return std::string(b);
        });
        ekle(list, "Ortalama güç", [] {
            if (!anlik.ctxOk) return std::string("-");
            char b[24];
            snprintf(b, sizeof(b), "%+.2f W", anlik.ctx.power[SysClkPowerSensor_Avg] / 1000.0f);
            return std::string(b);
        });

        list->addItem(new tsl::elm::CategoryHeader("Pil sağlığı"));
        ekle(list, "Sağlık", [] {
            if (!anlik.pilOk) return std::string("-");
            char b[16];
            snprintf(b, sizeof(b), "%%%.1f", anlik.pil.battery_age_percentage / 1000.0f);
            return std::string(b);
        });
        ekle(list, "Voltaj", [] {
            return anlik.pilOk ? std::to_string(anlik.pil.battery_charge_milli_voltage) + " mV" : std::string("-");
        });
        ekle(list, "Pil sıcaklığı", [] {
            return anlik.pilOk ? sicaklikMetni(anlik.pil.temperature_celcius) : std::string("-");
        });
        ekle(list, "Şarj akımı sınırı", [] {
            return anlik.pilOk ? std::to_string(anlik.pil.fast_charge_current_limit) + " mA" : std::string("-");
        });

        // Pil günlüğü
        const auto kayitlar = pilGunlugunuOku();
        list->addItem(new tsl::elm::CategoryHeader("Pil günlüğü (panel açılışları)"));
        if (kayitlar.size() < 2) {
            list->addItem(bilgiKutusu({
                "Uyku tüketimini ölçmek için: uyutmadan",
                "önce paneli bir kez aç, uyandırınca",
                "tekrar aç. Fark burada görünür.",
            }), bilgiYuksekligi(3));
        } else {
            int gosterilen = 0;
            for (size_t i = kayitlar.size() - 1; i > 0 && gosterilen < 6; --i, ++gosterilen) {
                const auto& yeni = kayitlar[i];
                const auto& eski = kayitlar[i - 1];
                const double saat = (yeni.zaman - eski.zaman) / 3600.0;
                time_t t = static_cast<time_t>(yeni.zaman);
                char zaman[16];
                strftime(zaman, sizeof(zaman), "%d.%m %H:%M", localtime(&t));
                char deger[48];
                if (eski.sarj || yeni.sarj || saat < 0.1) {
                    snprintf(deger, sizeof(deger), "%%%u → %%%u", eski.yuzde, yeni.yuzde);
                } else {
                    const double hiz = (static_cast<double>(eski.yuzde) - yeni.yuzde) / saat;
                    snprintf(deger, sizeof(deger), "%%%u→%%%u · %%%.1f/sa%s", eski.yuzde, yeni.yuzde, hiz,
                             (saat > 1.0 && hiz < 2.0) ? " (uyku?)" : "");
                }
                list->addItem(new tsl::elm::ListItem(zaman, deger));
            }
        }

        list->addItem(bilgiKutusu({
            "Kalan süre, pil sağlığı ve son ortalama",
            "tüketimden hesaplanan kaba bir tahmindir.",
        }), bilgiYuksekligi(2));

        frame->setContent(list);
        return frame;
    }

    void update() override {
        guncelle();
        canliYenile(m_canli);
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Depolama ve ağ
// ═════════════════════════════════════════════════════════════════════════════
class DepolamaGui : public tsl::Gui {
    std::vector<CanliSatir> m_canli;
    AlbumBilgi m_album;
public:
    DepolamaGui() { albumTara(ALBUM_DIZIN, m_album); }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Depolama ve Ağ", "Kontrol Merkezi");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Depolama"));
        list->addItem(new tsl::elm::CustomDrawer([](R* r, s32 x, s32 y, s32 w, s32) {
            auto satir = [&](s32 cy, const char* ad, bool ok, s64 bos, s64 toplam) {
                r->drawString(ad, false, x + 14, cy + 18, 16, R::a(renk::metin()));
                if (!ok || toplam <= 0) {
                    r->drawString("-", false, x + w - 60, cy + 18, 16, R::a(renk::soluk()));
                    return;
                }
                const std::string d = gbMetni(toplam - bos) + " / " + gbMetni(toplam);
                r->drawString(d, false, x + w - 170, cy + 18, 15, R::a(renk::soluk()), 160);
                const double oran = 1.0 - static_cast<double>(bos) / toplam;
                cubuk(r, x + 14, cy + 28, w - 28, oran, dolulukRengi(oran));
            };
            satir(y + 4, "SD kart", anlik.sdOk, anlik.sdBos, anlik.sdToplam);
            satir(y + 52, "Dahili hafıza", anlik.icOk, anlik.icBos, anlik.icToplam);
        }), 104);

        char b[64];
        snprintf(b, sizeof(b), "%u görsel · %u video", m_album.resim, m_album.video);
        list->addItem(new tsl::elm::ListItem("Albüm", b));
        list->addItem(new tsl::elm::ListItem("Albüm boyutu", gbMetni(m_album.bayt)));

        list->addItem(new tsl::elm::CategoryHeader("Ağ"));
        auto ekle = [&](const std::string& ad, std::function<std::string()> deger) {
            auto* item = new tsl::elm::ListItem(ad, deger());
            m_canli.push_back({ item, std::move(deger), "" });
            list->addItem(item);
        };
        ekle("Durum", [] {
            if (!anlik.netOk) return std::string("Kapalı");
            return std::string(anlik.netDurum == NifmInternetConnectionStatus_Connected ? "İnternet var" : "Bağlanıyor");
        });
        ekle("Ağ adı", [] { return anlik.ssid[0] ? std::string(anlik.ssid) : std::string("-"); });
        ekle("Sinyal", [] { return anlik.netOk ? std::to_string(anlik.sinyal) + " / 3" : std::string("-"); });
        ekle("IP adresi", [] { return anlik.ip ? ipMetni(anlik.ip) : std::string("-"); });

        list->addItem(bilgiKutusu({
            "IP adresi, Mac'ten FTP ile dosya",
            "aktarırken (ftpd, DBI) işine yarar.",
        }), bilgiYuksekligi(2));

        frame->setContent(list);
        return frame;
    }

    void update() override {
        guncelle();
        canliYenile(m_canli);
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Fan kontrolü
// ═════════════════════════════════════════════════════════════════════════════
static constexpr float FAN_GRAFIK_MIN = 30.0f, FAN_GRAFIK_MAX = 85.0f;
static constexpr s32 FAN_PANEL_H = 250;

static void fanPaneliCiz(R* r, s32 x, s32 y, s32 w, s32) {
    char b[64];
    const auto& a = anlik;
    const float t = a.ctxOk ? a.ctx.temps[SysClkThermalSensor_SOC] / 1000.0f : -1.0f;

    // Üst kart: anlık değerler
    kart(r, x, y + 4, w, 70);
    r->drawString("Fan", false, x + 14, y + 26, 15, R::a(renk::etiket()));
    if (a.fanOk) {
        snprintf(b, sizeof(b), "%%%.0f", a.fanYuzde);
        r->drawString(b, false, x + 14, y + 60, 30, R::a(renk::metin()));
    } else {
        r->drawString("-", false, x + 14, y + 60, 30, R::a(renk::soluk()));
    }
    r->drawString("SoC", false, x + 140, y + 26, 15, R::a(renk::etiket()));
    if (t > 0) r->drawString(sicaklikMetni(static_cast<u32>(t * 1000)), false, x + 140, y + 60, 24, R::a(sicaklikRengi(static_cast<u32>(t * 1000))));
    r->drawString("Mod", false, x + 270, y + 26, 15, R::a(renk::etiket()));
    r->drawString(kmfanModAdi(fanAyari.mod), false, x + 270, y + 56, 16, R::a(tsl::onTextColor), w - 280);

    // Eğri grafiği: x = sıcaklık, y = fan seviyesi
    const s32 gx = x + 44, gy = y + 92, gw = w - 58, gh = 128;
    kart(r, x, y + 80, w, FAN_PANEL_H - 82);
    r->drawRect(gx, gy + gh, gw, 1, R::a(renk::cizgi()));
    r->drawRect(gx, gy, 1, gh, R::a(renk::cizgi()));
    r->drawString("%100", false, x + 6, gy + 10, 12, R::a(renk::soluk()));
    r->drawString("%0", false, x + 18, gy + gh, 12, R::a(renk::soluk()));
    for (int c = 30; c <= 85; c += 10) {
        const s32 px = gx + static_cast<s32>((c - FAN_GRAFIK_MIN) / (FAN_GRAFIK_MAX - FAN_GRAFIK_MIN) * gw);
        snprintf(b, sizeof(b), "%d°", c);
        r->drawString(b, false, px - 8, gy + gh + 16, 12, R::a(renk::soluk()));
    }

    for (s32 i = 0; i <= gw; i += 2) {
        const float tc = FAN_GRAFIK_MIN + (FAN_GRAFIK_MAX - FAN_GRAFIK_MIN) * i / gw;
        // güvenlik tabanı
        const float taban = kmfanGuvenlik(tc);
        r->drawRect(gx + i, gy + gh - static_cast<s32>(taban * gh), 2, 2, R::a(renk::kirmizi()));
        // seçili modun etkin eğrisi
        const float hedef = kmfanHedef(&fanAyari, tc);
        if (hedef >= 0.0f) r->drawRect(gx + i, gy + gh - static_cast<s32>(hedef * gh) - 1, 2, 3, R::a(tsl::onTextColor));
    }

    // Şu anki nokta
    if (t > 0) {
        const float oran = std::clamp((t - FAN_GRAFIK_MIN) / (FAN_GRAFIK_MAX - FAN_GRAFIK_MIN), 0.0f, 1.0f);
        const s32 px = gx + static_cast<s32>(oran * gw);
        r->drawRect(px, gy, 1, gh, R::a(renk::sari()));
        if (a.fanOk) r->drawCircle(px, gy + gh - static_cast<s32>(a.fanYuzde / 100.0 * gh), 5, true, R::a(renk::sari()));
    }

    r->drawRect(x + 14, y + FAN_PANEL_H - 14, 12, 3, R::a(tsl::onTextColor));
    r->drawString("seçili eğri", false, x + 30, y + FAN_PANEL_H - 8, 12, R::a(renk::soluk()));
    r->drawRect(x + 124, y + FAN_PANEL_H - 14, 12, 3, R::a(renk::kirmizi()));
    r->drawString("güvenlik tabanı", false, x + 140, y + FAN_PANEL_H - 8, 12, R::a(renk::soluk()));
    r->drawRect(x + 268, y + FAN_PANEL_H - 14, 12, 3, R::a(renk::sari()));
    r->drawString("şu an", false, x + 284, y + FAN_PANEL_H - 8, 12, R::a(renk::soluk()));
}

class FanGui : public tsl::Gui {
    tsl::elm::ListItem* m_modlar[KMFAN_MOD_SAYISI] = {};
    tsl::elm::ListItem* m_modulDurumu = nullptr;
    int m_oncekiMod = -1;
    int m_oncekiDurum = -1;

    static int modulDurumKodu() { return !fanModuluKurulu() ? 0 : (fanModuluCalisiyor() ? 2 : 1); }

    void modSec(int mod) {
        if (mod != KMFAN_OTOMATIK && !fanModuluKurulu()) {
            bildir("Fan modülü kurulu değil (README)");
            return;
        }
        fanAyari.mod = mod;
        if (!fanAyariYaz()) { bildir("Ayar yazılamadı"); return; }
        if (mod != KMFAN_OTOMATIK && !fanModuluCalisiyor()) {
            bildir(fanModuluBaslat() ? "Fan modülü başlatıldı" : "Modül başlatılamadı");
        } else {
            bildir(std::string("Fan: ") + kmfanModAdi(mod));
        }
    }

public:
    FanGui() { fanAyariOku(); }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Fan Kontrolü", "km-fan modülü");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CustomDrawer(fanPaneliCiz), FAN_PANEL_H + 4);

        list->addItem(new tsl::elm::CategoryHeader("Mod"));
        static const char* aciklama[KMFAN_MOD_SAYISI] = { "sistem", "düşük ses", "önerilen", "en serin", "" };
        for (int m = 0; m < KMFAN_MOD_SAYISI; ++m) {
            m_modlar[m] = menuSatiri(kmfanModAdi(m), [this, m] { modSec(m); }, aciklama[m]);
            list->addItem(m_modlar[m]);
        }

        list->addItem(new tsl::elm::CategoryHeader("Sabit hız"));
        auto* hiz = new tsl::elm::TrackBar("", false, false, true, "Sabit hız", "%");
        hiz->setProgress(static_cast<u16>(fanAyari.sabitYuzde));
        hiz->setValueChangedListener([](u16 v) {
            fanAyari.sabitYuzde = std::clamp<int>(v, 0, 100);
            fanAyariYaz();
        });
        list->addItem(hiz);

        list->addItem(new tsl::elm::CategoryHeader("Modül"));
        m_modulDurumu = new tsl::elm::ListItem("Durum");
        list->addItem(m_modulDurumu);
        auto* acilis = new tsl::elm::ToggleListItem("Açılışta başlat", fanAcilistaBasliyor());
        acilis->setStateChangedListener([](bool d) {
            if (!fanModuluKurulu()) { bildir("Fan modülü kurulu değil"); return; }
            fanAcilistaBaslat(d);
        });
        list->addItem(acilis);
        list->addItem(menuSatiri("Modülü durdur", [] {
            bildir(fanModuluDurdur() ? "Fan sisteme devredildi" : "Durdurulamadı");
        }));

        list->addItem(bilgiKutusu({
            "Kırmızı çizgi güvenlik tabanıdır: hangi",
            "mod seçilirse seçilsin fan bunun altına",
            "inmez (70°C'de en az %70, 76°C'de %100).",
            "Otomatik'te modül fana dokunmaz; sorun",
            "olursa fan kendiliğinden sisteme döner.",
        }), bilgiYuksekligi(5));

        frame->setContent(list);
        return frame;
    }

    void update() override {
        guncelle();
        if (fanAyari.mod != m_oncekiMod) {
            m_oncekiMod = fanAyari.mod;
            static const char* aciklama[KMFAN_MOD_SAYISI] = { "sistem", "düşük ses", "önerilen", "en serin", "" };
            for (int m = 0; m < KMFAN_MOD_SAYISI; ++m) {
                if (!m_modlar[m]) continue;
                if (m == KMFAN_SABIT && m != fanAyari.mod) m_modlar[m]->setValue("%" + std::to_string(fanAyari.sabitYuzde), true);
                else m_modlar[m]->setValue(m == fanAyari.mod ? ult::CHECKMARK_SYMBOL : aciklama[m], m != fanAyari.mod);
            }
        }
        static u64 sonDurum = 0;
        const u64 t = armGetSystemTick();
        if (m_modulDurumu && armTicksToNs(t - sonDurum) > 1000000000ULL) {
            sonDurum = t;
            const int d = modulDurumKodu();
            if (d != m_oncekiDurum) {
                m_oncekiDurum = d;
                m_modulDurumu->setValue(d == 0 ? "kurulu değil" : d == 1 ? "durdu" : "çalışıyor");
            }
        }
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Mac bağlantısı (km-ag)
// ═════════════════════════════════════════════════════════════════════════════
static constexpr s32 MAC_PANEL_H = 168;

static void macPaneliCiz(R* r, s32 x, s32 y, s32 w, s32) {
    const auto& a = anlik;
    const bool bagli = a.netOk && a.netDurum == NifmInternetConnectionStatus_Connected && a.ip;
    kart(r, x, y + 4, w, MAC_PANEL_H - 8);

    const char* durum = !agAyari.acik ? "Kapalı" : !agModuluCalisiyor() ? "Modül çalışmıyor" : !bagli ? "Wi-Fi bağlı değil" : "Açık · Mac'ten bağlanabilirsin";
    const tsl::Color durumRenk = !agAyari.acik ? renk::soluk() : (agModuluCalisiyor() && bagli) ? renk::yesil() : renk::sari();
    r->drawString(durum, false, x + 14, y + 28, 16, R::a(durumRenk), w - 28);

    r->drawString("Safari'de aç", false, x + 14, y + 58, 14, R::a(renk::etiket()));
    char adres[48];
    if (bagli) snprintf(adres, sizeof(adres), "http://%s:%d", ipMetni(a.ip).c_str(), agAyari.port);
    else snprintf(adres, sizeof(adres), "-");
    r->drawString(adres, false, x + 14, y + 86, 22, R::a(agAyari.acik ? renk::metin() : renk::soluk()), w - 28);

    r->drawString("PIN", false, x + 14, y + 116, 14, R::a(renk::etiket()));
    std::string pin = agAyari.pin[0] ? std::string(agAyari.pin) : "------";
    std::string aralikli;
    for (size_t i = 0; i < pin.size(); ++i) { if (i == 3) aralikli += "  "; aralikli += pin[i]; aralikli += ' '; }
    r->drawString(aralikli, true, x + 14, y + 148, 30, R::a(agAyari.acik ? tsl::onTextColor : renk::soluk()));
}

class MacGui : public tsl::Gui {
    tsl::elm::ToggleListItem* m_acik = nullptr;
    tsl::elm::ListItem* m_modulDurumu = nullptr;
    u64 m_sonKontrol = 0;
    int m_oncekiDurum = -1;

    static std::string otoMetni() { return agAyari.otokapat ? std::to_string(agAyari.otokapat) + " dk" : "Kapanmaz"; }

public:
    MacGui() { agAyariOku(); }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Mac Bağlantısı", "Kablosuz erişim · km-ag");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CustomDrawer(macPaneliCiz), MAC_PANEL_H);

        list->addItem(new tsl::elm::CategoryHeader("Bağlantı"));
        m_acik = new tsl::elm::ToggleListItem("Kablosuz erişim", agAyari.acik != 0);
        m_acik->setStateChangedListener([](bool d) {
            if (d && !agModuluKurulu()) { bildir("Mac modülü kurulu değil (README)"); return; }
            if (d && !agAyari.pin[0]) yeniPin();
            agAyari.acik = d;
            if (!agAyariYaz()) { bildir("Ayar yazılamadı"); return; }
            if (d && !agModuluCalisiyor()) bildir(agModuluBaslat() ? "Modül başlatıldı" : "Modül başlatılamadı");
            else bildir(d ? "Erişim açık" : "Erişim kapalı");
            // Açılışta başlamıyorsa kapatınca modülü tamamen durdur: bellek boşalsın
            if (!d && srv.pmshell && !agAcilistaBasliyor() && agModuluCalisiyor()) {
                svcSleepThread(1200000000ULL); // dinleyiciyi kendisi kapatsın
                pmshellTerminateProgram(KMAG_TID);
            }
        });
        list->addItem(m_acik);

        list->addItem(menuSatiri("Yeni PIN oluştur", [] {
            yeniPin();
            agAyariYaz();
            bildir("Yeni PIN: açık oturumlar kapandı");
        }));

        auto* oto = new tsl::elm::ListItem("Otomatik kapanma", otoMetni());
        oto->setClickListener([oto](u64 k) {
            if (!(k & KEY_A)) return false;
            static const int secenek[] = { 15, 30, 60, 0 };
            size_t i = 0;
            while (i < 4 && secenek[i] != agAyari.otokapat) ++i;
            agAyari.otokapat = secenek[(i + 1) % 4];
            agAyariYaz();
            oto->setValue(otoMetni());
            return true;
        });
        list->addItem(oto);

        list->addItem(new tsl::elm::CategoryHeader("Modül"));
        m_modulDurumu = new tsl::elm::ListItem("Durum");
        list->addItem(m_modulDurumu);
        auto* acilis = new tsl::elm::ToggleListItem("Açılışta başlat", agAcilistaBasliyor());
        acilis->setStateChangedListener([](bool d) {
            if (!agModuluKurulu()) { bildir("Mac modülü kurulu değil"); return; }
            agAcilistaBaslat(d);
        });
        list->addItem(acilis);

        list->addItem(bilgiKutusu({
            "Mac ve Switch aynı Wi-Fi'da olmalı.",
            "Safari'ye adresi yaz, PIN'i gir. Mac'e",
            "hiçbir şey kurman gerekmez.",
            "Mac'ten: durum, dosya yükle/indir, albüm.",
            "/Nintendo, /bootloader ve Atmosphère",
            "çekirdeği korumalı: yazılamaz.",
            "Konsol uyurken bağlantı kopar.",
        }), bilgiYuksekligi(7));

        frame->setContent(list);
        return frame;
    }

    void update() override {
        guncelle();
        const u64 t = armGetSystemTick();
        if (armTicksToNs(t - m_sonKontrol) < 1000000000ULL) return;
        m_sonKontrol = t;
        // Modül hareketsizlikten kendini kapatmış olabilir: ayarı tekrar oku
        const int eski = agAyari.acik;
        agAyariOku();
        if (m_acik && agAyari.acik != eski) m_acik->setState(agAyari.acik != 0);
        const int d = !agModuluKurulu() ? 0 : (agModuluCalisiyor() ? 2 : 1);
        if (m_modulDurumu && d != m_oncekiDurum) {
            m_oncekiDurum = d;
            m_modulDurumu->setValue(d == 0 ? "kurulu değil" : d == 1 ? "durdu" : "çalışıyor");
        }
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Hızlı işlemler
// ═════════════════════════════════════════════════════════════════════════════
class HizliIslemGui : public tsl::Gui {
    int m_gucOnay = -1; // 0 = yeniden başlat, 1 = kapat
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Hızlı İşlemler", "Kontrol Merkezi");
        auto* list = new tsl::elm::List();

        if (srv.lbl) {
            list->addItem(new tsl::elm::CategoryHeader("Ekran parlaklığı"));
            auto* parlaklik = new tsl::elm::TrackBar("☀");
            float p = 0.5f;
            lblGetCurrentBrightnessSetting(&p);
            parlaklik->setProgress(static_cast<u16>(p * 100.0f + 0.5f));
            parlaklik->setValueChangedListener([](u16 v) {
                lblSetCurrentBrightnessSetting(std::clamp(v, static_cast<u16>(1), static_cast<u16>(100)) / 100.0f);
            });
            list->addItem(parlaklik);
        }

        if (srv.sysclk) {
            list->addItem(new tsl::elm::CategoryHeader("sys-clk"));
            auto* acik = new tsl::elm::ToggleListItem("Profilleri uygula", anlik.ctxOk ? anlik.ctx.enabled != 0 : true);
            acik->setStateChangedListener([](bool durum) {
                if (R_SUCCEEDED(sysclkIpcSetEnabled(durum))) bildir(durum ? "sys-clk açık" : "sys-clk kapalı, stok değerler");
            });
            list->addItem(acik);

            list->addItem(menuSatiri("Geçici ayarları temizle", [] {
                for (int m = 0; m < SysClkModule_EnumMax; ++m) sysclkIpcRemoveOverride(static_cast<SysClkModule>(m));
                bildir("Geçici frekanslar temizlendi");
            }));
        }

        list->addItem(new tsl::elm::CategoryHeader("Diğer overlay'ler"));
        struct Kisayol { const char* ad; const char* anahtar; const char* arguman; };
        static const Kisayol kisayollar[] = {
            { "Performans HUD (mini)", "status", "-mini" },
            { "FPS sayacı", "status", "-fps_counter" },
            { "FPS hedefi (FPSLocker)", "fpslocker", "" },
            { "sys-clk overlay", "sys-clk", "" },
        };
        bool herhangi = false;
        for (const auto& ks : kisayollar) {
            const std::string yol = overlayBul(ks.anahtar);
            if (yol.empty()) continue;
            herhangi = true;
            const std::string arg = ks.arguman;
            list->addItem(menuSatiri(ks.ad, [yol, arg] { overlayAc(yol, arg); }, ult::getNameFromPath(yol)));
        }
        if (!herhangi) list->addItem(bilgiKutusu({ "Status Monitor / FPSLocker bulunamadı." }), bilgiYuksekligi(1));

        list->addItem(new tsl::elm::CategoryHeader("Bildirimler"));
        list->addItem(menuSatiri("Ultrahand bildirimi dene", [] {
            bildir(ultrahandBildirimiGonder("Kontrol Merkezi", "Bildirim kanalı çalışıyor") ? "Gönderildi" : "Yazılamadı");
        }));

        list->addItem(new tsl::elm::CategoryHeader("Güç (iki kez A)"));
        static const char* gucAdlari[2] = { "Yeniden başlat", "Kapat" };
        for (int i = 0; i < 2; ++i) {
            auto* item = new tsl::elm::ListItem(gucAdlari[i]);
            item->setClickListener([this, i, item](u64 k) {
                if (!(k & KEY_A)) return false;
                if (m_gucOnay != i) {
                    m_gucOnay = i;
                    item->setValue("Emin misin? A");
                    return true;
                }
                spsmShutdown(i == 0);
                return true;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Yedekler
// ═════════════════════════════════════════════════════════════════════════════
class YedekGui : public tsl::Gui {
    std::string m_onayBekleyen;
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Yedekler", "sys-clk config.ini");
        auto* list = new tsl::elm::List();

        list->addItem(menuSatiri("Şimdi yedek al", [] {
            if (yedekAl("elle")) {
                bildir("Yedek alındı");
                tsl::swapTo<YedekGui>();
            } else {
                bildir("config.ini bulunamadı");
            }
        }));

        auto yedekler = yedekleriListele();
        list->addItem(new tsl::elm::CategoryHeader(yedekler.empty() ? "Yedek yok" : "Geri yükle (iki kez A)"));
        for (const auto& ad : yedekler) {
            // 20261105-191500_oto.ini → "05.11 19:15 · oto"
            std::string etiket = ad;
            if (ad.size() > 16) {
                etiket = ad.substr(6, 2) + "." + ad.substr(4, 2) + " " + ad.substr(9, 2) + ":" + ad.substr(11, 2);
                size_t alt = ad.find('_');
                if (alt != std::string::npos) etiket += " · " + ad.substr(alt + 1, ad.size() - alt - 5);
            }
            auto* item = new tsl::elm::ListItem(etiket);
            item->setClickListener([this, ad, item](u64 k) {
                if (!(k & KEY_A)) return false;
                if (m_onayBekleyen != ad) {
                    m_onayBekleyen = ad;
                    item->setValue("Emin misin? A");
                    return true;
                }
                bildir(yedekGeriYukle(ad) ? "Geri yüklendi" : "Geri yüklenemedi");
                tsl::swapTo<YedekGui>();
                return true;
            });
            list->addItem(item);
        }

        list->addItem(bilgiKutusu({
            "Konum: /config/kontrol-merkezi/yedek/",
            "Son 10 yedek tutulur.",
        }), bilgiYuksekligi(2));

        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Ayarlar
// ═════════════════════════════════════════════════════════════════════════════
class AyarlarGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Ayarlar", "Kontrol Merkezi");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Isı uyarısı"));
        auto* uyari = new tsl::elm::ToggleListItem("Panel açıkken uyar", ayar.isiUyarisi);
        uyari->setStateChangedListener([](bool d) { ayar.isiUyarisi = d; ayarlariYaz(); });
        list->addItem(uyari);

        auto* esik = new tsl::elm::ListItem("Uyarı eşiği", std::to_string(ayar.isiEsigi) + "°C");
        esik->setClickListener([esik](u64 k) {
            if (!(k & KEY_A)) return false;
            size_t i = 0;
            const size_t n = sizeof(ESIK_SECENEKLERI) / sizeof(ESIK_SECENEKLERI[0]);
            while (i < n && ESIK_SECENEKLERI[i] != ayar.isiEsigi) ++i;
            ayar.isiEsigi = ESIK_SECENEKLERI[(i + 1) % n];
            ayarlariYaz();
            esik->setValue(std::to_string(ayar.isiEsigi) + "°C");
            return true;
        });
        list->addItem(esik);

        list->addItem(new tsl::elm::CategoryHeader("Güvenlik"));
        auto* oc = new tsl::elm::ToggleListItem("Overclock için onay iste", ayar.ocOnayi);
        oc->setStateChangedListener([](bool d) { ayar.ocOnayi = d; ayarlariYaz(); });
        list->addItem(oc);

        list->addItem(new tsl::elm::CategoryHeader("Veriler"));
        list->addItem(menuSatiri("Pil günlüğünü sıfırla", [] {
            remove(PIL_GUNLUGU);
            bildir("Pil günlüğü sıfırlandı");
        }));

        list->addItem(bilgiKutusu({
            "Eşiği değiştirmek için A'ya bas (60-80°C).",
            "Ayarlar: /config/kontrol-merkezi/ayarlar.ini",
        }), bilgiYuksekligi(2));

        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Sistem kontrolü (sürümler ve kurulu araçlar)
// ═════════════════════════════════════════════════════════════════════════════
class SistemGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Sistem Kontrolü", std::string("Kontrol Merkezi v") + SURUM);
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Sürümler"));
        list->addItem(new tsl::elm::ListItem("Model", modelAdi()));
        list->addItem(new tsl::elm::ListItem("Firmware (HOS)", hosSurumu()));
        list->addItem(new tsl::elm::ListItem("Atmosphère", amsSurumu()));
        std::string sc = srv.sysclk ? std::string(srv.sysclkSurum)
                       : srv.sysclkVar ? "uyumsuz (API " + std::to_string(srv.sysclkApi) + ")" : "çalışmıyor";
        list->addItem(new tsl::elm::ListItem("sys-clk", sc));

        list->addItem(new tsl::elm::CategoryHeader("Kurulu araçlar"));
        struct Arac { const char* ad; std::string yol; };
        const Arac araclar[] = {
            { "sys-clk modülü", "sdmc:/atmosphere/contents/00FF0000636C6BFF/exefs.nsp" },
            { "SaltyNX",        "sdmc:/atmosphere/contents/0000000000534C56/exefs.nsp" },
            { "km-fan modülü",  KMFAN_MODUL },
            { "km-ag modülü",   KMAG_MODUL },
        };
        for (const auto& a : araclar)
            list->addItem(new tsl::elm::ListItem(a.ad, dosyaVar(a.yol) ? ult::CHECKMARK_SYMBOL : ult::CROSSMARK_SYMBOL));

        struct Ovl { const char* ad; const char* anahtar; };
        const Ovl ovller[] = { { "FPSLocker", "fpslocker" }, { "Status Monitor", "status" }, { "sys-clk overlay", "sys-clk" } };
        for (const auto& o : ovller)
            list->addItem(new tsl::elm::ListItem(o.ad, overlayBul(o.anahtar).empty() ? ult::CROSSMARK_SYMBOL : ult::CHECKMARK_SYMBOL));

        list->addItem(new tsl::elm::CategoryHeader("Servisler"));
        auto durum = [](bool b) { return b ? ult::CHECKMARK_SYMBOL : ult::CROSSMARK_SYMBOL; };
        list->addItem(new tsl::elm::ListItem("Pil (psm)", durum(srv.psm)));
        list->addItem(new tsl::elm::ListItem("Ağ (nifm)", durum(srv.nifm)));
        list->addItem(new tsl::elm::ListItem("Oyun adları (ns)", durum(srv.ns)));
        list->addItem(new tsl::elm::ListItem("Oynama süresi (pdm)", durum(srv.pdm)));
        list->addItem(new tsl::elm::ListItem("Parlaklık (lbl)", durum(srv.lbl)));
        list->addItem(new tsl::elm::ListItem("Fan okuma (pwm)", durum(srv.pwm)));
        list->addItem(new tsl::elm::ListItem("Modül yönetimi (pm)", durum(srv.pmshell)));

        frame->setContent(list);
        return frame;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Ana ekran
// ═════════════════════════════════════════════════════════════════════════════
class AnaGui : public tsl::Gui {
    tsl::elm::ListItem* m_buOyun = nullptr;
    std::string m_oncekiDeger;
    u64 m_oncekiTid = ~0ULL;

    void satiriGuncelle() {
        const u64 hedef = anlik.tid ? anlik.tid : anlik.sonOyun;
        std::string deger;
        if (!srv.sysclk) deger = "sys-clk yok";
        else if (!hedef) deger = "Oyun yok";
        else deger = profilAdi(anlik.profil);

        if (m_buOyun && (deger != m_oncekiDeger || hedef != m_oncekiTid)) {
            m_buOyun->setText(anlik.tid ? "Bu oyunun profili" : "Son oyunun profili");
            m_buOyun->setValue(deger);
            m_oncekiDeger = deger;
            m_oncekiTid = hedef;
        }
    }

public:
    tsl::elm::Element* createUI() override {
        guncelle(true);
        auto* frame = new tsl::elm::OverlayFrame("Kontrol Merkezi", modelAdi() + " · v" + SURUM);
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CustomDrawer(paneliCiz), PANEL_YUKSEKLIK);

        list->addItem(new tsl::elm::CategoryHeader("Performans"));
        m_buOyun = menuSatiri("Bu oyunun profili", [] {
            const u64 hedef = anlik.tid ? anlik.tid : anlik.sonOyun;
            if (!hedef) { bildir("Önce bir oyun aç"); return; }
            tsl::changeTo<ProfilGui>(hedef);
        });
        list->addItem(m_buOyun);
        list->addItem(menuSatiri("Tüm oyun profilleri", [] { tsl::changeTo<OyunListesiGui>(); }));

        list->addItem(new tsl::elm::CategoryHeader("Detaylar"));
        list->addItem(menuSatiri("Fan kontrolü", [] { tsl::changeTo<FanGui>(); }, kmfanModAdi(fanAyari.mod)));
        list->addItem(menuSatiri("Canlı izleme", [] { tsl::changeTo<CanliIzlemeGui>(); }, "grafikler"));
        list->addItem(menuSatiri("Batarya", [] { tsl::changeTo<BataryaGui>(); }, "sağlık · uyku"));
        list->addItem(menuSatiri("Depolama ve ağ", [] { tsl::changeTo<DepolamaGui>(); }));

        list->addItem(new tsl::elm::CategoryHeader("Sistem"));
        list->addItem(menuSatiri("Mac bağlantısı", [] { tsl::changeTo<MacGui>(); }, agAyari.acik ? "açık" : "kapalı"));
        list->addItem(menuSatiri("Hızlı işlemler", [] { tsl::changeTo<HizliIslemGui>(); }));
        list->addItem(menuSatiri("Yedekler", [] { tsl::changeTo<YedekGui>(); }));
        list->addItem(menuSatiri("Ayarlar", [] { tsl::changeTo<AyarlarGui>(); }));
        list->addItem(menuSatiri("Sistem kontrolü", [] { tsl::changeTo<SistemGui>(); }));

        satiriGuncelle();
        frame->setContent(list);
        return frame;
    }

    void update() override {
        guncelle();
        satiriGuncelle();
    }
};

// ═════════════════════════════════════════════════════════════════════════════
class KontrolMerkezi : public tsl::Overlay {
public:
    void initServices() override {
        servisleriBaslat();
        ayarlariOku();
        fanAyariOku();
        agAyariOku();
        anlik.sonOyun = sonOyunuOku();
    }
    void exitServices() override { servisleriKapat(); }
    void onShow() override {
        guncelle(true);
        pilKaydiEkle();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<AnaGui>(); }
};

int main(int argc, char** argv) {
    return tsl::loop<KontrolMerkezi>(argc, argv);
}
