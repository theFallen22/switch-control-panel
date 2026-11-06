// km-ag: Switch'e özel parçalar (durum bilgisi, rastgele sayı, zaman)
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sysclk.h>
#include <sysclk/client/ipc.h>

#include "platform.h"
#include "pwm_fan.h"

#define QLAUNCH_TID   0x0100000000001000ULL
#define DURUM_DOSYASI "sdmc:/config/kontrol-merkezi/durum.ini"

static bool      g_sysclk = false;
static uint64_t  g_sysclkDeneme = 0;
static TsSession g_tsSoc, g_tsPcb;
static bool      g_tsSocAcik = false, g_tsPcbAcik = false;

uint64_t platformMs(void) { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }
void platformUyu(unsigned ms) { svcSleepThread((u64)ms * 1000000ULL); }
void platformRastgele(void* buf, size_t n) { randomGet(buf, n); }

static bool sicaklik(TsSession* s, bool* acik, u32 kod, TsLocation eski, float* c) {
    if (hosversionAtLeast(10, 0, 0)) {
        if (!*acik) *acik = R_SUCCEEDED(tsOpenSession(s, kod));
        if (!*acik) return false;
        if (R_FAILED(tsSessionGetTemperature(s, c))) { tsSessionClose(s); *acik = false; return false; }
        return true;
    }
    s32 mc = 0;
    if (R_FAILED(tsGetTemperatureMilliC(eski, &mc))) return false;
    *c = mc / 1000.0f;
    return true;
}

// sys-clk açılışta bizden sonra başlayabilir: 10 sn'de bir yeniden dene
static void sysclkBaglan(void) {
    if (g_sysclk) return;
    const uint64_t simdi = platformMs();
    if (g_sysclkDeneme && simdi - g_sysclkDeneme < 10000) return;
    g_sysclkDeneme = simdi;
    if (!sysclkIpcRunning() || R_FAILED(sysclkIpcInitialize())) return;
    u32 api = 0;
    if (R_SUCCEEDED(sysclkIpcGetAPIVersion(&api)) && api == SYSCLK_IPC_API_VERSION) g_sysclk = true;
    else sysclkIpcExit();
}

// Overlay, son görülen oyunun adını durum.ini'ye yazar; ns servisini burada açmayız (bellek)
static void oyunAdi(uint64_t tid, char* out, size_t maks) {
    out[0] = '\0';
    if (!tid) return;
    FILE* f = fopen(DURUM_DOSYASI, "r");
    if (!f) return;
    char satir[160];
    uint64_t dosyaTid = 0;
    char ad[128] = "";
    while (fgets(satir, sizeof(satir), f)) {
        satir[strcspn(satir, "\r\n")] = '\0';
        if (!strncmp(satir, "son_oyun=", 9)) dosyaTid = strtoull(satir + 9, NULL, 16);
        else if (!strncmp(satir, "son_oyun_adi=", 13)) snprintf(ad, sizeof(ad), "%s", satir + 13);
    }
    fclose(f);
    if (dosyaTid == tid) snprintf(out, maks, "%s", ad);
}

void platformDurum(KmDurum* d) {
    u32 yuzde = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&yuzde))) { d->pilOk = true; d->pil = (int)yuzde; }
    PsmChargerType sarj = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&sarj))) d->sarj = sarj != PsmChargerType_Unconnected;

    d->socOk = sicaklik(&g_tsSoc, &g_tsSocAcik, TsDeviceCode_LocationExternal, TsLocation_External, &d->soc);
    d->pcbOk = sicaklik(&g_tsPcb, &g_tsPcbAcik, TsDeviceCode_LocationInternal, TsLocation_Internal, &d->pcb);

    double fan = 0.0;
    if (!pwmFanYuzde(&fan)) { pwmFanAc(0x3D000001u); d->fanOk = pwmFanYuzde(&fan); }
    else d->fanOk = true;
    d->fan = (float)fan;

    sysclkBaglan();
    if (g_sysclk) {
        SysClkContext c;
        if (R_SUCCEEDED(sysclkIpcGetCurrentContext(&c))) {
            d->saatOk = true;
            d->cpu = (c.realFreqs[SysClkModule_CPU] ? c.realFreqs[SysClkModule_CPU] : c.freqs[SysClkModule_CPU]) / 1000000;
            d->gpu = (c.realFreqs[SysClkModule_GPU] ? c.realFreqs[SysClkModule_GPU] : c.freqs[SysClkModule_GPU]) / 1000000;
            d->mem = (c.realFreqs[SysClkModule_MEM] ? c.realFreqs[SysClkModule_MEM] : c.freqs[SysClkModule_MEM]) / 1000000;
            d->ram = (int)c.ramLoad[SysClkRamLoad_All];
            d->guc = c.power[SysClkPowerSensor_Avg] / 1000.0f;
            d->tid = c.applicationId;
        } else {
            sysclkIpcExit();
            g_sysclk = false;
        }
    }
    if (!d->tid) {
        u64 pid = 0, tid = 0;
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && R_SUCCEEDED(pmdmntGetProgramId(&tid, pid))) d->tid = tid;
    }
    if (d->tid == QLAUNCH_TID) d->tid = 0;
    oyunAdi(d->tid, d->oyun, sizeof(d->oyun));
}
