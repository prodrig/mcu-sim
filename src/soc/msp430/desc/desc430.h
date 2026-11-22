// =============================================================================
// desc430.h — Cómo es un descriptor de MSP430 (§5 de doc/analisis_msp430.md)
//
// Cada `msp430<chip>.h` de este directorio lo genera herramientas/gen_msp430.py
// cruzando cuatro fuentes de TI: devices.csv, las cabeceras de CCS y de GCC,
// el targetdb de CCS y la base de datos de la MSP Debug Stack (devicedb.h).
// Este fichero, el que define los tipos, se escribe a mano.
//
// Las regiones van con su final INCLUIDO (como en devices.csv), y una región
// que el chip no tiene vale {0, 0}.
// =============================================================================
#ifndef STM32_SOC_MSP430_DESC_DESC430_H
#define STM32_SOC_MSP430_DESC_DESC430_H

#include <cstdint>
#include <cstddef>

namespace stm32 { namespace m430 {

enum class Arq : uint8_t { CPU = 0, CPUX = 1, CPUXV2 = 2 };
// El tamaño del EEM [MSPDS EEM_defs.h; SLAU414]
enum class TamEem : uint8_t { Ninguno, XS, S, M, L };
// El multiplicador (MPY_TYPE de devices.csv)
enum class Mpy : uint8_t { Ninguno = 0, Mpy16 = 1, Mpy16Mac = 2, Mpy32 = 8 };

// Las erratas de CPU de devices.csv (CPU_Bugs)
enum : uint32_t {
    ERR_CPU4 = 1u << 0, ERR_CPU7 = 1u << 1, ERR_CPU8 = 1u << 2, ERR_CPU11 = 1u << 3,
    ERR_CPU12 = 1u << 4, ERR_CPU13 = 1u << 5, ERR_CPU18 = 1u << 6, ERR_CPU19 = 1u << 7,
    ERR_CPU20 = 1u << 8, ERR_CPU21 = 1u << 9, ERR_CPU22 = 1u << 10, ERR_CPU23 = 1u << 11,
    ERR_CPU24 = 1u << 12, ERR_CPU25 = 1u << 13, ERR_CPU26 = 1u << 14, ERR_CPU27 = 1u << 15,
    ERR_CPU28 = 1u << 16, ERR_CPU29 = 1u << 17, ERR_CPU30 = 1u << 18, ERR_CPU31 = 1u << 19,
    ERR_CPU32 = 1u << 20, ERR_CPU33 = 1u << 21, ERR_CPU34 = 1u << 22, ERR_CPU35 = 1u << 23,
    ERR_CPU39 = 1u << 24, ERR_CPU40 = 1u << 25, ERR_CPU41 = 1u << 26, ERR_CPU42 = 1u << 27,
    ERR_CPU43 = 1u << 28, ERR_CPU44 = 1u << 29, ERR_CPU45 = 1u << 30, ERR_CPU46 = 1u << 31,
};

struct Region {
    uint32_t ini = 0, fin = 0;
    constexpr bool existe() const { return fin != 0 || ini != 0; }
    constexpr uint32_t tam() const { return existe() ? fin - ini + 1 : 0; }
    constexpr bool contiene(uint32_t a) const { return existe() && a >= ini && a <= fin; }
};

// Un módulo: su nombre normalizado (TA0, EUSCI_A0, PORT1...), el de la
// cabecera de CCS (__MSP430_HAS_<cab>__) y su base
struct Modulo { const char* nombre; const char* cabecera; uint32_t base; };
// Un vector: su nombre (sin _VECTOR), su dirección y su número en CCS
struct Vector { const char* nombre; uint16_t dir; uint8_t ccs; };
// Un registro del targetdb, con su dirección absoluta y su anchura en bits
struct Registro { const char* nombre; uint32_t dir; uint8_t bits; };
// Un módulo cuyo reloj puede parar el depurador [devicedb.h: eemTimers]
struct EemTimer { uint8_t indice; const char* nombre; uint8_t valor; bool para; };
// Un reloj que puede parar el depurador [devicedb.h: eemClocks]
struct EemClock { uint8_t indice; const char* nombre; };

struct Desc430 {
    const char* nombre;            // "msp430g2553"
    Arq      arq;
    uint32_t errata;               // ERR_*
    Mpy      mpy;
    bool     sbw;
    TamEem   eem;
    const char* eem_csv;           // EMEX_LOW, EMEX_SMALL_5XX...
    uint8_t  disparadores;
    bool     reloj_extendido;      // CLOCKCONTROL: STANDARD / EXTENDED
    uint8_t  contador_ciclos;      // CYCLECOUNTER
    uint16_t pila;                 // STACKSIZE (lo que reserva el enlazador)
    uint8_t  jtag_id;              // 0x89, 0x91, 0x98, 0x99 [SLAU320]
    uint16_t id_dispositivo;       // el que lee la herramienta [devicedb.h]
    uint32_t iv_base;              // la tabla de vectores [targetdb]

    // El mapa (devices.csv, contrastado con devicedb y con la hoja de datos)
    Region ram, ram2, ram_diminuta, ram_usb, ram_lea;
    Region bsl, info, info_a, info_b, info_c, info_d;
    Region principal, principal2, firma, vectores;
    // Lo que no está en devices.csv: de la hoja de datos (§2.2)
    Region bsl2, rom;

    const Modulo*   modulos;   size_t n_modulos;
    const Vector*   vecs;      size_t n_vecs;
    const Registro* registros; size_t n_registros;
    const EemTimer* eem_timers; size_t n_eem_timers;
    const EemClock* eem_clocks; size_t n_eem_clocks;

    const Modulo* modulo(const char* n) const {
        for (size_t i = 0; i < n_modulos; ++i) {
            const char *a = modulos[i].nombre, *b = n;
            while (*a && *a == *b) { ++a; ++b; }
            if (!*a && !*b) return &modulos[i];
        }
        return nullptr;
    }
    const Vector* vector(const char* n) const {
        for (size_t i = 0; i < n_vecs; ++i) {
            const char *a = vecs[i].nombre, *b = n;
            while (*a && *a == *b) { ++a; ++b; }
            if (!*a && !*b) return &vecs[i];
        }
        return nullptr;
    }
    // El registro de esa dirección (nullptr: no hay registro)
    const Registro* registro(uint32_t dir) const {
        for (size_t i = 0; i < n_registros; ++i)
            if (dir >= registros[i].dir && dir < registros[i].dir + registros[i].bits / 8u) return &registros[i];
        return nullptr;
    }
};

}} // namespace stm32::m430
#endif // STM32_SOC_MSP430_DESC_DESC430_H
