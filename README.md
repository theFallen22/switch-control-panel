# Kontrol Merkezi

Switch Lite için kendi overlay'imiz. Ultrahand menüsünden (ZL + ZR + ↓) açılır, oyunun üstünde çalışır.

Üç parçadan oluşur:
- **Overlay** (`kontrol-merkezi.ovl`): gördüğün panel.
- **Fan modülü** (`km-fan`, arka plan modülü): fanı panel kapalıyken de senin seçtiğin moda göre yönetir.
- **Mac bağlantısı modülü** (`km-ag`, arka plan modülü): Mac'ten Safari ile Switch'e kablosuz bağlanmanı sağlar.

## Ne yapıyor

### Ana ekran (gösterge paneli)
Saniyede bir yenilenir.
- **Oyun kartı:** açık oyunun adı, aktif profili, toplam oynama süresi ve kaç kez açıldığı. Ana menüdeysen son oynadığın oyun.
- **Batarya:** yüzde (renkli), şarj durumu ve **tahmini kalan süre**.
- **Sıcaklık:** SoC büyük, PCB ve yüzey küçük. Yeşil < 55°, sarı < 65°, üstü kırmızı.
- **Saat hızları:** anlık CPU / GPU / MEM ve **RAM yükü**. Güvenli sınırın üstü sarı.
- **Wi-Fi:** ağ adı, sinyal çubukları, IP adresi.
- **SD kart:** boş alan ve doluluk çubuğu.

### Performans
- **Bu oyunun profili:** Pil Tasarrufu / Dengeli / Performans / Özel. Ana menüdeysen son oyunun profilini açar.
- **Tüm oyun profilleri:** profil kayıtlı her oyun, profili ve toplam oynama süresiyle.
- **Özel profil editörü:** CPU / GPU / MEM'i tek tek seç ya da hazır bir profili kopyalayıp oradan düzenle.

| Profil | CPU | GPU | MEM | Ne için |
|---|---|---|---|---|
| Pil Tasarrufu | 816 | 307 | 1065 | Hafif / 2D oyunlar |
| Dengeli | stok | stok | stok | Varsayılan, sys-clk dokunmaz |
| Performans | 1224 | 460 | 1600 | Ağır oyunlar, güvenli sınırda |
| Özel | sen seç | sen seç | sen seç | Oyuna özel ince ayar |

Hiçbir oyuna otomatik overclock yapılmaz. Güvenli sınırın (CPU 1224 / GPU 460 / MEM 1600) üstü "OC" olarak işaretlenir ve **iki kez A** ister.

### Detaylar
- **Canlı izleme:** son 60 saniyenin grafikleri. SoC sıcaklığı, pil tüketimi (W), CPU ve GPU frekansı. Her birinde en az / ortalama / en çok değer.
- **Batarya:** doluluk, durum, kalan süre, anlık ve ortalama güç, **pil sağlığı (%)**, voltaj, pil sıcaklığı, şarj akımı sınırı.
- **Pil günlüğü (uyku tüketimi):** panel her açıldığında yüzde ve saat kaydedilir. Uyutmadan önce bir kez, uyandırınca bir kez açarsan aradaki saatlik düşüşü gösterir ("%0.6/sa (uyku?)").
- **Depolama ve ağ:** SD ve dahili hafıza doluluğu, albümdeki görsel / video sayısı ve boyutu, ağ adı, sinyal, IP.

### Sistem
- **Hızlı işlemler:** ekran parlaklığı, sys-clk aç/kapa, geçici frekansları temizle, Status Monitor (mini HUD ve FPS sayacı), FPSLocker ve sys-clk overlay'ine tek tuşla geçiş, bildirim testi, **yeniden başlat / kapat** (iki kez A).
- **Yedekler:** sys-clk ayarının yedeğini al / geri yükle.
- **Ayarlar:** ısı uyarısı açık/kapalı ve eşiği (60–80°C), overclock onayı, pil günlüğünü sıfırla.
- **Sistem kontrolü:** model, HOS, Atmosphère, sys-clk sürümü; SaltyNX / FPSLocker / Status Monitor kurulu mu; hangi sistem servislerine erişilebildiği.

### Fan kontrolü
Switch'te fanı normalde sistemin termal servisi yönetir. Fan modülü bunun yerine senin seçtiğin modu uygular.

| Mod | 45°C | 55°C | 65°C | 70°C | 75°C |
|---|---|---|---|---|---|
| Otomatik (sistem) | sistem | sistem | sistem | sistem | sistem |
| Sessiz | %0 | %20 | %51 | %70 | %95 |
| Dengeli | %22 | %40 | %66 | %82 | %100 |
| Serin | %50 | %70 | %95 | %100 | %100 |
| Sabit hız | seçtiğin % | seçtiğin % | en az %51 | en az %70 | en az %95 |

**Güvenlik tabanı:** Hangi mod seçilirse seçilsin fan bu değerlerin altına inmez: 62°C'de %40, 70°C'de %70, 76°C'de %100. "Sabit %0" bile seçsen konsol ısınınca fan devreye girer. Bu garanti 9.000'den fazla mod/sıcaklık kombinasyonunda test edildi.

**Başka korumalar:**
- Otomatik modda modül fana hiç dokunmaz. Varsayılan mod budur.
- Sıcaklık 5 kez üst üste okunamazsa modül fanı bırakır, kontrol sisteme döner.
- Modül hiçbir hatada konsolu çökertmez. Başlarken bir servis açılamazsa hiçbir şey yapmadan kapanır.
- Fan hızı yükselirken hızlı, düşerken yavaş değişir. Güvenlik tabanı bu yavaşlamayı beklemez.

**Fan ekranında:** anlık fan yüzdesi ve SoC sıcaklığı, seçili eğrinin ve güvenlik tabanının grafiği (sarı çizgi şu anki sıcaklık), mod seçimi, sabit hız kaydırıcısı, modülün durumu, "Açılışta başlat" ve "Modülü durdur". Fan yüzdesi ana ekrandaki sıcaklık kartında ve Canlı izleme'de de görünür.

### Mac bağlantısı (kablosuz)
Mac'e hiçbir şey kurmadan, Safari'den Switch'e bağlanırsın.

1. Switch'te Kontrol Merkezi → **Mac bağlantısı** → "Kablosuz erişim"i aç.
2. Ekranda bir adres (ör. `http://192.168.1.34:8080`) ve 6 haneli bir PIN görünür.
3. Mac'te Safari'ye adresi yaz, PIN'i gir.

Mac'te açılan sayfada:
- **Durum:** açık oyun, batarya, SoC sıcaklığı, fan, CPU/GPU/bellek frekansı ve RAM yükü (2 saniyede bir yenilenir).
- **Dosyalar:** SD kartı gez, dosya indir, **sürükle bırak ile yükle** (ilerleme çubuğuyla), klasör oluştur, sil. Aynı adda dosya varsa üzerine yazmadan önce sorar.
- **Albüm:** ekran görüntüleri ve videolar, en yeniler üstte. Büyük görmek için tıkla. **Yenileri indir** bir önceki indirmenden sonra çektiklerini indirir. Seçip toplu indirebilir ya da silebilirsin.

**Güvenlik:**
- Erişim varsayılan olarak **kapalı**. Kapalıyken modül çalışmaz ve ağda hiçbir şey dinlemez.
- Her bağlantı PIN ister. 5 yanlış denemede 1 dakika kilitlenir. "Yeni PIN oluştur" açık oturumları hemen düşürür.
- Varsayılan olarak 30 dakika hareketsiz kalırsa kendiliğinden kapanır (15 / 30 / 60 dk / kapanmaz).
- `/Nintendo`, `/bootloader`, `emuMMC` ve Atmosphère'in çekirdek dosyalarına **yazılamaz** ve silinemez. Albümden silmek serbest. Atmosphère'de sadece `contents`, `config`, `exefs_patches`, `kip_patches`, `hosts` klasörlerine yazılabilir (mod yüklemek için).
- `..` gibi yollarla SD kartın dışına çıkılamaz. SD'deki dosyalar tarayıcıda asla kod olarak çalıştırılmaz.
- Klasörler sadece boşsa silinir.
- Sadece aynı Wi-Fi'daki cihazlar erişebilir; internete açılmaz. Konsol uyurken bağlantı kopar.

**Test:** Sunucu bilgisayarda gerçekten çalıştırılıp test edildi. curl ile 35'ten fazla senaryo denendi: giriş, kilit, PIN değişimi, `..` ve `%2e%2e` ile kaçış, null bayt, korumalı klasörler, büyük harf hilesi, 3 MB yükleme ve indirmede birebir bütünlük, üzerine yazma, otomatik kapanma. Bunun üstüne arayüz gerçek bir tarayıcıda baştan sona kullanıldı. Bellek hatası denetleyicisi (AddressSanitizer) sıfır hata buldu.

### Isı uyarısı
Panel açıkken SoC sıcaklığı eşiği (varsayılan 70°C) geçerse dakikada en fazla bir kez uyarı verir. Panel kapalıyken uyarı için arka plan modülü gerekiyor (sonraki aşama).

## Gereksinimler

- Atmosphère + Ultrahand (zaten var)
- **sys-clk**: güncel sürüm (IPC API 4). Eski sürümse panel bunu "uyumsuz" diye gösterir ve sys-clk'e hiç dokunmaz.
- İsteğe bağlı: FPSLocker + SaltyNX, Status Monitor

## Derleme

Üç yol var, en kolayı birincisi.

### A) GitHub Actions (bilgisayara hiçbir şey kurmadan)
1. GitHub'da yeni bir **private** repo aç, bu klasörün içeriğini yükle.
2. *Actions* sekmesinde **Derle** iş akışı otomatik çalışır (2-3 dk).
3. Bitince sayfanın altındaki **kontrol-merkezi** dosyasını indir. İçinde `switch/.overlays/kontrol-merkezi.ovl` var.

### B) Mac'te Docker ile
1. Docker Desktop'ı kur (Apple Silicon sürümü) ve aç.
2. Terminalde bu klasöre gel ve çalıştır:
   ```bash
   ./derle.sh
   ```
3. Çıktı: `out/switch/.overlays/kontrol-merkezi.ovl`

### C) Mac'te devkitPro ile
```bash
sudo dkp-pacman -S switch-dev switch-curl switch-zlib switch-mbedtls switch-minizip
./derle.sh
```

`derle.sh` ilk çalışmada `kur.sh` ile libultrahand'i (test edilen sürüme sabitli) `lib/` altına indirir.

## Kurulum

1. **Yedek:** SD karttaki `/config/sys-clk/config.ini` dosyasını bilgisayarına kopyala.
2. Derlemeden çıkan klasörde (Mac'te gizli klasör yok):
   - `1_switch-.overlays_icine/kontrol-merkezi.ovl` → SD'de `/switch/.overlays/` içine
   - `2_atmosphere-contents_icine/` içindeki iki klasörün tamamı (`00FF00004B4D4346`, `00FF00004B4D4147`) → SD'de `/atmosphere/contents/` içine

   DBI'nin MTP modu + Mac'te OpenMTP ile kopyala. Switch tarafında `.overlays` görünmüyorsa OpenMTP ayarlarından gizli dosyaları göster.
3. Yeniden başlatma gerekmez. Modüller açılışta kendiliğinden başlamaz: bir fan modu seçince ya da Mac bağlantısını açınca panel onları başlatır.
4. Ultrahand'i aç (ZL + ZR + ↓) → **Kontrol Merkezi** → **Fan kontrolü**. İlk açılışta mod "Otomatik"tir, yani hiçbir şey değişmez. İstediğin modu seç.
5. Fan modülü birkaç gün sorunsuz çalışınca Fan kontrolü → **Açılışta başlat**'ı aç. Böylece yeniden başlatmadan sonra da seçtiğin mod geçerli olur.

**Bir şey ters giderse** (ör. konsol açılırken hata ekranı): SD kartı çıkar, bilgisayarda `/atmosphere/contents/00FF00004B4D4346/` ve `/atmosphere/contents/00FF00004B4D4147/` klasörlerini sil, kartı geri tak. Her şey eski haline döner.

## Panelin dokunduğu dosyalar

| Yol | Ne |
|---|---|
| `/config/sys-clk/config.ini` | Profiller buraya yazılır (sys-clk'in kendi formatı) |
| `/config/kontrol-merkezi/yedek/` | Otomatik ve elle alınan yedekler, son 10 tanesi |
| `/config/kontrol-merkezi/durum.ini` | Son oynanan oyun |
| `/config/kontrol-merkezi/ayarlar.ini` | Panel ayarları |
| `/config/kontrol-merkezi/pil-gunlugu.csv` | Pil günlüğü, son 40 kayıt |
| `/config/kontrol-merkezi/fan.ini` | Fan modu ve sabit hız |
| `/config/kontrol-merkezi/fan.log` | Fan modülünün olay günlüğü (her açılışta sıfırlanır) |
| `/config/kontrol-merkezi/ag.ini` | Mac bağlantısı: açık/kapalı, PIN, port, otomatik kapanma |
| `/config/ultrahand/notifications/` | Sadece "bildirim dene" butonunda |

Başka hiçbir sistem dosyasına ya da ayara yazmaz.

## Geri dönüş

- **Bir profili geri almak:** Profil ekranında **Dengeli** seç.
- **Tüm sys-clk ayarını geri almak:** Kontrol Merkezi → Yedekler → bir yedeğe iki kez A. sys-clk değişikliği kendisi algılar.
- **Panel açılmıyorsa:** SD'ye kopyaladığın `config.ini`'yi `/config/sys-clk/` altına geri koy.
- **Fanı sisteme geri vermek:** Fan kontrolü → **Otomatik (sistem)**. Ya da **Modülü durdur**.
- **Fan modülünü açılışta başlatmamak:** Fan kontrolü → "Açılışta başlat"ı kapat, ya da `/atmosphere/contents/00FF00004B4D4346/flags/boot2.flag` dosyasını sil.
- **Fan modülünü tamamen kaldırmak:** `/atmosphere/contents/00FF00004B4D4346/` klasörünü sil ve yeniden başlat.
- **Mac bağlantısını kapatmak:** Mac bağlantısı → "Kablosuz erişim"i kapat. Açılışta başlamıyorsa modül tamamen durur.
- **Mac modülünü tamamen kaldırmak:** `/atmosphere/contents/00FF00004B4D4147/` klasörünü sil.
- **Paneli tamamen kaldırmak:** `/switch/.overlays/kontrol-merkezi.ovl` ve `/config/kontrol-merkezi/` klasörünü sil.

## FPSLocker ile birlikte kullanım

İkisi farklı işler yapar, çakışmaz:
- **Kontrol Merkezi / sys-clk:** donanım frekansları (pil ve ısı)
- **FPSLocker:** oyunun FPS hedefi

Örnek eşleşmeler:
- Hafif oyun: **Pil Tasarrufu** + FPSLocker'da 30 FPS kilidi
- 30 FPS'te takılan ağır oyun: **Performans**, FPS'e dokunma
- 60 FPS'i zorlayan oyun: **Performans** + FPSLocker'da 40 ya da 45 hedef

Hızlı işlemler ekranından FPSLocker'a tek tuşla geçebilirsin.

## Durum

v1.3.0. Kod gerçek libnx, libultrahand ve sys-clk başlıklarına karşı tip kontrolünden geçti ve Makefile tüm kaynakları doğru topluyor. Fan eğrileri bilgisayarda birim testinden geçti. Konsolda henüz çalıştırılmadı; fan modülünü ilk denediğinde Fan kontrolü ekranında sıcaklığı ve fan yüzdesini birkaç dakika izle. İlk açılışta bir sorun olursa ekranın fotoğrafı yeterli.

## Geliştirici notu

- `ag/arayuz.html` Mac'te açılan sayfadır. Değiştirdikten sonra `python3 araclar/html_gom.py` çalıştır, sayfa C koduna gömülür.
- Sunucuyu Mac'te test etmek için (Switch gerekmez):
  ```bash
  mkdir -p /tmp/sd/config/kontrol-merkezi
  printf '[ag]\nacik=1\npin=123456\nport=8080\notokapat=0\n' > /tmp/sd/config/kontrol-merkezi/ag.ini
  cc -DKM_KOK='"/tmp/sd"' -I ag/source -I ortak ag/source/sunucu.c ag/test/platform_test.c -o km-ag-test && ./km-ag-test
  ```
  Sonra tarayıcıda `http://localhost:8080`, PIN `123456`.

## Sonraki aşama

- Fan modülüne bildirimler eklemek: panel kapalıyken de düşük pil ve aşırı ısınma uyarısı, uyku tüketimini otomatik kaydetme
- Mac'ten Switch durumunu görme
