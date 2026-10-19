/* ===========================================================================
 * e4_demo — El firmware de verificacion de la fase T6 (entrega 1) del
 * MSP432E401Y
 *
 * Compilado con la cabecera de TI (msp432e401y.h, BSD-3) y nada mas: cada
 * registro se toca por el nombre y la direccion que TI le da.
 *
 * Un solo binario con varios MODOS, como p4_demo. El banco escribe el modo en
 * el buzon -el primer bloque de la SRAM, que no se borra en los resets- antes
 * de soltar el reset, y lee alli lo que el firmware ha visto:
 *
 *   0  parpadeo con PIOSC (16 MHz, lo que hay al salir del reset): los cuatro
 *      LED de la LaunchPad (PN1, PN0, PF4, PF0); identidad
 *   1  la secuencia buena para 120 MHz: MOSC de 25 MHz, PLL a 480 MHz,
 *      MEMTIM0 con 5 estados de espera y RSCLKCFG con USEPLL y MEMTIMU
 *   2  la secuencia MALA: MEMTIM0 escrito pero sin MEMTIMU, y USEPLL antes de
 *      que enganche el PLL; al final la buena
 *   3  un periferico sin reloj: leer GPION sin RCGCGPIO da un BusFault (se
 *      apunta y se sigue), y otra vez justo al darle reloj sin esperar a PR
 *   4  USR_SW1 (PJ0, con pull-up) por interrupcion de flanco de bajada
 *   5  el perro WDT0 con RESEN, sin alimentar: resetea; RESC dice WDT0
 *   6  SYSRESETREQ: RESC dice SW
 *   7  el GPIO de Tiva: el dato enmascarado por la direccion, LOCK/CR en PD7,
 *      y un pin de salida sin DEN
 *   8  CoreMark no: eso es ../coremark. Aqui, 120 MHz con PIOSC como fuente
 *      del PLL (SYSCTL_OSC_INT), sin cristal
 * ===========================================================================*/
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"

typedef struct {
    volatile uint32_t modo;          /* 0x00 lo pone el banco                 */
    volatile uint32_t arranques;     /* 0x04                                  */
    volatile uint32_t estado;        /* 0x08 hasta donde se ha llegado        */
    volatile uint32_t resc;          /* 0x0C RESC al arrancar                 */
    volatile uint32_t did0;          /* 0x10                                  */
    volatile uint32_t did1;          /* 0x14                                  */
    volatile uint32_t flashpp;       /* 0x18 FLASH_CTRL->PP                   */
    volatile uint32_t ssize;         /* 0x1C                                  */
    volatile uint32_t rsclkcfg;      /* 0x20 al acabar de configurar          */
    volatile uint32_t memtim0;       /* 0x24                                  */
    volatile uint32_t pllstat;       /* 0x28                                  */
    volatile uint32_t faults;        /* 0x2C BusFaults vistos                 */
    volatile uint32_t bfar;          /* 0x30 el ultimo                        */
    volatile uint32_t cfsr;          /* 0x34                                  */
    volatile uint32_t pulsaciones;   /* 0x38                                  */
    volatile uint32_t ris;           /* 0x3C GPIOJ->RIS en la interrupcion    */
    volatile uint32_t m7_dato;       /* 0x40 lo que se lee tras escribir enmascarado */
    volatile uint32_t m7_cr;         /* 0x44 GPIOD->CR antes y despues        */
    volatile uint32_t m7_afsel;      /* 0x48 GPIOD->AFSEL tras intentar y tras abrir */
    volatile uint32_t m7_lock;       /* 0x4C GPIOD->LOCK cerrado y abierto    */
    volatile uint32_t pr_tras_rcgc;  /* 0x50 PRGPIO justo al poner RCGC        */
    volatile uint32_t did0_rom;      /* 0x54 ROM_APITABLE[0] si hay ROM (no)   */
    volatile uint32_t vueltas;       /* 0x58                                  */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static void espera(uint32_t n) { for (volatile uint32_t i = 0; i < n; ++i) { } }

/* --- Los relojes, a mano: lo que hace SysCtlClockFreqSet de DriverLib ------- */
#define MEMTIM_40  (SYSCTL_MEMTIM0_FBCHT_1_5 | (1u << SYSCTL_MEMTIM0_FWS_S) | \
                    SYSCTL_MEMTIM0_EBCHT_1_5 | (1u << SYSCTL_MEMTIM0_EWS_S) | SYSCTL_MEMTIM0_MB1)
#define MEMTIM_120 (SYSCTL_MEMTIM0_FBCHT_3_5 | (5u << SYSCTL_MEMTIM0_FWS_S) | \
                    SYSCTL_MEMTIM0_EBCHT_3_5 | (5u << SYSCTL_MEMTIM0_EWS_S) | SYSCTL_MEMTIM0_MB1)

static int mosc_25(void) {
    SYSCTL->MOSCCTL = (SYSCTL->MOSCCTL & ~(SYSCTL_MOSCCTL_PWRDN | SYSCTL_MOSCCTL_NOXTAL))
                      | SYSCTL_MOSCCTL_OSCRNG;
    for (uint32_t n = 524288; n; --n)
        if (SYSCTL->RIS & SYSCTL_RIS_MOSCPUPRIS) return 1;
    return 0;
}

/* 120 MHz: fuente (MOSC o PIOSC), M = 96 (25 MHz, N = 5) o 30 (16 MHz, N = 1)
 * para un VCO de 480; Q = 2 y PSYSDIV = 1. `bien` = 0 hace los dos errores */
static void a_120(int mosc, int bien) {
    const uint32_t src = mosc ? (SYSCTL_RSCLKCFG_OSCSRC_MOSC | SYSCTL_RSCLKCFG_PLLSRC_MOSC)
                              : (SYSCTL_RSCLKCFG_OSCSRC_PIOSC | SYSCTL_RSCLKCFG_PLLSRC_PIOSC);
    /* Los tiempos de 25 MHz, el maximo de MOSC, ANTES de cambiar de fuente:
     * es lo que hace DriverLib, y sin ello el MOSC iria con FWS = 0 */
    SYSCTL->MEMTIM0 = MEMTIM_40;
    SYSCTL->RSCLKCFG = (SYSCTL->RSCLKCFG & ~(SYSCTL_RSCLKCFG_PSYSDIV_M | SYSCTL_RSCLKCFG_OSCSRC_M |
                                             SYSCTL_RSCLKCFG_PLLSRC_M | SYSCTL_RSCLKCFG_USEPLL))
                       | SYSCTL_RSCLKCFG_MEMTIMU;
    SYSCTL->RSCLKCFG |= src;
    if (mosc) {
        SYSCTL->PLLFREQ1 = ((5u - 1u) << SYSCTL_PLLFREQ1_N_S) | ((2u - 1u) << SYSCTL_PLLFREQ1_Q_S);
        SYSCTL->PLLFREQ0 = 96u | (SYSCTL->PLLFREQ0 & SYSCTL_PLLFREQ0_PLLPWR);
    } else {
        SYSCTL->PLLFREQ1 = ((1u - 1u) << SYSCTL_PLLFREQ1_N_S) | ((2u - 1u) << SYSCTL_PLLFREQ1_Q_S);
        SYSCTL->PLLFREQ0 = 30u | (SYSCTL->PLLFREQ0 & SYSCTL_PLLFREQ0_PLLPWR);
    }
    SYSCTL->MEMTIM0 = MEMTIM_120;
    if (SYSCTL->PLLFREQ0 & SYSCTL_PLLFREQ0_PLLPWR) SYSCTL->RSCLKCFG |= SYSCTL_RSCLKCFG_NEWFREQ;
    else SYSCTL->PLLFREQ0 |= SYSCTL_PLLFREQ0_PLLPWR;
    if (!bien) {
        /* Error 1: USEPLL sin esperar a LOCK; error 2: sin MEMTIMU */
        SYSCTL->RSCLKCFG |= (1u << SYSCTL_RSCLKCFG_PSYSDIV_S) | src | SYSCTL_RSCLKCFG_USEPLL;
        espera(2000);
        buzon.estado = 20;
        /* Y ahora bien */
        SYSCTL->RSCLKCFG |= SYSCTL_RSCLKCFG_MEMTIMU;
    }
    for (uint32_t n = 32768; n; --n) if (SYSCTL->PLLSTAT & SYSCTL_PLLSTAT_LOCK) break;
    SYSCTL->RSCLKCFG |= (1u << SYSCTL_RSCLKCFG_PSYSDIV_S) | src | SYSCTL_RSCLKCFG_USEPLL |
                        SYSCTL_RSCLKCFG_MEMTIMU;
    buzon.rsclkcfg = SYSCTL->RSCLKCFG;
    buzon.memtim0  = SYSCTL->MEMTIM0;
    buzon.pllstat  = SYSCTL->PLLSTAT;
}

/* --- Los LED ---------------------------------------------------------------- */
static void reloj_gpio(uint32_t bits) {
    SYSCTL->RCGCGPIO |= bits;
    while ((SYSCTL->PRGPIO & bits) != bits) { }
}
static void leds(void) {
    reloj_gpio(SYSCTL_RCGCGPIO_R12 | SYSCTL_RCGCGPIO_R5);     /* N y F */
    GPION->DIR |= 0x03; GPION->DEN |= 0x03;
    GPIOF->DIR |= 0x11; GPIOF->DEN |= 0x11;
}
/* Escribe solo PN0/PN1 por el dato enmascarado: base + (mascara << 2) */
#define GPION_D01 (*(volatile uint32_t *)(GPIO_PORTN_BASE + (0x03u << 2)))
#define GPIOF_D04 (*(volatile uint32_t *)(GPIO_PORTF_BASE + (0x11u << 2)))
static void parpadea(void) __attribute__((noreturn));
static void parpadea(void) {
    buzon.estado = 100;
    for (uint32_t k = 0;; ++k) {
        GPION_D01 = (k & 1u) ? 0x02u : 0x01u;           /* D1 y D2 alternos */
        GPIOF_D04 = (k & 2u) ? 0x11u : 0x00u;           /* D3 y D4 a la mitad */
        buzon.vueltas = k;
        espera(2000);
    }
}

/* --- Fallos ----------------------------------------------------------------- */
/* Un BusFault preciso: se apunta y se salta la instruccion que fallo */
void BusFault_Handler(void) __attribute__((naked));
void BusFault_Handler(void) {
    __asm volatile(
        "tst   lr, #4            \n"
        "ite   eq                \n"
        "mrseq r0, msp           \n"
        "mrsne r0, psp           \n"
        "b     bus_fault_c       \n");
}
void bus_fault_c(uint32_t *marco) {
    buzon.faults++;
    buzon.cfsr = SCB->CFSR;
    buzon.bfar = SCB->BFAR;
    SCB->CFSR = SCB->CFSR;                               /* W1C */
    const uint16_t ins = *(const uint16_t *)marco[6];
    marco[6] += ((ins & 0xF800u) >= 0xE800u) ? 4u : 2u;
}

void GPIOJ_IRQHandler(void) {
    buzon.ris = GPIOJ->RIS;
    GPIOJ->ICR = GPIOJ->MIS;
    buzon.pulsaciones++;
}

int main(void) {
    buzon.arranques++;
    buzon.resc = SYSCTL->RESC;
    SYSCTL->RESC = 0;
    buzon.did0 = SYSCTL->DID0;
    buzon.did1 = SYSCTL->DID1;
    buzon.flashpp = FLASH_CTRL->PP;
    buzon.ssize = FLASH_CTRL->SSIZE;
    SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
    buzon.estado = 1;

    switch (buzon.modo) {
    case 0:
        leds();
        parpadea();
    case 1:
        if (!mosc_25()) { buzon.estado = 90; for (;;) { } }
        a_120(1, 1);
        leds();
        parpadea();
    case 2:
        if (!mosc_25()) { buzon.estado = 90; for (;;) { } }
        a_120(1, 0);
        leds();
        parpadea();
    case 3: {
        volatile uint32_t x;
        x = GPION->DIR;                                 /* sin reloj: BusFault */
        (void)x;
        buzon.estado = 30;
        SYSCTL->RCGCGPIO |= SYSCTL_RCGCGPIO_R12;
        buzon.pr_tras_rcgc = SYSCTL->PRGPIO;             /* todavia no esta listo */
        while (!(SYSCTL->PRGPIO & SYSCTL_PRGPIO_R12)) { }
        x = GPION->DIR;                                  /* ahora si */
        buzon.estado = 31;
        leds();
        parpadea();
    }
    case 4:
        reloj_gpio(SYSCTL_RCGCGPIO_R8);                  /* J */
        GPIOJ->DIR &= ~0x01u;
        GPIOJ->PUR |= 0x01u;
        GPIOJ->DEN |= 0x01u;
        GPIOJ->IS &= ~0x01u;                             /* flanco */
        GPIOJ->IBE &= ~0x01u;
        GPIOJ->IEV &= ~0x01u;                            /* de bajada */
        GPIOJ->ICR = 0x01u;
        GPIOJ->IM |= 0x01u;
        NVIC_EnableIRQ(GPIOJ_IRQn);
        buzon.estado = 40;
        for (;;) { __WFI(); }
    case 5:
        if (buzon.arranques > 1) { buzon.estado = 51; leds(); parpadea(); }
        SYSCTL->RCGCWD |= SYSCTL_RCGCWD_R0;
        while (!(SYSCTL->PRWD & SYSCTL_PRWD_R0)) { }
        WATCHDOG0->LOAD = 16000u;                         /* 1 ms a 16 MHz */
        WATCHDOG0->CTL = WDT_CTL_RESEN | WDT_CTL_INTEN;
        WATCHDOG0->LOCK = 0;                              /* cerrado */
        buzon.estado = 50;
        for (;;) { }
    case 6:
        if (buzon.arranques > 1) { buzon.estado = 61; for (;;) { } }
        buzon.estado = 60;
        NVIC_SystemReset();
    case 7: {
        reloj_gpio(SYSCTL_RCGCGPIO_R12 | SYSCTL_RCGCGPIO_R3);   /* N y D */
        GPION->DIR |= 0x03; GPION->DEN |= 0x03;
        GPION->DATA = 0x00;
        *(volatile uint32_t *)(GPIO_PORTN_BASE + (0x02u << 2)) = 0xFFu;   /* solo PN1 */
        buzon.m7_dato = (GPION->DATA & 0xFFu) |
                        ((*(volatile uint32_t *)(GPIO_PORTN_BASE + (0x01u << 2)) & 0xFFu) << 8);
        /* PD7 esta protegido: sin LOCK, AFSEL no cambia */
        const uint32_t cr0 = GPIOD->CR, lock0 = GPIOD->LOCK;
        GPIOD->AFSEL |= 0x80u;
        const uint32_t af0 = GPIOD->AFSEL;
        GPIOD->LOCK = GPIO_LOCK_KEY;
        const uint32_t lock1 = GPIOD->LOCK;
        GPIOD->CR |= 0x80u;
        GPIOD->AFSEL |= 0x80u;
        const uint32_t af1 = GPIOD->AFSEL;
        GPIOD->AFSEL &= ~0x80u;
        GPIOD->CR &= ~0x80u;
        GPIOD->LOCK = 0;
        buzon.m7_cr = cr0 | (GPIOD->CR << 8);
        buzon.m7_afsel = af0 | (af1 << 8);
        buzon.m7_lock = lock0 | (lock1 << 8) | (GPIOD->LOCK << 16);
        /* PF0 de salida, pero sin DEN: el modelo avisa y el pin no se mueve */
        reloj_gpio(SYSCTL_RCGCGPIO_R5);
        GPIOF->DIR |= 0x01u;
        GPIOF->DATA |= 0x01u;
        buzon.estado = 70;
        for (;;) { }
    }
    case 8:
        a_120(0, 1);
        leds();
        parpadea();
    default:
        buzon.estado = 99;
        for (;;) { }
    }
}
