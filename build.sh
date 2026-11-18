#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "building minetest"
echo "or shall we say baretest"
echo "made by cucuzacu"

if [ "$1" = "clean" ]; then
    echo "cleaning build artifacts..."
    make -f Makefile.miyoo clean execlean 2>/dev/null || true
    rm -rf baretest-miyoo
    echo "clean complete"
    exit 0
fi

if [ ! -d "/opt/miyoo/bin" ]; then
    echo "miyoo sdk not found, installing..."
    sudo mkdir -p /opt/miyoo
    cd /opt/miyoo
    sudo wget https://github.com/MiyooCFW/toolchain/releases/download/v2.0.0/miyoo-toolchain-v2.0.0-arm-buildroot-linux-musleabi_sdk-buildroot.tar.gz
    sudo tar -xf miyoo-toolchain-*.tar.gz --strip-components=1
    sudo ./relocate-sdk.sh
    cd "$SCRIPT_DIR"
fi

export PATH="/opt/miyoo/bin:$PATH"
export CROSS_COMPILE="arm-buildroot-linux-musleabi-"
export SYSROOT="/opt/miyoo/arm-buildroot-linux-musleabi/sysroot"

echo "building..."
NPROC=$(nproc 2>/dev/null || echo 2)

make -f Makefile.miyoo -j"$NPROC" miyoo
    
RELEASE_DIR="baretest-miyoo"
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR/bin"
cp minetest "$RELEASE_DIR/bin/"
cp -r data "$RELEASE_DIR/"
cp minetest.conf "$RELEASE_DIR/"
rm minetest

echo "Build finished successfully yayyyyyyyyy!"