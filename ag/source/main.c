// km-ag — Kontrol Merkezi Mac bağlantısı modülü (sysmodule)
// Ayar dosyasında "acik=1" olana kadar hiçbir porta dinlemez.
#include <switch.h>
#include "pwm_fan.h"
#include "sunucu.h"

#define INNER_HEAP_SIZE 0x100000 // 1 MB: soket tamponları (~300 KB) + liste/JSON

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

static char g_heap[INNER_HEAP_SIZE];
static bool g_hazir = false;

void __libnx_initheap(void) {
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = g_heap;
    fake_heap_end   = g_heap + sizeof(g_heap);
}

// Az bellek kullanan soket ayarı: aynı anda tek bağlantıya hizmet ediyoruz
static const SocketInitConfig g_soket = {
    .tcp_tx_buf_size     = 0x8000,
    .tcp_rx_buf_size     = 0x10000,
    .tcp_tx_buf_max_size = 0x20000,
    .tcp_rx_buf_max_size = 0x20000,
    .udp_tx_buf_size     = 0x2400,
    .udp_rx_buf_size     = 0xA500,
    .sb_efficiency       = 1,
    .num_bsd_sessions    = 2,
    .bsd_service_type    = BsdServiceType_Auto,
};

void __appInit(void) {
    // Hata olursa çökmek yerine sessizce çıkacağız (bkz. main)
    if (R_FAILED(smInitialize())) return;

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    const bool fs = R_SUCCEEDED(fsInitialize()) && R_SUCCEEDED(fsdevMountSdmc());
    const bool ag = R_SUCCEEDED(socketInitialize(&g_soket));
    psmInitialize();
    tsInitialize();
    pmdmntInitialize();
    pwmFanAc(0x3D000001u);
    g_hazir = fs && ag;
    // sm açık kalır: sys-clk ve pwm'e sonradan bağlanabilmek için
}

void __appExit(void) {
    pwmFanKapat();
    pmdmntExit();
    tsExit();
    psmExit();
    socketExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    if (!g_hazir) return 0;
    sunucuDongusu(NULL);
    return 0;
}
