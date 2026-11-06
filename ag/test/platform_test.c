// Bilgisayarda test için sahte platform: sunucu.c'yi Linux/macOS'ta çalıştırır.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "platform.h"
#include "sunucu.h"

uint64_t platformMs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}
void platformUyu(unsigned ms) { usleep(ms * 1000); }
void platformRastgele(void* buf, size_t n) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(buf, 1, n, f) != n) memset(buf, 7, n); fclose(f); }
}
void platformDurum(KmDurum* d) {
    d->pilOk = true; d->pil = 72; d->sarj = false;
    d->socOk = true; d->soc = 48.6f; d->pcbOk = true; d->pcb = 41.0f;
    d->fanOk = true; d->fan = 52.0f;
    d->saatOk = true; d->cpu = 1224; d->gpu = 460; d->mem = 1600; d->ram = 381; d->guc = -6.1f;
    d->tid = 0x0100535012974000ULL;
    snprintf(d->oyun, sizeof(d->oyun), "Hades \"test\" <b>");
}
int main(void) { sunucuDongusu(NULL); return 0; }
