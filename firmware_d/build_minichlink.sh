#!/data/data/com.termux/files/usr/bin/sh
set -e
D="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$D/mc_build" && cp -r "$D/../ch32fun-master/minichlink" "$D/mc_build" && cd "$D/mc_build"
sed -i 's/^#elif defined(__linux__)$/#elif 0 \/* android: use libusb backend *\//' hidapi.c
R=https://raw.githubusercontent.com/libusb/hidapi/hidapi-0.15.0
curl -fsSL $R/libusb/hidapi_libusb.h -o hidapi_libusb.h
curl -fsSL $R/libusb/hidapi_thread_pthread.h -o hidapi_thread_pthread.h
curl -fsSL $R/hidapi/hidapi.h -o hidapi.h
clang -o minichlink minichlink.c pgm-wch-linke.c pgm-wch-isp.c pgm-esp32s2-ch32xx.c nhc-link042.c ardulink.c serial_dev.c pgm-b003fun.c minichgdb.c chips.c ch5xx.c -I$PREFIX/include/libusb-1.0 -I. -I"$D/../ch32fun-master/minichlink" -DCH32V003 -DMINICHLINK '-DVERSION="android"' -O1 -Wall -Wno-unused-function -lpthread -lusb-1.0
cp minichlink "$D/tools/minichlink" && echo "minichlink rebuilt"
