/* hmac.c — E8: el acelerador HMAC, con la secuencia de esp_hmac_calculate
 * de ESP-IDF y los resultados de Python (hmac_vec.h, de gen_hmac.py). El
 * banco quema antes los eFuse: KEY0 = HMAC_K0 con propósito 8 (subida) y
 * RD_DIS, KEY1 = HMAC_K1 con propósito 6 (JTAG), KEY2 = HMAC_K2 con
 * propósito 7 (DS), y SOFT_DIS_JTAG con DIS_USB_JTAG (el JTAG va a los
 * pines y está cerrado).
 *   res[0]  bits de lo que sale bien:
 *           0 32 bytes: un bloque rellenado por el software (ONE_BLOCK)
 *           1 64 bytes: un bloque y el relleno del hardware (MESSAGE_END)
 *           2 100 bytes: dos bloques, el último con PAD
 *           3 200 bytes: tres bloques con ING y el último con PAD
 *           4 KEY1 (propósito 6) pedida en subida: QUERY_ERROR = 1
 *           5 la CPU lee ceros en RD_KEY0 (RD_DIS) y el HMAC la usa igual
 *           6 QUERY_BUSY = 1 justo después de SET_MESSAGE_ONE
 *           7 SET_RESULT_FINISH borra RD_RESULT
 *   res[1]  la primera palabra del resultado de 32 bytes
 *   res[2]  DATE
 *   res[3]  el paso del JTAG: 1 tras un WR_JTAG equivocado, 2 tras el bueno,
 *           3 tras SET_INVALIDATE_JTAG (el banco mira la fuente del JTAG) */
#include "c3.h"
#include "hmac_vec.h"
#define HMAC(o)  R(0x6003E000u + (o))
#define EFUSE_RD_KEY0 R(0x6000889Cu)

static void espera(void) { while (HMAC(0x6C)) {} }
static int bloque(const uint8_t *b) {             /* hmac_ll_write_block_512 */
    espera();
    for (unsigned i = 0; i < 16; ++i)
        HMAC(0x80 + 4 * i) = (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 |
                             (uint32_t)b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24;
    HMAC(0x50) = 1;
    return HMAC(0x6C) != 0;
}
static uint32_t configura(unsigned proposito, unsigned clave) {
    espera();
    HMAC(0x40) = 1;
    HMAC(0x44) = proposito;
    HMAC(0x48) = clave;
    HMAC(0x4C) = 1;
    espera();
    uint32_t e = HMAC(0x68);
    if (e) HMAC(0x5C) = 2;
    return e;
}
static int resultado(const uint8_t *h) {
    espera();
    int ok = 1;
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t w = HMAC(0xC0 + 4 * i);
        uint32_t e = (uint32_t)h[4 * i] | (uint32_t)h[4 * i + 1] << 8 | (uint32_t)h[4 * i + 2] << 16 | (uint32_t)h[4 * i + 3] << 24;
        if (w != e) ok = 0;
    }
    return ok;
}
static void ultimo(uint8_t *blk, const uint8_t *m, unsigned r, unsigned n) {
    for (unsigned i = 0; i < 64; ++i) blk[i] = 0;
    for (unsigned i = 0; i < r; ++i) blk[i] = m[i];
    blk[r] = 0x80;
    uint64_t bits = (uint64_t)n * 8u + 512u;
    for (unsigned i = 0; i < 8; ++i) blk[63 - i] = (uint8_t)(bits >> (8 * i));
}
/* esp_hmac_calculate, para n > 55 (varios bloques) */
static int calcula(const uint8_t *m, unsigned n, const uint8_t *h) {
    uint8_t blk[64];
    if (configura(8, 0)) return 0;
    unsigned enteros = n / 64, resto = n % 64;
    for (unsigned i = 1; i < enteros; ++i) { bloque(m); m += 64; espera(); HMAC(0x54) = 1; }
    if (enteros) { bloque(m); m += 64; }
    if (resto >= 64 - 9) {
        for (unsigned i = 0; i < 64; ++i) blk[i] = i < resto ? m[i] : (i == resto ? 0x80 : 0);
        espera(); HMAC(0x54) = 1;
        bloque(blk);
        resto = 0; m = blk;           /* el último lleva solo la longitud */
        for (unsigned i = 0; i < 64; ++i) blk[i] = 0;
        uint64_t bits = (uint64_t)n * 8u + 512u;
        for (unsigned i = 0; i < 8; ++i) blk[63 - i] = (uint8_t)(bits >> (8 * i));
    } else {
        ultimo(blk, m, resto, n);
    }
    espera(); HMAC(0xF0) = 1;          /* PAD */
    bloque(blk);
    int ok = resultado(h);
    HMAC(0x5C) = 2;
    return ok;
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    uint8_t blk[64];
    buzon.res[2] = HMAC(0xF8);
    /* 32 bytes: un bloque, ONE_BLOCK */
    if (!configura(8, 0)) {
        ultimo(blk, HMAC_MSG, 32, 32);
        if (bloque(blk)) ok |= 1u << 6;
        espera(); HMAC(0xF4) = 1;
        if (resultado(HMAC_H32)) ok |= 1u << 0;
        buzon.res[1] = HMAC(0xC0);
        HMAC(0x5C) = 2;
        if (HMAC(0xC0) == 0 && HMAC(0xDC) == 0) ok |= 1u << 7;
    }
    /* 64 bytes: el relleno lo pone el hardware */
    if (!configura(8, 0)) {
        bloque(HMAC_MSG);
        espera(); HMAC(0x58) = 1;
        if (resultado(HMAC_H64)) ok |= 1u << 1;
        HMAC(0x5C) = 2;
    }
    if (calcula(HMAC_MSG, 100, HMAC_H100)) ok |= 1u << 2;
    if (calcula(HMAC_MSG, 200, HMAC_H200)) ok |= 1u << 3;
    if (configura(8, 1)) ok |= 1u << 4;
    if (EFUSE_RD_KEY0 == 0 && (ok & 1u)) ok |= 1u << 5;
    /* El JTAG: la bajada con KEY1, un intento equivocado y el bueno */
    if (!configura(6, 1)) {
        espera();
        HMAC(0xF8) = 1;
        for (unsigned k = 0; k < 8; ++k) HMAC(0xFC) = HMAC_JW[k] ^ (k == 3 ? 1u : 0u);
        buzon.res[3] = 1;
        espera_us(20);
        HMAC(0xF8) = 1;
        for (unsigned k = 0; k < 8; ++k) HMAC(0xFC) = HMAC_JW[k];
        buzon.res[3] = 2;
        espera_us(20);
        HMAC(0x60) = 1;
        buzon.res[3] = 3;
    }
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
