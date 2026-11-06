// Kontrol Merkezi — Mac bağlantısı (km-ag) ayarları
// Overlay yazar, km-ag modülü okur. Modül sadece otomatik kapanmada "acik=0" yazar.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KM_KOK
#define KM_KOK "sdmc:"   // testte başka bir klasör verilir
#endif

#define KMAG_TID        0x00FF00004B4D4147ULL
#define KMAG_TID_STR    "00FF00004B4D4147"
#define KMAG_AYAR       KM_KOK "/config/kontrol-merkezi/ag.ini"
#define KMAG_MODUL      KM_KOK "/atmosphere/contents/" KMAG_TID_STR "/exefs.nsp"
#define KMAG_BOOT_FLAG  KM_KOK "/atmosphere/contents/" KMAG_TID_STR "/flags/boot2.flag"
#define KMAG_VARSAYILAN_PORT 8080

typedef struct {
    int  acik;        // 1 = sunucu dinliyor
    char pin[8];      // 6 haneli
    int  port;
    int  otokapat;    // dakika, 0 = kapanmaz
} KmAgAyar;

static inline void kmagVarsayilan(KmAgAyar* a) {
    a->acik = 0;
    a->pin[0] = '\0';
    a->port = KMAG_VARSAYILAN_PORT;
    a->otokapat = 30;
}

static inline void kmagAyarOku(KmAgAyar* a) {
    kmagVarsayilan(a);
    FILE* f = fopen(KMAG_AYAR, "r");
    if (!f) return;
    char satir[64];
    while (fgets(satir, sizeof(satir), f)) {
        char* eq = strchr(satir, '=');
        if (!eq) continue;
        *eq = '\0';
        char* deger = eq + 1;
        deger[strcspn(deger, "\r\n")] = '\0';
        if (!strcmp(satir, "acik")) a->acik = atoi(deger) != 0;
        else if (!strcmp(satir, "port")) { int p = atoi(deger); if (p >= 1024 && p <= 65535) a->port = p; }
        else if (!strcmp(satir, "otokapat")) { int d = atoi(deger); if (d >= 0 && d <= 600) a->otokapat = d; }
        else if (!strcmp(satir, "pin")) {
            size_t n = strlen(deger), ok = (n == 6);
            for (size_t i = 0; ok && i < n; ++i) ok = deger[i] >= '0' && deger[i] <= '9';
            if (ok) memcpy(a->pin, deger, 7);
        }
    }
    fclose(f);
}

static inline int kmagAyarYaz(const KmAgAyar* a) {
    FILE* f = fopen(KMAG_AYAR, "w");
    if (!f) return 0;
    fprintf(f, "[ag]\nacik=%d\npin=%s\nport=%d\notokapat=%d\n", a->acik ? 1 : 0, a->pin, a->port, a->otokapat);
    fclose(f);
    return 1;
}

#ifdef __cplusplus
}
#endif
