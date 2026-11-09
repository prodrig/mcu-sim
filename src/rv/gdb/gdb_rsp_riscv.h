// =============================================================================
// gdb_rsp_riscv.h — El piso RISC-V del servidor de GDB
//
// E2 de doc/analisis_esp32_riscv.md, §3.7. El protocolo de GDB es el de
// common/gdb_rsp.h (GdbRspBase, partido en E0.3); aquí está la arquitectura:
// todo lo que en ARM son DHCSR, DCRSR, FPB y DWT, en RISC-V es el Debug
// Module, y se le habla por `dmi_if` con RvDmCliente.
//
//   * Registros: x0-x31 y pc (el dpc del hart parado), 33 de 32 bits, con un
//     target.xml `org.gnu.gdb.riscv.cpu`.
//   * Parar, seguir y paso: haltreq, resumereq y dcsr.step. Al engancharse se
//     ponen dcsr.ebreakm y ebreaku: un ebreak entra en depuración.
//   * Puntos de ruptura: Z0 con ebreak o c.ebreak, según el tamaño que diga
//     GDB, guardando lo que había; Z1 y los watchpoints Z2-Z4, con triggers
//     mcontrol (action = 1, dmode = 1). Cuántos hay se DESCUBRE, como OpenOCD.
//   * Memoria: por SBA si la hay; si no, por el program buffer.
//   * Varios harts: un hilo de GDB cada uno, en modo «todo parado»: cuando
//     uno se para, el stub para los demás antes de avisar a GDB, y `c` los
//     suelta a todos.
//   * `monitor reset halt` / `R`: ndmreset con resethaltreq.
//
// Lo que no es de aquí: el transporte (quién pone el dmi_if: el DM en directo
// o un DTM por los pines) lo ponen los stubs de rv/gdb/gdb_stub_rv.h.
// =============================================================================
#ifndef STM32_RV_GDB_GDB_RSP_RISCV_H
#define STM32_RV_GDB_GDB_RSP_RISCV_H

#include <map>
#include <string>
#include <vector>
#include "../../common/gdb_rsp.h"
#include "../debug/rv_dm_cliente.h"

namespace stm32 {

// El programador de memoria no volátil «ninguno»: todo es RAM (laboratorio)
class ProgramadorNinguno : public programador_nv_if {
public:
    static ProgramadorNinguno& el() { static ProgramadorNinguno p; return p; }
    const char* nombre() const override { return "ninguno"; }
    int sector_de(uint32_t) const override { return -1; }
    uint32_t tam_sector(int) const override { return 0; }
    unsigned n_sectores() const override { return 0; }
    bool borrar(acceso_nv_if&, uint32_t, uint32_t) override { return false; }
    bool programar(acceso_nv_if&, uint32_t, const uint8_t*, unsigned, unsigned&) override {
        return false;
    }
    void terminar(acceso_nv_if&) override {}
};

class GdbRspRiscv : public GdbRspBase {
public:
    unsigned n_triggers() const { return n_trig_; }
    RvDmCliente& cliente() { return dm_; }

protected:
    GdbRspRiscv(dmi_if& d, unsigned puerto, const char* etiqueta)
        : GdbRspBase(puerto, etiqueta), dm_(d) {
        pon_programador(&ProgramadorNinguno::el());
    }

    // =======================================================================
    // Enganche y estado
    // =======================================================================
    bool arq_enganchar() override {
        if (!dm_.activar()) return false;
        hart_parado_ = 0;
        trig_.clear();
        n_trig_ = 0;
        trig_contados_ = false;
        return true;
    }
    // Al pararse el primer hart se cuentan los triggers (hace falta tenerlo
    // parado) y se ponen ebreakm/ebreaku
    void prepara_hart() {
        uint32_t dcsr = 0;
        if (dm_.lee_reg(RvDmCliente::CSR_DCSR, dcsr))
            dm_.escribe_reg(RvDmCliente::CSR_DCSR, dcsr | (1u << 15) | (1u << 12));
        if (!trig_contados_) {
            n_trig_ = dm_.cuenta_triggers();
            trig_.assign(n_trig_, Trig{});
            trig_contados_ = true;
        }
    }

    bool arq_parado() override {
        for (uint32_t h = 0; h < dm_.n_harts(); ++h) {
            dm_.selecciona(h);
            if (dm_.disponible() && dm_.parado()) {
                hart_parado_ = h;
                para_todos();
                return true;
            }
        }
        return false;
    }
    void arq_parar() override {
        para_todos();
        hart_parado_ = 0;
    }
    void arq_seguir() override {
        for (uint32_t h = 0; h < dm_.n_harts(); ++h) {
            dm_.selecciona(h);
            if (dm_.disponible() && dm_.parado()) dm_.seguir();
        }
    }
    void arq_paso() override {
        const uint32_t h = hilo_c() ? hilo_c() - 1 : hart_parado_;
        dm_.selecciona(h);
        dm_.paso();
        hart_parado_ = h;
    }
    std::string arq_parada(unsigned senal) override {
        dm_.selecciona(hart_parado_);
        uint32_t dcsr = 0, pc = 0;
        dm_.lee_reg(RvDmCliente::CSR_DCSR, dcsr);
        dm_.lee_reg(RvDmCliente::CSR_DPC, pc);
        const unsigned causa = (dcsr >> 6) & 7u;
        std::string s = "T0";
        s.push_back(hexd(senal & 0xFu));
        if (senal == 5 && causa == 1) s += "swbreak:;";
        if (senal == 5 && causa == 2) {
            // ¿Qué trigger? El que tenga hit
            for (unsigned i = 0; i < trig_.size(); ++i) {
                if (!trig_[i].usado) continue;
                uint32_t t1 = 0;
                dm_.escribe_reg(RvDmCliente::CSR_TSELECT, i);
                dm_.lee_reg(RvDmCliente::CSR_TDATA1, t1);
                if (!(t1 & MC_HIT)) continue;
                dm_.escribe_reg(RvDmCliente::CSR_TDATA1, t1 & ~MC_HIT);
                if (trig_[i].tipo == 1) s += "hwbreak:;";
                else s += std::string(trig_[i].tipo == 2 ? "watch" : trig_[i].tipo == 3 ? "rwatch"
                                                                                       : "awatch") +
                          ":" + hex_be(trig_[i].dir) + ";";
                break;
            }
        }
        s += "20:" + hex32(pc) + ";";
        if (dm_.n_harts() > 1) s += "thread:" + hilo_hex(hart_parado_ + 1) + ";";
        else s += "thread:1;";
        return s;
    }

    // =======================================================================
    // Registros
    // =======================================================================
    unsigned arq_n_regs() const override { return 33; }
    uint32_t arq_reg(unsigned n) override {
        dm_.selecciona(hilo_g() ? hilo_g() - 1 : hart_parado_);
        uint32_t v = 0;
        if (n == 0) return 0;
        dm_.lee_reg(n < 32 ? RvDmCliente::GPR(n) : RvDmCliente::CSR_DPC, v);
        return v;
    }
    void arq_set_reg(unsigned n, uint32_t v) override {
        dm_.selecciona(hilo_g() ? hilo_g() - 1 : hart_parado_);
        if (n == 0) return;
        dm_.escribe_reg(n < 32 ? RvDmCliente::GPR(n) : RvDmCliente::CSR_DPC, v);
    }
    const std::string& arq_target_xml() const override {
        static const std::string x = [] {
            static const char* const abi[32] = {
                "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "fp", "s1", "a0", "a1",
                "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
                "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
            std::string s =
                "<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                "<target version=\"1.0\">\n<architecture>riscv:rv32</architecture>\n"
                "<feature name=\"org.gnu.gdb.riscv.cpu\">\n";
            for (unsigned i = 0; i < 32; ++i) {
                const bool ptr = i == 1 || i == 2 || i == 8;
                s += "  <reg name=\"" + std::string(abi[i]) + "\" bitsize=\"32\" regnum=\"" +
                     std::to_string(i) + "\" type=\"" +
                     (i == 1 ? "code_ptr" : ptr ? "data_ptr" : "int") + "\"/>\n";
            }
            s += "  <reg name=\"pc\" bitsize=\"32\" regnum=\"32\" type=\"code_ptr\"/>\n"
                 "</feature>\n</target>\n";
            return s;
        }();
        return x;
    }

    // =======================================================================
    // Puntos de ruptura y watchpoints
    // =======================================================================
    bool arq_poner_bp(uint32_t a, unsigned tipo, unsigned tam) override {
        if (tipo == 1) return pon_trigger(a, 1, tam);
        if (sw_.count(a)) return true;
        uint32_t w = 0;
        const uint32_t al = a & ~3u;
        if (!mem_leer(al, w)) return false;
        Sw s;
        s.tam = tam == 2 ? 2 : 4;
        if (s.tam == 2) {
            s.orig = (w >> ((a & 2u) * 8u)) & 0xFFFFu;
            if (!mem_escribir16(a, 0x9002u)) return false;                 // c.ebreak
        } else {
            uint32_t w2 = 0;
            if ((a & 2u) && !mem_leer(al + 4, w2)) return false;
            s.orig = (a & 2u) ? ((w >> 16) | (w2 << 16)) : w;
            if (a & 2u) {
                if (!mem_escribir16(a, 0x0073u) || !mem_escribir16(a + 2, 0x0010u)) return false;
            } else if (!mem_escribir(a, 0x00100073u)) return false;           // ebreak
        }
        sw_[a] = s;
        return true;
    }
    bool arq_quitar_bp(uint32_t a, unsigned tipo, unsigned tam) override {
        if (tipo == 1) return quita_trigger(a, 1);
        auto it = sw_.find(a);
        if (it == sw_.end()) return true;
        const Sw s = it->second;
        sw_.erase(it);
        if (s.tam == 2) return mem_escribir16(a, uint16_t(s.orig));
        if (a & 2u) return mem_escribir16(a, uint16_t(s.orig)) &&
                           mem_escribir16(a + 2, uint16_t(s.orig >> 16));
        return mem_escribir(a, s.orig);
    }
    bool arq_poner_wp(uint32_t a, unsigned tipo, unsigned tam) override {
        return pon_trigger(a, tipo, tam);
    }
    bool arq_quitar_wp(uint32_t a, unsigned tipo, unsigned /*tam*/) override {
        return quita_trigger(a, tipo);
    }

    void arq_reset() override {
        // El reset de la herramienta de verdad, si lo hace por un registro
        // del chip (openocd-esp32: RTC_CNTL_SW_SYS_RST)
        if (herr_ && herr_->tiene_reset_soc()) dm_.reset(true, [this] { herr_->reset_soc(*this); });
        else dm_.reset(true);
        hart_parado_ = 0;
        // El reset borra los triggers y dcsr: vuelven a ponerse
        for (unsigned i = 0; i < dm_.n_harts(); ++i) { dm_.selecciona(i); prepara_hart(); }
        for (Trig& t : trig_) t.usado = false;
        sw_.clear();
    }

    unsigned arq_n_hilos() const override { return dm_.n_harts() ? dm_.n_harts() : 1; }
    bool arq_elige_hilo(unsigned h) override { return h >= 1 && h <= dm_.n_harts(); }

    const char* arq_nombre_xml() const override { return "riscv"; }
    const char* arq_nombre_humano() const override { return "RISC-V"; }
    const char* arq_gdb_recomendado() const override { return "riscv32-esp-elf-gdb"; }

    // =======================================================================
    // Memoria
    // =======================================================================
    bool mem_leer(uint32_t a, uint32_t& v) override { return dm_.lee32(a, v); }
    bool mem_escribir(uint32_t a, uint32_t v) override { return dm_.escribe32(a, v); }
    bool mem_escribir16(uint32_t a, uint16_t v) override { return dm_.escribe16(a, v); }
    unsigned mem_leer_bloque(uint32_t a, uint32_t* w, unsigned n) override {
        return dm_.lee_bloque(a, w, n);
    }

    RvDmCliente dm_;

private:
    static constexpr uint32_t MC_HIT = 1u << 20;
    struct Trig { bool usado = false; unsigned tipo = 0; uint32_t dir = 0; };
    struct Sw { unsigned tam = 4; uint32_t orig = 0; };
    std::vector<Trig> trig_;
    std::map<uint32_t, Sw> sw_;
    unsigned n_trig_ = 0;
    bool trig_contados_ = false;
    uint32_t hart_parado_ = 0;

    static std::string hex_be(uint32_t v) {
        static const char* d = "0123456789abcdef";
        std::string s;
        for (int i = 28; i >= 0; i -= 4) s.push_back(d[(v >> i) & 0xFu]);
        return s;
    }
    static char hexd(unsigned v) { return "0123456789abcdef"[v & 0xFu]; }

    // Para todos los harts que corran (modo «todo parado» de GDB)
    void para_todos() {
        for (uint32_t h = 0; h < dm_.n_harts(); ++h) {
            dm_.selecciona(h);
            if (!dm_.disponible()) continue;
            if (!dm_.parado()) dm_.parar();
            prepara_hart();
        }
    }

    // Los triggers se programan en TODOS los harts, como hace OpenOCD
    bool pon_trigger(uint32_t a, unsigned tipo, unsigned /*tam*/) {
        unsigned i = 0;
        while (i < trig_.size() && trig_[i].usado) ++i;
        if (i == trig_.size()) return false;
        // tipo 2 (dmode) | dmode | action = 1 | m | u, más execute, store o load
        uint32_t t1 = (2u << 28) | (1u << 27) | (1u << 12) | (1u << 6) | (1u << 3);
        t1 |= tipo == 1 ? 4u : tipo == 2 ? 2u : tipo == 3 ? 1u : 3u;
        bool ok = true;
        for (uint32_t h = 0; h < dm_.n_harts(); ++h) {
            dm_.selecciona(h);
            if (!dm_.disponible() || !dm_.parado()) continue;
            ok = dm_.escribe_reg(RvDmCliente::CSR_TSELECT, i) &&
                 dm_.escribe_reg(RvDmCliente::CSR_TDATA1, 0) &&
                 dm_.escribe_reg(RvDmCliente::CSR_TDATA2, a) &&
                 dm_.escribe_reg(RvDmCliente::CSR_TDATA1, t1) && ok;
        }
        if (!ok) return false;
        trig_[i] = Trig{true, tipo, a};
        return true;
    }
    bool quita_trigger(uint32_t a, unsigned tipo) {
        for (unsigned i = 0; i < trig_.size(); ++i) {
            if (!trig_[i].usado || trig_[i].dir != a || trig_[i].tipo != tipo) continue;
            for (uint32_t h = 0; h < dm_.n_harts(); ++h) {
                dm_.selecciona(h);
                if (!dm_.disponible() || !dm_.parado()) continue;
                dm_.escribe_reg(RvDmCliente::CSR_TSELECT, i);
                dm_.escribe_reg(RvDmCliente::CSR_TDATA1, 0);
            }
            trig_[i].usado = false;
            return true;
        }
        return true;
    }
};

} // namespace stm32
#endif // STM32_RV_GDB_GDB_RSP_RISCV_H
