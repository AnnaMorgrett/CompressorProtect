import re, sys
s = open(sys.argv[1]).read()
def sub(pat, fn, name):
    global s
    s, n = re.subn(pat, fn, s)
    if n < 1: sys.exit("PATCH FAILED: " + name)
    print("patched", name, n)
nmi = r'asm volatile("li a0, 0x40021008\n li a1, 0x800000\n lw a2, 0(a0)\n or a2, a2, a1\n sw a2, 0(a0)\n mret" : : : "a0","a1","a2","memory");'
sub(r'(void NMI_RCC_CSS_IRQHandler\( void \)\s*\{\s*)RCC->INTR \|= RCC_CSSC;[^\n]*', lambda m: m.group(1) + nmi, 'nmi')
def st(v): return r'asm volatile("li a0, 0xE000F000\n li a1, %d\n sw a1, 0(a0)" : : : "a0","a1","memory");' % v
sub(r'SysTick->CTLR = 5;(\s*#else\s*)SysTick->CTLR = 1;', lambda m: st(5) + m.group(1) + st(1), 'systick')
sub(r'[ \t]*__builtin_unreachable\(\);[^\n]*Disable warning about no return\.[^\n]*\n?', lambda m: '', 'unreachable')
open(sys.argv[2], 'w').write(s)
