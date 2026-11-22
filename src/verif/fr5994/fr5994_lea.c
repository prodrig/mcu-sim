/* fr5994_lea.c — testfr5 (M8): el LEA del FR5994 con la MSP DSP Library
 * (DSPLib, BSD-3, TI 2017). El mismo programa se compila dos veces:
 *
 *   elf/fr5994_lea.elf      con el LEA (lo que DSPLib hace por defecto en un
 *                           chip con __MSP430_HAS_LEA__): las órdenes, sus
 *                           parámetros en la pila del LEA y la interrupción
 *                           que despierta de LPM0; las INTERLEAVE, cargadas en
 *                           la memoria de código (msp_lea_loadCommand);
 *   elf/fr5994_lea_cpu.elf  con MSP_DISABLE_LEA: la CPU con el MPY32.
 *
 * Por cada función sale `nombre estado huella`, con la huella FNV-1a de
 * los bytes del resultado: las dos versiones tienen que dar lo mismo. Al
 * final, los ciclos de SMCLK de todo (TA0 los cuenta) [de modelo].
 *
 * Todo sale por la UART (UCA0, 115200, SMCLK = DCO a 8 MHz) en texto. */
#include <msp430.h>
#include <stdint.h>
#include "DSPLib.h"

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }

#define N 256
DSPLIB_DATA(VA, 512) static int16_t VA[N];             /* la FFT: alineada a su tamaño */
DSPLIB_DATA(VB, 4) static int16_t VB[N];
DSPLIB_DATA(VC, 4) static int16_t VC[N];
DSPLIB_DATA(VK, 4) static int16_t VK[32];                     /* coeficientes */
DSPLIB_DATA(VS, 4) static int16_t VS[16];                     /* estados */
DSPLIB_DATA(VR, 4) static int32_t VR[4];                      /* resultados escalares */
#define A32 ((int32_t*)VA)
#define B32 ((int32_t*)VB)
#define C32 ((int32_t*)VC)

static uint32_t sem = 12345;
static int16_t azar(int16_t amp) { sem = sem * 1103515245UL + 12345UL; return (int16_t)((int32_t)((sem >> 16) & 0x7FFF) % (2 * amp + 1) - amp); }
static void llena(int16_t* p, unsigned n, int16_t amp) { for (unsigned i = 0; i < n; ++i) p[i] = azar(amp); }
static void llena32(int32_t* p, unsigned n, int16_t amp) { for (unsigned i = 0; i < n; ++i) p[i] = ((int32_t)azar(amp) << 16) | (uint16_t)azar(0x7FFF); }
static void prep(int16_t amp) { sem = 12345; llena(VA, N, amp); llena(VB, N, amp); for (unsigned i = 0; i < N; ++i) VC[i] = 0; }

static uint32_t fnv(const void* p, unsigned n) {
    const uint8_t* b = (const uint8_t*)p;
    uint32_t h = 0x811C9DC5UL;
    while (n--) { h ^= *b++; h *= 0x01000193UL; }
    return h;
}
static void sale(const char* q, msp_status st, const void* p, unsigned n) {
    cad(q); pon(' '); dec(st); pon(' '); hex(fnv(p, n), 8); pon('\n');
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                     /* 8 MHz */
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
    P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 4;
    UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
#if defined(MSP_USE_LEA)
    cad("hola lea rev "); dec(msp_lea_getRevision()); pon('\n');
#else
    cad("hola lea cpu\n");
#endif
    fin_tx();
    uint32_t ciclos = 0;
    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR;
#define MIDE(x) do { TA0CTL |= TACLR; TA0CTL &= ~TAIFG; x; const uint16_t t_ = TA0R; ciclos += t_ + ((TA0CTL & TAIFG) ? 65536UL : 0); } while (0)
    msp_status st;

    /* Vectores Q15 (con una suma que satura) */
    {
        msp_add_q15_params p = {N};
        prep(20000); MIDE(st = msp_add_q15(&p, VA, VB, VC)); sale("add_q15", st, VC, 2 * N);
        msp_sub_q15_params q = {N};
        prep(20000); MIDE(st = msp_sub_q15(&q, VA, VB, VC)); sale("sub_q15", st, VC, 2 * N);
        msp_mpy_q15_params m = {N};
        prep(30000); MIDE(st = msp_mpy_q15(&m, VA, VB, VC)); sale("mpy_q15", st, VC, 2 * N);
        msp_mac_q15_params a = {N};
        prep(3000); MIDE(st = msp_mac_q15(&a, VA, VB, &VR[0])); sale("mac_q15", st, VR, 4);
        msp_max_q15_params x = {N}; uint16_t ix = 0;
        prep(30000); VA[77] = 32000; VA[200] = 32000;
        MIDE(st = msp_max_q15(&x, VA, &VC[0], &ix)); VC[1] = (int16_t)ix; sale("max_q15", st, VC, 4);
        msp_min_q15_params y = {N};
        prep(30000); MIDE(st = msp_min_q15(&y, VA, &VC[0], &ix)); VC[1] = (int16_t)ix; sale("min_q15", st, VC, 4);
        msp_max_uq15_params u = {N};
        prep(30000); MIDE(st = msp_max_uq15(&u, (uint16_t*)VA, (uint16_t*)&VC[0], &ix)); VC[1] = (int16_t)ix; sale("max_uq15", st, VC, 4);
        msp_min_uq15_params v = {N};
        prep(30000); MIDE(st = msp_min_uq15(&v, (uint16_t*)VA, (uint16_t*)&VC[0], &ix)); VC[1] = (int16_t)ix; sale("min_uq15", st, VC, 4);
    }
    /* Vectores IQ31 */
    {
        msp_add_iq31_params p = {N / 2};
        prep(1); llena32(A32, N / 2, 20000); llena32(B32, N / 2, 20000);
        MIDE(st = msp_add_iq31(&p, A32, B32, C32)); sale("add_iq31", st, VC, 2 * N);
        msp_sub_iq31_params s = {N / 2};
        MIDE(st = msp_sub_iq31(&s, A32, B32, C32)); sale("sub_iq31", st, VC, 2 * N);
        msp_mpy_iq31_params m = {N / 2};
        MIDE(st = msp_mpy_iq31(&m, A32, B32, C32)); sale("mpy_iq31", st, VC, 2 * N);
        msp_mac_iq31_params a = {N / 2};
        prep(1); llena32(A32, N / 2, 2000); llena32(B32, N / 2, 2000);
        MIDE(st = msp_mac_iq31(&a, A32, B32, &VR[0])); sale("mac_iq31", st, VR, 4);
        msp_max_iq31_params x = {N / 2}; uint16_t ix = 0;
        MIDE(st = msp_max_iq31(&x, A32, &VR[0], &ix)); VR[1] = ix; sale("max_iq31", st, VR, 8);
        msp_min_uq31_params y = {N / 2};
        MIDE(st = msp_min_uq31(&y, (uint32_t*)A32, (uint32_t*)&VR[0], &ix)); VR[1] = ix; sale("min_uq31", st, VR, 8);
    }
    /* Complejos Q15 */
    {
        msp_cmplx_mpy_q15_params m = {N / 2};
        prep(20000); MIDE(st = msp_cmplx_mpy_q15(&m, VA, VB, VC)); sale("cmplx_mpy_q15", st, VC, 2 * N);
        msp_cmplx_mac_q15_params a = {N / 2};
        prep(2000); MIDE(st = msp_cmplx_mac_q15(&a, VA, VB, &VR[0])); sale("cmplx_mac_q15", st, VR, 8);
    }
    /* Matrices: 8x12 por 12x10 */
    {
        msp_matrix_mpy_q15_params p = {8, 12, 12, 10};
        prep(12000); MIDE(st = msp_matrix_mpy_q15(&p, VA, VB, VC)); sale("matrix_mpy_q15", st, VC, 2 * 80);
        msp_matrix_add_q15_params q = {16, 16};
        prep(20000); MIDE(st = msp_matrix_add_q15(&q, VA, VB, VC)); sale("matrix_add_q15", st, VC, 2 * N);
    }
    /* FIR: 32 salidas, 16 tomas */
    {
        prep(12000); llena(VK, 32, 4000);
        msp_fir_q15_params p = {32, 16, VK, false};
        MIDE(st = msp_fir_q15(&p, VA, VC)); sale("fir_q15", st, VC, 2 * 32);
        msp_cmplx_fir_q15_params c = {16, 8, VK, false};
        prep(12000); MIDE(st = msp_cmplx_fir_q15(&c, VA, VC)); sale("cmplx_fir_q15", st, VC, 4 * 16);
        prep(1); llena32(A32, 64, 8000); llena32(VR, 4, 0);
        static int32_t* const k32 = (int32_t*)VK;
        llena32(k32, 16, 6000);
        msp_fir_iq31_params l = {32, 16, k32, false};
        MIDE(st = msp_fir_iq31(&l, A32, C32)); sale("fir_iq31", st, VC, 4 * 32);
        msp_cmplx_fir_iq31_params cl = {8, 8, k32, false};
        MIDE(st = msp_cmplx_fir_iq31(&cl, A32, C32)); sale("cmplx_fir_iq31", st, VC, 8 * 8);
    }
    /* IIR: los biquads */
    {
        msp_biquad_df1_q15_coeffs* k1 = (msp_biquad_df1_q15_coeffs*)VK;
        msp_biquad_df1_q15_states* s1 = (msp_biquad_df1_q15_states*)VS;
        k1->b2 = 0x0800; k1->b1By2 = 0x0800; k1->b0 = 0x0800; k1->reserved1 = 0; k1->a2 = (int16_t)0xD000; k1->a1By2 = 0x3000;
        s1->x2 = s1->x1 = s1->y2 = s1->y1 = 0;
        msp_biquad_df1_q15_params p = {64, k1, s1};
        prep(8000); MIDE(st = msp_biquad_df1_q15(&p, VA, VC)); sale("biquad_df1_q15", st, VC, 2 * 64); sale("  estados", st, VS, 8);
        msp_biquad_df2_q15_coeffs* k2 = (msp_biquad_df2_q15_coeffs*)VK;
        msp_biquad_df2_q15_states* s2 = (msp_biquad_df2_q15_states*)VS;
        k2->reserved1 = 0; k2->a2 = (int16_t)0xD000; k2->a1By2 = 0x3000; k2->a0 = 0x4000;
        k2->b0 = 0x1000; k2->b1By2 = 0x1000; k2->b2 = 0x1000; k2->reserved2 = 0;
        s2->q1 = s2->q2 = 0;
        msp_biquad_df2_q15_params q = {64, k2, s2};
        prep(8000); MIDE(st = msp_biquad_df2_q15(&q, VA, VC)); sale("biquad_df2_q15", st, VC, 2 * 64); sale("  estados", st, VS, 4);
        msp_biquad_df2_ext_q15_coeffs* k3 = (msp_biquad_df2_ext_q15_coeffs*)VK;
        msp_biquad_df2_ext_q15_states* s3 = (msp_biquad_df2_ext_q15_states*)VS;
        k3->bias = 0x00010000L; k3->reserved1 = 0; k3->a2 = (int16_t)0xD000; k3->a1By2 = 0x3000; k3->a0 = 0x4000;
        k3->b0 = 0x1000; k3->b1By2 = 0x1000; k3->b2 = 0x1000; k3->reserved2 = 0;
        for (unsigned i = 0; i < 8; ++i) VS[i] = 0;
        msp_biquad_df2_ext_q15_params e = {64, k3, s3};
        prep(8000); MIDE(st = msp_biquad_df2_ext_q15(&e, VA, VC)); sale("biquad_df2_ext_q15", st, VC, 2 * 64); sale("  estados", st, VS, 4);   /* q1 y q2: la CPU no calcula los extremos */
    }
    /* FFT */
    {
        msp_cmplx_fft_q15_params p = {64, true, msp_cmplx_twiddle_table_64_q15};
        prep(8000); MIDE(st = msp_cmplx_fft_fixed_q15(&p, VA)); sale("cmplx_fft_fixed_q15", st, VA, 4 * 64);
        uint16_t sh = 0;
        prep(2000); MIDE(st = msp_cmplx_fft_auto_q15(&p, VA, &sh)); sale("cmplx_fft_auto_q15", st, VA, 4 * 64);
        cad("  shift "); dec(sh); pon('\n');
        msp_fft_q15_params r = {128, true, msp_cmplx_twiddle_table_128_q15};
        prep(8000); MIDE(st = msp_fft_fixed_q15(&r, VA)); sale("fft_fixed_q15", st, VA, 2 * 128);
        msp_cmplx_fft_iq31_params l = {32, true, msp_cmplx_twiddle_table_32_q15};
        prep(1); llena32(A32, 64, 400); MIDE(st = msp_cmplx_fft_iq31(&l, A32)); sale("cmplx_fft_iq31", st, VA, 8 * 32);
        msp_fft_iq31_params lr = {64, true, msp_cmplx_twiddle_table_64_q15};
        prep(1); llena32(A32, 64, 400); MIDE(st = msp_fft_iq31(&lr, A32)); sale("fft_iq31", st, VA, 4 * 64);
        msp_cmplx_bitrev_q15_params b = {128};
        prep(8000); MIDE(st = msp_cmplx_bitrev_q15(&b, VA)); sale("cmplx_bitrev_q15", st, VA, 4 * 128);
    }
    /* Intercalar: 4 canales de 32 */
    {
        msp_deinterleave_q15_params d = {32, 1, 4};
        prep(30000); MIDE(st = msp_deinterleave_q15(&d, VA, VC)); sale("deinterleave_q15", st, VC, 2 * 32);
        msp_deinterleave_q15_params d2 = {32, 2, 4};
        MIDE(st = msp_deinterleave_q15(&d2, VA, VC)); sale("deinterleave_q15_2", st, VC, 2 * 32);
        msp_interleave_q15_params i = {32, 3, 4};
        prep(30000); MIDE(st = msp_interleave_q15(&i, VA, VB)); sale("interleave_q15", st, VB, 2 * 128);
        msp_interleave_q15_params i2 = {32, 0, 4};
        MIDE(st = msp_interleave_q15(&i2, VA, VB)); sale("interleave_q15_0", st, VB, 2 * 128);
    }
    cad("ciclos "); dec((long)ciclos); pon('\n');
    cad("fin lea\n");
    fin_tx();
    for (;;) ;
}
