#pragma once
#include <switch/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pwmFanAc(u32 cihazKodu);       // sm oturumu içinde çağır
bool pwmFanYuzde(double* yuzde);    // 0..100
void pwmFanKapat(void);

#ifdef __cplusplus
}
#endif
