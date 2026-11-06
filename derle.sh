#!/usr/bin/env bash
# Kontrol Merkezi'ni (overlay + fan ve Mac modülleri) derler ve out/ altına SD düzeninde dizer.
#  1) devkitPro kuruluysa onu kullanır
#  2) yoksa Docker ile devkitPro'nun resmi imajında derler
set -euo pipefail
cd "$(dirname "$0")"

[ -f lib/libultrahand/ultrahand.mk ] || ./kur.sh

PORTLIBS="switch-curl switch-zlib switch-mbedtls switch-minizip"

if [ -n "${DEVKITPRO:-}" ] && [ -d "$DEVKITPRO/libnx" ]; then
    echo "devkitPro ile derleniyor ($DEVKITPRO)"
    make && make -C sysmodule && make -C ag
elif [ -d /opt/devkitpro/libnx ]; then
    echo "devkitPro ile derleniyor (/opt/devkitpro)"
    DEVKITPRO=/opt/devkitpro make && DEVKITPRO=/opt/devkitpro make -C sysmodule && DEVKITPRO=/opt/devkitpro make -C ag
elif command -v docker >/dev/null 2>&1; then
    echo "Docker ile derleniyor (devkitpro/devkita64)"
    docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkita64:latest \
        bash -c "dkp-pacman -S --noconfirm --needed $PORTLIBS >/dev/null 2>&1 || true; make && make -C sysmodule && make -C ag"
else
    cat <<'MSG'
Derleyici bulunamadı. İkisinden birini kur:
  A) Docker Desktop (Apple Silicon sürümü): https://www.docker.com/products/docker-desktop/
  B) devkitPro: https://devkitpro.org/wiki/devkitPro_pacman  (macOS paketi)
     Sonra:  sudo dkp-pacman -S switch-dev switch-curl switch-zlib switch-mbedtls switch-minizip
Ya da hiç kurulum yapmadan GitHub Actions ile derle (README'ye bak).
MSG
    exit 1
fi

echo
bash paketle.sh
echo
echo "out/ klasörünün içeriğini SD kartın köküne kopyala."
