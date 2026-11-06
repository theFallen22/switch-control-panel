#!/usr/bin/env bash
# Derlenen overlay, fan ve Mac modüllerini SD kartın yapısında out/ altına dizer.
# Sonuç: out/ klasörünün İÇERİĞİNİ SD kartın köküne kopyala.
set -euo pipefail
cd "$(dirname "$0")"

TID="00FF00004B4D4346"      # km-fan
AG_TID="00FF00004B4D4147"   # km-ag
[ -f kontrol-merkezi.ovl ] || { echo "kontrol-merkezi.ovl yok, önce derle"; exit 1; }
[ -f sysmodule/km-fan.nsp ] || { echo "sysmodule/km-fan.nsp yok, önce derle"; exit 1; }
[ -f ag/km-ag.nsp ] || { echo "ag/km-ag.nsp yok, önce derle"; exit 1; }

# Mac'te gizli klasör olmasın diye dosyalar gidecekleri yere göre adlandırılmış
# klasörlere konur (Finder ".overlays"ı gizler).
A="out/1_switch-.overlays_icine"
B="out/2_atmosphere-contents_icine"
mkdir -p "$A" "$B/$TID" "$B/$AG_TID"
cp kontrol-merkezi.ovl "$A/"
cp sysmodule/km-fan.nsp "$B/$TID/exefs.nsp"
cp sysmodule/toolbox.json "$B/$TID/toolbox.json"
cp ag/km-ag.nsp "$B/$AG_TID/exefs.nsp"
cp ag/toolbox.json "$B/$AG_TID/toolbox.json"
# Modüller açılışta BAŞLAMAZ (boot2.flag yok): panelden fan modu seçince ya da
# Mac bağlantısı açılınca başlar. Denendikten sonra panelden açılışta başlatılabilir.
cat > out/OKU-BENI.txt <<'TXT'
1_switch-.overlays_icine/kontrol-merkezi.ovl
    -> Switch SD kartında: switch/.overlays/ klasörünün İÇİNE

2_atmosphere-contents_icine/ içindeki İKİ klasörün tamamı
    (00FF00004B4D4346 ve 00FF00004B4D4147)
    -> Switch SD kartında: atmosphere/contents/ klasörünün İÇİNE

Yeniden başlatma gerekmez. Ultrahand: ZL + ZR + aşağı.
TXT

echo "Paket hazır: out/"
find out -type f | sed 's#^out/#  #'
