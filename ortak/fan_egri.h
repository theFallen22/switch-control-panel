// Kontrol Merkezi — fan eğrileri ve ayar dosyası
// Hem overlay (C++) hem arka plan modülü (C) tarafından kullanılır.
//
// GÜVENLİK: Hangi mod seçilirse seçilsin fan seviyesi asla KMFAN_GUVENLIK
// eğrisinin altına inmez. "Sabit %20" seçsen bile SoC 70°C'yi geçince fan
// en az %70'e, 76°C'de %100'e çıkar.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KMFAN_TID        0x00FF00004B4D4346ULL
#define KMFAN_TID_STR    "00FF00004B4D4346"
#define KMFAN_FAN_KODU   0x3D000001u
#define KMFAN_AYAR       "sdmc:/config/kontrol-merkezi/fan.ini"
#define KMFAN_GUNLUK     "sdmc:/config/kontrol-merkezi/fan.log"
#define KMFAN_MODUL      "sdmc:/atmosphere/contents/" KMFAN_TID_STR "/exefs.nsp"
#define KMFAN_BOOT_FLAG  "sdmc:/atmosphere/contents/" KMFAN_TID_STR "/flags/boot2.flag"

typedef enum {
    KMFAN_OTOMATIK = 0, // sistem yönetir, modül fana dokunmaz
    KMFAN_SESSIZ,
    KMFAN_DENGELI,
    KMFAN_SERIN,
    KMFAN_SABIT,
    KMFAN_MOD_SAYISI
} KmFanMod;

typedef struct { float sicaklik; float seviye; } KmFanNokta; // °C, 0..1
typedef struct { int mod; int sabitYuzde; } KmFanAyar;

static const KmFanNokta KMFAN_EGRI_SESSIZ[]  = { {45, 0.00f}, {55, 0.20f}, {65, 0.45f}, {72, 0.75f}, {78, 1.00f} };
static const KmFanNokta KMFAN_EGRI_DENGELI[] = { {40, 0.15f}, {50, 0.30f}, {60, 0.50f}, {68, 0.75f}, {75, 1.00f} };
static const KmFanNokta KMFAN_EGRI_SERIN[]   = { {35, 0.30f}, {45, 0.50f}, {55, 0.70f}, {62, 0.90f}, {68, 1.00f} };
static const KmFanNokta KMFAN_GUVENLIK[]     = { {55, 0.00f}, {62, 0.40f}, {70, 0.70f}, {76, 1.00f} };

#define KMFAN_ADET(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline float kmfanEgri(const KmFanNokta* n, int adet, float t) {
    if (t <= n[0].sicaklik) return n[0].seviye;
    for (int i = 0; i < adet - 1; ++i) {
        if (t <= n[i + 1].sicaklik) {
            const float oran = (t - n[i].sicaklik) / (n[i + 1].sicaklik - n[i].sicaklik);
            return n[i].seviye + oran * (n[i + 1].seviye - n[i].seviye);
        }
    }
    return n[adet - 1].seviye;
}

static inline float kmfanGuvenlik(float t) {
    return kmfanEgri(KMFAN_GUVENLIK, KMFAN_ADET(KMFAN_GUVENLIK), t);
}

static inline float kmfanSinirla(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Mod için ham hedef (güvenlik tabanı uygulanmadan). Otomatik modda -1.
static inline float kmfanHam(const KmFanAyar* a, float t) {
    switch (a->mod) {
        case KMFAN_SESSIZ:  return kmfanEgri(KMFAN_EGRI_SESSIZ,  KMFAN_ADET(KMFAN_EGRI_SESSIZ),  t);
        case KMFAN_DENGELI: return kmfanEgri(KMFAN_EGRI_DENGELI, KMFAN_ADET(KMFAN_EGRI_DENGELI), t);
        case KMFAN_SERIN:   return kmfanEgri(KMFAN_EGRI_SERIN,   KMFAN_ADET(KMFAN_EGRI_SERIN),   t);
        case KMFAN_SABIT:   return kmfanSinirla(a->sabitYuzde / 100.0f);
        default:            return -1.0f;
    }
}

// Uygulanacak son hedef: ham değer ile güvenlik tabanının büyüğü. Otomatik modda -1.
static inline float kmfanHedef(const KmFanAyar* a, float t) {
    const float ham = kmfanHam(a, t);
    if (ham < 0.0f) return -1.0f;
    const float taban = kmfanGuvenlik(t);
    return kmfanSinirla(ham > taban ? ham : taban);
}

static inline const char* kmfanModAdi(int m) {
    switch (m) {
        case KMFAN_OTOMATIK: return "Otomatik (sistem)";
        case KMFAN_SESSIZ:   return "Sessiz";
        case KMFAN_DENGELI:  return "Dengeli";
        case KMFAN_SERIN:    return "Serin";
        case KMFAN_SABIT:    return "Sabit hız";
        default:             return "?";
    }
}

static inline void kmfanVarsayilan(KmFanAyar* a) {
    a->mod = KMFAN_OTOMATIK;
    a->sabitYuzde = 60;
}

static inline void kmfanAyarOku(KmFanAyar* a) {
    kmfanVarsayilan(a);
    FILE* f = fopen(KMFAN_AYAR, "r");
    if (!f) return;
    char satir[64];
    while (fgets(satir, sizeof(satir), f)) {
        char* eq = strchr(satir, '=');
        if (!eq) continue;
        *eq = '\0';
        const long v = strtol(eq + 1, NULL, 10);
        if (!strcmp(satir, "mod") && v >= 0 && v < KMFAN_MOD_SAYISI) a->mod = (int)v;
        else if (!strcmp(satir, "sabit") && v >= 0 && v <= 100) a->sabitYuzde = (int)v;
    }
    fclose(f);
}

static inline int kmfanAyarYaz(const KmFanAyar* a) {
    FILE* f = fopen(KMFAN_AYAR, "w");
    if (!f) return 0;
    fprintf(f, "[fan]\nmod=%d\nsabit=%d\n", a->mod, a->sabitYuzde);
    fclose(f);
    return 1;
}

#ifdef __cplusplus
}
#endif
