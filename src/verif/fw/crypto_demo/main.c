/* ===========================================================================
 * main.c — Firmware de demostración del acelerador criptográfico, con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f417xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: **el mismo código que se grabaría en la
 * placa**. Eso es lo que esta prueba demuestra y lo que ninguna de las
 * anteriores demostraba — hasta aquí, el CRYP y el HASH se habían ejercitado
 * desde el banco, escribiendo registros desde fuera; esto los usa desde
 * DENTRO del chip, con el núcleo Cortex-M4 ejecutando de verdad.
 *
 * Hace tres cosas, y las tres tienen respuesta conocida y publicada:
 *
 *   1. AES-128 en ECB. Cifra el primer bloque del apéndice F.1.1 del NIST
 *      SP 800-38A y lo compara con el texto cifrado que ese documento da. Y
 *      luego lo DESCIFRA, que en ECB no es repetir la operación: hay que
 *      preparar antes la clave con `ALGOMODE = 111`.
 *
 *   2. SHA-1 de "abc", el primer caso del RFC 3174.
 *
 *   3. MD5 de "abc", el tercero del RFC 1321.
 *
 * Y mide con el SysTick lo que cuesta cifrar un bloque, que es la razón de que
 * el acelerador exista: un AES-128 por software en un Cortex-M4 son miles de
 * ciclos, y el bloque dice catorce.
 *
 * El buzón en 0x2000 0000 permite al banco comprobar el resultado sin leer un
 * puerto serie. No es trampa: es la misma técnica que usan los otros quince
 * firmwares de verificación de este proyecto.
 * ===========================================================================*/
#include "stm32f417xx.h"

#define PLAZO 2000000u              /* vueltas antes de rendirse */

volatile struct {
    volatile uint32_t done;        /* 1 cuando el firmware ha terminado      */
    volatile uint32_t aes_ok;      /* el cifrado coincide con el del NIST    */
    volatile uint32_t aes_vuelta;  /* y al descifrar sale el texto claro     */
    volatile uint32_t ct[4];       /* el texto cifrado, para que se vea      */
    volatile uint32_t sha1[5];     /* SHA-1("abc")                           */
    volatile uint32_t md5[4];      /* MD5("abc")                             */
    volatile uint32_t ciclos_aes;  /* lo que cuesta un bloque, medido        */
    volatile uint32_t hclk_mhz;    /* a qué frecuencia se midió              */
} mbox __attribute__((section(".mailbox")));

/* El vector F.1.1 del SP 800-38A: la misma clave y el mismo texto claro que
 * trae el ejemplo CRYP_AESModes de ST para esta placa. */
static const uint32_t KEY128[4] = { 0x2B7E1516u, 0x28AED2A6u, 0xABF71588u, 0x09CF4F3Cu };
static const uint32_t PLAIN[4]  = { 0x6BC1BEE2u, 0x2E409F96u, 0xE93D7E11u, 0x7393172Au };
static const uint32_t CIPHER[4] = { 0x3AD77BB4u, 0x0D7A3660u, 0xA89ECAF3u, 0x2466EF97u };

/* ---------------------------------------------------------------------------
 * Reloj: 168 MHz desde el HSI, sin cristal.
 *
 * Los otros demos arrancan del HSE porque su placa lleva cristal; este banco
 * monta el chip a secas, así que el PLL cuelga del HSI de 16 MHz: M = 16,
 * N = 336, P = 2 dan los mismos 168 MHz.
 * -------------------------------------------------------------------------*/
static void clock_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->PLLCFGR = (16u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
}

/* --- AES-128 ECB, un bloque, con el CRYP ---------------------------------*/
static void aes_clave(void)
{
    /* AES-128 usa el TROZO BAJO del espacio de 256 bits: K2 y K3. */
    CRYP->K2LR = KEY128[0];  CRYP->K2RR = KEY128[1];
    CRYP->K3LR = KEY128[2];  CRYP->K3RR = KEY128[3];
}

static void aes_bloque(const uint32_t* in, uint32_t* out, uint32_t algomode,
                       uint32_t dir)
{
    CRYP->CR = CRYP_CR_FFLUSH;                      /* con CRYPEN = 0 */
    CRYP->CR = algomode | dir | CRYP_CR_CRYPEN;
    for (unsigned i = 0; i < 4u; ++i) CRYP->DIN = in[i];
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t n = PLAZO;
        while (((CRYP->SR & CRYP_SR_OFNE) == 0u) && n--) { }
        out[i] = CRYP->DOUT;
    }
    CRYP->CR = 0u;
}

/* --- Un resumen con el HASH ----------------------------------------------*/
static void resume(const void* msg, uint32_t nbytes, uint32_t algo,
                   uint32_t* out, unsigned npal)
{
    const uint8_t* p = (const uint8_t*)msg;
    uint32_t n;

    /* DATATYPE = 10: el mensaje son BYTES, que es como está en memoria. */
    HASH->CR = HASH_CR_INIT | algo | (2u << HASH_CR_DATATYPE_Pos);
    for (uint32_t i = 0; i < nbytes; i += 4u) {
        uint32_t w = 0;
        for (unsigned b = 0; b < 4u; ++b)
            w |= (uint32_t)((i + b < nbytes) ? p[i + b] : 0u) << (8u * b);
        HASH->DIN = w;
    }
    /* NBLW: los bits que valen de la última palabra, como hace el HAL. */
    HASH->STR = 8u * (nbytes % 4u);
    HASH->STR = HASH_STR_DCAL | (8u * (nbytes % 4u));

    n = PLAZO;
    while (((HASH->SR & HASH_SR_DCIS) == 0u) && n--) { }
    for (unsigned i = 0; i < npal; ++i) out[i] = HASH->HR[i];
}

/* El startup de ST llama a `__libc_init_array` antes de `main`, y aqui no hay
 * biblioteca C: se enlaza con `-nostdlib`. Los otros demos de este proyecto
 * hacen lo mismo. */
void __libc_init_array(void) { }

int main(void)
{
    uint32_t ct[4], pt[4];
    uint32_t t0, t1;

    clock_init();

    /* Los dos bits que en un F405/F407 NO existen. */
    RCC->AHB2ENR |= RCC_AHB2ENR_CRYPEN | RCC_AHB2ENR_HASHEN;

    /* --- 1. Cifrar, y medir lo que cuesta -------------------------------- */
    aes_clave();
    SysTick->LOAD = 0x00FFFFFFu;
    SysTick->VAL  = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    t0 = SysTick->VAL;
    aes_bloque(PLAIN, ct, CRYP_CR_ALGOMODE_AES_ECB, 0u);
    t1 = SysTick->VAL;
    SysTick->CTRL = 0u;
    mbox.ciclos_aes = (t0 - t1) & 0x00FFFFFFu;
    mbox.hclk_mhz   = 168u;

    mbox.aes_ok = 1u;
    for (unsigned i = 0; i < 4u; ++i) {
        mbox.ct[i] = ct[i];
        if (ct[i] != CIPHER[i]) mbox.aes_ok = 0u;
    }

    /* --- 2. Y descifrar, que necesita PREPARAR la clave ------------------ */
    aes_clave();
    CRYP->CR = CRYP_CR_ALGOMODE_AES_KEY | CRYP_CR_CRYPEN;   /* ALGOMODE = 111 */
    {
        uint32_t n = PLAZO;
        while ((CRYP->CR & CRYP_CR_CRYPEN) && n--) { }      /* lo baja el HW */
    }
    aes_bloque(CIPHER, pt, CRYP_CR_ALGOMODE_AES_ECB, CRYP_CR_ALGODIR);
    mbox.aes_vuelta = 1u;
    for (unsigned i = 0; i < 4u; ++i)
        if (pt[i] != PLAIN[i]) mbox.aes_vuelta = 0u;

    /* --- 3. Los dos resúmenes de "abc" ----------------------------------- */
    resume("abc", 3u, 0u,             (uint32_t*)mbox.sha1, 5u);  /* ALGO = 0 */
    resume("abc", 3u, HASH_CR_ALGO,   (uint32_t*)mbox.md5,  4u);  /* ALGO = 1 */

    mbox.done = 1u;
    for (;;) { __asm volatile("wfi"); }
}
