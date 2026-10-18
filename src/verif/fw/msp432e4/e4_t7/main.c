/* ===========================================================================
 * e4_t7 — El CCM del MSP432E401Y con DriverLib de TI (fase T7, entrega 1):
 * CRC, SHA/MD5 (con SHA-224/256 y HMAC), AES y DES
 *
 * Un servidor de vectores: el banco escribe un caso en el buzon (la orden, la
 * clave, el IV, los datos) y sube `orden`; el firmware lo hace con las
 * funciones de DriverLib, como lo haria una aplicacion, deja el resultado en
 * `sal` y copia `orden` en `hecho`. Asi pasan por el chip TODOS los vectores
 * de verif/vectores (cryp.vec, hash.vec, cryp2.vec, sha2.vec y crc.vec) sin
 * copiarlos aqui: los numeros siguen teniendo una sola casa.
 *
 * Las operaciones (`op`):
 *   1  resumen        SHAMD5ConfigSet + SHAMD5DataProcess
 *   2  HMAC           SHAMD5HMACKeySet + SHAMD5HMACProcess; una clave de mas
 *                     de 64 bytes se resume antes con el mismo motor (RFC 2104)
 *   3  HMAC con la clave procesada: SHAMD5HMACPPKeyGenerate, reset, y
 *                     SHAMD5HMACPPKeySet + SHAMD5HMACProcess
 *   4  resumen en dos trozos, guardando el contexto entre los dos (IDIGEST
 *                     y DIGEST_COUNT, sin CLOSE_HASH), con registros: es lo
 *                     que hace un controlador que resume un fichero a pedazos
 *   5  AES            AESConfigSet, AESKey1Set, AESIVSet, AESDataProcess;
 *                     luego AESIVRead (el IV que queda, para encadenar)
 *   6  AES-GCM        ... y AESDataProcessAuth, con el AAD y la etiqueta
 *   7  DES/TDES       DESConfigSet, DESKeySet, DESIVSet, DESDataProcess
 *   8  CRC            CRCConfigSet; las palabras con CRCDataProcess y el resto
 *                     de bytes con SIZE = 8 bits y INIT = SEED (sigue)
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_shamd5.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_ccm.h"
#include "ti/devices/msp432e4/driverlib/types.h"

typedef struct {
    volatile uint32_t modo;          /* 0x00 */
    volatile uint32_t arranques;     /* 0x04 */
    volatile uint32_t estado;        /* 0x08  100: listo para ordenes */
    volatile uint32_t sysclk;        /* 0x0C */
    volatile uint32_t orden;         /* 0x10  la sube el banco */
    volatile uint32_t hecho;         /* 0x14  el firmware la iguala al acabar */
    volatile uint32_t op;            /* 0x18 */
    volatile uint32_t cfg;           /* 0x1C  CTRL de AES/DES, MODE de SHA, CRCCTRL */
    volatile uint32_t nclave;        /* 0x20  bytes */
    volatile uint32_t nent;          /* 0x24  bytes */
    volatile uint32_t naad;          /* 0x28  bytes */
    volatile uint32_t res;           /* 0x2C  el CRC */
    volatile uint32_t r[4];          /* 0x30  para lo que haga falta */
    uint32_t clave[64];              /* 0x40  hasta 256 bytes */
    uint32_t iv[4];                  /* 0x140 */
    uint32_t tag[4];                 /* 0x150 */
    uint32_t ivsal[4];               /* 0x160 */
    uint32_t pp[16];                 /* 0x170 la clave HMAC procesada */
    uint32_t aad[32];                /* 0x1B0 */
    uint32_t ent[256];               /* 0x230 hasta 1 KB */
    uint32_t sal[256];               /* 0x630 */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static uint32_t k64[16];

/* Sin biblioteca de C (-nostdlib): las dos copias que hacen falta */
static void pon_ceros(uint32_t *d, uint32_t n) { while (n--) *d++ = 0; }
static void copia(uint32_t *d, const uint32_t *s, uint32_t n) { while (n--) *d++ = *s++; }

static void habilita(uint32_t p) {
    SysCtlPeripheralEnable(p);
    while (!SysCtlPeripheralReady(p)) { }
}

static uint32_t algo_de(uint32_t cfg) { return cfg & SHAMD5_MODE_ALGO_M; }

/* Una clave HMAC de mas de 64 bytes se resume primero [RFC 2104, §2] */
static void prepara_clave(void) {
    pon_ceros(k64, 16);
    if (buzon.nclave > 64) {
        SHAMD5Reset(SHAMD5_BASE);
        SHAMD5ConfigSet(SHAMD5_BASE, algo_de(buzon.cfg) | SHAMD5_MODE_ALGO_CONSTANT |
                                     SHAMD5_MODE_CLOSE_HASH);
        SHAMD5DataProcess(SHAMD5_BASE, buzon.clave, buzon.nclave, k64);
    } else {
        copia(k64, buzon.clave, (buzon.nclave + 3) / 4);
    }
}

static void resumen_en_dos(void) {
    const uint32_t algo = algo_de(buzon.cfg), n = buzon.nent;
    const uint32_t primero = ((n - 1) / 64) * 64;      /* bloques enteros, sin el ultimo */
    uint32_t dig[8], cuenta, i;
    /* Primer trozo: las constantes del algoritmo y sin cerrar */
    SHAMD5Reset(SHAMD5_BASE);
    HWREG(SHAMD5_BASE + SHAMD5_O_MODE) = algo | SHAMD5_MODE_ALGO_CONSTANT;
    HWREG(SHAMD5_BASE + SHAMD5_O_LENGTH) = primero;
    for (i = 0; i < primero; i += 64) SHAMD5DataWrite(SHAMD5_BASE, buzon.ent + i / 4);
    while ((HWREG(SHAMD5_BASE + SHAMD5_O_IRQSTATUS) & SHAMD5_INT_OUTPUT_READY) == 0) { }
    for (i = 0; i < 8; ++i) dig[i] = HWREG(SHAMD5_BASE + SHAMD5_O_IDIGEST_A + 4 * i);
    cuenta = HWREG(SHAMD5_BASE + SHAMD5_O_DIGEST_COUNT);
    buzon.r[0] = cuenta;
    /* Otro reset entre medias, como si otro hilo hubiera usado el motor */
    SHAMD5Reset(SHAMD5_BASE);
    for (i = 0; i < 8; ++i) HWREG(SHAMD5_BASE + SHAMD5_O_IDIGEST_A + 4 * i) = dig[i];
    HWREG(SHAMD5_BASE + SHAMD5_O_DIGEST_COUNT) = cuenta;
    HWREG(SHAMD5_BASE + SHAMD5_O_MODE) = algo | SHAMD5_MODE_CLOSE_HASH;
    SHAMD5DataProcess(SHAMD5_BASE, buzon.ent + primero / 4, n - primero, buzon.sal);
}

static void crc(void) {
    const uint32_t n = buzon.nent, palabras = n / 4;
    CRCConfigSet(CCM0_BASE, buzon.cfg);
    uint32_t r = CRCDataProcess(CCM0_BASE, buzon.ent, palabras, true);
    if (n % 4) {
        CRCConfigSet(CCM0_BASE, (buzon.cfg & ~CCM_CRCCTRL_INIT_M) | CRC_CFG_SIZE_8BIT);
        r = CRCDataProcess(CCM0_BASE, (uint32_t *)((uint8_t *)buzon.ent + palabras * 4), n % 4, true);
    }
    buzon.res = r;
    buzon.r[0] = CRCResultRead(CCM0_BASE, false);       /* el estado, sin postproceso */
}

static void ejecuta(void) {
    switch (buzon.op) {
    case 1:
        SHAMD5Reset(SHAMD5_BASE);
        SHAMD5ConfigSet(SHAMD5_BASE, buzon.cfg);
        SHAMD5DataProcess(SHAMD5_BASE, buzon.ent, buzon.nent, buzon.sal);
        break;
    case 2:
        prepara_clave();
        SHAMD5Reset(SHAMD5_BASE);
        SHAMD5ConfigSet(SHAMD5_BASE, algo_de(buzon.cfg));
        SHAMD5HMACKeySet(SHAMD5_BASE, k64);
        SHAMD5HMACProcess(SHAMD5_BASE, buzon.ent, buzon.nent, buzon.sal);
        break;
    case 3:
        prepara_clave();
        SHAMD5Reset(SHAMD5_BASE);
        SHAMD5ConfigSet(SHAMD5_BASE, algo_de(buzon.cfg));
        SHAMD5HMACPPKeyGenerate(SHAMD5_BASE, k64, buzon.pp);
        SHAMD5Reset(SHAMD5_BASE);
        SHAMD5ConfigSet(SHAMD5_BASE, algo_de(buzon.cfg));
        SHAMD5HMACPPKeySet(SHAMD5_BASE, buzon.pp);
        SHAMD5HMACProcess(SHAMD5_BASE, buzon.ent, buzon.nent, buzon.sal);
        break;
    case 4:
        resumen_en_dos();
        break;
    case 5:
        AESReset(AES_BASE);
        AESConfigSet(AES_BASE, buzon.cfg);
        AESKey1Set(AES_BASE, buzon.clave, buzon.cfg & AES_CTRL_KEY_SIZE_M);
        AESIVSet(AES_BASE, buzon.iv);
        AESDataProcess(AES_BASE, buzon.ent, buzon.sal, buzon.nent);
        if (buzon.cfg & (AES_CFG_MODE_CBC | AES_CFG_MODE_CTR | AES_CFG_MODE_CFB))
            AESIVRead(AES_BASE, buzon.ivsal);
        break;
    case 6:
        AESReset(AES_BASE);
        AESConfigSet(AES_BASE, buzon.cfg);
        AESKey1Set(AES_BASE, buzon.clave, buzon.cfg & AES_CTRL_KEY_SIZE_M);
        AESIVSet(AES_BASE, buzon.iv);
        AESDataProcessAuth(AES_BASE, buzon.ent, buzon.sal, buzon.nent, buzon.aad, buzon.naad,
                           buzon.tag);
        break;
    case 7:
        DESReset(DES_BASE);
        DESConfigSet(DES_BASE, buzon.cfg);
        DESKeySet(DES_BASE, buzon.clave);
        DESIVSet(DES_BASE, buzon.iv);
        DESDataProcess(DES_BASE, buzon.ent, buzon.sal, buzon.nent);
        break;
    case 8:
        crc();
        break;
    default:
        buzon.res = 0xDEADBEEFu;
        break;
    }
}

int main(void) {
    buzon.arranques++;
    buzon.sysclk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                                      SYSCTL_CFG_VCO_480, 120000000);
    habilita(SYSCTL_PERIPH_CCM0);
    buzon.estado = 100;
    for (;;) {
        const uint32_t o = buzon.orden;
        if (o != buzon.hecho) {
            ejecuta();
            buzon.hecho = o;
        }
    }
}
