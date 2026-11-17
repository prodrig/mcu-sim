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
#include <memory>
#include <vector>
#include <string>
#include "../../common/gdb_rsp.h"
#include "../cpu/nucleo_m430.h"
#include "../debug/eem.h"
#include "../sonda/sonda_m430.h"
#include "../sonda/sonda_xv2.h"

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
    // 32 bits: los valores de los disparadores llegan a 20 bits en CPUX/Xv2
    virtual void eem_escribe(uint8_t dir, uint32_t v) = 0;
    virtual unsigned disparadores() const = 0;
    virtual void reset(bool parar) = 0;
    // La Flash (el `load`, P-20): borrar un segmento o la principal entera y
    // programar palabras. Por omisión, no se sabe
    virtual bool flash_borra(uint32_t, bool /*masivo*/) { return false; }
    virtual bool flash_escribe(uint32_t, const uint16_t*, unsigned) { return false; }
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
    void eem_escribe(uint8_t d, uint32_t v) override { e_.escribe(d, v); }
    unsigned disparadores() const override { return e_.disparadores(); }
    void reset(bool parar) override { if (al_reset) al_reset(); if (parar) n_.pide_parada(true);
                                      sc_core::wait(10, sc_core::SC_US); if (parar) this->parar(); }
    std::function<void()> al_reset;
    // --gdb-dap: la Flash se escribe por debajo del controlador (sin tiempo),
    // como la memoria; el segmento es el que diga el programador
    unsigned tam_segmento = 512;
    // El tamaño del segmento de una dirección, si no es siempre el mismo (5xx:
    // 512 B en la principal y 128 B en la información)
    std::function<unsigned(uint32_t)> tam_de;
    bool flash_borra(uint32_t a, bool masivo) override {
        if (masivo) return false;
        const unsigned tam_segmento = tam_de ? tam_de(a) : this->tam_segmento;
        const uint32_t b = a & ~(tam_segmento - 1u);
        for (uint32_t x = b; x < b + tam_segmento; x += 2) n_.escribe_dbg(x, 0xFFFF, false);
        return true;
    }
    bool flash_escribe(uint32_t a, const uint16_t* w, unsigned n) override {
        for (unsigned i = 0; i < n; ++i) n_.escribe_dbg(a + 2 * i, uint16_t(n_.lee_dbg(a + 2 * i, false) & w[i]), false);
        return true;
    }
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
    void eem_escribe(uint8_t d, uint32_t v) override { s_.escribe_eem(d, uint16_t(v)); }
    unsigned disparadores() const override { return n_tb_; }
    // --gdb: la Flash por JTAG, con el controlador y TCLK (como el Replicator)
    bool flash_borra(uint32_t a, bool masivo) override {
        if (!bajo_jtag_) parar_();
        s_.borra_flash(masivo ? SondaM430::BORRA_PRINCIPAL : SondaM430::BORRA_SEGMENTO, uint16_t(a));
        return true;
    }
    bool flash_escribe(uint32_t a, const uint16_t* w, unsigned n) override {
        if (!bajo_jtag_) parar_();
        s_.escribe_flash(uint16_t(a), w, n);
        return true;
    }
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

// --- --gdb en Xv2 (5xx/6xx): la sonda de bancada con las secuencias de Xv2 -----------
// Lo mismo que DbgSondaM430, con el PC de 20 bits, el EEM de 32 y la Flash
// con las rutinas del Replicator430Xv2 en la RAM del objetivo.
class DbgSondaXv2 : public m430_dbg_if {
public:
    // `info_a`: el segmento A de la información (se desbloquea solo para él)
    DbgSondaXv2(SondaXv2& s, unsigned disparadores, uint32_t info_a_ini = 0x1980, uint32_t info_a_fin = 0x19FF)
        : s_(s), n_tb_(disparadores), a_ini_(info_a_ini), a_fin_(info_a_fin) {}
    bool enganchar() override {
        if (s_.conecta() != 0x91) return false;
        return parar_(), true;
    }
    bool parado() override {
        if (bajo_jtag_) return true;
        if (s_.lee_eem(0x82) & EemM430::E_STOPPED) { parar_(); return true; }
        return false;
    }
    void parar() override { parar_(); }
    void seguir() override {
        if (!bajo_jtag_) return;
        s_.pon_reg(2, sr_);
        s_.escribe_eem(0x82, (s_.lee_eem(0x82) & 0xFF7Fu) | EemM430::CLEAR_STOP);
        s_.suelta_en(pc_);
        bajo_jtag_ = false;
    }
    void paso() override {
        if (!bajo_jtag_) parar_();
        s_.pon_reg(2, sr_);
        s_.pon_pc(pc_);
        s_.un_paso();
        pc_ = s_.lee_pc();
        sr_ = uint16_t(s_.lee_reg(2));
    }
    uint32_t reg(unsigned r) override {
        if (r == 0) return pc_;
        if (r == 2) return sr_;
        if (r == 3) return 0;
        return s_.lee_reg(r);
    }
    void pon_reg(unsigned r, uint32_t v) override {
        if (r == 0) { pc_ = v & 0xFFFFEu; return; }
        if (r == 2) { sr_ = uint16_t(v); return; }
        if (r == 3) return;
        s_.pon_reg(r, v & 0xFFFFFu);
    }
    uint16_t lee16(uint32_t a) override { return s_.lee(a); }
    void escribe16(uint32_t a, uint16_t v) override { s_.escribe(a, v); }
    uint16_t eem_lee(uint8_t d) override { return uint16_t(s_.lee_eem(d)); }
    void eem_escribe(uint8_t d, uint32_t v) override { s_.escribe_eem(d, v); }
    unsigned disparadores() const override { return n_tb_; }
    bool flash_borra(uint32_t a, bool masivo) override {
        if (!bajo_jtag_) parar_();
        const uint32_t r = sin_eem_();
        const bool ok = s_.borra_flash(masivo ? 0xA504 : 0xA502, a, llave(a));
        s_.escribe_eem(0x80, r);
        recupera_();
        return ok;
    }
    bool flash_escribe(uint32_t a, const uint16_t* w, unsigned n) override {
        if (!bajo_jtag_) parar_();
        const uint32_t r = sin_eem_();
        const bool ok = s_.escribe_flash(a, w, n, llave(a));
        s_.escribe_eem(0x80, r);
        recupera_();
        return ok;
    }
    // El POR por JTAG (SyncJtag_AssertPor); la CPU, en su vector de reset
    void reset(bool parar) override {
        s_.sincroniza_por();
        recupera_();
        if (!parar) seguir();
    }
private:
    SondaXv2& s_;
    unsigned n_tb_;
    uint32_t a_ini_, a_fin_;
    bool bajo_jtag_ = false;
    uint32_t pc_ = 0;
    uint16_t sr_ = 0;
    uint16_t llave(uint32_t a) const { return (a >= a_ini_ && a <= a_fin_) ? 0xA508 : 0xA548; }
    // Las rutinas de la Flash corren libres: sin reacciones del EEM y sin su
    // parada (como la MSPDS, que las corre con el EEM apartado); devuelve
    // BREAKREACT, para ponerlo luego
    uint32_t sin_eem_() {
        const uint32_t r = s_.lee_eem(0x80);
        s_.escribe_eem(0x80, 0);
        s_.escribe_eem(0x82, (s_.lee_eem(0x82) & 0xFFFFu & ~uint32_t(EemM430::E_STOPPED)) | EemM430::CLEAR_STOP);
        s_.pon_reg(2, 0);                       // sin GIE ni CPUOFF (la CPU podía estar en LPM)
        return r;
    }
    void parar_() {
        if (bajo_jtag_) return;
        s_.sincroniza();
        bajo_jtag_ = true;
        pc_ = s_.lee_pc();
        sr_ = uint16_t(s_.lee_reg(2));
    }
    // Tras un POR (el de las rutinas de la Flash, o `monitor reset`): el PC,
    // el del vector de reset, como la MSPDS
    void recupera_() {
        bajo_jtag_ = true;
        pc_ = s_.lee(0xFFFE) & 0xFFFEu;
        s_.pon_pc(pc_);
        sr_ = uint16_t(s_.lee_reg(2));
    }
};

// --- El programador de la Flash de 1xx/2xx/4xx (FLASH2) ---------------------------
// Segmentos de 512 B en la principal y de 64 B en la información (sin el A,
// que guarda la calibración: LOCKA). Lo hace el camino de depuración que haya
// detrás (la sonda, por JTAG y TCLK; el directo, por debajo del controlador).
class ProgramadorF2xx : public programador_nv_if {
public:
    ProgramadorF2xx(m430_dbg_if& d, uint32_t principal_ini, uint32_t info_ini, uint32_t info_a_ini)
        : d_(d), p_ini_(principal_ini), i_ini_(info_ini), a_ini_(info_a_ini) {}
    const char* nombre() const override { return "MSP430 FLASH2 (1xx/2xx/4xx)"; }
    int sector_de(uint32_t a) const override {
        if (a >= p_ini_ && a <= 0xFFFFu) return int((a - p_ini_) / 512u);
        if (a >= i_ini_ && a < a_ini_) return int(n_principal() + (a - i_ini_) / 64u);
        return -1;
    }
    uint32_t tam_sector(int s) const override { return unsigned(s) < n_principal() ? 512u : 64u; }
    unsigned n_sectores() const override { return n_principal() + (a_ini_ - i_ini_) / 64u; }
    bool borrar(acceso_nv_if&, uint32_t a, uint32_t n) override {
        for (uint32_t x = a; x < a + (n ? n : 1u); ) {
            const int s = sector_de(x);
            if (s < 0) return false;
            const uint32_t t = tam_sector(s);
            if (!d_.flash_borra(x & ~(t - 1u), false)) return false;
            x = (x & ~(t - 1u)) + t;
        }
        return true;
    }
    bool programar(acceso_nv_if&, uint32_t a, const uint8_t* dat, unsigned n, unsigned& palabras) override {
        // De palabra en palabra, con los extremos impares rellenos de 0xFF
        std::vector<uint16_t> w;
        const uint32_t ini = a & ~1u;
        for (uint32_t x = ini; x < a + n; x += 2) {
            auto b = [&](uint32_t y) -> uint16_t { return (y >= a && y < a + n) ? dat[y - a] : 0xFF; };
            w.push_back(uint16_t(b(x) | (b(x + 1) << 8)));
        }
        if (!d_.flash_escribe(ini, w.data(), unsigned(w.size()))) return false;
        palabras += unsigned(w.size());
        return true;
    }
    void terminar(acceso_nv_if&) override {}
private:
    m430_dbg_if& d_;
    uint32_t p_ini_, i_ini_, a_ini_;
    unsigned n_principal() const { return (0x10000u - p_ini_) / 512u; }
};

// --- El programador de la Flash de 5xx/6xx -----------------------------------------
// Segmentos de 512 B en la principal (hasta 0x243FF en el F5529, por encima de
// 0x10000) y de 128 B en la información (A-D; A, como en el chip, solo si se
// pide). Por el camino de depuración que haya detrás.
class ProgramadorF5xx : public programador_nv_if {
public:
    ProgramadorF5xx(m430_dbg_if& d, uint32_t p_ini, uint32_t p_fin, uint32_t i_ini, uint32_t i_fin)
        : d_(d), p_ini_(p_ini), p_fin_(p_fin), i_ini_(i_ini), i_fin_(i_fin) {}
    const char* nombre() const override { return "MSP430 FLASH (5xx/6xx)"; }
    int sector_de(uint32_t a) const override {
        if (a >= p_ini_ && a <= p_fin_) return int((a - p_ini_) / 512u);
        if (a >= i_ini_ && a <= i_fin_) return int(n_principal() + (a - i_ini_) / 128u);
        return -1;
    }
    uint32_t tam_sector(int s) const override { return unsigned(s) < n_principal() ? 512u : 128u; }
    unsigned n_sectores() const override { return n_principal() + (i_fin_ + 1 - i_ini_) / 128u; }
    bool borrar(acceso_nv_if&, uint32_t a, uint32_t n) override {
        for (uint32_t x = a; x < a + (n ? n : 1u); ) {
            const int s = sector_de(x);
            if (s < 0) return false;
            const uint32_t t = tam_sector(s);
            if (!d_.flash_borra(x & ~(t - 1u), false)) return false;
            x = (x & ~(t - 1u)) + t;
        }
        return true;
    }
    bool programar(acceso_nv_if&, uint32_t a, const uint8_t* dat, unsigned n, unsigned& palabras) override {
        std::vector<uint16_t> w;
        const uint32_t ini = a & ~1u;
        for (uint32_t x = ini; x < a + n; x += 2) {
            auto b = [&](uint32_t y) -> uint16_t { return (y >= a && y < a + n) ? dat[y - a] : 0xFF; };
            w.push_back(uint16_t(b(x) | (b(x + 1) << 8)));
        }
        if (!d_.flash_escribe(ini, w.data(), unsigned(w.size()))) return false;
        palabras += unsigned(w.size());
        return true;
    }
    void terminar(acceso_nv_if&) override {}
private:
    m430_dbg_if& d_;
    uint32_t p_ini_, p_fin_, i_ini_, i_fin_;
    unsigned n_principal() const { return (p_fin_ + 1 - p_ini_) / 512u; }
};

// --- El protocolo ------------------------------------------------------------------
class GdbRspM430 : public GdbRspBase {
protected:
    GdbRspM430(m430_dbg_if& d, unsigned puerto, const char* etiqueta) : GdbRspBase(puerto, etiqueta), d_(d) {
        pon_programador(&ProgramadorNinguno430::el());
    }
public:
    // La Flash del chip (FLASH2): el programador, sobre el mismo camino de depuración
    void pon_flash_2xx(uint32_t principal_ini, uint32_t info_ini, uint32_t info_a_ini) {
        prog_.reset(new ProgramadorF2xx(d_, principal_ini, info_ini, info_a_ini));
        pon_programador(prog_.get());
    }
    void pon_flash_5xx(uint32_t p_ini, uint32_t p_fin, uint32_t i_ini, uint32_t i_fin) {
        prog_.reset(new ProgramadorF5xx(d_, p_ini, p_fin, i_ini, i_fin));
        pon_programador(prog_.get());
    }
protected:
    std::unique_ptr<programador_nv_if> prog_;
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
            else for (unsigned i = 0; i < tb_.size(); ++i) {
                if (!tb_[i].usado || tb_[i].tipo == 0 || !((f >> i) & 1u)) continue;
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
        if (programador().sector_de(a) >= 0) return pon_tb(a, 1);   // en la Flash, de hardware
        const uint16_t orig = d_.lee16(a);
        d_.escribe16(a, 0x4343);                               // CLR.B R3
        // En la Flash no se puede escribir así: un disparador de búsqueda en
        // su lugar (lo que hace la herramienta con «software breakpoints» en
        // Flash cuando no los emula)
        if (d_.lee16(a) != 0x4343) return pon_tb(a, 1);
        // El disparador 0: búsqueda con MDB = 0x4343 (si una ruptura de
        // hardware no lo tiene ya: con el EEM XS solo hay dos)
        if (!tb_.empty() && tb_[0].usado && tb_[0].tipo != 0) { d_.escribe16(a, orig); return false; }
        sw_[a] = orig;
        if (!tb_.empty() && !tb_[0].usado) {
            tb_[0] = Tb{true, 0, 0};
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
        if (it == sw_.end()) return quita_tb(a, 1);
        d_.escribe16(a, it->second);
        sw_.erase(it);
        if (sw_.empty() && !tb_.empty() && tb_[0].tipo == 0) { tb_[0].usado = false; d_.eem_escribe(0x06, 0); react(); }
        return true;
    }
    bool arq_poner_wp(uint32_t a, unsigned tipo, unsigned) override { return pon_tb(a, tipo); }
    bool arq_quitar_wp(uint32_t a, unsigned tipo, unsigned) override { return quita_tb(a, tipo); }
    // `monitor reset` y `monitor reset halt` llegan aquí (la base los atiende
    // por el prefijo y deja el núcleo PARADO): el POR por JTAG y la CPU quieta
    // en el vector de reset, como `reset halt` de la herramienta
    void arq_reset() override { d_.reset(true); }
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
    // El 0 se guarda para las rupturas de software en RAM; si no hay
    // ninguna, también vale (el EEM XS del G2553 tiene dos disparadores)
    bool pon_tb(uint32_t a, unsigned tipo) {
        for (unsigned j = 1; j <= tb_.size(); ++j) {
            const unsigned i = j % unsigned(tb_.size());
            if (tb_[i].usado || (i == 0 && !sw_.empty())) continue;
            tb_[i] = Tb{true, tipo, a};
            const uint16_t trig = tipo == 1 ? 0x0000 : tipo == 2 ? 0x0026 : tipo == 3 ? 0x0024 : 0x0004;
            d_.eem_escribe(uint8_t(8 * i), a);
            d_.eem_escribe(uint8_t(8 * i + 2), trig);           // MAB, el tipo, =
            d_.eem_escribe(uint8_t(8 * i + 4), 0);
            d_.eem_escribe(uint8_t(8 * i + 6), uint16_t(1u << i));
            react();
            return true;
        }
        return false;
    }
    bool quita_tb(uint32_t a, unsigned tipo) {
        for (unsigned i = 0; i < tb_.size(); ++i)
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
