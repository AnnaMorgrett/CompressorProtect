#!/data/data/com.termux/files/usr/bin/sh
set -e
cd "$(dirname "$0")"
export PATH="$PWD/tools:$PATH"
cd build_native
if [ "$1" = clean ]; then rm -f fw.elf fw.bin fw.hex fw.lst fw.map fw_ext.bin nvm.ld ch32fun_clang.c; echo cleaned; exit 0; fi
python3 patch_ch32fun.py ../../ch32fun-master/ch32fun/ch32fun.c ch32fun_clang.c
clang -E -P -x c -DTARGET_MCU_LD=0 -DMCU_PACKAGE=1 -DCH32V003 ../../ch32fun-master/ch32fun/ch32fun.ld \
 | sed -E '/FLASH[[:space:]]*\(rx\)/ s/LENGTH[[:space:]]*=[[:space:]]*16K/LENGTH = 0x3FC0/' > nvm.ld
grep -q 'LENGTH = 0x3FC0' nvm.ld || { echo "FAIL: linker patch did not apply"; exit 1; }
grep -nE 'FLASH \(rx\)|RAM \(xrw\)' nvm.ld
rm -f fw.elf fw.bin
make build
SZ=$(stat -c %s fw.bin)
echo "== flash image: $SZ bytes, limit 16320 (0x3FC0); NVM 0x08003FC0-0x08003FFF untouched"
[ "$SZ" -le 16320 ] || { echo "FAIL: image would overlap NVM"; exit 1; }
llvm-size fw.elf | awk 'NR==2{print "== RAM static (data+bss): " $2+$3 " of 2048 bytes"}'
ls -l fw.elf fw.bin fw.map

mkdir -p ~/storage/shared/CompressorProtect/out && cp fw.elf fw.bin fw.map ~/storage/shared/CompressorProtect/out/ 2>/dev/null || true
