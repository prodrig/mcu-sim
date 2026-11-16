// =============================================================================
// gdb_m430.h — El servidor de GDB de un MSP430 (M2 de doc/analisis_msp430.md, §8)
//
// Sobre GdbRspBase, como el de RISC-V, con dos caminos detrás (`m430_dbg_if`):
//
//   --gdb-dap  `DbgDirectoM430`: el núcleo y el EEM en directo, una llamada por
//              acceso. Rápido.
//   --gdb      `DbgSondaM430`: la sonda de bancada (m430/sonda), por los pines,
//              en JTAG de 4 hilos o en Spy-Bi-Wire: cientos de flancos por
//              acceso, como con una sonda de verdad.
//
// Lo de arriba es lo mismo para los dos:
//   * registros: 16, de 4 bytes en el paquete `g` (los «raw registers» de
//     msp430-elf-gdb, uint32 en las tres variantes: comprobado con
//     `maint print raw-registers`);
//   * Z0: por software, escribiendo 0x4343 (CLR.B R3) en la RAM y con el
//     disparador 0 del EEM a «búsqueda con MDB = 0x4343»
//     [SoftwareBreakpoints430.cpp]; Z1, un disparador de búsqueda en el MAB;
//     Z2-Z4, disparadores de lectura o escritura en el MAB. Cada uno en su
//     combinación, todas en BREAKREACT. Tantos como dé el EEM;
//   * paso, Ctrl-C, `monitor reset` y `monitor reset halt` (el POR por JTAG,
//     parada en el vector de reset).
// =============================================================================
#ifndef STM32_M430_GDB_GDB_M430_H
#define STM32_M430_GDB_GDB_M430_H

#include <systemc>
#include <map>
#include <string>
#include "../../common/gdb_rsp.h"
#include "../cpu/nucleo_m430.h"
#include "../debug/eem.h"
#include "../sonda/sonda_m430.h"

namespace stm32 {

struct m430_dbg_if {
    virtual ~m430_dbg_if() = default;
    virtual bool enganchar() = 0;
    virtual bool parado() = 0;
    virtual void parar() = 0;
    virtual void seguir() = 0;
    virtual void paso() = 0;
    virtual uint32_t reg(unsigned n) = 0;
    virtual void pon_reg(unsigned n, uint32_t v) = 0;
    virtual uint16_t lee16(uint32_t a) = 0;
    virtual void escribe16(uint32_t a, uint16_t v) = 0;
    virtual uint16_t eem_lee(uint8_t dir) = 0;
    virtual void eem_escribe(uint8_t dir, uint16_t v) = 0;
    virtual unsigned disparadores() const = 0;
    virtual void reset(bool parar) = 0;
};

// --- --gdb-dap: el núcleo en directo --------------------------------------------
class DbgDirectoM430 : public m430_dbg_if {
public:
    DbgDirectoM430(NucleoM430& n, EemM430& e) : n_(n), e_(e) {}
    bool enganchar() override { return true; }
    bool parado() override { return n_.parado(); }
    void parar() override {
        n_.pide_parada(true);
        while (!n_.parado()) sc_core::wait(n_.ev_parado());
    }
    void seguir() override { n_.pide_parada(false); }
    void paso() override {
        n_.pide_parada(true);
        n_.pide_paso();
        sc_core::wait(n_.ev_parado());
    }
    uint32_t reg(unsigned r) override { return n_.cpu.reg(r); }
    void pon_reg(unsigned r, uint32_t v) override { n_.cpu.pon_reg(r, v); }
    uint16_t lee16(uint32_t a) override { return n_.lee_dbg(a, false); }
    void escribe16(uint32_t a, uint16_t v) override { n_.escribe_dbg(a, v, false); }
    uint16_t eem_lee(uint8_t d) override { return e_.lee(d); }
    void eem_escribe(uint8_t d, uint16_t v) override { e_.escribe(d, v); }
    unsigned disparadores() const override { return e_.disparadores(); }
    void reset(bool parar) override { if (al_reset) al_reset(); if (parar) n_.pide_parada(true);
                                      sc_core::wait(10, sc_core::SC_US); if (parar) this->parar(); }
    std::function<void()> al_reset;
private:
    NucleoM430& n_;
    EemM430& e_;
};

// --- --gdb: la sonda de bancada por los pines -----------------------------------
// Con la CPU parada, la sonda la tiene bajo JTAG (TCE1) y guarda el contexto
// (PC y SR): como SyncJtag_*_SaveContext; al seguir, lo restaura y la suelta.
class DbgSondaM430 : public m430_dbg_if {
public:
    DbgSondaM430(SondaM430& s, unsigned disparadores) : s_(s), n_tb_(disparadores) {}
    bool enganchar() override {
        if (s_.conecta() != 0x89) return false;
        return parar_(), true;
    }
    bool parado() override {
        if (bajo_jtag_) return true;
        // El EEM dice si ha parado la CPU (E_STOPPED en GENCTRL)
        if (s_.lee_eem(0x82) & EemM430::E_STOPPED) { parar_(); return true; }
        return false;
    }
    void parar() override { parar_(); }
    void seguir() override {
        if (!bajo_jtag_) return;
        s_.escribe_reg(2, sr_);
        s_.pon_pc(uint16_t(pc_));
        s_.escribe_eem(0x82, uint16_t(s_.lee_eem(0x82) | EemM430::CLEAR_STOP));
        s_.suelta_cpu();
        bajo_jtag_ = false;
    }
    void paso() override {
        if (!bajo_jtag_) parar_();
        s_.escribe_reg(2, sr_);
        s_.pon_pc(uint16_t(pc_));
        s_.un_paso();
        pc_ = s_.lee_pc();
        sr_ = s_.lee_reg(2);
    }
    uint32_t reg(unsigned r) override {
        if (r == 0) return pc_;
        if (r == 2) return sr_;
        if (r == 3) return 0;
        return s_.lee_reg(r);
    }
    void pon_reg(unsigned r, uint32_t v) override {
        if (r == 0) { pc_ = v & 0xFFFEu; return; }
        if (r == 2) { sr_ = uint16_t(v); return; }
        if (r == 3) return;
        s_.escribe_reg(r, uint16_t(v));
    }
    uint16_t lee16(uint32_t a) override { return s_.lee_palabra(uint16_t(a)); }
    void escribe16(uint32_t a, uint16_t v) override { s_.escribe_palabra(uint16_t(a), v); }
    uint16_t eem_lee(uint8_t d) override { return s_.lee_eem(d); }
    void eem_escribe(uint8_t d, uint16_t v) override { s_.escribe_eem(d, v); }
    unsigned disparadores() const override { return n_tb_; }
    void reset(bool parar) override {
        s_.sincroniza(true);
        bajo_jtag_ = true;
        pc_ = s_.lee_pc();
        sr_ = s_.lee_reg(2);
        if (!parar) seguir();
    }
private:
    SondaM430& s_;
    unsigned n_tb_;
    bool bajo_jtag_ = false;
    uint32_t pc_ = 0;
    uint16_t sr_ = 0;
    void parar_() {
        if (bajo_jtag_) return;
        s_.sincroniza(false);
        bajo_jtag_ = true;
        pc_ = s_.lee_pc();
        sr_ = s_.lee_reg(2);
    }
};

// --- El protocolo ------------------------------------------------------------------
class GdbRspM430 : public GdbRspBase {
protected:
    GdbRspM430(m430_dbg_if& d, unsigned puerto, const char* etiqueta) : GdbRspBase(puerto, etiqueta), d_(d) {
        pon_programador(&ProgramadorNinguno430::el());
    }
    class ProgramadorNinguno430 : public programador_nv_if {
    public:
        static ProgramadorNinguno430& el() { static ProgramadorNinguno430 p; return p; }
        const char* nombre() const override { return "ninguno"; }
        int sector_de(uint32_t) const override { return -1; }
        uint32_t tam_sector(int) const override { return 0; }
        unsigned n_sectores() const override { return 0; }
        bool borrar(acceso_nv_if&, uint32_t, uint32_t) override { return false; }
        bool programar(acceso_nv_if&, uint32_t, const uint8_t*, unsigned, unsigned&) override { return false; }
        void terminar(acceso_nv_if&) override {}
    };

    bool arq_enganchar() override {
        if (!d_.enganchar()) return false;
        sw_.clear();
        for (auto& t : tb_) t = Tb();
        tb_.assign(d_.disparadores(), Tb());
        d_.eem_escribe(0x82, EemM430::EEM_EN | 0x000C);         // EEM_EN | EMU_CLK_EN | EMU_FEAT_EN
        d_.eem_escribe(0x88, 0x0026);                          // GENCLKCTRL por omisión [§8.2]
        d_.eem_escribe(0x80, 0);
        return true;
    }
    bool arq_parado() override { return d_.parado(); }
    void arq_parar() override { d_.parar(); }
    void arq_seguir() override { d_.seguir(); }
    void arq_paso() override { d_.paso(); }
    std::string arq_parada(unsigned senal) override {
        const uint32_t pc = d_.reg(0);
        std::string s = "T0";
        s.push_back(hexd(senal & 0xFu));
        if (senal == 5) {
            const uint16_t f = d_.eem_lee(0x8E);
            if (f) d_.eem_escribe(0x8E, f);
            if (sw_.count(pc)) s += "swbreak:;";
            else for (unsigned i = 1; i < tb_.size(); ++i) {
                if (!tb_[i].usado || !((f >> i) & 1u)) continue;
                if (tb_[i].tipo == 1) s += "hwbreak:;";
                else s += std::string(tb_[i].tipo == 2 ? "watch" : tb_[i].tipo == 3 ? "rwatch" : "awatch") +
                          ":" + hex_be(tb_[i].dir) + ";";
                break;
            }
        }
        s += "00:" + hex32(pc) + ";thread:1;";
        return s;
    }
    unsigned arq_n_regs() const override { return 16; }
    uint32_t arq_reg(unsigned n) override { return n < 16 ? d_.reg(n) : 0; }
    void arq_set_reg(unsigned n, uint32_t v) override { if (n < 16) d_.pon_reg(n, v); }
    const std::string& arq_target_xml() const override { static const std::string x; return x; }

    bool arq_poner_bp(uint32_t a, unsigned tipo, unsigned) override {
        if (tipo == 1) return pon_tb(a, 1);
        if (sw_.count(a)) return true;
        sw_[a] = d_.lee16(a);
        d_.escribe16(a, 0x4343);                               // CLR.B R3
        // El disparador 0: búsqueda con MDB = 0x4343
        if (!tb_.empty() && !tb_[0].usado) {
            tb_[0].usado = true;
            d_.eem_escribe(0x00, 0x4343);
            d_.eem_escribe(0x02, 0x0001);                      // MDB, TRIG_0, =
            d_.eem_escribe(0x04, 0x0000);
            d_.eem_escribe(0x06, 0x0001);                      // combinación 0
            react();
        }
        return true;
    }
    bool arq_quitar_bp(uint32_t a, unsigned tipo, unsigned) override {
        if (tipo == 1) return quita_tb(a, 1);
        auto it = sw_.find(a);
        if (it == sw_.end()) return true;
        d_.escribe16(a, it->second);
        sw_.erase(it);
        if (sw_.empty() && !tb_.empty()) { tb_[0].usado = false; d_.eem_escribe(0x06, 0); react(); }
        return true;
    }
    bool arq_poner_wp(uint32_t a, unsigned tipo, unsigned) override { return pon_tb(a, tipo); }
    bool arq_quitar_wp(uint32_t a, unsigned tipo, unsigned) override { return quita_tb(a, tipo); }
    void arq_reset() override { d_.reset(false); }
    bool arq_monitor(const std::string& o) override {
        if (o == "reset halt") { d_.reset(true); return true; }
        if (o == "reset") { d_.reset(false); return true; }
        return false;
    }
    const char* arq_nombre_xml() const override { return "msp430"; }
    const char* arq_nombre_humano() const override { return "MSP430"; }
    const char* arq_gdb_recomendado() const override { return "msp430-elf-gdb"; }

    // La memoria: palabras de 16 bits (una de 32, en dos)
    bool mem_leer(uint32_t a, uint32_t& v) override {
        v = uint32_t(d_.lee16(a)) | (uint32_t(d_.lee16(a + 2)) << 16);
        return true;
    }
    bool mem_escribir(uint32_t a, uint32_t v) override {
        d_.escribe16(a, uint16_t(v));
        d_.escribe16(a + 2, uint16_t(v >> 16));
        return true;
    }
    bool mem_escribir16(uint32_t a, uint16_t v) override { d_.escribe16(a, v); return true; }

private:
    static std::string hex_be(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "%x", unsigned(v)); return b; }
    m430_dbg_if& d_;
    std::map<uint32_t, uint16_t> sw_;
    struct Tb { bool usado = false; unsigned tipo = 0; uint32_t dir = 0; };
    std::vector<Tb> tb_;

    void react() {
        uint16_t r = 0;
        for (unsigned i = 0; i < tb_.size(); ++i) if (tb_[i].usado) r = uint16_t(r | (1u << i));
        d_.eem_escribe(0x80, r);
    }
    // tipo: 1 búsqueda, 2 escritura, 3 lectura, 4 lectura o escritura
    bool pon_tb(uint32_t a, unsigned tipo) {
        for (unsigned i = 1; i < tb_.size(); ++i) {
            if (tb_[i].usado) continue;
            tb_[i] = Tb{true, tipo, a};
            const uint16_t trig = tipo == 1 ? 0x0000 : tipo == 2 ? 0x0026 : tipo == 3 ? 0x0024 : 0x0004;
            d_.eem_escribe(uint8_t(8 * i), uint16_t(a));
            d_.eem_escribe(uint8_t(8 * i + 2), trig);           // MAB, el tipo, =
            d_.eem_escribe(uint8_t(8 * i + 4), 0);
            d_.eem_escribe(uint8_t(8 * i + 6), uint16_t(1u << i));
            react();
            return true;
        }
        return false;
    }
    bool quita_tb(uint32_t a, unsigned tipo) {
        for (unsigned i = 1; i < tb_.size(); ++i)
            if (tb_[i].usado && tb_[i].dir == a && tb_[i].tipo == tipo) {
                tb_[i] = Tb();
                d_.eem_escribe(uint8_t(8 * i + 6), 0);
                react();
                return true;
            }
        return true;
    }
};

SC_MODULE(GdbStubM430), public GdbRspM430 {
    GdbStubM430(sc_core::sc_module_name nm, m430_dbg_if& d, unsigned puerto = 2000, const char* etiqueta = "gdb-430")
        : sc_core::sc_module(nm), GdbRspM430(d, puerto, etiqueta) {
        SC_HAS_PROCESS(GdbStubM430);
        SC_THREAD(run);
    }
private:
    void run() { servir(); }
};

} // namespace stm32
#endif // STM32_M430_GDB_GDB_M430_H
