// km-ag: sunucunun platforma bağlı parçaları.
// Switch'te platform_switch.c, bilgisayarda test için test/platform_test.c sağlar.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool  pilOk;   int pil; bool sarj;
    bool  socOk;   float soc;
    bool  pcbOk;   float pcb;
    bool  fanOk;   float fan;        // %
    bool  saatOk;  unsigned cpu, gpu, mem; int ram; float guc; // MHz, ‰, W
    uint64_t tid;  char oyun[96];
} KmDurum;

void     platformDurum(KmDurum* d);
void     platformRastgele(void* buf, size_t n);
uint64_t platformMs(void);          // monoton milisaniye
void     platformUyu(unsigned ms);
