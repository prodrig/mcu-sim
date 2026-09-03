// =============================================================================
// fsmc.h — Flexible Static Memory Controller (AHB3) [IR, §12.18]
//
// El FSMC es el periférico que convierte una lectura del bus AHB en un CICLO DE
// BUS EXTERNO: baja un chip select, pone una dirección, espera los ciclos que
// le hayan programado, muestrea dieciséis hilos y sube todo otra vez. No hay
// protocolo que negociar ni tramas que componer; hay TIEMPOS. Y de los tiempos
// sale todo lo que importa:
//
//     ADDSET        DATAST                 BUSTURN
//   ├────────┤├──────────────────┤├──────────────┤
//   NEx  ‾‾\____________________________/‾‾‾‾‾‾‾‾‾‾
//   A     ──<  direccion valida        >───────────
//   NOE  ‾‾‾‾‾‾‾‾‾\______________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
//   D    ─────────────────< dato >─────────────────   (lectura)
//
// -----------------------------------------------------------------------------
// LOS CUATRO BANCOS NO SON CUATRO COPIAS
//
// Aquí sí hay varios "canales", y son distintos de verdad -no como en el DCMI,
// donde había uno solo-. Los cuatro bancos del FSMC comparten el bus externo y
// poco más:
//
//   Banco 1  0x6000_0000  SRAM / PSRAM / NOR   4 chip selects (NE1..NE4)
//            registros BCRx + BTRx + BWTRx; modo multiplexado; ráfaga síncrona;
//            temporización de escritura independiente (EXTMOD).
//   Banco 2  0x7000_0000  NAND                 1 chip select (NCE2)
//   Banco 3  0x8000_0000  NAND                 1 chip select (NCE3)
//            registros PCRx + SRx + PMEMx + PATTx + ECCRx; dos espacios
//            (común y de atributos); CÁLCULO DE ECC POR HARDWARE.
//   Banco 4  0x9000_0000  PC Card / CompactFlash  1 chip select
//            PCR4 + SR4 + PMEM4 + PATT4 + PIO4; TRES espacios (común, atributos
//            y E/S); SIN ECC.
//
// Ni siquiera comparten el mapa de registros: el banco 1 tiene BCR/BTR/BWTR y
// no tiene PMEM/PATT; los bancos 2 y 3 tienen ECCR y no tienen BTR; el 4 tiene
// PIO4 y no tiene ECCR. Por eso los rasgos son POR BANCO, no del periférico.
//
// -----------------------------------------------------------------------------
// Y EN ESTE ENCAPSULADO FALTAN DIECISÉIS HILOS DE DIRECCIÓN
//
// La tabla de pines del informe [IR, cap. 2] es tajante: del FSMC salen al
// LQFP100 los dieciséis hilos de datos (D0-D15), OCHO de dirección (A16-A23),
// NOE, NWE, NWAIT, NBL0/1, CLK, NL y UN chip select (PD7). No aparece ni un
// solo A0-A15 -viven en PF0-PF15- ni NE2/NE3/NE4 ni nada del banco 4.
//
// La consecuencia es que en este chip **el bus externo solo se puede usar
// multiplexado**: sin A0-A15 no hay forma de decir qué palabra se quiere dentro
// de la memoria, salvo sacando la dirección baja por los propios hilos de datos
// y enganchándola con NL. Y no es casualidad que el valor de reset de BCR1
// traiga MUXEN = 1: es la única configuración que este encapsulado puede usar.
//
// -----------------------------------------------------------------------------
// SELECCIÓN DEL TIPO DE BANCO
//
//   * en TIEMPO DE COMPILACIÓN, con el alias de plantilla:
//         using FsmcF407 = FsmcT<CAPS_FSMC_F407>;   // 1 NOR + 2 NAND + PC Card
//         using FsmcNor  = FsmcT<CAPS_FSMC_NOR>;    // solo estáticas
//         using FsmcMin  = FsmcT<CAPS_FSMC_MIN>;    // un banco SRAM y nada más
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         FsmcBase f{"fsmc", FsmcCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA de cada registro: un bit que
// el banco no implementa lee cero igual que un bit reservado, y un registro que
// no existe se lee cero entero. Misma receta que UsartCaps, TimCaps, SpiCaps,
// I2cCaps, AdcCaps, DacCaps, SdioCaps, CanCaps y DcmiCaps.
// =============================================================================
#ifndef STM32_PERIPH_FSMC_H
#define STM32_PERIPH_FSMC_H

#include <array>
#include "../common/periph_base.h"

namespace stm32 {

// Qué clase de memoria gobierna un banco. No es una etiqueta: decide qué
// registros existen y qué forma tiene el ciclo de bus.
enum class FsmcKind : uint8_t { Ninguno = 0, NorPsram, Nand, PcCard };

// ---------------------------------------------------------------------------
// Rasgos de UN banco
// ---------------------------------------------------------------------------
struct FsmcBankCaps {
    FsmcKind kind      = FsmcKind::Ninguno;
    unsigned chip_sel  = 1;      // cuántos NE gobierna (4 en el banco 1)
    bool     mux       = false;  // BCR.MUXEN: dirección baja por los datos
    bool     sync      = false;  // BCR.BURSTEN: ráfaga síncrona
    bool     extmod    = false;  // BCR.EXTMOD + BWTR: escritura con sus tiempos
    bool     nor_flash = false;  // BCR.FACCEN: interfaz de NOR (no solo SRAM)
    bool     ecc       = false;  // ECCR: cálculo de ECC por hardware
    bool     io_space  = false;  // PIO4: tercer espacio de E/S (PC Card)
    bool     wait      = false;  // NWAIT
    unsigned max_width = 16;     // MWID máximo: 8 o 16 bits
    const char* nombre = "-";
};

// ---------------------------------------------------------------------------
// Rasgos del periférico: los cuatro bancos y lo que hay en los pines
// ---------------------------------------------------------------------------
struct FsmcCaps {
    FsmcBankCaps banco[4]{};
    // Lo que el ENCAPSULADO tiene soldado, que no es lo que el registro admite.
    unsigned lineas_addr = 8;    // cuántas A hay (A16..A23 en el LQFP100)
    unsigned addr_base   = 16;   // la primera de ellas (A16)
    unsigned lineas_dato = 16;
    unsigned chip_sel_pin = 1;   // chip selects con pin en el encapsulado
    const char* kind = "FSMC";
};

// --- Las variantes ---------------------------------------------------------
// El F407VG: banco 1 completo (aunque con un solo NE cableado), dos bancos NAND
// con su ECC y el banco 4 de PC Card. Ocho hilos de dirección y dieciséis de
// datos [IR, cap. 2].
constexpr FsmcCaps caps_fsmc_f407() {
    FsmcCaps c{};
    c.banco[0] = FsmcBankCaps{FsmcKind::NorPsram, 4, true,  true,  true,
                              true,  false, false, true, 16, "SRAM/PSRAM/NOR"};
    c.banco[1] = FsmcBankCaps{FsmcKind::Nand,     1, false, false, false,
                              false, true,  false, true,  8, "NAND"};
    c.banco[2] = FsmcBankCaps{FsmcKind::Nand,     1, false, false, false,
                              false, true,  false, true,  8, "NAND"};
    c.banco[3] = FsmcBankCaps{FsmcKind::PcCard,   1, false, false, false,
                              false, false, true,  true, 16, "PC Card"};
    c.kind = "FSMC F407VG (4 bancos; 8 hilos de direccion en LQFP100)";
    return c;
}
// Solo memorias estáticas: los cuatro chip selects del banco 1 y nada más. Es
// como aparece el bloque en los derivados sin NAND.
constexpr FsmcCaps caps_fsmc_nor() {
    FsmcCaps c{};
    c.banco[0] = FsmcBankCaps{FsmcKind::NorPsram, 4, true, true, true,
                              true, false, false, true, 16, "SRAM/PSRAM/NOR"};
    c.lineas_addr = 26; c.addr_base = 0; c.chip_sel_pin = 4;
    c.kind = "FSMC solo estaticas, 26 hilos de direccion";
    return c;
}
// El mínimo que sigue siendo un FSMC: un chip select, SRAM asíncrona de 8 bits,
// sin multiplexar, sin ráfaga, sin modo extendido y sin NWAIT.
constexpr FsmcCaps caps_fsmc_min() {
    FsmcCaps c{};
    c.banco[0] = FsmcBankCaps{FsmcKind::NorPsram, 1, false, false, false,
                              false, false, false, false, 8, "SRAM"};
    c.lineas_addr = 16; c.addr_base = 0; c.chip_sel_pin = 1;
    c.lineas_dato = 8;
    c.kind = "FSMC minimo: una SRAM asincrona de 8 bits";
    return c;
}

inline constexpr FsmcCaps CAPS_FSMC_F407 = caps_fsmc_f407();
inline constexpr FsmcCaps CAPS_FSMC_NOR  = caps_fsmc_nor();
inline constexpr FsmcCaps CAPS_FSMC_MIN  = caps_fsmc_min();

// ---------------------------------------------------------------------------
// El controlador
// ---------------------------------------------------------------------------
class FsmcBase : public sc_core::sc_module {
public:
    // Puerto esclavo desde la matriz (S6): bancos externos y registros.
    tlm_utils::simple_target_socket<FsmcBase> mem{"mem"};
    sc_core::sc_in<bool>  hclk{"hclk"};
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};
    sc_core::sc_in<bool>  rst_n{"rst_n"};
    sc_core::sc_in<bool>  clk_en{"clk_en"};
    sc_core::sc_out<bool> irq{"irq"};                     // IRQ 48

    // ---- El bus externo (AF12) --------------------------------------------
    // Los datos son BIDIRECCIONALES: el controlador los conduce al escribir y
    // los suelta al leer, que es de donde sale el tiempo de vuelta del bus
    // (BUSTURN) y la razón de que exista.
    sc_core::sc_vector<sc_core::sc_signal<bool>> d_out, d_oe, d_in;   // D0-15
    sc_core::sc_vector<sc_core::sc_signal<bool>> a_out;               // A16-A23
    sc_core::sc_vector<sc_core::sc_signal<bool>> ne;                  // NE1-4 / NCEx
    sc_core::sc_signal<bool> noe{"noe"}, nwe{"nwe"}, nl{"nl"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> nbl;                 // NBL0/1
    sc_core::sc_signal<bool> nwait_in{"nwait_in"};

    // ---- Registros [IR, §12.18.2] -----------------------------------------
    enum : uint32_t {
        R_BCR1 = 0x00, R_BTR1 = 0x04,
        R_PCR2 = 0x60, R_SR2 = 0x64, R_PMEM2 = 0x68, R_PATT2 = 0x6C, R_ECCR2 = 0x74,
        R_PCR3 = 0x80, R_SR3 = 0x84, R_PMEM3 = 0x88, R_PATT3 = 0x8C, R_ECCR3 = 0x94,
        R_PCR4 = 0xA0, R_SR4 = 0xA4, R_PMEM4 = 0xA8, R_PATT4 = 0xAC, R_PIO4 = 0xB0,
        R_BWTR1 = 0x104
    };
    // FSMC_BCRx
    enum : uint32_t {
        BCR_MBKEN = 1u << 0, BCR_MUXEN = 1u << 1, BCR_MTYP = 3u << 2,
        BCR_MWID = 3u << 4, BCR_FACCEN = 1u << 6, BCR_BURSTEN = 1u << 8,
        BCR_WAITPOL = 1u << 9, BCR_WRAPMOD = 1u << 10, BCR_WAITCFG = 1u << 11,
        BCR_WREN = 1u << 12, BCR_WAITEN = 1u << 13, BCR_EXTMOD = 1u << 14,
        BCR_ASYNCWAIT = 1u << 15, BCR_CBURSTRW = 1u << 19
    };
    // FSMC_PCRx (NAND / PC Card)
    enum : uint32_t {
        PCR_PWAITEN = 1u << 1, PCR_PBKEN = 1u << 2, PCR_PTYP = 1u << 3,
        PCR_PWID = 3u << 4, PCR_ECCEN = 1u << 6, PCR_ECCPS = 7u << 17
    };
    // FSMC_SRx
    enum : uint32_t {
        SR_IRS = 1u << 0, SR_ILS = 1u << 1, SR_IFS = 1u << 2,
        SR_IREN = 1u << 3, SR_ILEN = 1u << 4, SR_IFEN = 1u << 5, SR_FEMPT = 1u << 6
    };

    FsmcBase(sc_core::sc_module_name nm, const FsmcCaps& c)
        : sc_core::sc_module(nm),
          d_out("d_out", 16), d_oe("d_oe", 16), d_in("d_in", 16),
          a_out("a_out", 8), ne("ne", 4), nbl("nbl", 2), caps(c) {
        mem.register_b_transport(this, &FsmcBase::bt_mem);
        SC_HAS_PROCESS(FsmcBase);
        // UN SOLO ESCRITOR DE LOS PINES. El ciclo de bus lo dispara cualquier
        // maestro de la matriz -el núcleo, el DMA, el depurador- y SystemC no
        // admite que dos procesos conduzcan la misma señal. Así que el
        // b_transport no toca los pines: deja la petición, despierta a este
        // hilo y espera. Es también lo que hace el silicio, donde el
        // controlador es uno solo y el bus AHB se queda esperando.
        SC_THREAD(bus_proc);
        SC_METHOD(pub_proc);  sensitive << pub_ev_;
        SC_METHOD(rst_proc);  sensitive << rst_n;
        SC_METHOD(nand_int_proc); sensitive << nwait_in;
        dont_initialize();
        reset_regs();
    }

    const FsmcCaps caps;

    // ---- Ventanas para el banco de pruebas ---------------------------------
    uint64_t ciclos_ext() const { return n_ciclos_; }
    uint64_t bytes_leidos() const { return n_rd_; }
    uint64_t bytes_escritos() const { return n_wr_; }
    unsigned ultimo_banco() const { return ult_banco_; }
    unsigned ultimos_hclk() const { return ult_hclk_; }
    uint32_t peek(uint32_t off) { return leer_reg(off); }
    // ¿Se puede usar el bus tal y como está programado en ESTE encapsulado?
    // Sin A0-A15 no hay forma de direccionar si no se multiplexa.
    bool direccionable() const {
        return (bcr_[0] & BCR_MUXEN) || caps.addr_base == 0;
    }

protected:
    // ---- Banco de registros ------------------------------------------------
    uint32_t bcr_[4]{}, btr_[4]{}, bwtr_[4]{};
    uint32_t pcr_[4]{}, sr_[4]{}, pmem_[4]{}, patt_[4]{}, eccr_[4]{}, pio4_ = 0;
    // ---- ECC de la NAND ----------------------------------------------------
    uint32_t ecc_lp_[4]{};        // paridad de línea acumulada
    uint32_t ecc_cp_[4]{};        // paridad de columna
    uint32_t ecc_n_[4]{};         // bytes acumulados
    // ---- Estadísticas ------------------------------------------------------
    uint64_t n_ciclos_ = 0, n_rd_ = 0, n_wr_ = 0;
    unsigned ult_banco_ = 0, ult_hclk_ = 0;
    bool o_irq_ = false;

    // =======================================================================
    // Máscaras de escritura: aquí viven los rasgos
    // =======================================================================
    uint32_t mask_bcr(unsigned b) const {
        const FsmcBankCaps& k = caps.banco[b];
        if (k.kind != FsmcKind::NorPsram) return 0;      // el banco no es estático
        uint32_t m = BCR_MBKEN | BCR_MTYP | BCR_MWID | BCR_WREN;
        if (k.mux)       m |= BCR_MUXEN;
        if (k.nor_flash) m |= BCR_FACCEN;
        if (k.sync)      m |= BCR_BURSTEN | BCR_WRAPMOD | BCR_CBURSTRW;
        if (k.wait)      m |= BCR_WAITPOL | BCR_WAITCFG | BCR_WAITEN | BCR_ASYNCWAIT;
        if (k.extmod)    m |= BCR_EXTMOD;
        if (k.max_width == 8) m &= ~(2u << 4);           // MWID solo 0 o 1
        return m;
    }
    // El bit 7 de BCR es reservado, pero el valor de reset documentado
    // (0x30DB / 0x30D2) lo trae a uno y ahi se queda: no es escribible, pero
    // tampoco lee cero. Modelarlo como parte de la mascara seria mentir -se
    // podria borrar-, asi que va aparte [IR, §12.18.2-B].
    uint32_t bcr_fijo(unsigned b) const {
        return caps.banco[b].kind == FsmcKind::NorPsram ? (1u << 7) : 0u;
    }
    uint32_t mask_btr(unsigned b) const {
        const FsmcBankCaps& k = caps.banco[b];
        if (k.kind != FsmcKind::NorPsram) return 0;
        uint32_t m = 0x000FFFFFu;                        // ADDSET/ADDHLD/DATAST/BUSTURN
        if (k.sync)   m |= 0x0FF00000u;                  // CLKDIV y DATLAT
        if (k.extmod) m |= 0x30000000u;                  // ACCMOD
        return m;
    }
    uint32_t mask_pcr(unsigned b) const {
        const FsmcBankCaps& k = caps.banco[b];
        if (k.kind != FsmcKind::Nand && k.kind != FsmcKind::PcCard) return 0;
        uint32_t m = PCR_PBKEN | PCR_PTYP | PCR_PWID | 0x0001FE00u;  // TCLR/TAR
        if (k.wait) m |= PCR_PWAITEN;
        if (k.ecc)  m |= PCR_ECCEN | PCR_ECCPS;
        return m;
    }

    // =======================================================================
    // El decodificador de bancos [IR, §12.18.2-C]
    // =======================================================================
    static unsigned banco_de(uint64_t a) {
        if (a < 0x70000000ull) return 0;                 // 0x6000_0000 NOR/PSRAM
        if (a < 0x80000000ull) return 1;                 // 0x7000_0000 NAND
        if (a < 0x90000000ull) return 2;                 // 0x8000_0000 NAND
        return 3;                                        // 0x9000_0000 PC Card
    }
    // Dentro del banco 1, cuál de los cuatro chip selects [IR, §12.18.1]
    static unsigned subbanco_de(uint64_t a) {
        return unsigned((a >> 26) & 3u);                 // 4 x 64 MB
    }
    // EL CHIP SELECT DE UN BANCO NAND NO TIENE PAD PROPIO.
    // En el LQFP100, PD7 es a la vez FSMC_NE1 y FSMC_NCE2: es EL MISMO pad, y
    // el controlador saca por él uno u otro según a qué banco vaya el acceso.
    // NCE3 (PG9) y NCE4 (PG12/PG13) viven en puertos que este encapsulado no
    // trae, de modo que esas líneas se quedan dentro del chip [IR, cap. 2].
    unsigned ne_nand(unsigned b) const {
        if (b == 1) return caps.chip_sel_pin <= 1 ? 0u : 1u;
        return b;
    }

    // =======================================================================
    // Los tiempos, que son la razón de ser de este periférico
    //
    // Un acceso asíncrono dura ADDSET + ADDHLD + DATAST + BUSTURN ciclos de
    // HCLK [IR, §12.18.2-B]. Con EXTMOD = 1 la escritura usa los suyos, que
    // están en BWTR: es lo que permite una memoria con lectura lenta y
    // escritura rápida, o al revés.
    // =======================================================================
    struct Tiempos { unsigned addset, addhld, datast, busturn; };
    Tiempos tiempos(unsigned b, bool escritura) const {
        const uint32_t r = (escritura && (bcr_[b] & BCR_EXTMOD) && caps.banco[b].extmod)
                           ? bwtr_[b] : btr_[b];
        Tiempos t{};
        t.addset  = (r >> 0) & 0xFu;
        t.addhld  = (r >> 4) & 0xFu;
        t.datast  = (r >> 8) & 0xFFu;
        // BUSTURN solo lo trae el registro de lectura: el de escritura reserva
        // ese campo [IR, §12.18.2-B].
        t.busturn = (btr_[b] >> 16) & 0xFu;
        return t;
    }
    // Ciclos de HCLK de un acceso, contando el multiplexado y la ráfaga.
    unsigned ciclos_acceso(unsigned b, bool escritura) const {
        const Tiempos t = tiempos(b, escritura);
        const bool mux = (bcr_[b] & BCR_MUXEN) != 0;
        unsigned n = t.addset + 1u + t.datast + 1u + t.busturn;
        if (mux) n += t.addhld + 1u;                     // fase de dirección
        if ((bcr_[b] & BCR_BURSTEN) && caps.banco[b].sync) {
            // En ráfaga manda el reloj: CLKDIV ciclos por dato y DATLAT de
            // latencia inicial [IR, §12.18.2-B].
            const unsigned clkdiv = ((btr_[b] >> 20) & 0xFu) + 1u;
            const unsigned datlat = (btr_[b] >> 24) & 0xFu;
            n = clkdiv * (datlat + 1u) + clkdiv;
        }
        return n ? n : 1u;
    }

    // =======================================================================
    // El acceso desde la matriz
    // =======================================================================
    // La petición que el b_transport deja para el hilo del bus.
    struct Peticion {
        tlm::tlm_generic_payload* gp = nullptr;
        bool viva = false;
    };
    Peticion pet_;
    bool ocupado_ = false;
    sc_core::sc_event ini_ev_, fin_ev_, rst_bus_ev_;

    void bt_mem(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (a >= addr::FSMC_REGS) { acceso_registros(gp); return; }
        if (!clk_en.read() || !rst_n.read()) {
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        // El bus externo es de uno en uno: si hay un ciclo en marcha, se espera.
        while (ocupado_) sc_core::wait(fin_ev_);
        ocupado_ = true;
        pet_.gp = &gp;
        pet_.viva = true;
        ini_ev_.notify(sc_core::SC_ZERO_TIME);
        // Y aquí el maestro se queda parado el tiempo que dure el ciclo. No se
        // anota en `t`: el tiempo se CONSUME de verdad, porque un acceso a
        // memoria externa bloquea el bus AHB mientras dura [IR, §12.18].
        sc_core::wait(fin_ev_);
        ocupado_ = false;
        (void)t;
    }

    void bus_proc() {
        soltar_bus();
        for (;;) {
            sc_core::wait(ini_ev_ | rst_bus_ev_);
            if (!pet_.viva) { soltar_bus(); continue; }
            sc_core::sc_time cero = sc_core::SC_ZERO_TIME;
            acceso_externo(*pet_.gp, cero);
            pet_.viva = false;
            fin_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void acceso_registros(tlm::tlm_generic_payload& gp) {
        const uint32_t off = uint32_t(gp.get_address() - addr::FSMC_REGS) & 0xFFCu;
        unsigned char* d = gp.get_data_ptr();
        const unsigned len = gp.get_data_length();
        if (!clk_en.read()) { gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        if (len != 4 || (gp.get_address() & 3u)) {
            gp.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (gp.is_read()) {
            const uint32_t v = leer_reg(off);
            for (unsigned i = 0; i < 4; ++i) d[i] = uint8_t(v >> (8 * i));
        } else {
            uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i) v |= uint32_t(d[i]) << (8 * i);
            escribir_reg(off, v);
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    uint32_t leer_reg(uint32_t off) const {
        if (off < 0x20 && (off & 4u) == 0) return bcr_[off / 8];
        if (off < 0x20)                    return btr_[off / 8];
        // BWTR1..4 viven en 0x104, 0x10C, 0x114 y 0x11C: de ocho en ocho a
        // partir de 0x104, con el hueco de en medio reservado.
        if (off >= 0x104 && off < 0x120 && ((off - 0x104) & 7u) == 0)
            return caps.banco[(off - 0x104) / 8].extmod ? bwtr_[(off - 0x104) / 8] : 0u;
        switch (off) {
            case R_PCR2:  return pcr_[1];  case R_SR2:  return sr_estado(1);
            case R_PMEM2: return pmem_[1]; case R_PATT2: return patt_[1];
            case R_ECCR2: return caps.banco[1].ecc ? eccr_[1] : 0u;
            case R_PCR3:  return pcr_[2];  case R_SR3:  return sr_estado(2);
            case R_PMEM3: return pmem_[2]; case R_PATT3: return patt_[2];
            case R_ECCR3: return caps.banco[2].ecc ? eccr_[2] : 0u;
            case R_PCR4:  return pcr_[3];  case R_SR4:  return sr_estado(3);
            case R_PMEM4: return pmem_[3]; case R_PATT4: return patt_[3];
            case R_PIO4:  return caps.banco[3].io_space ? pio4_ : 0u;
            default: return 0;
        }
    }
    uint32_t sr_estado(unsigned b) const {
        // FEMPT: la FIFO de escritura está vacía. En este modelo las escrituras
        // se completan dentro del acceso, así que siempre lo está.
        return (sr_[b] & 0x3Fu) | SR_FEMPT;
    }

    void escribir_reg(uint32_t off, uint32_t v) {
        if (off < 0x20) {
            const unsigned b = off / 8;
            if (off & 4u) btr_[b] = v & mask_btr(b);
            else          bcr_[b] = (v & mask_bcr(b)) | bcr_fijo(b);
            return;
        }
        if (off >= 0x104 && off < 0x120 && ((off - 0x104) & 7u) == 0) {
            const unsigned b = (off - 0x104) / 8;
            if (caps.banco[b].extmod) bwtr_[b] = v & mask_btr(b);
            return;
        }
        auto nand = [&](unsigned b, uint32_t o) {
            switch (o) {
                case 0x00: {
                    const uint32_t antes = pcr_[b];
                    pcr_[b] = v & mask_pcr(b);
                    // Encender el ECC lo pone a cero: es un acumulador, y
                    // acumular sobre lo de la página anterior no sirve de nada.
                    if (!(antes & PCR_ECCEN) && (pcr_[b] & PCR_ECCEN)) ecc_reset(b);
                    return;
                }
                case 0x04: sr_[b] = (sr_[b] & ~0x3Fu) | (v & 0x38u) |
                                    ((sr_[b] & v & 0x07u) ^ (sr_[b] & 0x07u));
                           // los tres flags de interrupción son rc_w0
                           sr_[b] = (sr_[b] & ~0x07u) | (sr_[b] & v & 0x07u);
                           actualiza_irq();
                           return;
                case 0x08: pmem_[b] = v; return;
                case 0x0C: patt_[b] = v; return;
                case 0x10: if (b == 3 && caps.banco[3].io_space) pio4_ = v; return;
                case 0x14: return;                        // ECCR: solo lectura
                default: return;
            }
        };
        if (off >= 0x60 && off < 0x80) { nand(1, off - 0x60); return; }
        if (off >= 0x80 && off < 0xA0) { nand(2, off - 0x80); return; }
        if (off >= 0xA0 && off < 0xC0) { nand(3, off - 0xA0); return; }
    }

    void reset_regs() {
        // El banco 1 arranca HABILITADO y multiplexado; los otros tres, no
        // [IR, §12.18.2: reset 0x0000 30DB]. Si los cuatro arrancaran
        // habilitados, los cuatro chip selects competirían por el mismo bus.
        for (unsigned b = 0; b < 4; ++b) {
            bcr_[b] = ((b == 0 ? 0x000030DBu : 0x000030D2u) & mask_bcr(b)) | bcr_fijo(b);
            btr_[b] = 0x0FFFFFFFu & mask_btr(b);
            bwtr_[b] = 0x0FFFFFFFu & mask_btr(b);
            pcr_[b] = 0x00000018u & mask_pcr(b);
            sr_[b] = SR_FEMPT;
            pmem_[b] = patt_[b] = 0xFCFCFCFCu;
            eccr_[b] = 0;
            ecc_reset(b);
        }
        pio4_ = 0xFCFCFCFCu;
        n_ciclos_ = n_rd_ = n_wr_ = 0;
    }
    void rst_proc() {
        if (rst_n.read()) return;
        reset_regs();
        rst_bus_ev_.notify(sc_core::SC_ZERO_TIME);   // el hilo suelta los pines
        actualiza_irq();
    }
    void pub_proc() { irq.write(o_irq_); }
    void publish()  { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    sc_core::sc_event pub_ev_;

    // =======================================================================
    // El ciclo de bus externo, en los pines
    // =======================================================================
    void soltar_bus() {
        for (unsigned i = 0; i < 16; ++i) { d_oe[i].write(false); d_out[i].write(false); }
        for (unsigned i = 0; i < 8; ++i) a_out[i].write(false);
        for (unsigned i = 0; i < 4; ++i) ne[i].write(true);       // activos a cero
        noe.write(true); nwe.write(true); nl.write(true);
        nbl[0].write(true); nbl[1].write(true);
    }
    void pon_datos(uint32_t v, unsigned nbits) {
        for (unsigned i = 0; i < 16; ++i) {
            d_out[i].write(i < nbits ? ((v >> i) & 1u) != 0 : false);
            d_oe[i].write(i < nbits);
        }
    }
    void suelta_datos() { for (unsigned i = 0; i < 16; ++i) d_oe[i].write(false); }
    uint32_t lee_datos(unsigned nbits) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < nbits && i < 16; ++i) if (d_in[i].read()) v |= 1u << i;
        return v;
    }
    void pon_direccion(uint32_t a) {
        // Solo salen las líneas que el encapsulado tiene: en el LQFP100,
        // A16-A23. Las de abajo no existen, y por eso hace falta multiplexar.
        for (unsigned i = 0; i < 8 && i < caps.lineas_addr; ++i)
            a_out[i].write(((a >> (caps.addr_base + i)) & 1u) != 0);
    }
    sc_core::sc_time hclk_t(unsigned n) const {
        const double f = hclk_hz.read();
        return f > 0.0 ? sc_core::sc_time(double(n) * 1.0e12 / f, sc_core::SC_PS)
                       : sc_core::sc_time(double(n) * 6.0, sc_core::SC_NS);
    }

    // =======================================================================
    // Un acceso al bus externo, entero
    // =======================================================================
    void acceso_externo(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        const unsigned b = banco_de(a);
        const FsmcBankCaps& k = caps.banco[b];
        ult_banco_ = b;

        if (k.kind == FsmcKind::Ninguno) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (k.kind == FsmcKind::NorPsram) { acceso_estatico(gp, t, b); return; }
        acceso_nand(gp, t, b);
    }

    // ---- Bancos de memoria estática (banco 1) ------------------------------
    void acceso_estatico(tlm::tlm_generic_payload& gp, sc_core::sc_time& t, unsigned b) {
        if (!(bcr_[b] & BCR_MBKEN)) {          // banco apagado: nadie contesta
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const bool escr = gp.is_write();
        if (escr && !(bcr_[b] & BCR_WREN)) {   // banco protegido contra escritura
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        const uint64_t a = gp.get_address();
        const unsigned cs = (caps.banco[b].chip_sel > 1) ? subbanco_de(a) : 0u;
        const unsigned anchura = ((bcr_[b] >> 4) & 3u) == 0 ? 8u : 16u;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        const unsigned paso = anchura / 8u;               // bytes por ciclo externo
        unsigned hclk_total = 0;

        for (unsigned i = 0; i < len; i += paso) {
            uint32_t dato = 0;
            unsigned carriles = 0;               // qué bytes del ciclo son reales
            for (unsigned k2 = 0; k2 < paso && i + k2 < len; ++k2) {
                carriles |= 1u << k2;
                if (escr) dato |= uint32_t(d[i + k2]) << (8 * k2);
            }
            // LO QUE SALE POR LOS HILOS DE DIRECCION NO ES LA DIRECCION DE
            // BYTE. Con un bus de 16 bits el FSMC saca HADDR[25:1] por A[24:0]:
            // A0 no existe, porque cada direccion externa vale DOS bytes y son
            // NBL0/NBL1 los que eligen cual. Con un bus de 8 bits sale la
            // direccion de byte tal cual [IR, §12.18.2-B].
            const uint32_t dir = uint32_t((a + i) / paso);
            hclk_total += ciclo_estatico(b, cs, dir, dato, escr, anchura, carriles);
            if (!escr) for (unsigned k2 = 0; k2 < paso && i + k2 < len; ++k2)
                d[i + k2] = uint8_t(dato >> (8 * k2));
        }
        ult_hclk_ = hclk_total;
        t += hclk_t(hclk_total);
        if (escr) n_wr_ += len; else n_rd_ += len;
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // UN ciclo externo. Aquí es donde el periférico deja de ser un banco de
    // registros y se convierte en dieciséis hilos moviéndose.
    unsigned ciclo_estatico(unsigned b, unsigned cs, uint32_t dir, uint32_t& dato,
                            bool escr, unsigned anchura, unsigned carriles) {
        const Tiempos tt = tiempos(b, escr);
        const bool mux = (bcr_[b] & BCR_MUXEN) != 0;
        ++n_ciclos_;

        // 1. Chip select y dirección alta
        ne[cs].write(false);
        pon_direccion(dir);
        // 2. Fase de dirección multiplexada: la parte baja sale por los DATOS y
        //    se engancha con NL. Sin esto, en un encapsulado sin A0-A15 no hay
        //    forma de decirle a la memoria qué palabra se quiere.
        if (mux) {
            nl.write(false);
            pon_datos(dir & 0xFFFFu, 16);
            esperar(tt.addset + 1u);
            nl.write(true);                    // el flanco de subida engancha
            esperar(tt.addhld + 1u);
            suelta_datos();
        } else {
            esperar(tt.addset + 1u);
        }
        // 3. Fase de datos
        unsigned espera_extra = 0;
        if (escr) {
            pon_datos(dato, anchura);
            // NBL0 y NBL1 dicen QUÉ BYTE del ancho de bus se escribe. Sin
            // ellos, escribir un solo byte en una memoria de 16 bits
            // machacaría también el de al lado [IR, §12.18].
            nbl[0].write(!(carriles & 1u));
            nbl[1].write(anchura == 8 || !(carriles & 2u));
            nwe.write(false);
            espera_extra = esperar_nwait(b, tt.datast + 1u);
            nwe.write(true);
            // Los datos SIGUEN PUESTOS un ciclo despues de que NWE suba: es el
            // tiempo de mantenimiento (t_DH) que toda SRAM asincrona exige,
            // porque lo que guarda es lo que hay en los hilos DESPUES del
            // flanco de subida. Soltarlos en el mismo instante dejaria a la
            // memoria sin nada que enganchar -y, en el modelo, dejaria al
            // pad sin tiempo de propagar-.
            esperar(1);
            suelta_datos();
        } else {
            noe.write(false);
            espera_extra = esperar_nwait(b, tt.datast + 1u);
            dato = lee_datos(anchura);
            noe.write(true);
        }
        // 4. Vuelta del bus: el tiempo que la memoria tarda en soltar los hilos
        //    antes de que los coja el siguiente [IR, §12.18.2-B, BUSTURN].
        ne[cs].write(true);
        nbl[0].write(true); nbl[1].write(true);
        esperar(tt.busturn);
        if (!tt.busturn) sc_core::wait(sc_core::SC_ZERO_TIME);
        return tt.addset + 1u + (mux ? tt.addhld + 1u : 0u) + tt.datast + 1u +
               tt.busturn + espera_extra + (escr ? 1u : 0u);   // +1 = t_DH
    }

    // NWAIT: la memoria puede pedir más tiempo. Es lo único del bus externo que
    // no lo decide el controlador [IR, §12.18.2].
    unsigned esperar_nwait(unsigned b, unsigned n) {
        esperar(n);
        if (!(bcr_[b] & BCR_WAITEN) || !caps.banco[b].wait) return 0;
        const bool activo_alto = (bcr_[b] & BCR_WAITPOL) != 0;
        unsigned extra = 0;
        while (nwait_in.read() == activo_alto && extra < 256u) {
            esperar(1);
            ++extra;
        }
        return extra;
    }
    void esperar(unsigned n) { if (n) sc_core::wait(hclk_t(n)); }

    // =======================================================================
    // Bancos NAND (2 y 3) y PC Card (4)
    //
    // Una NAND no tiene bus de direcciones: tiene un puerto de ocho bits por el
    // que van mandatos, direcciones y datos, y dos señales -CLE y ALE- que
    // dicen cuál de las tres cosas es. El FSMC las saca por A16 y A17, de modo
    // que ESCRIBIR EN UNA DIRECCIÓN U OTRA del banco es lo que elige el tipo de
    // ciclo [IR, §12.18.2-C].
    // =======================================================================
    void acceso_nand(tlm::tlm_generic_payload& gp, sc_core::sc_time& t, unsigned b) {
        if (!(pcr_[b] & PCR_PBKEN)) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const uint64_t a = gp.get_address();
        const bool cle = ((a >> 16) & 1u) != 0;
        const bool ale = ((a >> 17) & 1u) != 0;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        // Los tiempos del espacio común están en PMEM; los del de atributos, en
        // PATT. SETUP/WAIT/HOLD/HIZ, un byte cada uno.
        const uint32_t r = pmem_[b];
        const unsigned setup = (r >> 0) & 0xFFu, espera_ = (r >> 8) & 0xFFu,
                       hold = (r >> 16) & 0xFFu;
        unsigned hclk_total = 0;

        const unsigned pin_ce = ne_nand(b);
        for (unsigned i = 0; i < len; ++i) {
            ne[pin_ce].write(false);
            a_out[0].write(cle);                 // A16 = CLE
            a_out[1].write(ale);                 // A17 = ALE
            esperar(setup + 1u);
            if (gp.is_write()) {
                pon_datos(d[i], 8);
                nwe.write(false);
                esperar(espera_ + 1u);
                nwe.write(true);
                esperar(1);                      // t_DH: el dato aguanta el flanco
                suelta_datos();
                if (!cle && !ale) ecc_acumula(b, d[i]);
            } else {
                noe.write(false);
                esperar(espera_ + 1u);
                d[i] = uint8_t(lee_datos(8));
                noe.write(true);
                if (!cle && !ale) ecc_acumula(b, d[i]);
            }
            esperar(hold + 1u);
            ne[pin_ce].write(true);
            a_out[0].write(false); a_out[1].write(false);
            hclk_total += setup + espera_ + hold + 3u + (gp.is_write() ? 1u : 0u);
            ++n_ciclos_;
        }
        ult_hclk_ = hclk_total;
        t += hclk_t(hclk_total);
        if (gp.is_write()) n_wr_ += len; else n_rd_ += len;
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // =======================================================================
    // El ECC de la NAND
    //
    // Es la única aritmética que hace el FSMC, y existe porque una NAND se
    // equivoca: sus celdas pierden carga y devuelven bits cambiados. El
    // controlador va acumulando, byte a byte y sin coste para el firmware, una
    // paridad de COLUMNA (qué bit de los ocho) y otra de LÍNEA (qué byte de la
    // página), y con las dos juntas se localiza y se corrige un bit.
    //
    // ⚠ NO DISPONIBLE EN LAS FUENTES: el informe [IR, §12.18.2-C] da el offset
    // del registro y nada más, y el propio informe advierte de que el detalle
    // bit a bit de la NAND del FSMC hay que sacarlo de RM0090. Se implementa el
    // esquema clásico de Hamming de las NAND, que es el que da ST, y se verifica
    // por CONSISTENCIA: se corrige un bit cambiado a propósito.
    // =======================================================================
    void ecc_reset(unsigned b) { ecc_lp_[b] = ecc_cp_[b] = ecc_n_[b] = 0; eccr_[b] = 0; }
    unsigned ecc_pagina(unsigned b) const {
        return 256u << ((pcr_[b] >> 17) & 7u);           // ECCPS: 256..8192 bytes
    }
    void ecc_acumula(unsigned b, uint8_t dato) {
        if (!caps.banco[b].ecc || !(pcr_[b] & PCR_ECCEN)) return;
        // Paridad de columna: el XOR de todos los bytes da, bit a bit, si cada
        // columna tiene un número par o impar de unos.
        ecc_cp_[b] ^= dato;
        // Paridad de línea: por cada bit de la DIRECCIÓN del byte, se acumula la
        // paridad de los bytes cuya dirección tiene ese bit a uno.
        const uint32_t idx = ecc_n_[b];
        const uint8_t par = paridad(dato);
        if (par) for (unsigned k = 0; k < 12; ++k)
            if ((idx >> k) & 1u) ecc_lp_[b] ^= 1u << k;
        ++ecc_n_[b];
        if (ecc_n_[b] >= ecc_pagina(b)) {
            // Página completa: el resultado se congela en ECCR y se empieza otra.
            eccr_[b] = (ecc_lp_[b] & 0xFFFFu) | (uint32_t(ecc_cp_[b] & 0xFFu) << 16);
            ecc_lp_[b] = ecc_cp_[b] = ecc_n_[b] = 0;
        } else {
            eccr_[b] = (ecc_lp_[b] & 0xFFFFu) | (uint32_t(ecc_cp_[b] & 0xFFu) << 16);
        }
    }
    static uint8_t paridad(uint8_t v) {
        v ^= uint8_t(v >> 4); v ^= uint8_t(v >> 2); v ^= uint8_t(v >> 1);
        return v & 1u;
    }

    // Las tres interrupciones de la NAND: flanco de subida, nivel alto y flanco
    // de bajada de la señal de listo/ocupado [IR, §12.18.2-C, FSMC_SRx].
    void nand_int_proc() {
        for (unsigned b = 1; b <= 2; ++b) {
            if (caps.banco[b].kind != FsmcKind::Nand) continue;
            if (nwait_in.read()) sr_[b] |= SR_IRS | SR_ILS;
            else                 sr_[b] |= SR_IFS;
        }
        actualiza_irq();
    }
    void actualiza_irq() {
        bool v = false;
        for (unsigned b = 1; b < 4; ++b) {
            const uint32_t s = sr_[b];
            if ((s & SR_IRS) && (s & SR_IREN)) v = true;
            if ((s & SR_ILS) && (s & SR_ILEN)) v = true;
            if ((s & SR_IFS) && (s & SR_IFEN)) v = true;
        }
        o_irq_ = v;
        publish();
    }
};

// ---------------------------------------------------------------------------
// La misma clase, con los rasgos fijados en tiempo de compilación
// ---------------------------------------------------------------------------
template <const FsmcCaps& C>
class FsmcT : public FsmcBase {
public:
    explicit FsmcT(sc_core::sc_module_name nm) : FsmcBase(nm, C) {
        static_assert(C.lineas_dato == 8 || C.lineas_dato == 16,
                      "el bus de datos del FSMC es de 8 o 16 hilos [IR, 12.18]");
        static_assert(C.banco[0].chip_sel >= 1 && C.banco[0].chip_sel <= 4,
                      "el banco 1 tiene entre uno y cuatro chip selects");
        // Un banco NAND sin ECC no tendria como saber que se le ha corrompido
        // una pagina; el silicio siempre lo trae.
        static_assert(C.banco[1].kind != FsmcKind::Nand || C.banco[1].ecc, "");
    }
};

using Fsmc     = FsmcT<CAPS_FSMC_F407>;   // el del F407VG
using FsmcNor  = FsmcT<CAPS_FSMC_NOR>;    // solo memorias estaticas
using FsmcMin  = FsmcT<CAPS_FSMC_MIN>;    // una SRAM de 8 bits y nada mas

} // namespace stm32
#endif // STM32_PERIPH_FSMC_H
