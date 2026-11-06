// km-ag — Kontrol Merkezi Mac bağlantısı: küçük, PIN korumalı HTTP sunucusu.
//
// Güvenlik kuralları:
//  * Sunucu varsayılan olarak KAPALI; overlay'den açılır. Açıkken bile her veri
//    isteği geçerli oturum çerezi ister. Çerez sadece doğru PIN ile alınır.
//  * 5 yanlış PIN → 60 sn kilit. PIN değişince eski oturumlar geçersiz olur.
//  * Yollar normalize edilir; ".." ile SD kökünün dışına çıkılamaz.
//  * /Nintendo, /bootloader ve Atmosphère'in çekirdek dosyalarına yazılamaz
//    (sadece /Nintendo/Album'den silinebilir).
//  * SD'deki dosyalar asla text/html olarak sunulmaz (tarayıcıda kod çalışmaz).
//  * Hareketsiz kalırsa (varsayılan 30 dk) kendini kapatır.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ag_ayar.h"
#include "arayuz_html.h"
#include "platform.h"
#include "sunucu.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define BASLIK_MAKS   8192
#define TAMPON        65536
#define YOL_MAKS      768
#define TOKEN_UZ      32
#define ALBUM_MAKS    6000
#define JSON_MAKS     (1024 * 1024)
#define BEKLEME_MS    15000
#define DOSYA_MAKS    0xFFFFFFFFLL   // FAT32 dosya sınırı

static char     g_baslik[BASLIK_MAKS + 1];
static char     g_tampon[TAMPON];
static char     g_token[TOKEN_UZ + 1];
static char     g_tokenPin[8];
static KmAgAyar g_ayar;
static int      g_yanlis = 0;
static uint64_t g_kilitBitis = 0;
static uint64_t g_sonEtkinlik = 0;

typedef struct {
    char yontem[8];
    char rota[128];
    char sorgu[YOL_MAKS * 3];
    long long uzunluk;
    char cerez[256];
    size_t govdeBas, govdeUz; // g_baslik içinde başlıktan sonra gelen gövde baytları
} Istek;

// ─── Dinamik metin tamponu ───────────────────────────────────────────────────
typedef struct { char* b; size_t n, kap; int hata; } Yazi;

static void yEkle(Yazi* y, const char* s, size_t n) {
    if (y->hata) return;
    if (y->n + n + 1 > y->kap) {
        size_t yeni = y->kap ? y->kap * 2 : 4096;
        while (yeni < y->n + n + 1) yeni *= 2;
        if (yeni > JSON_MAKS) { y->hata = 1; return; }
        char* t = realloc(y->b, yeni);
        if (!t) { y->hata = 1; return; }
        y->b = t; y->kap = yeni;
    }
    memcpy(y->b + y->n, s, n);
    y->n += n;
    y->b[y->n] = '\0';
}

static void yYaz(Yazi* y, const char* fmt, ...) {
    char t[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof(t), fmt, ap);
    va_end(ap);
    if (n > 0) yEkle(y, t, (size_t)n < sizeof(t) ? (size_t)n : sizeof(t) - 1);
}

static void yJson(Yazi* y, const char* s) {
    yEkle(y, "\"", 1);
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { char e[2] = { '\\', (char)*p }; yEkle(y, e, 2); }
        else if (*p < 0x20) yYaz(y, "\\u%04x", *p);
        else yEkle(y, (const char*)p, 1);
    }
    yEkle(y, "\"", 1);
}

// ─── Soket yardımcıları ──────────────────────────────────────────────────────
static int bekle(int fd, short olay, int ms) {
    struct pollfd p = { .fd = fd, .events = olay, .revents = 0 };
    int r;
    do { r = poll(&p, 1, ms); } while (r < 0 && errno == EINTR);
    return r > 0 && (p.revents & olay);
}

static int tamGonder(int fd, const void* veri, size_t n) {
    const char* p = (const char*)veri;
    while (n > 0) {
        if (!bekle(fd, POLLOUT, BEKLEME_MS)) return -1;
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -1; }
        p += k; n -= (size_t)k;
    }
    return 0;
}

static ssize_t al(int fd, void* buf, size_t n) {
    if (!bekle(fd, POLLIN, BEKLEME_MS)) return -1;
    ssize_t k;
    do { k = recv(fd, buf, n, 0); } while (k < 0 && errno == EINTR);
    return k;
}

static const char* durumMetni(int kod) {
    switch (kod) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        default:  return "Internal Server Error";
    }
}

static int yanitBasligi(int fd, int kod, const char* tur, long long uz, const char* ek) {
    char b[1024];
    int n = snprintf(b, sizeof(b),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "%s"
        "\r\n",
        kod, durumMetni(kod), tur, uz, ek ? ek : "");
    if (n <= 0 || n >= (int)sizeof(b)) return -1;
    return tamGonder(fd, b, (size_t)n);
}

static int yanit(int fd, int kod, const char* tur, const char* govde, size_t uz, const char* ek) {
    if (yanitBasligi(fd, kod, tur, (long long)uz, ek) < 0) return -1;
    return uz ? tamGonder(fd, govde, uz) : 0;
}

static int jsonYanit(int fd, int kod, Yazi* y) {
    if (y->hata) { free(y->b); return yanit(fd, 500, "application/json", "{\"hata\":\"liste cok buyuk\"}", 26, NULL); }
    int r = yanit(fd, kod, "application/json; charset=utf-8", y->b ? y->b : "{}", y->b ? y->n : 2, NULL);
    free(y->b);
    return r;
}

static int hata(int fd, int kod, const char* mesaj) {
    Yazi y = {0};
    yEkle(&y, "{\"hata\":", 8);
    yJson(&y, mesaj);
    yEkle(&y, "}", 1);
    return jsonYanit(fd, kod, &y);
}

// ─── URL ve yol işleme ───────────────────────────────────────────────────────
static int hexDeger(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL çözümü; %00 ve bozuk kodlamayı reddeder
static int urlCoz(const char* s, size_t uz, char* out, size_t maks) {
    size_t o = 0;
    for (size_t i = 0; i < uz; ++i) {
        char c = s[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= uz) return 0;
            int a = hexDeger(s[i + 1]), b = hexDeger(s[i + 2]);
            if (a < 0 || b < 0) return 0;
            c = (char)(a * 16 + b);
            if (c == 0) return 0;
            i += 2;
        }
        if (o + 1 >= maks) return 0;
        out[o++] = c;
    }
    out[o] = '\0';
    return 1;
}

static int sorguDeger(const char* sorgu, const char* anahtar, char* out, size_t maks) {
    const size_t ka = strlen(anahtar);
    const char* p = sorgu;
    while (p && *p) {
        const char* son = strchr(p, '&');
        const size_t uz = son ? (size_t)(son - p) : strlen(p);
        if (uz > ka && !strncmp(p, anahtar, ka) && p[ka] == '=') return urlCoz(p + ka + 1, uz - ka - 1, out, maks);
        p = son ? son + 1 : NULL;
    }
    return 0;
}

// "/a/./b//c" → "/a/b/c". ".." veya kontrol karakteri varsa reddeder.
static int yolTemizle(const char* girdi, char* cikti, size_t maks) {
    if (girdi[0] != '/') return 0;
    size_t o = 0;
    const char* p = girdi;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        const char* bas = p;
        while (*p && *p != '/') {
            if ((unsigned char)*p < 0x20 || *p == '\\' || *p == ':') return 0;
            ++p;
        }
        const size_t uz = (size_t)(p - bas);
        if (uz == 1 && bas[0] == '.') continue;
        if (uz == 2 && bas[0] == '.' && bas[1] == '.') return 0;
        if (o + uz + 2 >= maks) return 0;
        cikti[o++] = '/';
        memcpy(cikti + o, bas, uz);
        o += uz;
    }
    if (o == 0) cikti[o++] = '/';
    cikti[o] = '\0';
    return 1;
}

static void tamYol(const char* yol, char* out, size_t maks) {
    snprintf(out, maks, "%s%s", KM_KOK, yol);
}

static int onEk(const char* yol, const char* on) {
    const size_t n = strlen(on);
    return !strncasecmp(yol, on, n) && (yol[n] == '\0' || yol[n] == '/');
}

enum { IZIN_YAZ, IZIN_SIL };

static int izinli(const char* yol, int islem) {
    if (!strcmp(yol, "/")) return 0;
    if (onEk(yol, "/Nintendo/Album")) return islem == IZIN_SIL && strcasecmp(yol, "/Nintendo/Album") != 0;
    if (onEk(yol, "/Nintendo") || onEk(yol, "/bootloader") || onEk(yol, "/emuMMC")) return 0;
    static const char* kritik[] = { "/payload.bin", "/boot.dat", "/boot.ini", "/hbmenu.nro" };
    for (size_t i = 0; i < sizeof(kritik) / sizeof(kritik[0]); ++i)
        if (!strcasecmp(yol, kritik[i])) return 0;
    if (onEk(yol, "/atmosphere")) {
        static const char* serbest[] = { "/atmosphere/contents", "/atmosphere/config", "/atmosphere/exefs_patches",
                                         "/atmosphere/kip_patches", "/atmosphere/hosts" };
        for (size_t i = 0; i < sizeof(serbest) / sizeof(serbest[0]); ++i)
            if (onEk(yol, serbest[i]) && strcasecmp(yol, serbest[i]) != 0) return 1;
        return 0;
    }
    return 1;
}

static const char* dosyaTuru(const char* ad, int* gosterilebilir) {
    const char* n = strrchr(ad, '.');
    *gosterilebilir = 0;
    if (!n) return "application/octet-stream";
    if (!strcasecmp(n, ".jpg") || !strcasecmp(n, ".jpeg")) { *gosterilebilir = 1; return "image/jpeg"; }
    if (!strcasecmp(n, ".png")) { *gosterilebilir = 1; return "image/png"; }
    if (!strcasecmp(n, ".mp4")) { *gosterilebilir = 1; return "video/mp4"; }
    if (!strcasecmp(n, ".txt") || !strcasecmp(n, ".ini") || !strcasecmp(n, ".log") ||
        !strcasecmp(n, ".json") || !strcasecmp(n, ".csv") || !strcasecmp(n, ".md")) {
        *gosterilebilir = 1;
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

// ─── Oturum ──────────────────────────────────────────────────────────────────
static void tokenYenile(void) {
    unsigned char r[TOKEN_UZ / 2];
    platformRastgele(r, sizeof(r));
    for (size_t i = 0; i < sizeof(r); ++i) snprintf(g_token + i * 2, 3, "%02x", r[i]);
    memcpy(g_tokenPin, g_ayar.pin, sizeof(g_tokenPin));
}

static int oturumVar(const Istek* r) {
    const char* p = r->cerez;
    while ((p = strstr(p, "km=")) != NULL) {
        if (p == r->cerez || p[-1] == ' ' || p[-1] == ';') {
            const char* t = p + 3;
            unsigned fark = 0;
            for (int i = 0; i < TOKEN_UZ; ++i) {
                if (!t[i]) return 0;
                fark |= (unsigned)(t[i] ^ g_token[i]);
            }
            const char s = t[TOKEN_UZ];
            return fark == 0 && (s == '\0' || s == ';' || s == ' ');
        }
        p += 3;
    }
    return 0;
}

// ─── İstek okuma ─────────────────────────────────────────────────────────────
static int istekOku(int fd, Istek* r) {
    memset(r, 0, sizeof(*r));
    r->uzunluk = -1;
    size_t n = 0;
    char* son = NULL;
    while (!son) {
        if (n >= BASLIK_MAKS) return 431;
        ssize_t k = al(fd, g_baslik + n, BASLIK_MAKS - n);
        if (k <= 0) return -1;
        n += (size_t)k;
        g_baslik[n] = '\0';
        son = strstr(g_baslik, "\r\n\r\n");
    }
    r->govdeBas = (size_t)(son - g_baslik) + 4;
    r->govdeUz = n - r->govdeBas;
    *son = '\0';

    // İstek satırı: YÖNTEM HEDEF HTTP/1.x
    char* satir = g_baslik;
    char* sp1 = strchr(satir, ' ');
    if (!sp1 || sp1 - satir >= (long)sizeof(r->yontem)) return 400;
    memcpy(r->yontem, satir, (size_t)(sp1 - satir));
    char* hedef = sp1 + 1;
    char* sp2 = strchr(hedef, ' ');
    if (!sp2 || hedef[0] != '/') return 400;
    *sp2 = '\0';
    char* soru = strchr(hedef, '?');
    if (soru) {
        *soru = '\0';
        if (strlen(soru + 1) >= sizeof(r->sorgu)) return 431;
        strcpy(r->sorgu, soru + 1);
    }
    if (strlen(hedef) >= sizeof(r->rota)) return 404;
    strcpy(r->rota, hedef);

    // Başlıklar
    char* p = strstr(sp2 + 1, "\r\n");
    while (p) {
        p += 2;
        char* sonraki = strstr(p, "\r\n");
        if (sonraki) *sonraki = '\0';
        if (!strncasecmp(p, "Content-Length:", 15)) r->uzunluk = atoll(p + 15);
        else if (!strncasecmp(p, "Cookie:", 7)) {
            const char* v = p + 7;
            while (*v == ' ') ++v;
            snprintf(r->cerez, sizeof(r->cerez), "%s", v);
        }
        p = sonraki;
    }
    return 0;
}

// Küçük gövdeleri (giriş formu) tampona okur
static int kucukGovde(int fd, const Istek* r, char* out, size_t maks) {
    if (r->uzunluk < 0 || (size_t)r->uzunluk >= maks) return 0;
    size_t var = r->govdeUz < (size_t)r->uzunluk ? r->govdeUz : (size_t)r->uzunluk;
    memcpy(out, g_baslik + r->govdeBas, var);
    while (var < (size_t)r->uzunluk) {
        ssize_t k = al(fd, out + var, (size_t)r->uzunluk - var);
        if (k <= 0) return 0;
        var += (size_t)k;
    }
    out[var] = '\0';
    return 1;
}

// ─── Rotalar ─────────────────────────────────────────────────────────────────
static int rGiris(int fd, const Istek* r) {
    const uint64_t simdi = platformMs();
    if (simdi < g_kilitBitis) return hata(fd, 429, "Çok fazla yanlış deneme. 1 dakika bekle.");
    char govde[64], pin[16];
    if (!kucukGovde(fd, r, govde, sizeof(govde)) || !sorguDeger(govde, "pin", pin, sizeof(pin)))
        return hata(fd, 400, "PIN eksik");

    unsigned fark = strlen(pin) != 6 || strlen(g_ayar.pin) != 6;
    for (int i = 0; i < 6 && pin[i] && g_ayar.pin[i]; ++i) fark |= (unsigned)(pin[i] ^ g_ayar.pin[i]);
    if (fark) {
        if (++g_yanlis >= 5) { g_yanlis = 0; g_kilitBitis = simdi + 60000; }
        return hata(fd, 401, "PIN yanlış");
    }
    g_yanlis = 0;
    char ek[160];
    snprintf(ek, sizeof(ek), "Set-Cookie: km=%s; Path=/; HttpOnly; SameSite=Strict\r\n", g_token);
    return yanit(fd, 200, "application/json", "{\"tamam\":true}", 14, ek);
}

static int rCikis(int fd) {
    return yanit(fd, 200, "application/json", "{\"tamam\":true}", 14,
                 "Set-Cookie: km=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n");
}

static void sayiVeyaNull(Yazi* y, const char* ad, int ok, double v, int ondalik) {
    if (ok) yYaz(y, "\"%s\":%.*f,", ad, ondalik, v);
    else yYaz(y, "\"%s\":null,", ad);
}

static int rDurum(int fd) {
    KmDurum d;
    memset(&d, 0, sizeof(d));
    platformDurum(&d);
    Yazi y = {0};
    yEkle(&y, "{", 1);
    sayiVeyaNull(&y, "pil", d.pilOk, d.pil, 0);
    yYaz(&y, "\"sarj\":%s,", d.sarj ? "true" : "false");
    sayiVeyaNull(&y, "soc", d.socOk, d.soc, 1);
    sayiVeyaNull(&y, "pcb", d.pcbOk, d.pcb, 1);
    sayiVeyaNull(&y, "fan", d.fanOk, d.fan, 0);
    sayiVeyaNull(&y, "cpu", d.saatOk, d.cpu, 0);
    sayiVeyaNull(&y, "gpu", d.saatOk, d.gpu, 0);
    sayiVeyaNull(&y, "mem", d.saatOk, d.mem, 0);
    sayiVeyaNull(&y, "ram", d.saatOk, d.ram / 10.0, 0);
    sayiVeyaNull(&y, "guc", d.saatOk, d.guc, 1);
    yYaz(&y, "\"tid\":\"%016llX\",\"oyun\":", (unsigned long long)d.tid);
    yJson(&y, d.oyun);
    yYaz(&y, ",\"otokapat\":%d}", g_ayar.otokapat);
    return jsonYanit(fd, 200, &y);
}

typedef struct { char ad[256]; int klasor; long long boyut; long long zaman; } Oge;

static int ogeSirala(const void* a, const void* b) {
    const Oge* x = a; const Oge* z = b;
    if (x->klasor != z->klasor) return z->klasor - x->klasor;
    return strcasecmp(x->ad, z->ad);
}

static int rListe(int fd, const Istek* r) {
    char ham[YOL_MAKS], yol[YOL_MAKS], tam[YOL_MAKS + 16];
    if (!sorguDeger(r->sorgu, "yol", ham, sizeof(ham))) strcpy(ham, "/");
    if (!yolTemizle(ham, yol, sizeof(yol))) return hata(fd, 400, "Geçersiz yol");
    tamYol(yol, tam, sizeof(tam));
    DIR* d = opendir(tam);
    if (!d) return hata(fd, 404, "Klasör bulunamadı");

    size_t adet = 0, kap = 64;
    Oge* ogeler = malloc(kap * sizeof(Oge));
    if (!ogeler) { closedir(d); return hata(fd, 500, "Bellek yetersiz"); }
    struct dirent* e;
    while ((e = readdir(d)) != NULL && adet < 5000) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (adet == kap) {
            Oge* t = realloc(ogeler, kap * 2 * sizeof(Oge));
            if (!t) break;
            ogeler = t; kap *= 2;
        }
        Oge* o = &ogeler[adet];
        snprintf(o->ad, sizeof(o->ad), "%s", e->d_name);
        char cocuk[YOL_MAKS + 300];
        snprintf(cocuk, sizeof(cocuk), "%s%s%s", tam, tam[strlen(tam) - 1] == '/' ? "" : "/", e->d_name);
        struct stat st;
        if (stat(cocuk, &st) == 0) {
            o->klasor = S_ISDIR(st.st_mode);
            o->boyut = o->klasor ? 0 : (long long)st.st_size;
            o->zaman = (long long)st.st_mtime;
        } else {
            o->klasor = e->d_type == DT_DIR;
            o->boyut = 0;
            o->zaman = 0;
        }
        ++adet;
    }
    closedir(d);
    qsort(ogeler, adet, sizeof(Oge), ogeSirala);

    Yazi y = {0};
    yEkle(&y, "{\"yol\":", 7);
    yJson(&y, yol);
    // Bu klasörün içine yazılabilir mi? İçinde örnek bir yol üzerinden bak
    char ornek[YOL_MAKS + 4];
    snprintf(ornek, sizeof(ornek), "%s/_", strcmp(yol, "/") ? yol : "");
    yYaz(&y, ",\"yazilabilir\":%s,\"ogeler\":[", izinli(ornek, IZIN_YAZ) ? "true" : "false");
    for (size_t i = 0; i < adet; ++i) {
        if (i) yEkle(&y, ",", 1);
        yEkle(&y, "{\"ad\":", 6);
        yJson(&y, ogeler[i].ad);
        yYaz(&y, ",\"klasor\":%s,\"boyut\":%lld,\"zaman\":%lld}", ogeler[i].klasor ? "true" : "false", ogeler[i].boyut, ogeler[i].zaman);
    }
    yEkle(&y, "]}", 2);
    free(ogeler);
    return jsonYanit(fd, 200, &y);
}

typedef struct { char yol[160]; long long boyut; } AlbumOge;
typedef struct { AlbumOge* v; size_t n, kap; } AlbumListe;

static const char* tabanAd(const char* yol) {
    const char* s = strrchr(yol, '/');
    return s ? s + 1 : yol;
}

static int albumSirala(const void* a, const void* b) {
    // Dosya adları zaman damgasıyla başlar: büyük = yeni
    return strcmp(tabanAd(((const AlbumOge*)b)->yol), tabanAd(((const AlbumOge*)a)->yol));
}

static void albumTara(const char* goreli, AlbumListe* l, int derinlik) {
    if (derinlik > 5 || l->n >= ALBUM_MAKS) return;
    char tam[YOL_MAKS];
    tamYol(goreli, tam, sizeof(tam));
    DIR* d = opendir(tam);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && l->n < ALBUM_MAKS) {
        if (e->d_name[0] == '.') continue;
        char cocuk[YOL_MAKS];
        snprintf(cocuk, sizeof(cocuk), "%s/%s", goreli, e->d_name);
        char cocukTam[YOL_MAKS + 16];
        tamYol(cocuk, cocukTam, sizeof(cocukTam));
        struct stat st;
        if (stat(cocukTam, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { albumTara(cocuk, l, derinlik + 1); continue; }
        const char* uz = strrchr(e->d_name, '.');
        if (!uz || (strcasecmp(uz, ".jpg") && strcasecmp(uz, ".mp4"))) continue;
        if (strlen(cocuk) >= sizeof(l->v[0].yol)) continue;
        if (l->n == l->kap) {
            size_t yeni = l->kap ? l->kap * 2 : 128;
            AlbumOge* t = realloc(l->v, yeni * sizeof(AlbumOge));
            if (!t) break;
            l->v = t; l->kap = yeni;
        }
        strcpy(l->v[l->n].yol, cocuk);
        l->v[l->n].boyut = (long long)st.st_size;
        ++l->n;
    }
    closedir(d);
}

static int rAlbum(int fd, const Istek* r) {
    char t[16];
    long bas = sorguDeger(r->sorgu, "bas", t, sizeof(t)) ? atol(t) : 0;
    long adet = sorguDeger(r->sorgu, "adet", t, sizeof(t)) ? atol(t) : 60;
    if (bas < 0) bas = 0;
    if (adet < 1 || adet > 200) adet = 60;

    AlbumListe l = {0};
    albumTara("/Nintendo/Album", &l, 0);
    if (l.n) qsort(l.v, l.n, sizeof(AlbumOge), albumSirala);

    Yazi y = {0};
    yYaz(&y, "{\"toplam\":%zu,\"ogeler\":[", l.n);
    for (size_t i = (size_t)bas, k = 0; i < l.n && k < (size_t)adet; ++i, ++k) {
        if (k) yEkle(&y, ",", 1);
        yEkle(&y, "{\"yol\":", 7);
        yJson(&y, l.v[i].yol);
        const char* uz = strrchr(l.v[i].yol, '.');
        yYaz(&y, ",\"boyut\":%lld,\"video\":%s}", l.v[i].boyut, (uz && !strcasecmp(uz, ".mp4")) ? "true" : "false");
    }
    yEkle(&y, "]}", 2);
    free(l.v);
    return jsonYanit(fd, 200, &y);
}

static void dosyaAdiBasligi(const char* ad, char* out, size_t maks) {
    // RFC 5987: filename*=UTF-8''yuzde-kodlu
    size_t o = (size_t)snprintf(out, maks, "Content-Disposition: attachment; filename*=UTF-8''");
    for (const unsigned char* p = (const unsigned char*)ad; *p && o + 4 < maks; ++p) {
        if (isalnum(*p) || strchr("-._~", *p)) out[o++] = (char)*p;
        else o += (size_t)snprintf(out + o, maks - o, "%%%02X", *p);
    }
    snprintf(out + o, maks - o, "\r\n");
}

static int rIndir(int fd, const Istek* r) {
    char ham[YOL_MAKS], yol[YOL_MAKS], tam[YOL_MAKS + 16], t[4];
    if (!sorguDeger(r->sorgu, "yol", ham, sizeof(ham)) || !yolTemizle(ham, yol, sizeof(yol)))
        return hata(fd, 400, "Geçersiz yol");
    tamYol(yol, tam, sizeof(tam));
    struct stat st;
    if (stat(tam, &st) != 0 || !S_ISREG(st.st_mode)) return hata(fd, 404, "Dosya bulunamadı");
    FILE* f = fopen(tam, "rb");
    if (!f) return hata(fd, 404, "Dosya açılamadı");

    int goster = 0;
    const char* tur = dosyaTuru(yol, &goster);
    char ek[YOL_MAKS * 3 + 96];
    ek[0] = '\0';
    if (!(goster && sorguDeger(r->sorgu, "goster", t, sizeof(t)) && t[0] == '1'))
        dosyaAdiBasligi(tabanAd(yol), ek, sizeof(ek));

    int sonuc = yanitBasligi(fd, 200, tur, (long long)st.st_size, ek);
    size_t n;
    while (sonuc == 0 && (n = fread(g_tampon, 1, sizeof(g_tampon), f)) > 0) sonuc = tamGonder(fd, g_tampon, n);
    fclose(f);
    return sonuc;
}

static int rYukle(int fd, const Istek* r) {
    char ham[YOL_MAKS], yol[YOL_MAKS], tam[YOL_MAKS + 16], gecici[YOL_MAKS + 48], t[4];
    if (!sorguDeger(r->sorgu, "yol", ham, sizeof(ham)) || !yolTemizle(ham, yol, sizeof(yol)) || !strcmp(yol, "/"))
        return hata(fd, 400, "Geçersiz yol");
    if (!izinli(yol, IZIN_YAZ)) return hata(fd, 403, "Bu klasör korumalı, buraya yazılamaz");
    if (r->uzunluk < 0) return hata(fd, 400, "Boyut bilinmiyor");
    if (r->uzunluk > DOSYA_MAKS) return hata(fd, 413, "Dosya 4 GB'tan büyük olamaz");

    tamYol(yol, tam, sizeof(tam));
    const int uzerine = sorguDeger(r->sorgu, "uzerine", t, sizeof(t)) && t[0] == '1';
    struct stat st;
    if (stat(tam, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return hata(fd, 409, "Aynı adda klasör var");
        if (!uzerine) return hata(fd, 409, "Dosya zaten var");
    }
    snprintf(gecici, sizeof(gecici), "%s.km-yukleniyor", tam);
    FILE* f = fopen(gecici, "wb");
    if (!f) return hata(fd, 404, "Hedef klasör yok ya da yazılamıyor");

    long long kalan = r->uzunluk;
    int ok = 1;
    size_t ilk = r->govdeUz < (size_t)kalan ? r->govdeUz : (size_t)kalan;
    if (ilk && fwrite(g_baslik + r->govdeBas, 1, ilk, f) != ilk) ok = 0;
    kalan -= (long long)ilk;
    while (ok && kalan > 0) {
        ssize_t k = al(fd, g_tampon, kalan < (long long)sizeof(g_tampon) ? (size_t)kalan : sizeof(g_tampon));
        if (k <= 0) { ok = 0; break; }
        if (fwrite(g_tampon, 1, (size_t)k, f) != (size_t)k) { ok = 0; break; }
        kalan -= k;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        remove(gecici);
        return hata(fd, 500, "Yükleme yarıda kaldı ya da SD dolu");
    }
    if (uzerine) remove(tam);
    if (rename(gecici, tam) != 0) {
        remove(gecici);
        return hata(fd, 500, "Dosya yerine konamadı");
    }
    return yanit(fd, 201, "application/json", "{\"tamam\":true}", 14, NULL);
}

static int rKlasor(int fd, const Istek* r) {
    char ham[YOL_MAKS], yol[YOL_MAKS], tam[YOL_MAKS + 16];
    if (!sorguDeger(r->sorgu, "yol", ham, sizeof(ham)) || !yolTemizle(ham, yol, sizeof(yol)))
        return hata(fd, 400, "Geçersiz yol");
    if (!izinli(yol, IZIN_YAZ)) return hata(fd, 403, "Bu klasör korumalı");
    tamYol(yol, tam, sizeof(tam));
    if (mkdir(tam, 0777) != 0) return hata(fd, 409, errno == EEXIST ? "Zaten var" : "Oluşturulamadı");
    return yanit(fd, 201, "application/json", "{\"tamam\":true}", 14, NULL);
}

static int rSil(int fd, const Istek* r) {
    char ham[YOL_MAKS], yol[YOL_MAKS], tam[YOL_MAKS + 16];
    if (!sorguDeger(r->sorgu, "yol", ham, sizeof(ham)) || !yolTemizle(ham, yol, sizeof(yol)))
        return hata(fd, 400, "Geçersiz yol");
    if (!izinli(yol, IZIN_SIL)) return hata(fd, 403, "Bu konum korumalı, silinemez");
    tamYol(yol, tam, sizeof(tam));
    struct stat st;
    if (stat(tam, &st) != 0) return hata(fd, 404, "Bulunamadı");
    // Klasörler sadece boşsa silinir: yanlışlıkla toplu silme olmasın
    const int sonuc = S_ISDIR(st.st_mode) ? rmdir(tam) : remove(tam);
    if (sonuc != 0) return hata(fd, 409, S_ISDIR(st.st_mode) ? "Klasör boş değil" : "Silinemedi");
    return yanit(fd, 200, "application/json", "{\"tamam\":true}", 14, NULL);
}

static void baglantiIsle(int fd) {
    Istek r;
    const int okuma = istekOku(fd, &r);
    if (okuma < 0) return;
    if (okuma > 0) { hata(fd, okuma, "Geçersiz istek"); return; }
    g_sonEtkinlik = platformMs();

    const int get = !strcmp(r.yontem, "GET"), post = !strcmp(r.yontem, "POST");
    if (get && (!strcmp(r.rota, "/") || !strcmp(r.rota, "/index.html"))) {
        yanit(fd, 200, "text/html; charset=utf-8", ARAYUZ_HTML, ARAYUZ_HTML_BOYUT,
              "Content-Security-Policy: default-src 'self'; img-src 'self' blob:; media-src 'self'; "
              "style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; frame-ancestors 'none'\r\n");
        return;
    }
    if (post && !strcmp(r.rota, "/api/giris")) { rGiris(fd, &r); return; }
    if (post && !strcmp(r.rota, "/api/cikis")) { rCikis(fd); return; }

    if (!oturumVar(&r)) { hata(fd, 401, "Giriş gerekli"); return; }

    if (get && !strcmp(r.rota, "/api/durum")) rDurum(fd);
    else if (get && !strcmp(r.rota, "/api/liste")) rListe(fd, &r);
    else if (get && !strcmp(r.rota, "/api/album")) rAlbum(fd, &r);
    else if (get && !strcmp(r.rota, "/indir")) rIndir(fd, &r);
    else if (post && !strcmp(r.rota, "/yukle")) rYukle(fd, &r);
    else if (post && !strcmp(r.rota, "/api/klasor")) rKlasor(fd, &r);
    else if (post && !strcmp(r.rota, "/api/sil")) rSil(fd, &r);
    else hata(fd, 404, "Bulunamadı");
}

// ─── Dinleyici ve ana döngü ──────────────────────────────────────────────────
static int dinleyiciAc(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int bir = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &bir, sizeof(bir));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(s, 8) != 0) {
        close(s);
        return -1;
    }
    return s;
}

static time_t ayarZamani(void) {
    struct stat st;
    return stat(KMAG_AYAR, &st) == 0 ? st.st_mtime : 0;
}

void sunucuDongusu(volatile bool* dur) {
    int dinleyici = -1;
    kmagAyarOku(&g_ayar);
    tokenYenile();
    time_t sonZaman = ayarZamani();
    uint64_t sonKontrol = platformMs(), sonZorla = sonKontrol;

    while (!dur || !*dur) {
        const uint64_t simdi = platformMs();
        if (simdi - sonKontrol >= 1000) {
            sonKontrol = simdi;
            const time_t z = ayarZamani();
            const int zorla = simdi - sonZorla >= 5000;
            if (z != sonZaman || zorla) {
                sonZaman = z;
                if (zorla) sonZorla = simdi;
                const int eskiPort = g_ayar.port, eskiAcik = g_ayar.acik;
                kmagAyarOku(&g_ayar);
                if (strcmp(g_tokenPin, g_ayar.pin) != 0) tokenYenile(); // PIN değişti: oturumlar düşer
                if (dinleyici >= 0 && (g_ayar.port != eskiPort || !g_ayar.acik)) { close(dinleyici); dinleyici = -1; }
                if (g_ayar.acik && !eskiAcik) g_sonEtkinlik = simdi;
            }
        }

        if (!g_ayar.acik || strlen(g_ayar.pin) != 6) {
            if (dinleyici >= 0) { close(dinleyici); dinleyici = -1; }
            platformUyu(1000);
            continue;
        }

        // Hareketsizlikte kendini kapat
        if (g_ayar.otokapat > 0 && simdi - g_sonEtkinlik > (uint64_t)g_ayar.otokapat * 60000ULL) {
            g_ayar.acik = 0;
            kmagAyarYaz(&g_ayar);
            sonZaman = ayarZamani();
            continue;
        }

        if (dinleyici < 0) {
            dinleyici = dinleyiciAc(g_ayar.port);
            if (dinleyici < 0) { platformUyu(2000); continue; }
            g_sonEtkinlik = simdi;
        }

        struct pollfd p = { .fd = dinleyici, .events = POLLIN, .revents = 0 };
        const int r = poll(&p, 1, 1000);
        if (r < 0 && errno != EINTR) { close(dinleyici); dinleyici = -1; platformUyu(1000); continue; }
        if (r <= 0) continue;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            // Uykudan uyanınca soket bozulabilir: yeniden aç
            close(dinleyici); dinleyici = -1; platformUyu(1000); continue;
        }
        int c = accept(dinleyici, NULL, NULL);
        if (c < 0) {
            if (errno != EINTR && errno != EAGAIN) { close(dinleyici); dinleyici = -1; platformUyu(1000); }
            continue;
        }
        baglantiIsle(c);
        shutdown(c, SHUT_WR);
        close(c);
    }
    if (dinleyici >= 0) close(dinleyici);
}
