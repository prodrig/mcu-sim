// rvmodel_macros.h
// Las macros RVMODEL del laboratorio RISC-V de mcu-sim (src/rv/lab/soc_lab_rv.h):
// el mapa de la plataforma por omisión de Spike, con HTIF para terminar.
// Derivado de los de Spike y Hazard3 en riscv-arch-test.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _RVMODEL_MACROS_H
#define _RVMODEL_MACROS_H

// El laboratorio busca el símbolo `tohost` en el ELF
#define RVMODEL_DATA_SECTION \
        .pushsection .tohost,"aw",@progbits;                \
        .balign 8; .global tohost; tohost: .dword 0;         \
        .balign 8; .global fromhost; fromhost: .dword 0;     \
        .popsection

#define STANDARD_SM_SUPPORTED

##### TERMINATION #####

// HTIF: (código << 1) | 1. Bien = 1 (código 0), mal = 3 (código 1).
#define RVMODEL_HALT_PASS  \
  li x1, 1                ;\
  la t0, tohost           ;\
  write_tohost_pass:      ;\
    sw x1, 0(t0)          ;\
    sw x0, 4(t0)          ;\
    j write_tohost_pass   ;\

#define RVMODEL_HALT_FAIL \
  li x1, 3                ;\
  la t0, tohost           ;\
  write_tohost_fail:      ;\
    sw x1, 0(t0)          ;\
    sw x0, 4(t0)          ;\
    j write_tohost_fail   ;\

##### IO #####

// La UART de pega: THR en +0; LSR (+5) dice siempre «transmisor vacío»
.EQU UART_BASE_ADDR, 0x10000000
.EQU UART_THR, (UART_BASE_ADDR + 0)
.EQU UART_LSR, (UART_BASE_ADDR + 5)

#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR)               \
1:                           ;                       \
  lbu _R1, 0(_STR_PTR)        ;/* Load byte */        \
  beqz _R1, 3f                ;/* Exit if null */     \
2: /* uart_putc */           ;                      \
  li _R2, UART_LSR ;\
  4: /* uart_putc_wait_busy */ \
    lbu _R3, 0(_R2) ;\
    andi _R3, _R3, 0x20 ;\
    beqz _R3, 4b ;\
    li _R2, UART_THR ;\
    sb _R1, 0(_R2) ;\
  addi _STR_PTR, _STR_PTR, 1 ;/* Next char */        \
  j 1b                       ;/* Loop */             \
3:

##### Access Fault #####

// Nada en 0x0: el bus responde con error
#define RVMODEL_ACCESS_FAULT_ADDRESS 0x00000000

##### Machine Timer #####

// El CLINT: mtime a 10 MHz con el hart a 100 MHz (10 instrucciones por tick)
#define RVMODEL_MTIME_ADDRESS    0x0200BFF8
#define RVMODEL_MTIMECMP_ADDRESS 0x02004000

##### Machine Interrupts #####

#define RVMODEL_INTERRUPT_LATENCY 1024

// Desde U, RVTEST_SET_MTIME_INT_SOON_U hace varias llamadas a T-SBI de unas
// 1000 instrucciones (100 ticks) cada una
#define RVMODEL_TIMER_INT_SOON_DELAY 1000

#define CLINT_BASE_ADDRESS 0x02000000
#define RVMODEL_MSIP_ADDRESS (CLINT_BASE_ADDRESS + 0x0)

// El generador de interrupciones del laboratorio, el mismo que el de Sail
#define LAB_GEN_ADDRESS 0x0C000004

#define RVMODEL_SET_MEXT_INT(_R1, _R2)  \
  li _R1, (1 << 31) | (1 << 11);        \
  li _R2, LAB_GEN_ADDRESS;              \
  sw _R1, 0(_R2);

#define RVMODEL_CLR_MEXT_INT(_R1, _R2)  \
  li _R1, (1 << 11);                    \
  li _R2, LAB_GEN_ADDRESS;              \
  sw _R1, 0(_R2);

#endif // _RVMODEL_MACROS_H
