// =============================================================================
// rv_caps.h — Los rasgos de un núcleo RISC-V: lo que el integrador elige
//
// E1 de doc/analisis_esp32_riscv.md, §3.2. Misma receta que `CoreCaps`,
// `UsartCaps` o `TimCaps`: un struct `constexpr` por núcleo, y una clase que
// lo toma POR VALOR. Aquí se dice TODO lo que diferencia a un hart RISC-V de
// otro y no está en la especificación como obligatorio: qué extensiones,
// qué modos, si hay `mie`/`mip` o el enmascarado es cosa del controlador (el
// ESP32-C3), cuántas regiones de PMP, qué hace con un acceso desalineado,
// cuántos *triggers* tiene su Debug Module y cuánto tarda cada instrucción.
//
// NADA DE ESTE DIRECTORIO SABE DE ESPRESSIF. Las tres instancias del plan
// (C3, C6 HP y C6 LP) viven con su chip; aquí solo están las de laboratorio,
// con las que se pasan las pruebas de conformidad tal como vienen.
//
// Fuentes: RISC-V Unprivileged ISA (20191213) y Privileged Architecture
// (20211203); RISC-V External Debug Support 0.13.2.
// =============================================================================
#ifndef STM32_RV_HART_RV_CAPS_H
#define STM32_RV_HART_RV_CAPS_H

#include <cstdint>

namespace stm32 {

// --- Las extensiones de una letra, con el bit que tienen en `misa` -----------
constexpr uint32_t rv_letra(char c) { return 1u << unsigned(c - 'A'); }
inline constexpr uint32_t RV_EXT_A = rv_letra('A');
inline constexpr uint32_t RV_EXT_C = rv_letra('C');
inline constexpr uint32_t RV_EXT_E = rv_letra('E');
inline constexpr uint32_t RV_EXT_F = rv_letra('F');
inline constexpr uint32_t RV_EXT_I = rv_letra('I');
inline constexpr uint32_t RV_EXT_M = rv_letra('M');
inline constexpr uint32_t RV_EXT_N = rv_letra('N');   // interrupciones de usuario
inline constexpr uint32_t RV_EXT_S = rv_letra('S');
inline constexpr uint32_t RV_EXT_U = rv_letra('U');
inline constexpr uint32_t RV_EXT_X = rv_letra('X');   // hay extensiones propias

// --- Las de nombre largo, que no salen en `misa` -----------------------------
enum : uint32_t {
    RV_Z_ICSR     = 1u << 0,
    RV_Z_IFENCEI  = 1u << 1,
    RV_Z_ICNTR    = 1u << 2,   // cycle/time/instret de usuario
    RV_Z_IHPM     = 1u << 3,   // mhpmcounter3..31 (a cero si no hay sucesos)
    RV_Z_MSTATUSH = 1u << 4,   // Privileged 1.12: mstatush, mconfigptr y, con U, menvcfg(h)
};

// Los modos de privilegio, con su codificación de MPP
enum class RvModo : uint8_t { U = 0, S = 1, M = 3 };

// Qué hace el núcleo con un acceso desalineado a datos
enum class RvDesalineado : uint8_t {
    Trap,              // excepción 4 o 6, como Spike por omisión
    Hardware,          // lo parte en dos transacciones y no se entera nadie
    HardwareCpu863     // como Hardware, con la errata CPU-863 de Espressif:
                       // una carga desalineada SEGUIDA de un almacenamiento
                       // comprueba la segunda mitad de la carga con los
                       // permisos PMP/PMA del almacenamiento [ER-C3 §3.1]
};

// Modos de `mtvec` que admite el núcleo (bit a bit)
enum : uint8_t { RV_MTVEC_DIRECTO = 1, RV_MTVEC_VECTORIZADO = 2 };

// -----------------------------------------------------------------------------
// Los rasgos del Debug Module y de los *triggers* [Debug Spec 0.13.2]
// -----------------------------------------------------------------------------
struct RvDebugCaps {
    unsigned triggers;       // mcontrol (tipo 2); 0 = no hay Trigger Module
    bool     tcontrol;       // ¿existe tcontrol (0x7A5)? (C3 y C6 sí)
    unsigned progbuf;        // palabras del program buffer (0..16)
    unsigned datacount;      // data0..dataN (1..12)
    bool     impebreak;      // ebreak implícito tras el program buffer
    bool     sba;            // acceso al bus de sistema
    uint8_t  sba_anchos;     // bit 0: 8, 1: 16, 2: 32 bits
    // La región del DM en el mapa de memoria del hart (0 = no hay: el program
    // buffer corre en una dirección ficticia, como en los núcleos de
    // laboratorio). Dentro, con la disposición del riscv-dbg de PULP: los
    // datos en base + 0x380 y el program buffer justo debajo.
    uint32_t dm_base = 0, dm_tam = 0;
};

// -----------------------------------------------------------------------------
// Los tiempos: ciclos por clase de instrucción (LT con quantum, como el Cortex)
//
// Ninguno de los TRM de Espressif da estas cifras; las de un núcleo concreto
// van como parámetros sin fuente (sección D de doc/todo.md) y se calibran
// contra el CoreMark/MHz que publica el fabricante. A los ciclos de la tabla
// se suma la latencia que anote el bus en cada transacción.
// -----------------------------------------------------------------------------
struct RvTiempos {
    unsigned alu;            // aritmética, lógica, desplazamientos, lui, auipc
    unsigned carga;          // load (sin la latencia del bus)
    unsigned almacen;        // store
    unsigned salto_no;       // rama no tomada
    unsigned salto_si;       // rama tomada (vaciado del cauce)
    unsigned jal;
    unsigned jalr;
    unsigned mul;            // mul, mulh*
    unsigned div;            // div*, rem*
    unsigned csr;            // csrr*
    unsigned amo;            // lr, sc, amo* (sin el bus)
    unsigned trap;           // entrada a un trap (excepción o interrupción)
    unsigned retorno;        // mret, uret, dret
    unsigned lat_irq;        // ciclos desde la petición hasta que entra
};

// La de laboratorio: todo a un ciclo, como Spike. Sirve para comparar trazas.
inline constexpr RvTiempos RV_TIEMPOS_UNO{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};

// -----------------------------------------------------------------------------
// RvCaps: el núcleo entero
// -----------------------------------------------------------------------------
struct RvCaps {
    const char* nombre;            // "ESP32-C3", "laboratorio RV32IMAC"...
    unsigned    xlen;              // 32 (64 queda previsto, no hecho)
    uint32_t    ext;               // letras de misa
    uint32_t    zext;              // RV_Z_*
    bool        mie_mip;           // false: no hay mie/mip (ESP32-C3)
    uint32_t    mie_bits;          // bits de mie que existen (y de mip que se ven)
    bool        deleg_n;           // delegación a U con la extensión N (C6 HP)
    uint32_t    mideleg_reset;     // mideleg tras el reset (C6 HP: 0 y 4)
    uint8_t     mtvec_modos;       // RV_MTVEC_*
    uint32_t    mtvec_alin;        // alineación de la base de mtvec (4, 64, 256...)
    uint32_t    pc_reset;
    uint32_t    mtvec_reset;
    unsigned    pmp_regiones;      // 0, 4, 8 o 16
    bool        contadores_std;    // mcycle/minstret/mhpm*/mcountinhibit
    RvDesalineado desalineado;
    uint32_t    mvendorid, marchid, mimpid;
    bool        misa_cero;         // misa lee 0 (está permitido) en vez de las letras
    RvDebugCaps dbg;
    const RvTiempos* tiempos;

    bool tiene(uint32_t letra) const { return (ext & letra) != 0; }
    bool tiene_z(uint32_t z) const   { return (zext & z) != 0; }
    bool modo_u() const              { return tiene(RV_EXT_U); }
    uint32_t misa() const {
        if (misa_cero) return 0;
        return (xlen == 64 ? (2u << 30) : (1u << 30)) | ext;
    }
};

// -----------------------------------------------------------------------------
// Los núcleos de LABORATORIO (src/rv/lab): los de las pruebas de conformidad
// -----------------------------------------------------------------------------
// Sin tcontrol: con él, un trigger de acción 0 no salta en modo M hasta que
// se pone tcontrol.mte, y riscv-tests (rv32mi-p-breakpoint) y Spike dan por
// hecho que sí salta. Los núcleos de Espressif sí lo tienen.
inline constexpr RvDebugCaps RV_DBG_LAB{4, false, 8, 2, false, true, 0x7};

// RV32IMAC con M y U, mie/mip, mtvec directo y vectorizado, 16 PMP y
// contadores estándar: lo que dan por hecho riscv-tests y Spike.
inline constexpr RvCaps RV_LAB_IMAC{
    "laboratorio RV32IMAC", 32,
    RV_EXT_I | RV_EXT_M | RV_EXT_A | RV_EXT_C | RV_EXT_U,
    RV_Z_ICSR | RV_Z_IFENCEI | RV_Z_ICNTR | RV_Z_IHPM | RV_Z_MSTATUSH,
    true, 0x888u, false, 0, RV_MTVEC_DIRECTO | RV_MTVEC_VECTORIZADO, 4,
    0x80000000u, 0x00000000u, 16, true, RvDesalineado::Trap,
    0, 0, 0, false, RV_DBG_LAB, &RV_TIEMPOS_UNO
};
// El mismo sin A: el del ESP32-C3
inline constexpr RvCaps RV_LAB_IMC{
    "laboratorio RV32IMC", 32,
    RV_EXT_I | RV_EXT_M | RV_EXT_C | RV_EXT_U,
    RV_Z_ICSR | RV_Z_IFENCEI | RV_Z_ICNTR | RV_Z_IHPM | RV_Z_MSTATUSH,
    true, 0x888u, false, 0, RV_MTVEC_DIRECTO | RV_MTVEC_VECTORIZADO, 4,
    0x80000000u, 0x00000000u, 16, true, RvDesalineado::Trap,
    0, 0, 0, false, RV_DBG_LAB, &RV_TIEMPOS_UNO
};

} // namespace stm32
#endif // STM32_RV_HART_RV_CAPS_H
