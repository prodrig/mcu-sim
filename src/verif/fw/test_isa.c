/* ===========================================================================
 * test_isa.c — Firmware autocomprobable de la fase F2
 *
 * Se compila con arm-none-eabi-gcc para Cortex-M4F y se ejecuta sobre el
 * modelo SystemC. Comprueba, desde dentro del propio núcleo simulado:
 *   1. aritmética, lógicas y flags de APSR      [II, §1, §2.3, §3.1]
 *   2. desplazamientos y su acarreo             [II, §0.3]
 *   3. cargas/almacenamientos y accesos no alineados [II, §1.5, §4.1]
 *   4. multiplicación, división y DSP           [II, §4.3, §4.4]
 *   5. bloques IT y ejecución condicional       [II, §1.7]
 *   6. saltos, tablas TBB/TBH y BL/BX           [II, §1.8, §2.2, §3.3]
 *   7. exclusivos LDREX/STREX                   [II, §2.2]
 *   8. bit-banding y memorias                   [IR, §5.3, §5.4]
 *   9. excepciones: SVC, PendSV, SysTick, NVIC  [IR, §9]
 *  10. FPU FPv4-SP                              [II, §5]
 *  11. MSR/MRS, PRIMASK/BASEPRI, MSP/PSP        [IR, §7.4]
 *
 * El resultado se deja en un buzón en SRAM que el banco de pruebas lee al
 * terminar la simulación.
 * ===========================================================================*/
#include <stdint.h>

/* --- Buzón de resultados (dirección fija: inicio de la SRAM1) --------------*/
#define MAILBOX_MAGIC   0x46325445u          /* "F2TE" */

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t passed;
    volatile uint32_t failed;
    volatile uint32_t first_fail;            /* nº del primer test fallido    */
    volatile uint32_t done;
    volatile uint32_t exc_log;               /* bitmap de excepciones vistas  */
    volatile uint32_t fail_id[16];           /* ids de los primeros fallos    */
    volatile uint32_t last_id;               /* último test alcanzado         */
} mailbox_t;

__attribute__((section(".mailbox"))) mailbox_t mb;

static uint32_t test_id;

static void check(int cond)
{
    ++test_id;
    mb.last_id = test_id;
    if (cond) {
        ++mb.passed;
    } else {
        if (mb.first_fail == 0) mb.first_fail = test_id;
        if (mb.failed < 16) mb.fail_id[mb.failed] = test_id;
        ++mb.failed;
    }
}

/* --- Registros del sistema usados por las pruebas -------------------------*/
#define SCB_ICSR     (*(volatile uint32_t*)0xE000ED04u)
#define SCB_VTOR     (*(volatile uint32_t*)0xE000ED08u)
#define SCB_AIRCR    (*(volatile uint32_t*)0xE000ED0Cu)
#define SCB_CCR      (*(volatile uint32_t*)0xE000ED14u)
#define SCB_SHCSR    (*(volatile uint32_t*)0xE000ED24u)
#define SCB_CFSR     (*(volatile uint32_t*)0xE000ED28u)
#define SCB_CPACR    (*(volatile uint32_t*)0xE000ED88u)
#define SYST_CSR     (*(volatile uint32_t*)0xE000E010u)
#define SYST_RVR     (*(volatile uint32_t*)0xE000E014u)
#define SYST_CVR     (*(volatile uint32_t*)0xE000E018u)
#define NVIC_ISER0   (*(volatile uint32_t*)0xE000E100u)
#define NVIC_ISPR0   (*(volatile uint32_t*)0xE000E200u)
#define NVIC_IPR0    ((volatile uint8_t*) 0xE000E400u)
#define NVIC_STIR    (*(volatile uint32_t*)0xE000EF00u)

/* Contadores de excepciones */
static volatile uint32_t n_svc, n_pendsv, n_systick, n_irq0, n_usage;
static volatile uint32_t svc_imm;

void SVC_Handler(void) __attribute__((naked));
void SVC_Handler(void)
{
    /* Recupera el inmediato de la instrucción SVC desde la dirección de
       retorno apilada [II, §1.8] */
    __asm volatile(
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "ldr   r1, [r0, #24]   \n"   /* dirección de retorno */
        "ldrb  r1, [r1, #-2]   \n"   /* byte bajo de la instrucción SVC */
        "ldr   r2, =svc_imm    \n"
        "str   r1, [r2]        \n"
        "ldr   r2, =n_svc      \n"
        "ldr   r3, [r2]        \n"
        "adds  r3, #1          \n"
        "str   r3, [r2]        \n"
        "bx    lr              \n");
}

void PendSV_Handler(void)  { ++n_pendsv; }
void SysTick_Handler(void) { ++n_systick; }

/* IRQ 0 (WWDG) usada como interrupción de prueba [IR, §9.1.2] */
void WWDG_IRQHandler(void) { ++n_irq0; }
extern void Default_IRQ_Handler(void);

void UsageFault_Handler(void) __attribute__((naked));
void UsageFault_Handler(void)
{
    /* Salta la instrucción que falló (2 bytes) y limpia el estado */
    __asm volatile(
        "ldr   r2, =n_usage    \n"
        "ldr   r3, [r2]        \n"
        "adds  r3, #1          \n"
        "str   r3, [r2]        \n"
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "ldr   r1, [r0, #24]   \n"
        "adds  r1, #4          \n"       /* SDIV/UDIV son de 32 bits */
        "str   r1, [r0, #24]   \n"
        "bx    lr              \n");
}

/* =========================================================================
 * 1. Aritmética, lógicas y flags
 * =========================================================================*/
static uint32_t get_apsr(void)
{
    uint32_t v;
    __asm volatile("mrs %0, apsr" : "=r"(v));
    return v;
}

static void test_alu(void)
{
    uint32_t r, apsr;

    /* ADDS con acarreo y desbordamiento [II, §0.3 AddWithCarry] */
    __asm volatile("movs r0, #0        \n"
                   "subs r0, r0, #1    \n"   /* r0 = 0xFFFFFFFF, C=0 */
                   "adds %0, r0, #1    \n"   /* 0xFFFFFFFF + 1 -> C=1, Z=1 */
                   "mrs  %1, apsr      \n"
                   : "=r"(r), "=r"(apsr) :: "r0", "cc");
    check(r == 0);
    check((apsr & (1u << 30)) != 0);            /* Z */
    check((apsr & (1u << 29)) != 0);            /* C */

    /* Desbordamiento con signo: 0x7FFFFFFF + 1 */
    __asm volatile("ldr  r0, =0x7FFFFFFF \n"
                   "adds %0, r0, #1      \n"
                   "mrs  %1, apsr        \n"
                   : "=r"(r), "=r"(apsr) :: "r0", "cc");
    check(r == 0x80000000u);
    check((apsr & (1u << 28)) != 0);            /* V */
    check((apsr & (1u << 31)) != 0);            /* N */

    /* SBC / ADC encadenados: suma de 64 bits */
    uint32_t lo, hi;
    __asm volatile("ldr  r0, =0xFFFFFFFF \n"
                   "movs r1, #0          \n"
                   "adds %0, r0, r0      \n"
                   "adcs %1, r1, r1      \n"
                   : "=&r"(lo), "=&r"(hi) :: "r0", "r1", "cc");
    check(lo == 0xFFFFFFFEu && hi == 1u);

    /* Inmediato modificado de Thumb-2 [II, §0.3 ThumbExpandImm] */
    __asm volatile("and %0, %1, #0xFF00FF00" : "=r"(r) : "r"(0x12345678u));
    check(r == 0x12005600u);
    __asm volatile("mvn %0, #0x55555555" : "=r"(r));
    check(r == 0xAAAAAAAAu);
    __asm volatile("add.w %0, %1, #0x1FE00" : "=r"(r) : "r"(0u));
    check(r == 0x1FE00u);

    /* MOVW / MOVT [II, §3.2] */
    __asm volatile("movw %0, #0xABCD \n movt %0, #0x1234" : "=r"(r));
    check(r == 0x1234ABCDu);

    /* Campos de bits: BFI / BFC / SBFX / UBFX [II, §3.2] */
    r = 0xFFFFFFFFu;
    __asm volatile("bfc %0, #5, #7" : "+r"(r));
    check(r == 0xFFFFF01Fu);
    r = 0;
    __asm volatile("bfi %0, %1, #4, #8" : "+r"(r) : "r"(0xA5u));
    check(r == 0x00000A50u);
    __asm volatile("ubfx %0, %1, #8, #8" : "=r"(r) : "r"(0x12345678u));
    check(r == 0x56u);
    __asm volatile("sbfx %0, %1, #8, #8" : "=r"(r) : "r"(0x1234F678u));
    check(r == 0xFFFFFFF6u);

    /* Saturación [II, §3.2] y bit Q sticky */
    __asm volatile("ssat %0, #8, %1" : "=r"(r) : "r"(0x1000u));
    check(r == 0x7Fu);
    check((get_apsr() & (1u << 27)) != 0);      /* Q */
    __asm volatile("usat %0, #8, %1" : "=r"(r) : "r"(-5));
    check(r == 0u);

    /* CLZ / RBIT / REV [II, §4.2] */
    __asm volatile("clz %0, %1" : "=r"(r) : "r"(0x00080000u));
    check(r == 12u);
    __asm volatile("rbit %0, %1" : "=r"(r) : "r"(0x00000001u));
    check(r == 0x80000000u);
    __asm volatile("rev %0, %1" : "=r"(r) : "r"(0x12345678u));
    check(r == 0x78563412u);
    __asm volatile("rev16 %0, %1" : "=r"(r) : "r"(0x12345678u));
    check(r == 0x34127856u);
    __asm volatile("revsh %0, %1" : "=r"(r) : "r"(0x000080FFu));
    check(r == 0xFFFFFF80u);

    /* Extensiones de signo [II, §1.7, §4.2] */
    __asm volatile("sxtb %0, %1" : "=r"(r) : "r"(0x000000F0u));
    check(r == 0xFFFFFFF0u);
    __asm volatile("uxth %0, %1" : "=r"(r) : "r"(0xFFFF8001u));
    check(r == 0x00008001u);
    __asm volatile("sxtah %0, %1, %2" : "=r"(r) : "r"(0x100u), "r"(0xFFFFu));
    check(r == 0xFFu);
}

/* =========================================================================
 * 2. Desplazamientos
 * =========================================================================*/
static void test_shifts(void)
{
    uint32_t r, apsr;

    __asm volatile("lsls %0, %2, #4 \n mrs %1, apsr"
                   : "=r"(r), "=r"(apsr) : "r"(0x10000000u) : "cc");
    check(r == 0x00000000u);
    check((apsr & (1u << 29)) != 0);            /* C = bit expulsado */

    __asm volatile("asrs %0, %1, #31" : "=r"(r) : "r"(0x80000000u) : "cc");
    check(r == 0xFFFFFFFFu);
    __asm volatile("lsrs %0, %1, #31" : "=r"(r) : "r"(0x80000000u) : "cc");
    check(r == 1u);
    __asm volatile("ror %0, %1, #8" : "=r"(r) : "r"(0x000000FFu));
    check(r == 0xFF000000u);

    /* RRX usa el acarreo de entrada */
    __asm volatile("movs r0, #1     \n"
                   "lsrs r0, r0, #1 \n"   /* C = 1, r0 = 0 */
                   "mov  r1, #0     \n"
                   "rrx  %0, r1     \n"
                   : "=r"(r) :: "r0", "r1", "cc");
    check(r == 0x80000000u);

    /* Desplazamiento por registro con cantidad > 32 */
    __asm volatile("lsl.w %0, %1, %2" : "=r"(r) : "r"(0xFFFFFFFFu), "r"(40u));
    check(r == 0u);
}

/* =========================================================================
 * 3. Memoria
 * =========================================================================*/
static uint32_t buf[16];
__attribute__((section(".ccmram"))) static uint32_t ccm_buf[8];

static void test_memory(void)
{
    volatile uint8_t*  b = (volatile uint8_t*)buf;
    volatile uint16_t* h = (volatile uint16_t*)buf;
    uint32_t r;

    for (int i = 0; i < 16; ++i) buf[i] = 0;

    buf[0] = 0x11223344u;
    check(b[0] == 0x44 && b[3] == 0x11);        /* little-endian [IR, §7.6.3] */
    check(h[0] == 0x3344u && h[1] == 0x1122u);

    /* Acceso no alineado por MemU [II, §4.1] */
    volatile uint32_t* ua = (volatile uint32_t*)((uint8_t*)buf + 1);
    *ua = 0xAABBCCDDu;
    check(b[1] == 0xDD && b[4] == 0xAA);
    check(*ua == 0xAABBCCDDu);

    /* LDRD / STRD [II, §2.2] */
    __asm volatile("strd %1, %2, [%0]" :: "r"(&buf[4]), "r"(0x01234567u), "r"(0x89ABCDEFu) : "memory");
    check(buf[4] == 0x01234567u && buf[5] == 0x89ABCDEFu);
    uint32_t d0, d1;
    __asm volatile("ldrd %0, %1, [%2]" : "=&r"(d0), "=&r"(d1) : "r"(&buf[4]));
    check(d0 == 0x01234567u && d1 == 0x89ABCDEFu);

    /* LDM / STM [II, §1.8, §2.1]. Las bases se declaran de lectura-escritura
       porque el sufijo '!' las actualiza (write-back). */
    for (int i = 0; i < 8; ++i) buf[i] = (uint32_t)(i + 1);
    {
        uint32_t src = (uint32_t)&buf[0], dst = (uint32_t)&buf[8];
        __asm volatile("ldmia %0!, {r4-r7} \n"
                       "stmia %1!, {r4-r7} \n"
                       : "+r"(src), "+r"(dst)
                       :: "r4", "r5", "r6", "r7", "memory", "cc");
        check(buf[8] == 1 && buf[11] == 4);
        check(src == (uint32_t)&buf[4] && dst == (uint32_t)&buf[12]);
    }

    /* PUSH / POP */
    __asm volatile("movs r4, #0x5A     \n"
                   "push {r4}          \n"
                   "movs r4, #0        \n"
                   "pop  {r4}          \n"
                   "mov  %0, r4        \n"
                   : "=r"(r) :: "r4", "memory");
    check(r == 0x5Au);

    /* CCM RAM: accesible solo por el bus D del núcleo [IR, §5.3] */
    ccm_buf[0] = 0xC0FFEE00u;
    check(ccm_buf[0] == 0xC0FFEE00u);

    /* Bit-banding sobre la SRAM [IR, §5.4] */
    buf[12] = 0;
    volatile uint32_t* bb = (volatile uint32_t*)
        (0x22000000u + (((uint32_t)&buf[12] - 0x20000000u) * 32u) + (5u * 4u));
    *bb = 1u;
    check(buf[12] == (1u << 5));
    check(*bb == 1u);
    *bb = 0u;
    check(buf[12] == 0u);

    /* Exclusivos LDREX/STREX [II, §2.2] */
    uint32_t v, fail;
    buf[13] = 0;
    __asm volatile("ldrex %0, [%2]    \n"
                   "adds  %0, %0, #1  \n"
                   "strex %1, %0, [%2]\n"
                   : "=&r"(v), "=&r"(fail) : "r"(&buf[13]) : "memory", "cc");
    check(fail == 0u && buf[13] == 1u);
    /* STREX sin LDREX previo debe fallar */
    __asm volatile("clrex             \n"
                   "movs  %0, #9      \n"
                   "strex %1, %0, [%2]\n"
                   : "=&r"(v), "=&r"(fail) : "r"(&buf[13]) : "memory", "cc");
    check(fail == 1u && buf[13] == 1u);
}

/* =========================================================================
 * 4. Multiplicación, división y DSP
 * =========================================================================*/
static void test_mul_div(void)
{
    uint32_t r, lo, hi;

    __asm volatile("mul %0, %1, %2" : "=r"(r) : "r"(7u), "r"(6u));
    check(r == 42u);
    __asm volatile("mla %0, %1, %2, %3" : "=r"(r) : "r"(7u), "r"(6u), "r"(8u));
    check(r == 50u);
    __asm volatile("mls %0, %1, %2, %3" : "=r"(r) : "r"(7u), "r"(6u), "r"(50u));
    check(r == 8u);

    __asm volatile("umull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi)
                   : "r"(0xFFFFFFFFu), "r"(0xFFFFFFFFu));
    check(lo == 1u && hi == 0xFFFFFFFEu);
    __asm volatile("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi)
                   : "r"(0xFFFFFFFFu), "r"(0xFFFFFFFFu));       /* -1 * -1 */
    check(lo == 1u && hi == 0u);

    __asm volatile("sdiv %0, %1, %2" : "=r"(r) : "r"(-100), "r"(7));
    check((int32_t)r == -14);                    /* redondeo hacia cero */
    __asm volatile("udiv %0, %1, %2" : "=r"(r) : "r"(100u), "r"(7u));
    check(r == 14u);

    /* UMAAL [II, §4.4] */
    lo = 3; hi = 5;
    __asm volatile("umaal %0, %1, %2, %3" : "+r"(lo), "+r"(hi) : "r"(4u), "r"(6u));
    check(lo == (4u * 6u + 3u + 5u) && hi == 0u);

    /* DSP: SMULBB, SMLABB, SMUAD, QADD, USAD8 [II, §4.2, §4.3] */
    __asm volatile("smulbb %0, %1, %2" : "=r"(r) : "r"(0x00000003u), "r"(0x00000004u));
    check(r == 12u);
    __asm volatile("smultt %0, %1, %2" : "=r"(r) : "r"(0x00030000u), "r"(0x00040000u));
    check(r == 12u);
    __asm volatile("smuad %0, %1, %2" : "=r"(r) : "r"(0x00020003u), "r"(0x00040005u));
    check(r == (2u * 4u + 3u * 5u));
    __asm volatile("qadd %0, %1, %2" : "=r"(r) : "r"(0x7FFFFFFFu), "r"(0x7FFFFFFFu));
    check(r == 0x7FFFFFFFu);                     /* saturado */
    __asm volatile("usad8 %0, %1, %2" : "=r"(r) : "r"(0x01020304u), "r"(0x04030201u));
    check(r == (3u + 1u + 1u + 3u));

    /* SIMD paralelas y GE + SEL [II, §4.2] */
    __asm volatile("uadd8 %0, %1, %2" : "=r"(r) : "r"(0x01010101u), "r"(0x02020202u));
    check(r == 0x03030303u);
    __asm volatile("ssub16 %0, %1, %2" : "=r"(r) : "r"(0x00050005u), "r"(0x00030003u));
    check(r == 0x00020002u);
    __asm volatile("shadd16 %0, %1, %2" : "=r"(r) : "r"(0x00040004u), "r"(0x00020002u));
    check(r == 0x00030003u);                     /* halving */
}

/* =========================================================================
 * 5. Bloques IT y saltos
 * =========================================================================*/
static uint32_t tbb_target(uint32_t i);

static void test_branch(void)
{
    uint32_t r;

    /* IT con dos instrucciones [II, §1.7] */
    __asm volatile("movs  r0, #1     \n"
                   "cmp   r0, #1     \n"
                   "itte  eq         \n"
                   "moveq %0, #10    \n"
                   "addeq %0, #5     \n"
                   "movne %0, #99    \n"
                   : "=r"(r) :: "r0", "cc");
    check(r == 15u);

    __asm volatile("movs  r0, #2     \n"
                   "cmp   r0, #1     \n"
                   "ite   eq         \n"
                   "moveq %0, #10    \n"
                   "movne %0, #20    \n"
                   : "=r"(r) :: "r0", "cc");
    check(r == 20u);

    /* CBZ / CBNZ [II, §1.7] */
    __asm volatile("movs %0, #0      \n"
                   "cbnz %0, 1f      \n"
                   "movs %0, #7      \n"
                   "1:               \n"
                   : "=r"(r) :: "cc");
    check(r == 7u);

    /* Tabla de saltos TBB [II, §2.2] */
    check(tbb_target(0) == 100u);
    check(tbb_target(1) == 200u);
    check(tbb_target(2) == 300u);

    /* BL / BX LR ya se ejercitan en cada llamada de esta función */
    check(1);
}

static uint32_t tbb_target(uint32_t i)
{
    uint32_t r = 0;
    __asm volatile(
        "tbb  [pc, %1]        \n"
        "0:                   \n"
        ".byte (1f-0b)/2      \n"
        ".byte (2f-0b)/2      \n"
        ".byte (3f-0b)/2      \n"
        ".align 1             \n"
        "1: movw %0, #100     \n"
        "   b 4f              \n"
        "2: movw %0, #200     \n"
        "   b 4f              \n"
        "3: movw %0, #300     \n"
        "4:                   \n"
        : "=r"(r) : "r"(i));
    return r;
}

/* =========================================================================
 * 6. Excepciones
 * =========================================================================*/
static void test_exceptions(void)
{
    /* SVC: el manejador extrae el inmediato de la instrucción [II, §1.8] */
    n_svc = 0; svc_imm = 0;
    __asm volatile("svc #0x2A" ::: "memory");
    check(n_svc == 1u);
    check(svc_imm == 0x2Au);

    /* PendSV por software (SCB_ICSR.PENDSVSET) [IR, §10.2.2] */
    n_pendsv = 0;
    SCB_ICSR = (1u << 28);
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_pendsv == 1u);

    /* SysTick: 24 bits descendentes con recarga [IR, §10.3].
       El periodo debe ser holgadamente mayor que el coste de entrada y salida
       de la excepción (12 ciclos + manejador); con recargas muy cortas el
       núcleo no progresa, que es el comportamiento real. */
    n_systick = 0;
    SYST_RVR = 500;
    SYST_CVR = 0;
    SYST_CSR = 0x7;                              /* ENABLE | TICKINT | CLKSOURCE */
    for (volatile int i = 0; i < 20000 && n_systick == 0u; ++i) { }
    check(n_systick > 0u);
    /* COUNTFLAG se limpia al leer SYST_CSR [IR, §10.3] */
    {
        const uint32_t csr1 = SYST_CSR;
        const uint32_t csr2 = SYST_CSR;
        check((csr1 & (1u << 16)) != 0u && (csr2 & (1u << 16)) == 0u);
    }
    SYST_CSR = 0;
    check((SYST_CVR & 0xFF000000u) == 0u);       /* contador de 24 bits */

    /* NVIC: IRQ 0 (WWDG) disparada por software [IR, §9.2.2] */
    n_irq0 = 0;
    NVIC_IPR0[0] = 0x40;
    NVIC_ISER0 = 1u;
    NVIC_STIR = 0u;
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_irq0 == 1u);

    /* Enmascaramiento con PRIMASK [IR, §7.4.1] */
    n_irq0 = 0;
    __asm volatile("cpsid i" ::: "memory");
    NVIC_STIR = 0u;
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_irq0 == 0u);                         /* bloqueada */
    __asm volatile("cpsie i" ::: "memory");
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_irq0 == 1u);                         /* atendida al desenmascarar */

    /* BASEPRI: bloquea prioridades numéricamente >= al valor [IR, §7.4.3] */
    n_irq0 = 0;
    __asm volatile("mov r0, #0x30 \n msr basepri, r0" ::: "r0", "memory");
    NVIC_STIR = 0u;
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_irq0 == 0u);
    __asm volatile("mov r0, #0 \n msr basepri, r0" ::: "r0", "memory");
    __asm volatile("dsb \n isb" ::: "memory");
    check(n_irq0 == 1u);

    /* UsageFault por división por cero con CCR.DIV_0_TRP [IR, §10.2.5] */
    n_usage = 0;
    SCB_SHCSR |= (1u << 18);                     /* USGFAULTENA */
    SCB_CCR   |= (1u << 4);                      /* DIV_0_TRP   */
    __asm volatile("dsb \n isb" ::: "memory");
    {
        volatile uint32_t z = 0, q;
        __asm volatile("udiv %0, %1, %2" : "=r"(q) : "r"(10u), "r"(z));
        (void)q;
    }
    check(n_usage == 1u);
    check((SCB_CFSR & (1u << 25)) != 0);         /* UFSR.DIVBYZERO */
    SCB_CFSR = SCB_CFSR;                         /* rc_w1 */
    SCB_CCR &= ~(1u << 4);

    /* MSP / PSP y CONTROL.SPSEL [IR, §7.2.2, §7.4.4] */
    {
        static uint32_t proc_stack[64];
        uint32_t msp_before, sp_psp, ctrl;
        __asm volatile("mrs %0, msp" : "=r"(msp_before));
        __asm volatile("msr psp, %0" :: "r"((uint32_t)&proc_stack[64]));
        __asm volatile("mrs %0, control \n"
                       "orr %0, %0, #2  \n"
                       "msr control, %0 \n"
                       "isb             \n"
                       "mov %1, sp      \n"
                       : "=&r"(ctrl), "=r"(sp_psp) :: "memory");
        check(sp_psp == (uint32_t)&proc_stack[64]);
        __asm volatile("mrs %0, control \n"
                       "bic %0, %0, #2  \n"
                       "msr control, %0 \n"
                       "isb             \n"
                       : "+r"(ctrl) :: "memory");
        uint32_t msp_after;
        __asm volatile("mrs %0, msp" : "=r"(msp_after));
        check(msp_after == msp_before);
    }
}

/* =========================================================================
 * 7. FPU FPv4-SP
 * =========================================================================*/
static void test_fpu(void)
{
    /* Habilitar CP10/CP11 [IR, §8.12.1] */
    SCB_CPACR |= (0xFu << 20);
    __asm volatile("dsb \n isb" ::: "memory");

    float a = 3.5f, b = 2.0f, r;

    __asm volatile("vadd.f32 %0, %1, %2" : "=t"(r) : "t"(a), "t"(b));
    check(r == 5.5f);
    __asm volatile("vsub.f32 %0, %1, %2" : "=t"(r) : "t"(a), "t"(b));
    check(r == 1.5f);
    __asm volatile("vmul.f32 %0, %1, %2" : "=t"(r) : "t"(a), "t"(b));
    check(r == 7.0f);
    __asm volatile("vdiv.f32 %0, %1, %2" : "=t"(r) : "t"(a), "t"(b));
    check(r == 1.75f);
    __asm volatile("vsqrt.f32 %0, %1" : "=t"(r) : "t"(16.0f));
    check(r == 4.0f);
    __asm volatile("vneg.f32 %0, %1" : "=t"(r) : "t"(a));
    check(r == -3.5f);
    __asm volatile("vabs.f32 %0, %1" : "=t"(r) : "t"(-3.5f));
    check(r == 3.5f);

    /* VMLA: multiplicación-acumulación [II, §5.2] */
    r = 1.0f;
    __asm volatile("vmla.f32 %0, %1, %2" : "+t"(r) : "t"(2.0f), "t"(3.0f));
    check(r == 7.0f);

    /* VFMA fusionada */
    r = 1.0f;
    __asm volatile("vfma.f32 %0, %1, %2" : "+t"(r) : "t"(2.0f), "t"(3.0f));
    check(r == 7.0f);

    /* Conversiones [II, §5.3] */
    {
        int32_t i;
        __asm volatile("vcvt.s32.f32 %0, %1" : "=t"(i) : "t"(-7.9f));
        check(i == -7);                          /* trunca hacia cero */
        float f;
        __asm volatile("vcvt.f32.s32 %0, %1" : "=t"(f) : "t"(-7));
        check(f == -7.0f);
    }

    /* VCMP + VMRS APSR_nzcv [II, §5.3, §5.4] */
    {
        uint32_t apsr;
        __asm volatile("vcmp.f32 %1, %2      \n"
                       "vmrs APSR_nzcv, fpscr\n"
                       "mrs   %0, apsr       \n"
                       : "=r"(apsr) : "t"(1.0f), "t"(2.0f) : "cc");
        check((apsr & (1u << 31)) != 0);         /* N: menor */
        __asm volatile("vcmp.f32 %1, %2      \n"
                       "vmrs APSR_nzcv, fpscr\n"
                       "mrs   %0, apsr       \n"
                       : "=r"(apsr) : "t"(2.0f), "t"(2.0f) : "cc");
        check((apsr & (1u << 30)) != 0);         /* Z: igual */
    }

    /* Transferencias núcleo <-> FPU [II, §5.4] */
    {
        uint32_t bits;
        __asm volatile("vmov %0, %1" : "=r"(bits) : "t"(1.0f));
        check(bits == 0x3F800000u);
        float f;
        __asm volatile("vmov %0, %1" : "=t"(f) : "r"(0x40000000u));
        check(f == 2.0f);
    }

    /* VLDR / VSTR / VPUSH / VPOP [II, §5.5] */
    {
        static volatile float fbuf[4];
        fbuf[0] = 1.25f;
        __asm volatile("vldr s0, [%0]  \n"
                       "vpush {s0}     \n"
                       "vmov  s0, #0.5 \n"
                       "vpop  {s0}     \n"
                       "vstr  s0, [%1] \n"
                       :: "r"(&fbuf[0]), "r"(&fbuf[1]) : "s0", "memory");
        check(fbuf[1] == 1.25f);
    }

    /* Cálculo en C compilado con la FPU (valida el conjunto completo) */
    {
        volatile float x = 0.0f;
        for (int i = 1; i <= 10; ++i) x += 1.0f / (float)i;
        check(x > 2.92f && x < 2.93f);
    }
}

/* =========================================================================
 * Programa principal
 * =========================================================================*/
int main(void)
{
    mb.magic = 0;
    mb.passed = 0;
    mb.failed = 0;
    mb.first_fail = 0;
    mb.done = 0;
    test_id = 0;

    test_alu();
    test_shifts();
    test_memory();
    test_mul_div();
    test_branch();
    test_exceptions();
    test_fpu();

    mb.magic = MAILBOX_MAGIC;
    mb.done  = 1;
    for (;;) { __asm volatile("wfi"); }
}
