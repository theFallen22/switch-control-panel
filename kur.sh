#!/usr/bin/env bash
# libultrahand'i lib/libultrahand altına indirir (test edilmiş sürüme sabitli).
set -euo pipefail
cd "$(dirname "$0")"

LIBULTRAHAND_COMMIT="2e4df5430045217093dd77751cd9e16c92fe9144"
HEDEF="lib/libultrahand"

if [ -f "$HEDEF/ultrahand.mk" ]; then
    echo "libultrahand zaten var: $HEDEF"
    exit 0
fi

mkdir -p lib

# 1) git varsa onunla (sabit commit'e)
if command -v git >/dev/null 2>&1; then
    git clone -q https://github.com/ppkantorski/libultrahand.git "$HEDEF" \
        && git -C "$HEDEF" checkout -q "$LIBULTRAHAND_COMMIT" \
        && { echo "Tamam: $HEDEF"; exit 0; }
    rm -rf "$HEDEF"
fi

# 2) yoksa arşiv olarak indir
URL="https://github.com/ppkantorski/libultrahand/archive/${LIBULTRAHAND_COMMIT}.tar.gz"
echo "libultrahand indiriliyor (${LIBULTRAHAND_COMMIT:0:7})..."
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o lib/libultrahand.tar.gz
else
    wget -qO lib/libultrahand.tar.gz "$URL"
fi
tar -xzf lib/libultrahand.tar.gz -C lib
rm lib/libultrahand.tar.gz
mv "lib/libultrahand-${LIBULTRAHAND_COMMIT}" "$HEDEF"
echo "Tamam: $HEDEF"
