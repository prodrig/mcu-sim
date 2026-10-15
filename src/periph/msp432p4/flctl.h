// =============================================================================
// flctl.h — La Flash del MSP432P4, su controlador (FLCTL) y el TLV
//
// Desde T2, el controlador ENTERO: leer, proteger, borrar y programar.
// [doc/analisis_msp432.md, §4.3 y §8 T2]
//
// LA MEMORIA. Dos memorias, cada una en dos bancos:
//
//   principal     0x0000_0000, 256 KB en el P401R: banco 0 la primera mitad y
//                 banco 1 la segunda, en sectores de 4 KB (32 por banco)
//   información   0x0020_0000, 16 KB: banco 0 con el buzón (sector 0) y el
//                 TLV (sector 1); banco 1 con el BSL (sectores 0 y 1)
//
// LO QUE HACE EL FLCTL, con los nombres de la cabecera de TI [H] y de Tock
// (chips/msp432/src/flctl.rs, que da los mismos campos):
//
//   * PROTECCIÓN. Un bit por sector en BANKx_MAIN_WEPROT y BANKx_INFO_WEPROT;
//     todo protegido al salir del reset. Un sector protegido no se borra ni
//     se programa. El TLV no se desprotege nunca: es de TI. [OO: OpenOCD lo
//     trata como sector de solo lectura]
//   * BORRADO. ERASE_SECTADDR + ERASE_CTLSTAT.START, de sector (MODE = 0) o
//     masivo (MODE = 1: todos los sectores no protegidos, principal e
//     información). STATUS pasa por 2 («en curso») hasta 3 («hecho»), y se
//     queda ahí hasta CLR_STAT. Un sector reservado da ADDR_ERR.
//   * PROGRAMACIÓN DE PALABRA. Con PRG_CTLSTAT.ENABLE, cada escritura en la
//     Flash la programa: de inmediato (MODE = 0) o al juntar 128 bits
//     (MODE = 1). Un sector protegido da PRG_ERR en el IFG.
//   * PROGRAMACIÓN EN RÁFAGA. Hasta cuatro palabras de 128 bits desde
//     PRGBRST_DATA0_0…DATA3_3 a PRGBRST_STARTADDR, alineada a 16 bytes.
//     BURST_STATUS cuenta las palabras (2-5) y acaba en 7.
//   * VERIFICACIÓN DESPUÉS. Programar solo puede poner bits a CERO: lo que
//     ya era cero no vuelve a uno sin borrar. Con VER_PST (o AUTO_PST en la
//     ráfaga), si lo que queda no es lo pedido, AVPST (o PST_ERR).
//   * LEER MIENTRAS SE ESCRIBE, pero en el OTRO banco. Mientras un banco se
//     borra o se programa, leerlo detiene al que lee hasta que acaba. Por eso
//     un programa que corre en el banco 0 puede reprogramar el banco 1 sin
//     pararse, y por eso un borrado del propio banco congela la CPU.
//   * IFG, IE, CLRIFG, SETIFG, y la línea FLCTL_IRQn.
//
// LOS TIEMPOS son de modelo, no del datasheet, y lo dice P-16 n: 30 µs por
// escritura de palabra y por cada 128 bits de una ráfaga, 5 ms por sector,
// 40 ms un borrado masivo.
//
// LO QUE NO HACE: la verificación ANTES de programar (VER_PRE, AUTO_PRE) se
// guarda pero no se ejecuta; los modos de lectura de margen y de
// verificación (RD_MODE) se aceptan y leen lo mismo que el normal; la
// lectura en ráfaga (RDBRST) y el contador de rendimiento (BMRK) guardan sus
// registros y no hacen nada. [P-16 e, o]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_FLCTL_H
#define STM32_PERIPH_MSP432P4_FLCTL_H

#include <cstring>
#include <functional>
#include <vector>
#include <tlm_utils/simple_target_socket.h>
#include "../../common/periph_base.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

// -----------------------------------------------------------------------------
// EL TLV SINTÉTICO
//
// El Device Descriptor es lo que el firmware de TI consulta para saber qué chip
// es y, sobre todo, cómo están CALIBRADOS sus relojes y su ADC. En un chip de
// verdad lo escribe TI en fábrica, distinto en cada pieza. Aquí se rellena con
// valores TÍPICOS -inventados de forma razonable donde no hay dato público-, y
// `limitaciones()` lo dice. La ESTRUCTURA sí es la de verdad: palabra a palabra
// la de `TLV_Type` [H], con las etiquetas de driverlib/sysctl.h (CS = 3,
// FLASHCTL = 4, ADC14 = 5, REF = 8, DEVINFO = 11, DIEREC = 12, RANDNUM = 13,
// BSL = 15, fin = 0x0BD0E11D) [TI, SDK 3.40, sysctl.h].
//
// Las constantes del DCO están elegidas para que un paso de DCOTUNE mueva la
// frecuencia algo menos de un 0,1 %: con FCAL = 400 y K = 1/632,
// 1/K + 768 - FCAL = 1000. [sintético; P-16 f]
// -----------------------------------------------------------------------------
inline std::vector<uint32_t> tlv_sintetico(uint32_t device_id, uint32_t hwrev) {
    auto f = [](float x) { uint32_t u; std::memcpy(&u, &x, 4); return u; };
    std::vector<uint32_t> t;
    t.push_back(0);                                  // TLV_CHECKSUM [sintético]
    t.push_back(11); t.push_back(4);                 // DEVINFO
    t.push_back(device_id); t.push_back(hwrev);
    t.push_back(0x00000001u);                        // BCREV
    t.push_back(0x00000001u);                        // ROM_DRVLIB_REV
    t.push_back(12); t.push_back(8);                 // DIEREC
    for (int i = 0; i < 8; ++i) t.push_back(0);
    t.push_back(3); t.push_back(16);                 // CS
    t.push_back(400); t.push_back(400);              // DCOIR_FCAL_RSEL04, _RSEL5
    for (int i = 0; i < 4; ++i) t.push_back(0);
    t.push_back(f(1.0f / 632.0f)); t.push_back(f(1.0f / 632.0f));   // DCOIR_CONSTK
    t.push_back(400); t.push_back(400);              // DCOER_FCAL
    for (int i = 0; i < 4; ++i) t.push_back(0);
    t.push_back(f(1.0f / 632.0f)); t.push_back(f(1.0f / 632.0f));   // DCOER_CONSTK
    t.push_back(5); t.push_back(24);                 // ADC14
    t.push_back(0x00008000u); t.push_back(0);        // ganancia 1,0 y offset 0
    for (int i = 0; i < 16; ++i) t.push_back(0);
    for (int i = 0; i < 6; ++i) t.push_back(0);      // sensor de temperatura
    t.push_back(8); t.push_back(3);                  // REF
    t.push_back(0); t.push_back(0); t.push_back(0);
    t.push_back(4); t.push_back(2);                  // FLASHCTL
    t.push_back(5); t.push_back(50);                 // pulsos max. de programa y borrado
    t.push_back(13); t.push_back(4);                 // RANDNUM
    t.push_back(0x5EED0001u); t.push_back(0x5EED0002u);
    t.push_back(0x5EED0003u); t.push_back(0x5EED0004u);
    t.push_back(15); t.push_back(4);                 // BSL
    for (int i = 0; i < 4; ++i) t.push_back(0xFFFFFFFFu);
    t.push_back(0x0BD0E11Du);                        // TLV_END
    return t;
}

// -----------------------------------------------------------------------------
// Lo que la memoria le pide al controlador. Está aquí, como interfaz, porque
// los dos se necesitan: la memoria pregunta al controlador cuántos estados de
// espera paga y si el banco está ocupado; el controlador escribe en la
// memoria al borrar y al programar.
// -----------------------------------------------------------------------------
class ctrl_flash_if {
public:
    virtual ~ctrl_flash_if() = default;
    virtual unsigned wait_banco(unsigned b) const = 0;
    virtual bool     buf_i(unsigned b) const = 0;
    virtual bool     buf_d(unsigned b) const = 0;
    // Hasta cuándo está ocupado el banco (borrando o programando).
    virtual sc_core::sc_time ocupado_hasta(unsigned b) const = 0;
    // Una escritura del bus en la Flash: la programa, o no, el controlador.
    // `t` es el tiempo anotado de quien escribe, y crece si tiene que esperar.
    virtual void escritura(uint32_t a, const uint8_t* d, unsigned n,
                           sc_core::sc_time& t) = 0;
};

// -----------------------------------------------------------------------------
// LA MEMORIA: principal + información, con los estados de espera
// -----------------------------------------------------------------------------
class FlashP4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<FlashP4> tsk{"tsk"};
    sc_core::sc_in<double> mclk_hz{"mclk_hz"};
    ctrl_flash_if* flctl = nullptr;

    FlashP4(sc_core::sc_module_name nm, uint32_t main_size,
            uint32_t device_id, uint32_t hwrev)
        : sc_core::sc_module(nm), main_(main_size, 0xFF), info_(p4::INFO_SIZE, 0xFF) {
        tsk.register_b_transport(this, &FlashP4::bt);
        tsk.register_transport_dbg(this, &FlashP4::dbg);
        const std::vector<uint32_t> t = tlv_sintetico(device_id, hwrev);
        std::memcpy(info_.data() + (p4::TLV_B - p4::INFO_B), t.data(), t.size() * 4);
    }

    uint32_t main_size() const { return uint32_t(main_.size()); }
    bool es_principal(uint32_t a) const { return a < main_.size(); }
    bool es_info(uint32_t a) const {
        return a >= p4::INFO_B && a < p4::INFO_B + p4::INFO_SIZE;
    }
    bool es_flash(uint32_t a) const { return es_principal(a) || es_info(a); }
    // Banco 0 = la primera mitad de cada memoria.
    unsigned banco_de(uint32_t a) const {
        if (es_principal(a)) return a < main_.size() / 2 ? 0u : 1u;
        return (a - p4::INFO_B) < p4::INFO_SIZE / 2 ? 0u : 1u;
    }
    // Para el cargador y las pruebas: escribe sin pasar por el controlador,
    // que es lo que hace un programador de fábrica.
    bool poke_byte(uint32_t a, uint8_t v) {
        uint8_t* p = celda(a);
        if (!p) return false;
        *p = v;
        return true;
    }
    uint8_t* celda(uint32_t a) {
        if (es_principal(a)) return &main_[a];
        if (es_info(a))      return &info_[a - p4::INFO_B];
        return nullptr;
    }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t* p = const_cast<FlashP4*>(this)->celda(a + i);
            v |= uint32_t(p ? *p : 0xFF) << (8 * i);
        }
        return v;
    }
    // Estadística: estados de espera pagados y esperas por banco ocupado.
    uint64_t esperas_pagadas() const { return n_esperas_; }
    uint64_t lecturas_detenidas() const { return n_detenidas_; }

private:
    std::vector<uint8_t> main_, info_;
    uint64_t n_esperas_ = 0, n_detenidas_ = 0;
    // La última línea de 128 bits leída por cada banco y tipo de acceso.
    uint32_t linea_[2][2] = {{~0u, ~0u}, {~0u, ~0u}};

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t a   = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        if (!celda(a) || !celda(a + len - 1) ||
            (es_principal(a) != es_principal(a + len - 1))) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.is_write()) {
            if (flctl) flctl->escritura(a, gp.get_data_ptr(), len, t);
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        const unsigned b = banco_de(a);
        // EL BANCO OCUPADO detiene al que lee hasta que acaba. El otro banco
        // se lee como siempre: es el «read while write» del silicio.
        if (flctl) {
            const sc_core::sc_time ahora = sc_core::sc_time_stamp() + t;
            const sc_core::sc_time fin = flctl->ocupado_hasta(b);
            if (fin > ahora) { t += fin - ahora; ++n_detenidas_; }
        }
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) d[i] = *celda(a + i);
        // Estados de espera
        if (flctl) {
            const AhbExt* e = gp.get_extension<AhbExt>();
            const bool instr = e && e->instr;
            const bool buf = instr ? flctl->buf_i(b) : flctl->buf_d(b);
            const uint32_t linea = a >> 4;
            uint32_t& ult = linea_[b][instr ? 0 : 1];
            const bool acierto = buf && ult == linea;
            ult = linea;
            const unsigned w = flctl->wait_banco(b);
            const double f = mclk_hz.read();
            if (!acierto && w && f > 0.0) {
                n_esperas_ += w;
                t += sc_core::sc_time(double(w) * 1.0e12 / f, sc_core::SC_PS);
            }
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    // El depurador lee y escribe la memoria tal cual, sin controlador: es lo
    // que hace `transport_dbg` en todo el modelo.
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) {
            uint8_t* p = celda(a + i);
            if (!p) return i;
            if (gp.is_read()) d[i] = *p;
            else              *p = d[i];
        }
        return len;
    }
};

// -----------------------------------------------------------------------------
// EL FLCTL: los registros [H, FLCTL_Type]
// -----------------------------------------------------------------------------
class FlctlP4 : public BusSlave, public ctrl_flash_if {
public:
    enum : uint32_t {
        R_POWER_STAT = 0x000, R_BANK0_RDCTL = 0x010, R_BANK1_RDCTL = 0x014,
        R_RDBRST_CTLSTAT = 0x020, R_RDBRST_STARTADDR = 0x024, R_RDBRST_LEN = 0x028,
        R_RDBRST_FAILADDR = 0x03C, R_RDBRST_FAILCNT = 0x040,
        R_PRG_CTLSTAT = 0x050, R_PRGBRST_CTLSTAT = 0x054, R_PRGBRST_STARTADDR = 0x058,
        R_PRGBRST_DATA = 0x060,          // 16 palabras, hasta 0x09C
        R_ERASE_CTLSTAT = 0x0A0, R_ERASE_SECTADDR = 0x0A4,
        R_BANK0_INFO_WEPROT = 0x0B0, R_BANK0_MAIN_WEPROT = 0x0B4,
        R_BANK1_INFO_WEPROT = 0x0C0, R_BANK1_MAIN_WEPROT = 0x0C4,
        R_BMRK_CTLSTAT = 0x0D0, R_BMRK_IFETCH = 0x0D4, R_BMRK_DREAD = 0x0D8,
        R_BMRK_CMP = 0x0DC,
        R_IFG = 0x0F0, R_IE = 0x0F4, R_CLRIFG = 0x0F8, R_SETIFG = 0x0FC,
        R_TIMCTL0 = 0x100, R_TIMCTL_FIN = 0x124
    };
    static constexpr uint32_t RDCTL_BUFI = 1u << 4, RDCTL_BUFD = 1u << 5;
    // PRG_CTLSTAT [H]
    static constexpr uint32_t PRG_ENABLE = 1u << 0, PRG_MODE = 1u << 1,
                              PRG_VER_PRE = 1u << 2, PRG_VER_PST = 1u << 3;
    // PRGBRST_CTLSTAT [H]
    static constexpr uint32_t BRST_START = 1u << 0, BRST_AUTO_PRE = 1u << 6,
                              BRST_AUTO_PST = 1u << 7, BRST_PRE_ERR = 1u << 19,
                              BRST_PST_ERR = 1u << 20, BRST_ADDR_ERR = 1u << 21,
                              BRST_CLR_STAT = 1u << 23;
    // ERASE_CTLSTAT [H]
    static constexpr uint32_t ERASE_START = 1u << 0, ERASE_MODE_MASA = 1u << 1,
                              ERASE_ADDR_ERR = 1u << 18, ERASE_CLR_STAT = 1u << 19;
    // IFG [H]
    static constexpr uint32_t IFG_RDBRST = 1u << 0, IFG_AVPRE = 1u << 1,
                              IFG_AVPST = 1u << 2, IFG_PRG = 1u << 3,
                              IFG_PRGB = 1u << 4, IFG_ERASE = 1u << 5,
                              IFG_BMRK = 1u << 8, IFG_PRG_ERR = 1u << 9;
    static constexpr uint32_t IFG_TODOS = 0x33Fu;

    // Los tiempos de modelo [P-16 n]
    sc_core::sc_time t_palabra{30, sc_core::SC_US};
    sc_core::sc_time t_rafaga_128{30, sc_core::SC_US};
    sc_core::sc_time t_sector{5, sc_core::SC_MS};
    sc_core::sc_time t_masivo{40, sc_core::SC_MS};

    sc_core::sc_out<bool> irq{"irq"};            // FLCTL_IRQn

    // Quien quiere saber que han cambiado los estados de espera: el vigilante
    // de topes del SoC, que avisa si no bastan para la frecuencia de MCLK.
    std::function<void()> al_cambiar;
    // La memoria que se borra y se programa
    FlashP4* mem = nullptr;

    explicit FlctlP4(sc_core::sc_module_name nm)
        : BusSlave(nm, p4::FLCTL_B, 0x400) {
        SC_HAS_PROCESS(FlctlP4);
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(fin_proc);   sensitive << fin_ev_; dont_initialize();
        SC_METHOD(irq_proc);   sensitive << irq_ev_;
        valores_de_reset();
    }

    // --- ctrl_flash_if ------------------------------------------------------
    unsigned wait_banco(unsigned b) const override { return (rdctl_[b & 1u] >> 12) & 0xFu; }
    bool     buf_i(unsigned b) const override { return (rdctl_[b & 1u] & RDCTL_BUFI) != 0; }
    bool     buf_d(unsigned b) const override { return (rdctl_[b & 1u] & RDCTL_BUFD) != 0; }
    sc_core::sc_time ocupado_hasta(unsigned b) const override {
        return (op_.activa && (op_.bancos & (1u << (b & 1u)))) ? op_.fin
                                                               : sc_core::SC_ZERO_TIME;
    }
    void escritura(uint32_t a, const uint8_t* d, unsigned n, sc_core::sc_time& t) override;

    // El MÁS LENTO de los dos bancos manda: el programa vive en los dos.
    unsigned wait_minimo() const {
        const unsigned a = wait_banco(0), b = wait_banco(1);
        return a < b ? a : b;
    }

    // --- Lo que mira el banco de pruebas -------------------------------------
    // ¿Está protegido el sector de esta dirección?
    bool protegido(uint32_t a) const {
        unsigned reg = 0, bit = 0;
        if (!sector_de(a, reg, bit)) return true;
        return (regs_[reg / 4] >> bit) & 1u;
    }
    unsigned escrituras_ignoradas() const { return n_ignoradas_; }
    unsigned palabras_programadas() const { return n_palabras_; }
    unsigned sectores_borrados()    const { return n_sectores_; }
    uint32_t ifg() const { return regs_[R_IFG / 4]; }

protected:
    uint32_t rdctl_[2] = {0, 0};
    uint32_t regs_[0x124 / 4 + 1] = {};
    uint32_t prg_ = 0;                  // PRG_CTLSTAT, la parte que se escribe
    uint32_t brst_cfg_ = 0, brst_st_ = 0;   // PRGBRST_CTLSTAT: config y estado
    uint32_t erase_cfg_ = 0, erase_st_ = 0; // ERASE_CTLSTAT: config y estado
    // El búfer del modo de palabra completa
    uint8_t  linea_buf_[16] = {};
    uint16_t linea_llena_ = 0;
    uint32_t linea_dir_ = ~0u;
    unsigned n_ignoradas_ = 0, n_palabras_ = 0, n_sectores_ = 0;
    bool     avisado_ = false;

    // LA OPERACIÓN EN CURSO. El controlador hace una cosa cada vez: una
    // escritura que llega mientras tanto ESPERA (se le alarga el tiempo a
    // quien escribe), igual que una lectura del banco ocupado.
    enum class Op { Palabra, Rafaga, Sector, Masivo };
    struct OpEnCurso {
        bool activa = false;
        Op tipo = Op::Palabra;
        sc_core::sc_time inicio, fin;
        unsigned bancos = 0;            // máscara: 1 banco 0, 2 banco 1
        unsigned palabras = 0;          // en una ráfaga
        uint32_t ifg_al_acabar = 0;
    } op_;
    sc_core::sc_event fin_ev_, irq_ev_;

    void valores_de_reset() {
        for (uint32_t& r : regs_) r = 0;
        rdctl_[0] = rdctl_[1] = 0;
        prg_ = brst_cfg_ = brst_st_ = erase_cfg_ = erase_st_ = 0;
        linea_llena_ = 0; linea_dir_ = ~0u;
        op_ = OpEnCurso();
        // Todo protegido contra escritura y borrado al salir del reset: hay
        // que desprotegerlo a propósito. [1F: TRM; P-16 e]
        regs_[R_BANK0_MAIN_WEPROT / 4] = 0xFFFFFFFFu;
        regs_[R_BANK1_MAIN_WEPROT / 4] = 0xFFFFFFFFu;
        regs_[R_BANK0_INFO_WEPROT / 4] = 0x3u;
        regs_[R_BANK1_INFO_WEPROT / 4] = 0x3u;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        if (al_cambiar) al_cambiar();
    }
    void irq_proc() { irq.write((regs_[R_IFG / 4] & regs_[R_IE / 4] & IFG_TODOS) != 0); }
    void pon_ifg(uint32_t m) { regs_[R_IFG / 4] |= m; irq_ev_.notify(sc_core::SC_ZERO_TIME); }

    // Una operación terminada deja su huella: el IFG, y los estados.
    void acaba_op() {
        if (!op_.activa) return;
        op_.activa = false;
        switch (op_.tipo) {
            case Op::Sector: case Op::Masivo: erase_st_ = (erase_st_ & ~(3u << 16)) | (3u << 16); break;
            case Op::Rafaga: brst_st_ = (brst_st_ & ~(7u << 16)) | (7u << 16); break;
            case Op::Palabra: break;
        }
        pon_ifg(op_.ifg_al_acabar);
    }
    // Si la operación ya debería haber acabado cuando alguien mira, acaba.
    void al_dia(sc_core::sc_time ahora) {
        if (op_.activa && ahora >= op_.fin) acaba_op();
    }
    void fin_proc() { al_dia(sc_core::sc_time_stamp()); }

    // Empieza una operación en `inicio` (que puede ser más tarde que ahora:
    // esperando a la anterior). Devuelve cuándo acaba.
    sc_core::sc_time empieza(Op tipo, sc_core::sc_time inicio, sc_core::sc_time dura,
                             unsigned bancos, uint32_t ifg, unsigned palabras = 0) {
        al_dia(inicio);
        op_.activa = true; op_.tipo = tipo; op_.inicio = inicio;
        op_.fin = inicio + dura; op_.bancos = bancos;
        op_.palabras = palabras; op_.ifg_al_acabar = ifg;
        fin_ev_.notify(op_.fin - sc_core::sc_time_stamp());
        return op_.fin;
    }
    // Cuándo puede empezar algo que se pide `ahora`: cuando acabe lo que haya.
    sc_core::sc_time libre_desde(sc_core::sc_time ahora) const {
        return (op_.activa && op_.fin > ahora) ? op_.fin : ahora;
    }

    // --- Direcciones, sectores y protección ----------------------------------
    // El registro de protección y el bit de una dirección; false si no es Flash.
    bool sector_de(uint32_t a, unsigned& reg, unsigned& bit) const {
        if (!mem) return false;
        if (mem->es_principal(a)) {
            const uint32_t mitad = mem->main_size() / 2;
            const unsigned b = a < mitad ? 0u : 1u;
            reg = b ? R_BANK1_MAIN_WEPROT : R_BANK0_MAIN_WEPROT;
            bit = ((a % mitad) / 0x1000u) & 31u;
            return true;
        }
        if (mem->es_info(a)) {
            const uint32_t o = a - p4::INFO_B;
            const unsigned b = o < p4::INFO_SIZE / 2 ? 0u : 1u;
            reg = b ? R_BANK1_INFO_WEPROT : R_BANK0_INFO_WEPROT;
            bit = (o % (p4::INFO_SIZE / 2)) / 0x1000u;
            return true;
        }
        return false;
    }
    static bool es_tlv(uint32_t a) {
        return a >= p4::TLV_B && a < p4::TLV_B + 0x1000u;
    }
    // Las direcciones de ERASE_SECTADDR y PRGBRST_STARTADDR, según TYPE: la
    // de la información se acepta absoluta (0x0020_xxxx) o relativa.
    static uint32_t absoluta(uint32_t a, unsigned tipo) {
        if (tipo == 1 && a < p4::INFO_SIZE) return a + p4::INFO_B;
        return a;
    }
    // Programa: solo puede poner bits a cero. Devuelve si quedó lo pedido.
    bool programa(uint32_t a, const uint8_t* d, unsigned n) {
        bool igual = true;
        for (unsigned i = 0; i < n; ++i) {
            uint8_t* c = mem->celda(a + i);
            if (!c) continue;
            *c = uint8_t(*c & d[i]);
            if (*c != d[i]) igual = false;
        }
        n_palabras_ += (n + 3) / 4;
        return igual;
    }
    void borra_sector(uint32_t a) {
        const uint32_t base = a & ~0xFFFu;
        for (uint32_t i = 0; i < 0x1000u; ++i)
            if (uint8_t* c = mem->celda(base + i)) *c = 0xFF;
        ++n_sectores_;
    }

    // --- Las tres operaciones --------------------------------------------------
    void inicia_borrado() {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        const unsigned tipo = (erase_cfg_ >> 2) & 3u;
        const sc_core::sc_time ini = libre_desde(ahora);
        al_dia(ini);
        if (erase_cfg_ & ERASE_MODE_MASA) {
            // Todo lo no protegido, de las dos memorias, menos el TLV
            const uint32_t m = mem->main_size();
            for (uint32_t a = 0; a < m; a += 0x1000u)
                if (!protegido(a)) borra_sector(a);
            for (uint32_t a = p4::INFO_B; a < p4::INFO_B + p4::INFO_SIZE; a += 0x1000u)
                if (!es_tlv(a) && !protegido(a)) borra_sector(a);
            erase_st_ = 2u << 16;
            empieza(Op::Masivo, ini, t_masivo, 3u, IFG_ERASE);
            return;
        }
        const uint32_t a = absoluta(regs_[R_ERASE_SECTADDR / 4], tipo);
        unsigned reg, bit;
        const bool tipo_ok = (tipo == 0 && mem->es_principal(a)) ||
                             (tipo == 1 && mem->es_info(a));
        if (!tipo_ok || !sector_de(a, reg, bit) || es_tlv(a)) {
            // Reservado o fuera: no se borra nada, y se dice
            erase_st_ = ERASE_ADDR_ERR | (3u << 16);
            pon_ifg(IFG_ERASE);
            return;
        }
        // Un sector protegido no se borra, pero la operación «acaba» igual, y
        // en el acto: es el firmware el que tiene que comprobarlo leyendo.
        // [1F; P-16 e]
        if (protegido(a)) {
            erase_st_ = 3u << 16;
            pon_ifg(IFG_ERASE);
            return;
        }
        borra_sector(a);
        erase_st_ = 2u << 16;
        empieza(Op::Sector, ini, t_sector, 1u << mem->banco_de(a), IFG_ERASE);
    }

    void inicia_rafaga() {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        const unsigned tipo = (brst_cfg_ >> 1) & 3u;
        const unsigned len  = (brst_cfg_ >> 3) & 7u;
        const uint32_t a = absoluta(regs_[R_PRGBRST_STARTADDR / 4], tipo);
        const sc_core::sc_time ini = libre_desde(ahora);
        al_dia(ini);
        const uint32_t fin = a + 16u * len;
        const bool ok = len >= 1 && len <= 4 && (a & 15u) == 0 &&
                        ((tipo == 0 && mem->es_principal(a) && mem->es_principal(fin - 1)) ||
                         (tipo == 1 && mem->es_info(a) && mem->es_info(fin - 1))) &&
                        !es_tlv(a) && !es_tlv(fin - 1);
        if (!ok) {
            brst_st_ = BRST_ADDR_ERR | (7u << 16);
            pon_ifg(IFG_PRGB);
            return;
        }
        for (uint32_t s = a; s < fin; s += 16)
            if (protegido(s)) {
                brst_st_ = (7u << 16);
                pon_ifg(IFG_PRGB | IFG_PRG_ERR);
                return;
            }
        uint8_t d[64];
        for (unsigned i = 0; i < 16; ++i) {
            const uint32_t w = regs_[(R_PRGBRST_DATA / 4) + i];
            for (unsigned k = 0; k < 4; ++k) d[4 * i + k] = uint8_t(w >> (8 * k));
        }
        const bool igual = programa(a, d, 16u * len);
        uint32_t ifg = IFG_PRGB;
        brst_st_ = 2u << 16;
        if (!igual && (brst_cfg_ & BRST_AUTO_PST)) { brst_st_ |= BRST_PST_ERR; ifg |= IFG_AVPST; }
        empieza(Op::Rafaga, ini, t_rafaga_128 * double(len),
                1u << mem->banco_de(a), ifg, len);
    }

    uint32_t estado_rafaga(sc_core::sc_time ahora) const {
        uint32_t st = brst_st_;
        if (op_.activa && op_.tipo == Op::Rafaga) {
            if (ahora < op_.inicio) st = (st & ~(7u << 16)) | (1u << 16);   // pendiente
            else {
                unsigned k = unsigned((ahora - op_.inicio) / t_rafaga_128);
                if (k >= op_.palabras) k = op_.palabras - 1;
                st = (st & ~(7u << 16)) | ((2u + k) << 16);
            }
        }
        return st;
    }

    uint32_t reg_read(uint32_t off) override {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        al_dia(ahora);
        switch (off) {
            // PSTAT = 4 (activa) y las cuatro referencias estables.
            // [Tock, flctl.rs: PSTAT 4 = Active; 1F para los demás bits]
            case R_POWER_STAT:  return 0x0000007Cu;
            case R_BANK0_RDCTL: return rdctl_[0] | ((rdctl_[0] & 0xFu) << 16);
            case R_BANK1_RDCTL: return rdctl_[1] | ((rdctl_[1] & 0xFu) << 16);
            case R_PRG_CTLSTAT: {
                uint32_t v = prg_;
                if (op_.activa && op_.tipo == Op::Palabra)
                    v |= (ahora < op_.inicio ? 1u : 2u) << 16 |
                         ((op_.bancos & 2u) ? (1u << 18) : 0u);
                return v;
            }
            case R_PRGBRST_CTLSTAT: return brst_cfg_ | estado_rafaga(ahora);
            case R_ERASE_CTLSTAT: {
                uint32_t st = erase_st_;
                if (op_.activa && (op_.tipo == Op::Sector || op_.tipo == Op::Masivo) &&
                    ahora < op_.inicio)
                    st = (st & ~(3u << 16)) | (1u << 16);
                return erase_cfg_ | st;
            }
            case R_CLRIFG: case R_SETIFG: return 0;
            default:
                return off < sizeof(regs_) ? regs_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        auto mezcla = [&](uint32_t cur) { return (cur & ~m) | (v & m); };
        al_dia(sc_core::sc_time_stamp());
        switch (off) {
            case R_POWER_STAT: return;                       // solo lectura
            case R_BANK0_RDCTL: case R_BANK1_RDCTL: {
                uint32_t& r = rdctl_[off == R_BANK1_RDCTL ? 1 : 0];
                r = mezcla(r) & 0x0000F03Fu;
                if (al_cambiar) al_cambiar();
                return;
            }
            case R_PRG_CTLSTAT:
                prg_ = mezcla(prg_) & 0xFu;
                linea_llena_ = 0; linea_dir_ = ~0u;
                return;
            case R_PRGBRST_CTLSTAT: {
                const uint32_t w = v & m;
                if (w & BRST_CLR_STAT) brst_st_ = 0;
                brst_cfg_ = mezcla(brst_cfg_) & 0xFEu;
                if (w & BRST_START) inicia_rafaga();
                return;
            }
            case R_ERASE_CTLSTAT: {
                const uint32_t w = v & m;
                if (w & ERASE_CLR_STAT) erase_st_ = 0;
                erase_cfg_ = mezcla(erase_cfg_) & 0xEu;
                if (w & ERASE_START) inicia_borrado();
                return;
            }
            case R_ERASE_SECTADDR:
                regs_[off / 4] = mezcla(regs_[off / 4]) & 0x003FFFFFu;
                return;
            case R_PRGBRST_STARTADDR:
                regs_[off / 4] = mezcla(regs_[off / 4]) & 0x003FFFFFu;
                return;
            case R_BANK0_INFO_WEPROT:
                // El sector 1 es el TLV: no se desprotege [OO]
                regs_[off / 4] = (mezcla(regs_[off / 4]) & 0x3u) | 0x2u;
                return;
            case R_BANK1_INFO_WEPROT:
                regs_[off / 4] = mezcla(regs_[off / 4]) & 0x3u;
                return;
            case R_IFG:    regs_[R_IFG / 4] = mezcla(regs_[R_IFG / 4]) & IFG_TODOS;
                           irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_IE:     regs_[R_IE / 4] = mezcla(regs_[R_IE / 4]) & IFG_TODOS;
                           irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_CLRIFG: regs_[R_IFG / 4] &= ~(v & m);
                           irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_SETIFG: pon_ifg(v & m & IFG_TODOS); return;
            default:
                if (off >= R_TIMCTL0) return;                // solo lectura
                if (off < sizeof(regs_)) regs_[off / 4] = mezcla(regs_[off / 4]);
                return;
        }
    }
};

// Una escritura del bus en la Flash. Sin ENABLE no programa nada -y se dice
// una vez-; con él, programa la palabra o la junta en la línea de 128 bits.
inline void FlctlP4::escritura(uint32_t a, const uint8_t* d, unsigned n,
                               sc_core::sc_time& t) {
    const sc_core::sc_time ahora = sc_core::sc_time_stamp() + t;
    al_dia(sc_core::sc_time_stamp());
    if (!(prg_ & PRG_ENABLE)) {
        ++n_ignoradas_;
        if (!avisado_) {
            avisado_ = true;
            SC_REPORT_WARNING("msp432p4/flash",
                "escritura en la Flash ignorada: sin FLCTL_PRG_CTLSTAT.ENABLE no se "
                "programa nada (o con FLCTL_PRGBRST, en rafaga)");
        }
        return;
    }
    // Espera a que el controlador esté libre: quien escribe se queda ahí
    const sc_core::sc_time ini = libre_desde(ahora);
    if (ini > ahora) t += ini - ahora;
    al_dia(ini);
    if (protegido(a) || es_tlv(a)) {
        pon_ifg(IFG_PRG_ERR);
        return;
    }
    if (prg_ & PRG_MODE) {
        // PALABRA COMPLETA: se juntan los 16 bytes de la línea. Una escritura
        // a otra línea antes de llenarla tira lo junto y da PRG_ERR. [1F]
        const uint32_t lin = a & ~15u;
        if (linea_dir_ != lin) {
            if (linea_llena_) pon_ifg(IFG_PRG_ERR);
            linea_dir_ = lin; linea_llena_ = 0;
            std::memset(linea_buf_, 0xFF, sizeof linea_buf_);
        }
        for (unsigned i = 0; i < n && ((a + i) & ~15u) == lin; ++i) {
            linea_buf_[(a + i) & 15u] = d[i];
            linea_llena_ = uint16_t(linea_llena_ | (1u << ((a + i) & 15u)));
        }
        if (linea_llena_ != 0xFFFFu) return;
        const bool igual = programa(lin, linea_buf_, 16);
        linea_llena_ = 0; linea_dir_ = ~0u;
        uint32_t ifg = IFG_PRG;
        if (!igual && (prg_ & PRG_VER_PST)) ifg |= IFG_AVPST;
        empieza(Op::Palabra, ini, t_palabra, 1u << mem->banco_de(a), ifg);
        return;
    }
    // INMEDIATA: cada escritura es una programación
    const bool igual = programa(a, d, n);
    uint32_t ifg = IFG_PRG;
    if (!igual && (prg_ & PRG_VER_PST)) ifg |= IFG_AVPST;
    empieza(Op::Palabra, ini, t_palabra, 1u << mem->banco_de(a), ifg);
}

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_FLCTL_H
