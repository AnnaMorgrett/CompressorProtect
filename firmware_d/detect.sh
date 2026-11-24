#!/data/data/com.termux/files/usr/bin/sh
D="$(cd "$(dirname "$0")" && pwd)"
echo "== USB devices (expect the ESP32-S2 as 303a:4004)"
su -c 'for d in /sys/bus/usb/devices/*; do [ -f $d/idVendor ] && echo "$(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product 2>/dev/null)"; done'
echo "== minichlink (no flash command, read-only)"
su -c "LD_LIBRARY_PATH=$PREFIX/lib $D/tools/minichlink" 2>&1 | head -25
