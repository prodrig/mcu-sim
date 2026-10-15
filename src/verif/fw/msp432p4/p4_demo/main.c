/* ===========================================================================
 * p4_demo — El firmware de verificacion de la fase T1 del MSP432P401R
 *
 * Compilado con la cabecera de TI (msp432p401r.h, BSD-3) y nada mas: cada
 * registro se toca por el nombre y la direccion que TI le da. Si una sola
 * direccion del modelo no fuera la del P401R, este firmware no funcionaria.
 *
 * Un solo binario con varios MODOS. El banco escribe el modo en el buzon
 * -el primer bloque de la SRAM, que no se borra en los resets- antes de
 * soltar el reset, y lee alli lo que el firmware ha visto:
 *
 *   0  parpadeo a 3 MHz (lo que hay al salir del reset), con el LED RGB
 *   1  la secuencia buena para 48 MHz: VCORE1, un estado de espera, DCO a 48
 *   2  la secuencia MALA: transicion invalida del PCM, DCO a 48 con VCORE0 y
 *      luego VCORE1 sin estados de espera; y al final la buena
 *   3  el perro: no se para, y resetea el chip una y otra vez
 *   4  un pulsador en P1.1 con interrupcion por flanco de bajada y P1IV
 *   5  identidad: TLV, SYSCTL, la llave del CS, el PMAP y el alias de SRAM
 *   6  violacion de la contrasena del perro
 *   7  reset blando por RSTCTL: los perifericos NO se reinician
 *   8  reset duro por RSTCTL: los perifericos SI
 *   9  (T2) se reprograma la Flash: borra un sector del banco 1 y otro del
 *      banco 0 -contando cuanto avanza la CPU mientras tanto-, programa
 *      palabra a palabra y en rafaga, y prueba los cinco errores: sector
 *      protegido, un 0 que se quiere volver 1, el TLV, una rafaga
 *      desalineada y una escritura sin ENABLE
 * ===========================================================================*/
#ifndef NO_MSP_CLASSIC_DEFINES
#define NO_MSP_CLASSIC_DEFINES
#endif
#include "msp432p401r.h"

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

typedef struct {
    volatile uint32_t modo;              /* 0x00 lo pone el banco             */
    volatile uint32_t arranques;         /* 0x04 veces que se ha entrado      */
    volatile uint32_t estado;            /* 0x08 hasta donde se ha llegado    */
    volatile uint32_t hard_stat;         /* 0x0C RSTCTL al arrancar           */
    volatile uint32_t soft_stat;         /* 0x10                              */
    volatile uint32_t pin_stat;          /* 0x14                              */
    volatile uint32_t device_id;         /* 0x18 TLV                          */
    volatile uint32_t hwrev;             /* 0x1C                              */
    volatile uint32_t flash_size;        /* 0x20 SYSCTL                       */
    volatile uint32_t sram_size;         /* 0x24                              */
    volatile uint32_t cs_ctl0_sin_llave; /* 0x28                              */
    volatile uint32_t cs_key;            /* 0x2C                              */
    volatile uint32_t pcm_ifg;           /* 0x30                              */
    volatile uint32_t pcm_ctl0;          /* 0x34                              */
    volatile uint32_t pulsaciones;       /* 0x38                              */
    volatile uint32_t iv;                /* 0x3C                              */
    volatile uint32_t wdt_ctl;           /* 0x40                              */
    volatile uint32_t pmap;              /* 0x44                              */
    volatile uint32_t tlv_fin;           /* 0x48                              */
    volatile uint32_t sram_alias;        /* 0x4C                              */
    volatile uint32_t p1dir;             /* 0x50                              */
    volatile uint32_t reboot_stat;       /* 0x54                              */
    volatile uint32_t m9_vueltas_b1;     /* 0x58 vueltas mientras borra el 1  */
    volatile uint32_t m9_vueltas_b0;     /* 0x5C vueltas mientras borra el 0  */
    volatile uint32_t m9_res;            /* 0x60 un bit por prueba superada   */
    volatile uint32_t m9_ifg;            /* 0x64 FLCTL->IFG al acabar         */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static void para_perro(void) { WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD; }

static void espera(uint32_t n) {
    for (volatile uint32_t i = 0; i < n; ++i) { }
}

static void pcm_a(uint32_t amr) {
    while (PCM->CTL1 & PCM_CTL1_PMR_BUSY) { }
    PCM->CTL0 = PCM_CTL0_KEY_VAL | amr;
    while (PCM->CTL1 & PCM_CTL1_PMR_BUSY) { }
}

static void estados_de_espera(uint32_t w) {
    FLCTL->BANK0_RDCTL = (FLCTL->BANK0_RDCTL & ~FLCTL_BANK0_RDCTL_WAIT_MASK) | w;
    FLCTL->BANK1_RDCTL = (FLCTL->BANK1_RDCTL & ~FLCTL_BANK1_RDCTL_WAIT_MASK) | w;
}

static void dco(uint32_t rsel) {
    CS->KEY = CS_KEY_VAL;
    CS->CTL0 = rsel;                    /* DCOTUNE = 0: el centro exacto */
    CS->KEY = 0;
}

/* El parpadeo: P1.0 conmuta y el RGB recorre sus colores. Para siempre. */
static void parpadea(void) {
    P1->DIR |= BIT0;
    P2->DS  |= BIT0 | BIT1 | BIT2;      /* alta corriente en el RGB */
    P2->DIR |= BIT0 | BIT1 | BIT2;
    uint32_t c = 0;
    buzon.estado = 100;
    for (;;) {
        P1->OUT ^= BIT0;
        P2->OUT = (P2->OUT & ~(BIT0 | BIT1 | BIT2)) | (++c & 7u);
        espera(300);
    }
}

/* --- Modo 9: la Flash, con los registros del FLCTL y nada mas -------------- */
#define B1_SECTOR   0x00030000u     /* banco 1, sector 16                     */
#define B1_PROTEG   0x00031000u     /* banco 1, sector 17: se deja protegido  */
#define B0_SECTOR   0x00010000u     /* banco 0, sector 16: lejos del codigo   */

static uint32_t borra(uint32_t addr, uint32_t tipo) {
    uint32_t n = 0;
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;
    FLCTL->ERASE_SECTADDR = addr;
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_START | tipo;
    while ((FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_STATUS_MASK) !=
           FLCTL_ERASE_CTLSTAT_STATUS_3)
        n++;
    return n;
}

static void autoprograma(void) {
    volatile uint32_t* const b1 = (volatile uint32_t*)B1_SECTOR;
    uint32_t res = 0;
    FLCTL->CLRIFG = 0xFFFFFFFFu;
    FLCTL->BANK1_MAIN_WEPROT &= ~(1u << 16);
    FLCTL->BANK0_MAIN_WEPROT &= ~(1u << 16);

    /* 1. Borrar en el OTRO banco: la CPU sigue corriendo desde el 0 */
    buzon.m9_vueltas_b1 = borra(B1_SECTOR, FLCTL_ERASE_CTLSTAT_TYPE_0);
    if (FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_STATUS_3) res |= 1u << 0;
    uint32_t todo = 0xFFFFFFFFu;
    for (int i = 0; i < 1024; ++i) todo &= b1[i];
    if (todo == 0xFFFFFFFFu) res |= 1u << 1;
    /* ...y en el MISMO banco: la CPU se queda parada hasta que acaba */
    buzon.m9_vueltas_b0 = borra(B0_SECTOR, FLCTL_ERASE_CTLSTAT_TYPE_0);
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;

    /* 2. Programar palabra a palabra, con verificacion despues */
    FLCTL->PRG_CTLSTAT = FLCTL_PRG_CTLSTAT_ENABLE | FLCTL_PRG_CTLSTAT_VER_PST;
    static const uint32_t datos[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    for (int i = 0; i < 4; ++i) b1[i] = datos[i];
    while (FLCTL->PRG_CTLSTAT & FLCTL_PRG_CTLSTAT_STATUS_MASK) { }
    if (b1[0] == datos[0] && b1[3] == datos[3] && (FLCTL->IFG & FLCTL_IFG_PRG) &&
        !(FLCTL->IFG & FLCTL_IFG_AVPST))
        res |= 1u << 2;

    /* 3. En rafaga: dos lineas de 128 bits en 0x3_0040 */
    volatile uint32_t* const dat = &FLCTL->PRGBRST_DATA0_0;
    for (int i = 0; i < 8; ++i) dat[i] = 0xA5000000u | (uint32_t)i;
    FLCTL->PRGBRST_CTLSTAT = FLCTL_PRGBRST_CTLSTAT_CLR_STAT;
    FLCTL->PRGBRST_STARTADDR = B1_SECTOR + 0x40u;
    FLCTL->PRGBRST_CTLSTAT = FLCTL_PRGBRST_CTLSTAT_START | FLCTL_PRGBRST_CTLSTAT_LEN_2 |
                             FLCTL_PRGBRST_CTLSTAT_TYPE_0;
    while ((FLCTL->PRGBRST_CTLSTAT & FLCTL_PRGBRST_CTLSTAT_BURST_STATUS_MASK) !=
           FLCTL_PRGBRST_CTLSTAT_BURST_STATUS_7) { }
    FLCTL->PRGBRST_CTLSTAT = FLCTL_PRGBRST_CTLSTAT_CLR_STAT;
    if (b1[16] == 0xA5000000u && b1[23] == 0xA5000007u && (FLCTL->IFG & FLCTL_IFG_PRGB))
        res |= 1u << 3;

    /* 4. Un sector protegido no se programa: PRG_ERR */
    FLCTL->CLRIFG = 0xFFFFFFFFu;
    *(volatile uint32_t*)B1_PROTEG = 0x12345678u;
    while (FLCTL->PRG_CTLSTAT & FLCTL_PRG_CTLSTAT_STATUS_MASK) { }
    if ((FLCTL->IFG & FLCTL_IFG_PRG_ERR) && *(volatile uint32_t*)B1_PROTEG == 0xFFFFFFFFu)
        res |= 1u << 4;

    /* 5. Un 0 no vuelve a 1 sin borrar: AVPST */
    FLCTL->CLRIFG = 0xFFFFFFFFu;
    b1[0] = 0xFFFFFFFFu;
    while (FLCTL->PRG_CTLSTAT & FLCTL_PRG_CTLSTAT_STATUS_MASK) { }
    if ((FLCTL->IFG & FLCTL_IFG_AVPST) && b1[0] == datos[0]) res |= 1u << 5;

    /* 6. El TLV no se borra: ADDR_ERR */
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;
    FLCTL->ERASE_SECTADDR = 0x00201000u;
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_START | FLCTL_ERASE_CTLSTAT_TYPE_1;
    if ((FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_ADDR_ERR) && TLV->TLV_END == 0x0BD0E11Du)
        res |= 1u << 6;
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;

    /* 7. Una rafaga desalineada: ADDR_ERR, y no se programa nada */
    FLCTL->PRGBRST_STARTADDR = B1_SECTOR + 0x84u;
    FLCTL->PRGBRST_CTLSTAT = FLCTL_PRGBRST_CTLSTAT_START | FLCTL_PRGBRST_CTLSTAT_LEN_2 |
                             FLCTL_PRGBRST_CTLSTAT_TYPE_0;
    if ((FLCTL->PRGBRST_CTLSTAT & FLCTL_PRGBRST_CTLSTAT_ADDR_ERR) && b1[33] == 0xFFFFFFFFu)
        res |= 1u << 7;
    FLCTL->PRGBRST_CTLSTAT = FLCTL_PRGBRST_CTLSTAT_CLR_STAT;

    /* 8. Sin ENABLE, escribir en la Flash no programa nada */
    FLCTL->PRG_CTLSTAT = 0;
    b1[40] = 0;
    if (b1[40] == 0xFFFFFFFFu) res |= 1u << 8;

    FLCTL->BANK1_MAIN_WEPROT |= 1u << 16;
    FLCTL->BANK0_MAIN_WEPROT |= 1u << 16;
    buzon.m9_ifg = FLCTL->IFG;
    buzon.m9_res = res;
}

void PORT1_IRQHandler(void) {
    const uint16_t v = P1->IV;           /* leerlo baja el flag */
    buzon.iv = v;
    if (v == 0x04) {                     /* P1.1: 2*(1+1) */
        buzon.pulsaciones++;
        P1->OUT ^= BIT0;
    }
}

int main(void) {
    buzon.arranques++;
    buzon.hard_stat   = RSTCTL->HARDRESET_STAT;
    buzon.soft_stat   = RSTCTL->SOFTRESET_STAT;
    buzon.pin_stat    = RSTCTL->PINRESET_STAT;
    buzon.reboot_stat = RSTCTL->REBOOTRESET_STAT;
    RSTCTL->HARDRESET_CLR = 0xFFFF;
    RSTCTL->SOFTRESET_CLR = 0xFFFF;

    switch (buzon.modo) {
    case 0:
        para_perro();
        parpadea();
        break;

    case 1:
        para_perro();
        pcm_a(PCM_CTL0_AMR__AM_LDO_VCORE1);
        estados_de_espera(FLCTL_BANK0_RDCTL_WAIT_1);
        dco(CS_CTL0_DCORSEL_5);
        parpadea();
        break;

    case 2:
        para_perro();
        /* Transicion invalida: regulador y tension a la vez */
        pcm_a(PCM_CTL0_AMR__AM_DCDC_VCORE1);
        buzon.pcm_ifg  = PCM->IFG;
        buzon.pcm_ctl0 = PCM->CTL0;
        PCM->CLRIFG = PCM_CLRIFG_CLR_AM_INVALID_TR_IFG;
        /* 48 MHz con VCORE0: el modelo tiene que avisar */
        dco(CS_CTL0_DCORSEL_5);
        buzon.estado = 1;
        /* Bajar a 3 MHz para poder subir de tension, como manda TI */
        dco(CS_CTL0_DCORSEL_1);
        pcm_a(PCM_CTL0_AMR__AM_LDO_VCORE1);
        /* 48 MHz con VCORE1 pero sin estado de espera: otro aviso */
        dco(CS_CTL0_DCORSEL_5);
        buzon.estado = 2;
        estados_de_espera(FLCTL_BANK0_RDCTL_WAIT_1);
        parpadea();
        break;

    case 3:
        /* El perro no se para: cada 2^15 ciclos de SMCLK, un reset */
        P1->DIR |= BIT0;
        P1->OUT |= BIT0;
        buzon.wdt_ctl = WDT_A->CTL;
        for (;;) { }

    case 4:
        para_perro();
        P1->DIR |= BIT0;
        P1->DIR &= ~BIT1;
        P1->REN |= BIT1;                 /* pull-up interno: S1 va a masa */
        P1->OUT |= BIT1;
        P1->IES |= BIT1;                 /* flanco de bajada */
        P1->IFG  = 0;
        P1->IE  |= BIT1;
        NVIC_EnableIRQ(PORT1_IRQn);
        __enable_irq();
        buzon.estado = 1;
        for (;;) { __WFI(); }

    case 5: {
        para_perro();
        buzon.device_id  = TLV->DEVICE_ID;
        buzon.hwrev      = TLV->HWREV;
        buzon.tlv_fin    = TLV->TLV_END;
        buzon.flash_size = SYSCTL->FLASH_SIZE;
        buzon.sram_size  = SYSCTL->SRAM_SIZE;
        buzon.cs_key     = CS->KEY;
        CS->KEY = 0;                     /* sin llave... */
        CS->CTL0 = CS_CTL0_DCORSEL_5;    /* ...esto no hace nada */
        buzon.cs_ctl0_sin_llave = CS->CTL0;
        P2MAP->PMAP_REGISTER0 = 0x55;    /* sin la llave del PMAP: tampoco */
        buzon.pmap = P2MAP->PMAP_REGISTER0;
        *(volatile uint32_t*)0x20008000u = 0xCAFE432Fu;
        buzon.sram_alias = *(volatile uint32_t*)0x01008000u;
        buzon.estado = 1;
        for (;;) { }
    }

    case 6:
        if (buzon.arranques == 1) {
            WDT_A->CTL = 0x1234;         /* contrasena equivocada */
            for (;;) { }
        }
        para_perro();
        buzon.estado = 1;
        for (;;) { }

    case 7:
    case 8:
        para_perro();
        if (buzon.arranques == 1) {
            P1->DIR |= BIT0;
            P1->OUT |= BIT0;
            RSTCTL->RESET_REQ = RSTCTL_RESETREQ_RSTKEY_VAL |
                (buzon.modo == 7 ? RSTCTL_RESET_REQ_SOFT_REQ : RSTCTL_RESET_REQ_HARD_REQ);
            for (;;) { }
        }
        buzon.p1dir = P1->DIR;
        buzon.estado = 1;
        for (;;) { }

    case 9:
        para_perro();
        autoprograma();
        buzon.estado = 1;
        for (;;) { }

    default:
        para_perro();
        for (;;) { }
    }
    return 0;
}
