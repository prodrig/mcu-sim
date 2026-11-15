/* ds.c — E8: la firma digital (RSA_DS) con la secuencia de ds_ll.h de
 * ESP-IDF y los vectores de gen_ds.py (C, como gen_digital_signature_tests.py
 * de ESP-IDF; Z, de Python). El banco quema antes KEY3 = DS_HK con propósito
 * 7 (DOWN_DIGITAL_SIGNATURE).
 *   res[0]  bits de lo que sale bien:
 *           0 sin el HMAC, QUERY_BUSY sigue a 1 tras 1,1 ms y KEY_WRONG = 0
 *           1 con la bajada del HMAC (KEY3, propósito 7), la clave llega
 *           2 la firma RSA-1024: QUERY_CHECK = 0 y Z como Python
 *           3 SET_FINISH borra Z_MEM
 *           4 C con un byte cambiado: QUERY_CHECK = 1 (MD) y Z a cero
 *           5 C con el relleno mal: QUERY_CHECK = 2 y Z como Python
 *           6 tras SET_INVALIDATE_DS en el HMAC, la clave ya no llega
 *           7 DATE = 0x20200618
 *   res[1]  lo que tarda la firma, en tics del SYSTIMER (16 MHz)
 *   res[2]  la palabra baja de Z */
#include "c3.h"
#include "ds_vec.h"
#define DS(o)    R(0x6003D000u + (o))
#define HMAC(o)  R(0x6003E000u + (o))

static void pon(uint32_t off, const uint32_t *v, unsigned n) { for (unsigned i = 0; i < n; ++i) DS(off + 4 * i) = v[i]; }
static int z_igual(const uint32_t *v) {
    for (unsigned i = 0; i < DS_N; ++i) if (DS(0xA00 + 4 * i) != v[i]) return 0;
    return 1;
}
static int z_cero(void) {
    for (unsigned i = 0; i < DS_N; ++i) if (DS(0xA00 + 4 * i)) return 0;
    return 1;
}
static int clave_lista(void) {                   /* el plazo de ESP-IDF: 1100 us */
    uint64_t f = systimer_ahora() + 1100u * 16u;
    while (DS(0xE0C)) if (systimer_ahora() > f) return 0;
    return 1;
}
/* Una firma con el C dado: devuelve QUERY_CHECK y deja los tics en *t */
static uint32_t firma(const uint32_t *c, uint32_t *t) {
    DS(0xE00) = 1;
    if (!clave_lista()) { DS(0xE08) = 1; return 0xFFu; }
    pon(0x630, DS_IV, 4);
    pon(0x800, DS_X, DS_N);
    pon(0x000, c, 96);
    pon(0x200, c + 96, 96);
    pon(0x400, c + 192, 96);
    pon(0x600, c + 288, 12);
    uint32_t t0 = (uint32_t)systimer_ahora();
    DS(0xE04) = 1;
    while (DS(0xE0C)) {}
    if (t) *t = (uint32_t)systimer_ahora() - t0;
    return DS(0xE14);
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0, t = 0;
    if (DS(0xE20) == 0x20200618u) ok |= 1u << 7;
    /* Sin la bajada del HMAC: la clave no llega */
    DS(0xE00) = 1;
    if (!clave_lista() && DS(0xE10) == 0) ok |= 1u << 0;
    DS(0xE08) = 1;
    /* La bajada del DS: KEY3 con propósito 7 */
    HMAC(0x40) = 1;
    HMAC(0x44) = 7;
    HMAC(0x48) = 3;
    HMAC(0x4C) = 1;
    while (HMAC(0x6C)) {}
    if (HMAC(0x68) == 0) {
        uint32_t ck = firma(DS_C, &t);
        if (ck != 0xFFu) ok |= 1u << 1;
        if (ck == 0 && z_igual(DS_Z)) ok |= 1u << 2;
        buzon.res[1] = t;
        buzon.res[2] = DS(0xA00);
        DS(0xE08) = 1;
        while (DS(0xE0C)) {}
        if (z_cero()) ok |= 1u << 3;
        if (firma(DS_CMAL, 0) == 1 && z_cero()) ok |= 1u << 4;
        DS(0xE08) = 1;
        if (firma(DS_CPAD, 0) == 2 && z_igual(DS_Z)) ok |= 1u << 5;
        DS(0xE08) = 1;
        HMAC(0x64) = 1;                              /* SET_INVALIDATE_DS */
        DS(0xE00) = 1;
        if (!clave_lista()) ok |= 1u << 6;
        DS(0xE08) = 1;
    }
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
