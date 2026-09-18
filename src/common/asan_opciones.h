// =============================================================================
// asan_opciones.h — Cómo hay que arrancar AddressSanitizer en un modelo SystemC
//
// SystemC implementa sus SC_THREAD como CORRUTINAS: cada proceso tiene su propia
// pila y el planificador salta de una a otra cambiando el puntero de pila a
// mano. AddressSanitizer, desde GCC 13, trae encendido por omisión
// `detect_stack_use_after_return`, que sustituye los marcos de pila por «marcos
// falsos» reservados en el montón para poder detectar punteros a locales que
// sobreviven al retorno.
//
// Las dos cosas juntas no funcionan. El registro de marcos falsos es POR HILO
// del sistema operativo, y todas las corrutinas de SystemC comparten uno; en
// cuanto el planificador salta de una pila a otra, un proceso ve los marcos
// falsos del anterior. El síntoma no es un aviso, es un desastre: punteros a
// interfaz corruptos y un SIGSEGV antes de la primera comprobación de la suite,
// en un sitio que no tiene nada que ver.
//
//     ERROR: AddressSanitizer: SEGV on unknown address 0x000000000020
//       in sc_core::sc_in<bool>::value_changed_event() const
//
// Perseguir eso como si fuera un fallo del modelo cuesta una tarde, así que en
// vez de dejarlo escrito en un README lo dejamos escrito en el binario: ASan
// llama a `__asan_default_options()` al arrancar, antes de main, y lo que
// devuelva vale como si estuviera en la variable de entorno ASAN_OPTIONS. Quien
// quiera cambiarlo puede seguir haciéndolo: ASAN_OPTIONS manda sobre esto.
//
// El fichero no hace nada cuando se compila sin sanitizers, que es siempre
// salvo en `make asan407`.
// =============================================================================
#ifndef STM32_COMMON_ASAN_OPCIONES_H
#define STM32_COMMON_ASAN_OPCIONES_H

#if defined(__SANITIZE_ADDRESS__)
#  define STM32_CON_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define STM32_CON_ASAN 1
#  endif
#endif

#ifdef STM32_CON_ASAN
extern "C" const char* __asan_default_options() {
    return "detect_stack_use_after_return=0";
}
#endif

#endif // STM32_COMMON_ASAN_OPCIONES_H
