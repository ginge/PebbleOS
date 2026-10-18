# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production MPU restore assembly and each descriptor-store boundary."""
import ast
import os
import re
import resource
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get('PEBBLE_SDK_PATH', '/home/baz/pebbleos-sdk-0.1.8'))
CC = SDK / 'arm-none-eabi/bin/arm-none-eabi-gcc'
QEMU = SDK / 'qemu/bin/qemu-pebble'

HARNESS = r'''
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
static uint32_t old[8], next[8];
static volatile unsigned irqs, nmis;
uint32_t restored_r0, restored_r1;
static void output(const char *text) {
 register uint32_t r0 __asm("r0")=4;
 register const char *r1 __asm("r1")=text;
 __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
}
__attribute__((noreturn)) static void finish(unsigned status) {
 uint32_t args[2]={0x20026,status}; register uint32_t r0 __asm("r0")=0x20;
 register uint32_t *r1 __asm("r1")=args;
 __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory"); while(1){}
}
#define CHECK(x) do { if(!(x)) {output("FAIL " #x "\n");finish(1);} } while(0)
static uint32_t mask(void) {uint32_t x;__asm("mrs %0,primask":"=r"(x));return x;}
static int matches(const uint32_t *words) {
 uint32_t selector=REG(0xe000ed98);int same=1;
 for(unsigned i=0;i<4;++i) {
  REG(0xe000ed98)=FIRST+i;
  if(REG(0xe000ed9c)!=words[2*i] || REG(0xe000eda0)!=words[2*i+1]) same=0;
 }
 REG(0xe000ed98)=selector;return same;
}
void irq(void) { CHECK(matches(old) || matches(next)); irqs++; }
void nmi(void) {
 // NMI bypasses PRIMASK: inspect every forced intermediate mapping safely.
 uint32_t selector=REG(0xe000ed98),base[4],limit[4],enabled[4];
 for(unsigned i=0;i<4;++i) {
  REG(0xe000ed98)=FIRST+i;
  base[i]=REG(0xe000ed9c)&~31u;
  limit[i]=(REG(0xe000eda0)&~31u)|31u;
  enabled[i]=REG(0xe000eda0)&1;
  CHECK(!enabled[i] || base[i]>0x003fffff); // No overlap with executable flash.
 }
 for(unsigned i=0;i<4;++i) for(unsigned j=i+1;j<4;++j)
  CHECK(!enabled[i] || !enabled[j] || limit[i]<base[j] || limit[j]<base[i]);
 REG(0xe000ed98)=selector; nmis++;
}
void fault(void) {output("FAIL unexpected processor fault\n");finish(2);}
uint32_t restore(uint32_t sentinel,const uint32_t *words);
__attribute__((noreturn)) void reset(void) {
 __asm volatile("cpsid i");
 for(unsigned i=0;i<16;++i) {REG(0xe000ed98)=i;REG(0xe000eda0)=0;}
 REG(0xe000ed98)=0;REG(0xe000ed9c)=6;REG(0xe000eda0)=0x003fffe1;
 REG(0xe000ed98)=1;REG(0xe000ed9c)=0x20005001;REG(0xe000eda0)=0x20005fe1;
 for(unsigned i=0;i<4;++i) {
  unsigned other=i^1; // New ranges overlap OTHER old slots, not their own.
  old[2*i]=0x20006000+i*0x1000;old[2*i+1]=0x20006fe1+i*0x1000;
  next[2*i]=0x20006000+other*0x1000;next[2*i+1]=0x20006fe1+other*0x1000;
  if(SCENARIO==0 || (SCENARIO==3 && (i&1))) next[2*i]=next[2*i+1]=0;
  if(SCENARIO==1) old[2*i]=old[2*i+1]=0;
  REG(0xe000ed98)=FIRST+i;REG(0xe000ed9c)=old[2*i];REG(0xe000eda0)=old[2*i+1];
 }
 REG(0xe000edc0)=0x44;REG(0xe000ed94)=5;
 REG(0xe000e400)=0x80;REG(0xe000e100)=1;
 __asm volatile("dsb\nisb");
 uint32_t original=MASK;
 __asm volatile("msr primask,%0"::"r"(original):"memory");
 CHECK(restore(0x12345678,next)==0x12345678);
 CHECK(mask()==MASK && matches(next));
 CHECK(restored_r0==0x12345678 && restored_r1==(uintptr_t)(next+8));
 REG(0xe000ed98)=0;CHECK(REG(0xe000ed9c)==6 && REG(0xe000eda0)==0x003fffe1);
 REG(0xe000ed98)=1;CHECK(REG(0xe000ed9c)==0x20005001 && REG(0xe000eda0)==0x20005fe1);
 if(INJECT) {
  CHECK(nmis>0);
  if(MASK) CHECK(irqs==0);
  __asm volatile("cpsie i\ndsb\nisb");
  CHECK(irqs>0);
 }
 output("PASS MPU restore\n");finish(0);
}
__attribute__((section(".vectors"))) const uintptr_t vectors[]={
 0x20004000,(uintptr_t)reset,(uintptr_t)nmi,(uintptr_t)fault,(uintptr_t)fault,
 (uintptr_t)fault,(uintptr_t)fault,0,0,0,0,0,0,0,0,0,(uintptr_t)irq
};
'''
LINKER = '''ENTRY(reset)
MEMORY { FLASH (rx) : ORIGIN = 0, LENGTH = 4M
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K }
SECTIONS { .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } >FLASH
 .ramcode : { *(.ramcode*) } >RAM
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } >RAM }
'''


V7_BEFORE = r'''
// r1 -> thread's MPU words; clobbers r2, r4-r11.
#define RESTORE_MPU \
  "  ldr r2, =0xe000ed98 \n"            /* MPU_RNR */                      \
  "  mov r4, #" STR(FIRST_MPU_REGION) " \n"                                  \
  "  str r4, [r2] \n"                                                        \
  "  ldr r2, =0xe000ed9c \n"            /* MPU_RBAR and its aliases */      \
  "  ldmia r1!, {r4-r11} \n"                                                 \
  "  stmia r2!, {r4-r11} \n"

PBL_NAKED void PendSV_Handler
'''

def production_macro(first, v8=True, source=None):
    if source is None:
        source = (ROOT / 'kernel/arch/arm/arch.c').read_text()
    block = source[source.index('// r1 -> thread\'s MPU words;'):source.index('PBL_NAKED void PendSV_Handler')]
    pp = '#define STR_(x) #x\n#define STR(x) STR_(x)\n' + block + '\nRESTORE_MPU\n'
    args = [str(CC), '-E', '-P', '-x', 'c', f'-DFIRST_MPU_REGION={first}', '-']
    if v8:
        args.insert(-1, '-DCONFIG_ARMV8_M_MAINLINE')
    expanded = subprocess.check_output(args, input=pp, text=True)
    return ''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', expanded))


def assembly(instructions, inject):
    # For the boundary test, expose each store of the production STM explicitly.
    if inject:
        stores = ''.join(f'str r{4+i}, [r2, #{4*i}]\n' for i in range(8))
        instructions = instructions.replace('  stmia r2!, {r4-r11} \n', stores+'add r2,r2,#32\n')
        probe = '''push {r0-r3,r12,lr}
ldr r0,=0xe000e200
mov r1,#1
str r1,[r0]
ldr r0,=0xe000ed04
ldr r1,=0x80000000
str r1,[r0]
dsb
isb
pop {r0-r3,r12,lr}
'''
        instructions = ''.join(line+'\n'+probe for line in instructions.splitlines() if line.strip())
    return '''.syntax unified
.thumb
.section .ramcode,"ax",%progbits
.global restore
.thumb_func
restore:
push {r4-r11,lr}
sub sp,sp,#4
'''+instructions+'''ldr r3,=restored_r0
str r0,[r3]
ldr r3,=restored_r1
str r1,[r3]
add sp,sp,#4
pop {r4-r11,pc}
.ltorg
'''


@unittest.skipUnless(CC.exists() and QEMU.exists(), 'ARM SDK/QEMU unavailable')
class MpuRestoreTests(unittest.TestCase):
    def test_actual_restore_and_intermediate_maps(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp/'test.c').write_text(HARNESS)
            (tmp/'test.ld').write_text(LINKER)
            def run(first, scenario, mask, inject, instructions):
                (tmp/'restore.S').write_text(assembly(instructions, inject))
                subprocess.run([str(CC), '-mcpu=cortex-m33', '-mthumb', '-mgeneral-regs-only',
                                '-O1', '-nostdlib', '-fno-builtin', f'-DFIRST={first}',
                                f'-DSCENARIO={scenario}', f'-DMASK={mask}', f'-DINJECT={inject}',
                                '-Wl,--no-warn-rwx-segments,-T,'+str(tmp/'test.ld'),
                                str(tmp/'test.c'), str(tmp/'restore.S'), '-o', str(tmp/'test.elf')], check=True)
                return subprocess.run([str(QEMU), '-M', 'pebble-gabbro', '-display', 'none',
                                       '-serial', 'null', '-monitor', 'none', '-semihosting-config',
                                       'enable=on,target=native', '-kernel', str(tmp/'test.elf')],
                                      check=False, capture_output=True, text=True, timeout=10,
                                      preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            for first in [4, 8]:
                actual = production_macro(first)
                for scenario in range(4):
                    for mask in [0, 1]:
                        for inject in [0, 1]:
                            with self.subTest(first=first, scenario=scenario, mask=mask, inject=inject):
                                result = run(first, scenario, mask, inject, actual)
                                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                                self.assertIn('PASS MPU restore', result.stdout+result.stderr)
            # Removing the disable stores must expose the overlapping-map failure.
            broken = production_macro(4)
            broken = re.sub(r'  str r3, \[r2, #(?:4|12|20|28)\] \n', '', broken)
            result = run(4, 0, 0, 1, broken)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('FAIL', result.stdout+result.stderr)
            # Compare ARMv7 expansion with the preserved pre-fix source exactly.
            before = V7_BEFORE
            for first in [4, 8]:
                self.assertEqual(production_macro(first, v8=False),
                                 production_macro(first, v8=False, source=before))
