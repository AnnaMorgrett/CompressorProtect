#!/data/data/com.termux/files/usr/bin/sh
D="$(cd "$(dirname "$0")" && pwd)"
BIN="$D/build_native/fw.bin"
[ -f "$BIN" ] || { echo "no fw.bin: run ./build.sh first"; exit 1; }
SZ=$(stat -c %s "$BIN")
[ "$SZ" -le 16320 ] || { echo "REFUSED: $SZ bytes would overlap the NVM block (limit 16320)"; exit 1; }
echo "Target: CH32V003A4M6 | file: $BIN ($SZ bytes) | start 0x08000000"
echo "NVM 0x08003FC0-0x08003FFF is outside the image and not written"
echo "LOW-VOLTAGE BENCH ONLY: no mains connected to anything."
printf 'Type FLASH to program: '; read a; [ "$a" = FLASH ] || { echo aborted; exit 1; }
su -c "LD_LIBRARY_PATH=$PREFIX/lib $D/tools/minichlink -w $BIN flash -b"
