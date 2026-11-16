/* dbg.c — el programa de las pruebas de depuración del núcleo MSP430 (M2):
 * un bucle que llama a objetivo() y escribe vigilada. */
volatile unsigned contador, vigilada;
void objetivo(void) __attribute__((noinline));
void objetivo(void) { contador++; }
int main(void) {
    for (;;) {
        objetivo();
        vigilada = contador;
    }
}
