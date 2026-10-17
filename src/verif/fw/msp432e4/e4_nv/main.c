/* ===========================================================================
 * e4_nv — La Flash y la EEPROM del MSP432E401Y desde el propio firmware, con
 * DriverLib de TI (fase T6, entrega 2)
 *
 * DriverLib del E4 (flash.c, eeprom.c, sysctl.c: BSD-3) compilada de sus
 * fuentes, sin la ROM: aqui no hay MAP_.
 *
 *   modo 0  la Flash: FlashErase de un sector de 16 KB -contando cuanto
 *           avanza la CPU mientras tanto-, FlashProgram de 64 palabras con el
 *           bufer y de una suelta, un 0 que se quiere volver 1 (INVDRIS), un
 *           sector de solo lectura (FlashProtectSet: ARIS y -1) y una
 *           escritura directa (error de bus)
 *   modo 1  la EEPROM: EEPROMInit, EEPROMProgram de 8 palabras, EEPROMRead,
 *           un bloque protegido; y, en el segundo arranque, que lo escrito
 *           sigue ahi
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"

typedef struct {
    volatile uint32_t modo;          /* 0x00 */
    volatile uint32_t arranques;     /* 0x04 */
    volatile uint32_t estado;        /* 0x08 */
    volatile uint32_t res;           /* 0x0C un bit por prueba superada */
    volatile uint32_t vueltas;       /* 0x10 mientras borra */
    volatile uint32_t ris;           /* 0x14 FCRIS al acabar */
    volatile uint32_t faults;        /* 0x18 */
    volatile uint32_t ee[8];         /* 0x1C lo leido de la EEPROM */
    volatile uint32_t ee_done;       /* 0x3C EEDONE del bloque protegido */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

#define SECTOR_PRUEBA  0x00080000u      /* el sector 32: lejos del firmware */
/* El de proteccion, por debajo de 256 KB: FlashProtectSet de DriverLib del
 * E4 calcula el registro como (bloque / 32) % 4, que es el de los Tiva de
 * 256 KB; por encima elegiria el FMPPEn equivocado (P-17 g) */
#define SECTOR_PROT    0x00020000u

static uint32_t datos[64];

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
    SCB->CFSR = SCB->CFSR;
    const uint16_t ins = *(const uint16_t *)marco[6];
    marco[6] += ((ins & 0xF800u) >= 0xE800u) ? 4u : 2u;
}

/* Lo que hace FlashErase, a mano, para contar vueltas mientras dura */
static uint32_t borra_contando(uint32_t a) {
    uint32_t n = 0;
    FLASH_CTRL->FCMISC = FLASH_FCMISC_AMISC | FLASH_FCMISC_VOLTMISC | FLASH_FCMISC_ERMISC;
    FLASH_CTRL->FMA = a;
    FLASH_CTRL->FMC = FLASH_FMC_WRKEY | FLASH_FMC_ERASE;
    while (FLASH_CTRL->FMC & FLASH_FMC_ERASE) ++n;
    return n;
}

static void modo_flash(void) {
    uint32_t r = 0;
    buzon.vueltas = borra_contando(SECTOR_PRUEBA);
    {
        const uint32_t *p = (const uint32_t *)SECTOR_PRUEBA;
        uint32_t todo = 0xFFFFFFFFu;
        for (unsigned i = 0; i < 0x4000u / 4; ++i) todo &= p[i];
        if (todo == 0xFFFFFFFFu) r |= 1u << 0;
    }
    for (unsigned i = 0; i < 64; ++i) datos[i] = 0xA5000000u | i;
    if (FlashProgram(datos, SECTOR_PRUEBA, sizeof datos) == 0) {
        const uint32_t *p = (const uint32_t *)SECTOR_PRUEBA;
        unsigned bien = 0;
        for (unsigned i = 0; i < 64; ++i) bien += p[i] == (0xA5000000u | i);
        if (bien == 64) r |= 1u << 1;
    }
    /* Una palabra suelta, no alineada a la fila */
    datos[0] = 0x12345678u;
    if (FlashProgram(datos, SECTOR_PRUEBA + 0x1004u, 4) == 0 &&
        *(const uint32_t *)(SECTOR_PRUEBA + 0x1004u) == 0x12345678u) r |= 1u << 2;
    /* Un 0 no vuelve a 1 sin borrar: INVDRIS y el dato es el AND */
    datos[0] = 0xFFFF0000u;
    FlashProgram(datos, SECTOR_PRUEBA + 0x1004u, 4);
    if ((FLASH_CTRL->FCRIS & FLASH_FCRIS_INVDRIS) &&
        *(const uint32_t *)(SECTOR_PRUEBA + 0x1004u) == 0x12340000u) r |= 1u << 3;
    /* Solo lectura: ni se borra ni se programa */
    if (FlashProtectSet(SECTOR_PROT, FlashReadOnly) == 0 &&
        FlashErase(SECTOR_PROT) == -1 && (FLASH_CTRL->FCRIS & FLASH_FCRIS_ARIS)) r |= 1u << 4;
    datos[0] = 0;
    if (FlashProgram(datos, SECTOR_PROT, 4) == -1 &&
        *(const uint32_t *)SECTOR_PROT == 0xFFFFFFFFu) r |= 1u << 5;
    /* Escribir la Flash como si fuera RAM: error de bus, y nada cambia */
    *(volatile uint32_t *)(SECTOR_PRUEBA + 0x2000u) = 0;
    if (buzon.faults == 1 && *(const uint32_t *)(SECTOR_PRUEBA + 0x2000u) == 0xFFFFFFFFu)
        r |= 1u << 6;
    buzon.ris = FLASH_CTRL->FCRIS;
    buzon.res = r;
}

static void modo_eeprom(void) {
    uint32_t r = 0;
    SysCtlPeripheralEnable(SYSCTL_PERIPH_EEPROM0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_EEPROM0)) { }
    if (EEPROMInit() == EEPROM_INIT_OK) r |= 1u << 0;
    if (EEPROMSizeGet() == 6144 && EEPROMBlockCountGet() == 96) r |= 1u << 1;
    uint32_t v[8];
    if (buzon.arranques == 1) {
        for (unsigned i = 0; i < 8; ++i) v[i] = 0xEE000000u | (i << 8) | i;
        if (EEPROMProgram(v, 0x40, sizeof v) == 0) r |= 1u << 2;
    }
    for (unsigned i = 0; i < 8; ++i) v[i] = 0;
    EEPROMRead(v, 0x40, sizeof v);
    unsigned bien = 0;
    for (unsigned i = 0; i < 8; ++i) { buzon.ee[i] = v[i]; bien += v[i] == (0xEE000000u | (i << 8) | i); }
    if (bien == 8) r |= 1u << 3;
    /* Un bloque de solo lectura: NOPERM y la palabra no cambia */
    if (buzon.arranques == 1) {
        EEPROMBlockProtectSet(5, EEPROM_PROT_RO_LNA_URO);
        uint32_t w = 0x5555AAAAu;
        buzon.ee_done = EEPROMProgram(&w, 5 * 64, 4);
        EEPROMRead(&w, 5 * 64, 4);
        if ((buzon.ee_done & EEPROM_RC_NOPERM) && w == 0xFFFFFFFFu) r |= 1u << 4;
    } else {
        r |= 1u << 4;
    }
    buzon.res = r;
    if (buzon.arranques == 1) { buzon.estado = 10; NVIC_SystemReset(); }
}

int main(void) {
    buzon.arranques++;
    SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
    buzon.estado = 1;
    if (buzon.modo == 0) modo_flash();
    else if (buzon.modo == 1) modo_eeprom();
    buzon.estado = 100;
    for (;;) { }
}
