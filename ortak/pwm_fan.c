// Fanın gerçek dönüş oranını pwm servisinden okur (sadece okuma, fana yazmaz).
#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <switch.h>
#include "pwm_fan.h"

static Service g_pwm;
static Service g_kanal;
static bool g_pwmAcik = false;
static bool g_kanalAcik = false;

bool pwmFanAc(u32 cihazKodu) {
    if (g_kanalAcik) return true;
    if (hosversionBefore(6, 0, 0)) return false; // OpenSession2 6.0.0+
    if (!g_pwmAcik) g_pwmAcik = R_SUCCEEDED(smGetService(&g_pwm, "pwm"));
    if (!g_pwmAcik) return false;
    g_kanalAcik = R_SUCCEEDED(serviceDispatchIn(&g_pwm, 2, cihazKodu,
        .out_num_objects = 1,
        .out_objects = &g_kanal,
    ));
    return g_kanalAcik;
}

bool pwmFanYuzde(double* yuzde) {
    if (!g_kanalAcik) return false;
    double duty = 0.0;
    if (R_FAILED(serviceDispatchOut(&g_kanal, 7, duty))) return false;
    // Duty cycle ters: %100 duty = fan durgun
    double y = 100.0 - duty;
    if (y < 0.0) y = 0.0;
    if (y > 100.0) y = 100.0;
    *yuzde = y;
    return true;
}

void pwmFanKapat(void) {
    if (g_kanalAcik) serviceClose(&g_kanal);
    if (g_pwmAcik) serviceClose(&g_pwm);
    g_kanalAcik = g_pwmAcik = false;
}
