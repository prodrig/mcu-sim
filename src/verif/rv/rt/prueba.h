/* prueba.h — Macros de las pruebas propias del núcleo RISC-V (verif/rv/propias)
 * Cada prueba es un `main` en ensamblador que devuelve 0 si todo va bien o el
 * número de la comprobación que falló. */
#define FALLA(n)          li a0, n; j termina
#define CHEQUEA(n, r, v)  li t6, v; li a0, n; bne r, t6, termina
#define PASA              li a0, 0; j termina
#define CLINT_MSIP        0x02000000
#define CLINT_MTIMECMP    0x02004000
#define CLINT_MTIME       0x0200BFF8
#define UART              0x10000000
