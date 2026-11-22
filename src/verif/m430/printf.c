/* printf.c — el printf de newlib por la E/S del laboratorio (M1 de
 * doc/analisis_msp430.md): enteros de 16 y 32 bits, cadenas, caracteres,
 * hexadecimal, punteros (de 20 bits con -mlarge) y coma flotante. Lo que
 * imprime msp430-elf-run es lo esperado (printf_*.txt). Se enlaza con
 * -Wl,-u,_printf_float: sin él, newlib no saca la coma flotante. */
#include <stdio.h>
#include <string.h>
static const char* const dias[] = {"lunes", "martes", "miércoles"};
volatile long grande = 123456789L;
volatile double x = 3.14159;
int main(void) {
    int i;
    printf("hola, mundo\n");
    for (i = -2; i <= 2; ++i) printf("%d %u %x %o|", i, (unsigned)i, (unsigned)i, (unsigned)i & 7);
    printf("\n%ld %lx %lu\n", grande, grande, (unsigned long)grande * 3UL);
    printf("[%8s][%-8s][%c%c]\n", dias[0], dias[1], 'o', 'k');
    printf("%s tiene %u letras\n", dias[2], (unsigned)strlen(dias[2]));
    printf("%.3f %e %g\n", x, x * 1000.0, x / 7.0);
    printf("%p\n", (void*)&grande != (void*)0 ? (void*)1 : (void*)0);
    return 0;
}
