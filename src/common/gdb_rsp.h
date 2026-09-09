// =============================================================================
// gdb_rsp.h — El motor del Remote Serial Protocol de GDB, sin transporte
//
// Aquí vive TODO lo que un servidor GDB para un Cortex-M tiene que saber y que
// no depende de por dónde llegue al DAP:
//
//   * el socket TCP y el tramado `$cuerpo#suma`,
//   * el intérprete de paquetes (g/G/p/P, m/M/X, c/s/vCont, Z/z, q..., v...),
//   * el mapa de 23 registros y el `target.xml` que lo describe,
//   * los puntos de ruptura sobre el FPB y los watchpoints sobre el DWT,
//   * y el programador de Flash, porque un `load` sobre 0x0800 0000 no es una
//     escritura al bus sino una secuencia del controlador [IR, §5.5-5.9].
//
// Lo ÚNICO que no está aquí es cómo se lee y se escribe una palabra en el
// espacio de direcciones del objetivo. Eso son cuatro funciones virtuales, y
// cada stub las resuelve a su manera:
//
//   verif/gdb_stub.h      -> mandando paquetes SWD por SWCLK/SWDIO, como un
//                            ST-LINK soldado a los pines. Es la sonda.
//   core/gdb_stub_dap.h   -> llamando directamente al AHB-AP del DebugSys. No
//                            hay pines, ni bits, ni relojes: es el atajo.
//
// La consecuencia es que los dos stubs son EL MISMO depurador. Lo que cambia
// es el coste de simulación de cada acceso, no el comportamiento visto por
// GDB: los mismos paquetes, las mismas respuestas, los mismos registros.
// =============================================================================
#ifndef STM32_COMMON_GDB_RSP_H
#define STM32_COMMON_GDB_RSP_H

#include <systemc>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "red.h"          // la unica dependencia del sistema operativo

namespace stm32 {

// -----------------------------------------------------------------------------
// Dónde se engancha el depurador. Es el parámetro que elige entre los dos stubs
// (véase doc/stm32f407vg_fase6_gdb2.md).
//
//   Pines    : el núcleo EXPONE SWCLK/SWDIO/JTDI/SWO/NJTRST y quien quiera
//              depurar se conecta por fuera, como un ST-LINK. Es el modelo
//              fiel: cada acceso cuesta ~50 flancos de SWCLK simulados.
//   Interno  : el núcleo RESERVA esos cinco pines (nadie más puede usarlos,
//              igual que en el silicio) pero no los usa, y crea dentro de sí
//              un stub que habla con el DAP por llamada de función. Es el
//              modelo rápido: un acceso es una transacción TLM.
// -----------------------------------------------------------------------------
enum class DebugAttach { Pines, Interno };

class GdbRsp {
public:
    virtual ~GdbRsp() { cerrar(); }

    // --- Mandos -------------------------------------------------------------
    void set_verbose(bool v)          { verboso_ = v; }
    // Cada cuánto tiempo SIMULADO se atiende el socket. Es el compromiso entre
    // capacidad de respuesta frente a GDB y coste de simulación.
    void set_poll(sc_core::sc_time t) { poll_ = t; }
    // Parar el núcleo en cuanto GDB se conecte, que es lo que espera cualquier
    // IDE al lanzar una sesión.
    void set_halt_on_attach(bool h)   { halt_al_conectar_ = h; }
    void set_enabled(bool on) {
        if (on && !anunciado_) {
            anunciado_ = true;
            std::printf("[%s] escuchando en localhost:%u\n", etiqueta_, puerto_);
            std::fflush(stdout);
        }
        activo_ = on;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Estado observable ---------------------------------------------------
    bool     escuchando() const { return red::valido(srv_); }
    bool     conectado() const  { return red::valido(cli_); }
    unsigned puerto() const     { return puerto_; }
    unsigned paquetes() const   { return n_paq_; }
    unsigned flash_palabras() const { return n_flash_; }
    unsigned accesos() const    { return n_acc_; }
    const std::string& ultimo() const { return ultimo_; }

protected:
    explicit GdbRsp(unsigned puerto, const char* etiqueta)
        : puerto_(puerto), etiqueta_(etiqueta) {}

    // =======================================================================
    // EL TRANSPORTE — lo único que distingue a los dos stubs
    // =======================================================================
    // Prepara el camino hasta el DAP (en la sonda: reset de línea, conmutación
    // JTAG->SWD, IDCODE y encendido; en el interno: nada). Devuelve si hay
    // objetivo al otro lado.
    virtual bool dap_enganchar() = 0;
    virtual bool dap_leer(uint32_t a, uint32_t& v) = 0;
    virtual bool dap_escribir(uint32_t a, uint32_t v) = 0;
    // Lectura de un bloque alineado. Por omisión, palabra a palabra; la sonda
    // la sustituye por una ráfaga con auto-incremento de TAR, que es mucho más
    // barata en flancos.
    virtual unsigned dap_leer_bloque(uint32_t a, uint32_t* w, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            if (!dap_leer(a + 4 * i, w[i])) return i;
        return n;
    }

    // El enganche completo: transporte + las llaves que hacen que la depuración
    // exista. Sin C_DEBUGEN no hay BKPT que valga; sin TRCENA el FPB y el DWT
    // están apagados.
    bool enganchar() {
        enganchado_ = dap_enganchar();
        if (enganchado_) {
            escribir(R_DHCSR, LLAVE | 1u);
            escribir(R_DEMCR, 1u << 24);              // TRCENA: DWT y FPB vivos
            escribir(B_FPB + 0x00, 3u);               // FP_CTRL: KEY | ENABLE
        }
        return enganchado_;
    }

    // El cuerpo del hilo. Los dos stubs lo usan tal cual.
    void servir() {
        // Puerto 0: el stub existe pero no escucha. Es lo que hace falta cuando
        // el modo elegido es el otro y los dos no pueden compartir puerto.
        if (!puerto_) return;
        if (!abrir()) {
            std::printf("[%s] no se pudo abrir el puerto %u\n", etiqueta_, puerto_);
            std::fflush(stdout);
            return;
        }
        for (;;) {
            sc_core::wait(poll_, ev_);
            if (!activo_) continue;
            aceptar();
            if (!red::valido(cli_)) continue;
            rx_poll();
            procesar_rx();
            // Con el objetivo corriendo hay que vigilar si se ha parado solo
            // -un punto de ruptura, un watchpoint- para avisar a GDB.
            if (corriendo_ && red::valido(cli_)) {
                if (++espera_ >= vueltas_vigilancia_) {
                    espera_ = 0;
                    if (parado()) { corriendo_ = false; responder(parada()); }
                }
            }
        }
    }

    // =======================================================================
    // El modelo de depuración de ARM, en registros
    // =======================================================================
    static constexpr uint32_t R_DHCSR = 0xE000EDF0u, R_DCRSR = 0xE000EDF4u,
                              R_DCRDR = 0xE000EDF8u, R_DEMCR = 0xE000EDFCu,
                              R_AIRCR = 0xE000ED0Cu, R_DFSR  = 0xE000ED30u;
    static constexpr uint32_t B_FPB = 0xE0002000u, B_DWT = 0xE0001000u;
    static constexpr uint32_t LLAVE = 0xA05F0000u;

    bool leer(uint32_t a, uint32_t& v)    { ++n_acc_; return dap_leer(a, v); }
    bool escribir(uint32_t a, uint32_t v) { ++n_acc_; return dap_escribir(a, v); }
    uint32_t leer_o(uint32_t a) { uint32_t v = 0; leer(a, v); return v; }

    bool parado() { return (leer_o(R_DHCSR) & (1u << 17)) != 0; }
    void parar()  { escribir(R_DHCSR, LLAVE | 0x3u); }
    void seguir() { escribir(R_DHCSR, LLAVE | 0x1u); }
    void paso()   { escribir(R_DHCSR, LLAVE | 0x5u); }

    unsigned puerto_;
    const char* etiqueta_;
    bool     enganchado_ = false;

private:
    // Los registros del núcleo, por la pareja DCRSR/DCRDR.
    uint32_t reg(unsigned sel) { escribir(R_DCRSR, sel & 0x7Fu); return leer_o(R_DCRDR); }
    void set_reg(unsigned sel, uint32_t v) {
        escribir(R_DCRDR, v);
        escribir(R_DCRSR, (1u << 16) | (sel & 0x7Fu));
    }

    // ---- Mapa de registros que ve GDB --------------------------------------
    // 17 del perfil M (r0-r12, sp, lr, pc, xpsr) mas 6 de sistema. El orden y
    // el numero los fija el target.xml que se le manda.
    static constexpr unsigned N_REGS = 23;
    uint32_t reg_gdb(unsigned n) {
        if (n < 13) return reg(n);
        switch (n) {
            case 13: return reg(13);                  // sp en uso
            case 14: return reg(14);                  // lr
            case 15: return reg(15);                  // pc (retorno de depuracion)
            case 16: return reg(16);                  // xpsr
            case 17: return reg(17);                  // msp
            case 18: return reg(18);                  // psp
            case 19: return reg(20) & 0xFFu;          // primask
            case 20: return (reg(20) >> 8) & 0xFFu;   // basepri
            case 21: return (reg(20) >> 16) & 0xFFu;  // faultmask
            case 22: return (reg(20) >> 24) & 0xFFu;  // control
            default: return 0;
        }
    }
    void set_reg_gdb(unsigned n, uint32_t v) {
        if (n < 19) { set_reg(n, v); return; }
        uint32_t p = reg(20);
        switch (n) {
            case 19: p = (p & ~0x000000FFu) | (v & 0xFFu); break;
            case 20: p = (p & ~0x0000FF00u) | ((v & 0xFFu) << 8); break;
            case 21: p = (p & ~0x00FF0000u) | ((v & 0xFFu) << 16); break;
            case 22: p = (p & ~0xFF000000u) | ((v & 0xFFu) << 24); break;
            default: return;
        }
        set_reg(20, p);
    }

    // =======================================================================
    // Puntos de ruptura (FPB) y watchpoints (DWT)
    // =======================================================================
    static constexpr unsigned N_FPB = 6, N_DWT = 4;
    uint32_t fpb_[N_FPB] = {};
    uint32_t dwt_[N_DWT] = {};

    bool poner_bp(uint32_t addr) {
        for (unsigned i = 0; i < N_FPB; ++i) if (fpb_[i] == (addr | 1u)) return true;
        for (unsigned i = 0; i < N_FPB; ++i) {
            if (fpb_[i]) continue;
            // REPLACE elige la media palabra: 01 la baja, 10 la alta.
            const uint32_t rep = (addr & 2u) ? 2u : 1u;
            const uint32_t v = (addr & 0x1FFFFFFCu) | (rep << 30) | 1u;
            escribir(B_FPB + 0x08 + 4 * i, v);
            fpb_[i] = addr | 1u;
            return true;
        }
        return false;                                 // no quedan comparadores
    }
    bool quitar_bp(uint32_t addr) {
        for (unsigned i = 0; i < N_FPB; ++i) {
            if (fpb_[i] != (addr | 1u)) continue;
            escribir(B_FPB + 0x08 + 4 * i, 0);
            fpb_[i] = 0;
            return true;
        }
        return false;
    }
    // GDB pide tipo 2 (escritura), 3 (lectura) y 4 (acceso). El DWT los llama
    // 6, 5 y 4 en su campo FUNCTION.
    bool poner_wp(uint32_t addr, unsigned tipo, unsigned len) {
        for (unsigned i = 0; i < N_DWT; ++i) {
            if (dwt_[i]) continue;
            unsigned f = (tipo == 2) ? 6u : (tipo == 3) ? 5u : 4u;
            unsigned m = 0;
            while ((1u << m) < len && m < 5) ++m;
            escribir(B_DWT + 0x20 + 0x10 * i, addr);
            escribir(B_DWT + 0x24 + 0x10 * i, m);
            escribir(B_DWT + 0x28 + 0x10 * i, f);
            dwt_[i] = addr | 1u;
            return true;
        }
        return false;
    }
    bool quitar_wp(uint32_t addr) {
        for (unsigned i = 0; i < N_DWT; ++i) {
            if (dwt_[i] != (addr | 1u)) continue;
            escribir(B_DWT + 0x28 + 0x10 * i, 0);
            dwt_[i] = 0;
            return true;
        }
        return false;
    }

    // =======================================================================
    // Programación de la Flash [IR, §5.5-5.9]
    //
    // Un `load` de GDB sobre 0x0800 0000 NO puede ser una escritura al bus: la
    // Flash de un STM32 se programa por su controlador. El stub hace la
    // secuencia entera, exactamente como un ST-LINK.
    // =======================================================================
    static constexpr uint32_t FLASH_R = 0x40023C00u;
    static constexpr uint32_t F_ACR = 0x00, F_KEYR = 0x04, F_SR = 0x0C, F_CR = 0x10;
    // El mapa de sectores del F407: 4 de 16 KiB, 1 de 64 KiB y 7 de 128 KiB.
    // Un programador lleva siempre su propia descripción del dispositivo; ésta
    // es la del STM32F405/407.
    static int sector_de(uint32_t a) {
        if (a < 0x08000000u || a >= 0x08100000u) return -1;
        const uint32_t o = a - 0x08000000u;
        if (o < 0x10000u) return int(o / 0x4000u);            // 0-3: 16 KiB
        if (o < 0x20000u) return 4;                           // 4: 64 KiB
        return 5 + int((o - 0x20000u) / 0x20000u);            // 5-11: 128 KiB
    }
    void flash_espera() {
        for (unsigned i = 0; i < 1000; ++i) {
            if (!(leer_o(FLASH_R + F_SR) & (1u << 16))) return;   // BSY
            sc_core::wait(sc_core::sc_time(2, sc_core::SC_US));
        }
    }
    void flash_desbloquear() {
        if (!(leer_o(FLASH_R + F_CR) & (1u << 31))) return;   // ya desbloqueada
        escribir(FLASH_R + F_KEYR, 0x45670123u);
        escribir(FLASH_R + F_KEYR, 0xCDEF89ABu);
    }
    void flash_bloquear() {
        escribir(FLASH_R + F_CR, leer_o(FLASH_R + F_CR) | (1u << 31));
    }
    bool flash_borrar(uint32_t addr, uint32_t len) {
        flash_desbloquear();
        flash_espera();
        escribir(FLASH_R + F_SR, 0xF3u);                      // limpiar banderas
        uint32_t a = addr;
        while (a < addr + len) {
            const int s = sector_de(a);
            if (s < 0) return false;
            // PSIZE = 10 (32 bits): exige VDD >= 2,7 V, que es el caso.
            escribir(FLASH_R + F_CR, (1u << 1) | (uint32_t(s) << 3) | (2u << 8));
            escribir(FLASH_R + F_CR,
                     (1u << 1) | (uint32_t(s) << 3) | (2u << 8) | (1u << 16));
            flash_espera();
            // Saltar al sector siguiente
            const uint32_t tam = (s < 4) ? 0x4000u : (s == 4) ? 0x10000u : 0x20000u;
            a = (a & ~(tam - 1u)) + tam;
        }
        escribir(FLASH_R + F_CR, 0);
        return true;
    }
    bool flash_programar(uint32_t addr, const uint8_t* d, unsigned n) {
        flash_desbloquear();
        flash_espera();
        escribir(FLASH_R + F_CR, 1u | (2u << 8));             // PG, PSIZE = 32
        for (unsigned i = 0; i < n; i += 4) {
            uint32_t w = 0xFFFFFFFFu;
            for (unsigned k = 0; k < 4 && i + k < n; ++k)
                w = (w & ~(0xFFu << (8 * k))) | (uint32_t(d[i + k]) << (8 * k));
            if (!escribir(addr + i, w)) { escribir(FLASH_R + F_CR, 0); return false; }
            ++n_flash_;
        }
        flash_espera();
        escribir(FLASH_R + F_CR, 0);
        return true;
    }

    // =======================================================================
    // El lado del socket
    // =======================================================================
    bool abrir() {
        srv_ = red::escucha_local(puerto_);
        return red::valido(srv_);
    }
    void cerrar() {
        red::cerrar(cli_);
        red::cerrar(srv_);
    }
    void aceptar() {
        if (red::valido(cli_) || !red::valido(srv_)) return;
        const red::socket_t f = red::acepta(srv_);
        if (!red::valido(f)) return;
        cli_ = f;
        rx_.clear();
        sin_ack_ = false;
        std::printf("[%s] cliente conectado\n", etiqueta_); std::fflush(stdout);
        if (!enganchado_) enganchar();
        if (halt_al_conectar_) { parar(); }
    }
    void desconectar() {
        red::cerrar(cli_);
        std::printf("[%s] cliente desconectado\n", etiqueta_); std::fflush(stdout);
    }
    void tx(const char* d, size_t n) {
        if (!red::valido(cli_)) return;
        size_t k = 0;
        while (k < n) {
            const long r = red::enviar(cli_, d + k, n - k);
            if (r > 0) { k += size_t(r); continue; }
            if (r < 0 && red::reintentar()) break;
            desconectar();
            return;
        }
    }
    void rx_poll() {
        if (!red::valido(cli_)) return;
        char b[4096];
        for (;;) {
            const long r = red::recibir(cli_, b, sizeof b);
            if (r > 0) { rx_.append(b, size_t(r)); continue; }
            if (r == 0) { desconectar(); return; }
            if (red::reintentar()) return;
            desconectar();
            return;
        }
    }

    // --- Tramado del RSP -----------------------------------------------------
    static uint8_t suma(const std::string& s) {
        unsigned c = 0;
        for (char ch : s) c += uint8_t(ch);
        return uint8_t(c);
    }
    static char hexd(unsigned v) { return char(v < 10 ? '0' + v : 'a' + v - 10); }
    static unsigned dehex(char c) {
        if (c >= '0' && c <= '9') return unsigned(c - '0');
        if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return unsigned(c - 'A' + 10);
        return 0;
    }
    static std::string hex32(uint32_t v) {          // little endian, como GDB
        std::string s;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t b = uint8_t(v >> (8 * i));
            s.push_back(hexd(b >> 4)); s.push_back(hexd(b & 0xF));
        }
        return s;
    }
    void responder(const std::string& d) {
        std::string p = "$" + d + "#";
        const uint8_t c = suma(d);
        p.push_back(hexd(c >> 4)); p.push_back(hexd(c & 0xF));
        tx(p.data(), p.size());
        ultimo_ = d;
    }

    void procesar_rx() {
        for (;;) {
            if (rx_.empty()) return;
            // Ctrl-C fuera de paquete: PARAR. Es como GDB interrumpe.
            if (rx_[0] == '\x03') {
                rx_.erase(0, 1);
                parar();
                corriendo_ = false;
                responder(parada());
                continue;
            }
            if (rx_[0] == '+' || rx_[0] == '-') { rx_.erase(0, 1); continue; }
            const size_t ini = rx_.find('$');
            if (ini == std::string::npos) { rx_.clear(); return; }
            if (ini) rx_.erase(0, ini);
            const size_t fin = rx_.find('#');
            if (fin == std::string::npos || rx_.size() < fin + 3) return;
            const std::string cuerpo = rx_.substr(1, fin - 1);
            const unsigned c = dehex(rx_[fin + 1]) * 16u + dehex(rx_[fin + 2]);
            rx_.erase(0, fin + 3);
            if (c != suma(cuerpo)) { if (!sin_ack_) tx("-", 1); continue; }
            if (!sin_ack_) tx("+", 1);
            ++n_paq_;
            if (verboso_) std::printf("[%s] <- %s\n", etiqueta_, cuerpo.c_str());
            atender(cuerpo);
        }
    }

    // El aviso de parada que GDB espera: la señal y, de propina, unos cuantos
    // registros para que no tenga que pedirlos.
    std::string parada() {
        const uint32_t dfsr = leer_o(R_DFSR);
        std::string s = "T05";
        if (dfsr & 2u) s += "swbreak:;";
        else if (dfsr & 4u) s += "watch:;";
        s += "0d:" + hex32(reg_gdb(13)) + ";";        // sp
        s += "0f:" + hex32(reg_gdb(15)) + ";";        // pc
        s += "thread:1;";
        return s;
    }

    // =======================================================================
    // El intérprete del RSP
    // =======================================================================
    void atender(const std::string& p) {
        if (p.empty()) { responder(""); return; }
        switch (p[0]) {
            case '?':  responder(parada()); return;
            case 'g':  {                              // todos los registros
                std::string s;
                for (unsigned i = 0; i < N_REGS; ++i) s += hex32(reg_gdb(i));
                responder(s);
                return;
            }
            case 'G': {
                for (unsigned i = 0; i < N_REGS && 1 + 8 * i + 8 <= p.size(); ++i)
                    set_reg_gdb(i, palabra(p, 1 + 8 * i));
                responder("OK");
                return;
            }
            case 'p': {                               // un registro
                const unsigned n = num(p, 1, 16);
                if (n >= N_REGS) { responder("E01"); return; }
                responder(hex32(reg_gdb(n)));
                return;
            }
            case 'P': {
                const size_t e = p.find('=');
                if (e == std::string::npos) { responder("E01"); return; }
                const unsigned n = num(p, 1, 16);
                if (n >= N_REGS) { responder("E01"); return; }
                set_reg_gdb(n, palabra(p, e + 1));
                responder("OK");
                return;
            }
            case 'm': return cmd_leer_mem(p);
            case 'M': return cmd_escribir_mem(p, false);
            case 'X': return cmd_escribir_mem(p, true);
            case 'c': corriendo_ = true; seguir(); return;    // sin respuesta
            case 's': paso(); responder(parada()); return;
            case 'Z': return cmd_punto(p, true);
            case 'z': return cmd_punto(p, false);
            case 'H': responder("OK"); return;
            case 'k': desconectar(); return;
            case 'D': responder("OK"); desconectar(); return;
            case 'r': case 'R': cmd_reset(); responder("OK"); return;
            case 'q': return cmd_q(p);
            case 'Q':
                if (p == "QStartNoAckMode") { responder("OK"); sin_ack_ = true; return; }
                responder("");
                return;
            case 'v': return cmd_v(p);
            default:  responder(""); return;          // no soportado
        }
    }

    static unsigned num(const std::string& s, size_t i, unsigned base) {
        unsigned v = 0;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            unsigned d;
            if (c >= '0' && c <= '9') d = unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') d = unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = unsigned(c - 'A' + 10);
            else break;
            if (d >= base) break;
            v = v * base + d;
        }
        return v;
    }
    static uint32_t palabra(const std::string& s, size_t i) {   // 8 hex, LE
        uint32_t v = 0;
        for (unsigned k = 0; k < 4 && i + 2 * k + 1 < s.size(); ++k) {
            const unsigned b = dehex(s[i + 2 * k]) * 16u + dehex(s[i + 2 * k + 1]);
            v |= uint32_t(b) << (8 * k);
        }
        return v;
    }

    void cmd_leer_mem(const std::string& p) {
        const size_t c = p.find(',');
        if (c == std::string::npos) { responder("E01"); return; }
        const uint32_t a = num(p, 1, 16);
        unsigned n = num(p, c + 1, 16);
        if (n > 1024) n = 1024;
        std::string s;
        // Se lee por palabras alineadas y se recorta: es lo que puede hacer el
        // AHB-AP, cuyo DRW es de 32 bits.
        const uint32_t base = a & ~3u;
        const unsigned nw = (a + n - base + 3u) / 4u;
        std::vector<uint32_t> w(nw ? nw : 1u, 0);
        n_acc_ += nw;
        if (nw && dap_leer_bloque(base, w.data(), nw) != nw) {
            responder("E01");
            return;
        }
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t off = a + i - base;
            const uint8_t b = uint8_t(w[off / 4] >> (8 * (off % 4)));
            s.push_back(hexd(b >> 4)); s.push_back(hexd(b & 0xF));
        }
        responder(s);
    }

    void cmd_escribir_mem(const std::string& p, bool binario) {
        const size_t c = p.find(',');
        const size_t d = p.find(':');
        if (c == std::string::npos || d == std::string::npos) { responder("E01"); return; }
        const uint32_t a = num(p, 1, 16);
        const unsigned n = num(p, c + 1, 16);
        std::vector<uint8_t> dat;
        if (binario) desescapar(p, d + 1, dat);
        else for (unsigned i = 0; i < n && d + 1 + 2 * i + 1 < p.size(); ++i)
            dat.push_back(uint8_t(dehex(p[d + 1 + 2 * i]) * 16u +
                                  dehex(p[d + 2 + 2 * i])));
        if (n == 0) { responder("OK"); return; }      // sonda de longitud cero
        if (dat.size() < n) dat.resize(n, 0);
        if (escribir_bytes(a, dat.data(), n)) responder("OK");
        else                                  responder("E01");
    }

    // La escritura byte a byte sobre un AP de 32 bits: leer-modificar-escribir
    // en los extremos no alineados. GDB escribe trozos de cualquier tamano.
    bool escribir_bytes(uint32_t a, const uint8_t* d, unsigned n) {
        unsigned i = 0;
        while (i < n) {
            const uint32_t dir = a + i;
            const uint32_t base = dir & ~3u;
            const unsigned off = dir - base;
            const unsigned trozo = (4u - off < n - i) ? (4u - off) : (n - i);
            uint32_t w = 0;
            if (trozo != 4u && !leer(base, w)) return false;
            for (unsigned k = 0; k < trozo; ++k)
                w = (w & ~(0xFFu << (8 * (off + k)))) |
                    (uint32_t(d[i + k]) << (8 * (off + k)));
            if (!escribir(base, w)) return false;
            i += trozo;
        }
        return true;
    }
    static void desescapar(const std::string& p, size_t i, std::vector<uint8_t>& out) {
        for (; i < p.size(); ++i) {
            uint8_t b = uint8_t(p[i]);
            if (b == 0x7D) { ++i; if (i >= p.size()) break; b = uint8_t(p[i]) ^ 0x20; }
            out.push_back(b);
        }
    }

    void cmd_punto(const std::string& p, bool poner) {
        // Z<tipo>,<dir>,<tam>
        const size_t c1 = p.find(',');
        if (c1 == std::string::npos) { responder("E01"); return; }
        const size_t c2 = p.find(',', c1 + 1);
        const unsigned tipo = num(p, 1, 16);
        const uint32_t a = num(p, c1 + 1, 16);
        const unsigned tam = (c2 == std::string::npos) ? 2u : num(p, c2 + 1, 16);
        bool ok = false;
        if (tipo == 0 || tipo == 1) ok = poner ? poner_bp(a) : quitar_bp(a);
        else if (tipo >= 2 && tipo <= 4) ok = poner ? poner_wp(a, tipo, tam)
                                                    : quitar_wp(a);
        responder(ok ? "OK" : "E01");
    }

    void cmd_reset() {
        // SYSRESETREQ por AIRCR, y captura del vector de reset para quedarse
        // parado en la primera instruccion: es lo que hace `monitor reset halt`.
        escribir(R_DEMCR, leer_o(R_DEMCR) | 1u);      // VC_CORERESET
        escribir(R_AIRCR, 0x05FA0004u);
        sc_core::wait(sc_core::sc_time(500, sc_core::SC_US));
        enganchar();                                  // el DP se resetea con el chip
        escribir(R_DEMCR, leer_o(R_DEMCR) & ~1u);
        parar();
        corriendo_ = false;
    }

    void cmd_q(const std::string& p) {
        if (p.rfind("qSupported", 0) == 0) {
            responder("PacketSize=1000;qXfer:features:read+;QStartNoAckMode+;"
                      "swbreak+;hwbreak+;vContSupported+");
            return;
        }
        if (p.rfind("qXfer:features:read:target.xml:", 0) == 0) {
            const size_t c = p.rfind(',');
            const size_t d = p.rfind(':');
            const unsigned off = num(p, d + 1, 16);
            const unsigned len = num(p, c + 1, 16);
            const std::string& x = target_xml();
            if (off >= x.size()) { responder("l"); return; }
            const unsigned n = (len < x.size() - off) ? len : unsigned(x.size() - off);
            responder((off + n < x.size() ? "m" : "l") + x.substr(off, n));
            return;
        }
        if (p == "qAttached")   { responder("1"); return; }
        if (p == "qC")          { responder("QC1"); return; }
        if (p == "qfThreadInfo"){ responder("m1"); return; }
        if (p == "qsThreadInfo"){ responder("l"); return; }
        if (p == "qOffsets")    { responder("Text=0;Data=0;Bss=0"); return; }
        if (p.rfind("qRcmd,", 0) == 0) { cmd_monitor(p.substr(6)); return; }
        responder("");
    }

    // `monitor ...` desde la consola de GDB o desde el IDE.
    void cmd_monitor(const std::string& hex) {
        std::string c;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            c.push_back(char(dehex(hex[i]) * 16u + dehex(hex[i + 1])));
        if (c.rfind("reset", 0) == 0)      { cmd_reset(); responder("OK"); return; }
        if (c.rfind("halt", 0) == 0)       { parar(); corriendo_ = false;
                                             responder("OK"); return; }
        if (c.rfind("resume", 0) == 0)     { seguir(); corriendo_ = true;
                                             responder("OK"); return; }
        responder("");
    }

    void cmd_v(const std::string& p) {
        if (p == "vCont?") { responder("vCont;c;C;s;S"); return; }
        if (p.rfind("vCont;", 0) == 0) {
            const char a = p[6];
            if (a == 's' || a == 'S') { paso(); responder(parada()); return; }
            corriendo_ = true; seguir();
            return;
        }
        if (p == "vMustReplyEmpty") { responder(""); return; }
        if (p.rfind("vFlashErase:", 0) == 0) {
            const size_t c = p.find(',', 12);
            const uint32_t a = num(p, 12, 16);
            const uint32_t n = num(p, c + 1, 16);
            responder(flash_borrar(a, n) ? "OK" : "E01");
            return;
        }
        if (p.rfind("vFlashWrite:", 0) == 0) {
            const size_t d = p.find(':', 12);
            const uint32_t a = num(p, 12, 16);
            std::vector<uint8_t> dat;
            desescapar(p, d + 1, dat);
            responder(flash_programar(a, dat.data(), unsigned(dat.size())) ? "OK"
                                                                          : "E01");
            return;
        }
        if (p == "vFlashDone") { flash_bloquear(); responder("OK"); return; }
        responder("");
    }

    // La descripción del objetivo. Es lo que le dice a GDB que esto es un
    // Cortex-M y cuántos registros tiene: sin ella supondría un ARM clásico con
    // sus registros de coma flotante FPA y el paquete `g` no cuadraría.
    static const std::string& target_xml() {
        static const std::string x =
            "<?xml version=\"1.0\"?>"
            "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
            "<target version=\"1.0\">"
            "<architecture>arm</architecture>"
            "<feature name=\"org.gnu.gdb.arm.m-profile\">"
            "<reg name=\"r0\" bitsize=\"32\"/><reg name=\"r1\" bitsize=\"32\"/>"
            "<reg name=\"r2\" bitsize=\"32\"/><reg name=\"r3\" bitsize=\"32\"/>"
            "<reg name=\"r4\" bitsize=\"32\"/><reg name=\"r5\" bitsize=\"32\"/>"
            "<reg name=\"r6\" bitsize=\"32\"/><reg name=\"r7\" bitsize=\"32\"/>"
            "<reg name=\"r8\" bitsize=\"32\"/><reg name=\"r9\" bitsize=\"32\"/>"
            "<reg name=\"r10\" bitsize=\"32\"/><reg name=\"r11\" bitsize=\"32\"/>"
            "<reg name=\"r12\" bitsize=\"32\"/>"
            "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
            "<reg name=\"lr\" bitsize=\"32\"/>"
            "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
            "<reg name=\"xpsr\" bitsize=\"32\" regnum=\"25\"/>"
            "</feature>"
            "<feature name=\"org.gnu.gdb.arm.m-system\">"
            "<reg name=\"msp\" bitsize=\"32\" type=\"data_ptr\"/>"
            "<reg name=\"psp\" bitsize=\"32\" type=\"data_ptr\"/>"
            "<reg name=\"primask\" bitsize=\"32\"/>"
            "<reg name=\"basepri\" bitsize=\"32\"/>"
            "<reg name=\"faultmask\" bitsize=\"32\"/>"
            "<reg name=\"control\" bitsize=\"32\"/>"
            "</feature>"
            "</target>";
        return x;
    }

    // --- Estado --------------------------------------------------------------
    red::socket_t srv_ = red::invalido(), cli_ = red::invalido();
    std::string rx_, ultimo_;
    bool     verboso_ = false, sin_ack_ = false, corriendo_ = false;
    bool     anunciado_ = false;
    bool     halt_al_conectar_ = true, activo_ = true;
    unsigned n_paq_ = 0, n_flash_ = 0, n_acc_ = 0, espera_ = 0;
    unsigned vueltas_vigilancia_ = 8;
    sc_core::sc_time poll_{100, sc_core::SC_US};
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif // STM32_COMMON_GDB_RSP_H
