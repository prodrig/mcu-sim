// =============================================================================
// sonda_cmsis_dap.h — Una sonda CMSIS-DAP por TCP, en los pines SWD
//
// Fase X2 de `doc/analisis_xds110.md`. OpenOCD trae, desde septiembre de 2025,
// un *backend* `tcp` para su controlador `cmsis-dap`
// (src/jtag/drivers/cmsis_dap_tcp.c): los mismos paquetes CMSIS-DAP que irían
// por USB, por un socket. Esta pieza es el otro extremo: recibe las órdenes,
// las ejecuta como SWD en SWCLK y SWDIO -con los tiempos de la velocidad que
// pida `DAP_SWJ_Clock`- y contesta. Sin USB/IP, sin drivers, en las tres
// plataformas:
//
//     openocd -c "adapter driver cmsis-dap" -c "cmsis-dap backend tcp"
//             -c "cmsis-dap tcp host localhost" -c "cmsis-dap tcp port 4441"
//             -c "transport select swd" -f target/ti/msp432.cfg
//
// (una sola línea; aquí partida para leerla)
//
// FRENTE A `remote_bitbang` (sonda_openocd.h), que es la misma idea un nivel
// más abajo: allí OpenOCD manda un carácter por flanco y espera la respuesta
// de cada bit leído, así que cada lectura es un viaje por el socket. Aquí un
// paquete trae decenas de transferencias del DAP y la sonda las ejecuta todas
// antes de contestar, como hace el firmware de una sonda de verdad. El
// protocolo SWD lo escribimos nosotros, con lo que exige ADIv5 (WAIT, FAULT,
// lecturas del AP aplazadas), como en swd_port.h.
//
// EL MARCO TCP [cmsis_dap_tcp.c]: cada orden y cada respuesta llevan delante
// 8 bytes en little endian: la firma "DAP\0" (0x00504144), la longitud de la
// carga (16 bits), el tipo (1 orden, 2 respuesta) y un byte reservado. La
// carga es el paquete CMSIS-DAP de siempre, de hasta 1024 bytes.
//
// LAS ÓRDENES [ARM, CMSIS-DAP 2.1, «Commands»]: DAP_Info, DAP_HostStatus,
// DAP_Connect (solo SWD), DAP_Disconnect, DAP_TransferConfigure,
// DAP_Transfer, DAP_TransferBlock, DAP_TransferAbort, DAP_WriteABORT,
// DAP_Delay, DAP_ResetTarget, DAP_SWJ_Pins (con nRESET, que es SRST),
// DAP_SWJ_Clock, DAP_SWJ_Sequence, DAP_SWD_Configure, DAP_SWD_Sequence y
// DAP_ExecuteCommands. JTAG y SWO contestan DAP_Invalid (0xFF), y DAP_Info lo
// dice en las capacidades: solo SWD.
//
// EL TIEMPO es el de la sonda de OpenOCD: mientras OpenOCD calla, la
// simulación avanza lo que pasa en el reloj de pared (con `espera_us` de
// tope), para que un algoritmo de Flash que OpenOCD sondea en tiempo de pared
// acabe a tiempo; y si no llega nada, a pasos de `poll`.
//
// NO es parte del MCU: vive fuera y no ve nada que no pase por SWCLK, SWDIO y
// NRST.
// =============================================================================
#ifndef STM32_VERIF_SONDA_CMSIS_DAP_H
#define STM32_VERIF_SONDA_CMSIS_DAP_H

#include <systemc>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../common/analog_net.h"
#include "../common/red.h"
#include "sonda_red.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// El protocolo, sin SystemC: lo que se puede probar sin simular nada
// ---------------------------------------------------------------------------
namespace cmsis_dap {

inline constexpr uint32_t FIRMA = 0x00504144u;            // "DAP\0"
inline constexpr uint8_t  ORDEN = 1, RESPUESTA = 2;
inline constexpr unsigned CABECERA = 8;
inline constexpr unsigned PAQUETE = 1024;                  // carga máxima
inline constexpr unsigned PUERTO_OMISION = 4441;           // el de OpenOCD

enum : uint8_t {
    INFO = 0x00, HOST_STATUS = 0x01, CONNECT = 0x02, DISCONNECT = 0x03,
    TRANSFER_CONFIGURE = 0x04, TRANSFER = 0x05, TRANSFER_BLOCK = 0x06,
    TRANSFER_ABORT = 0x07, WRITE_ABORT = 0x08, DELAY = 0x09, RESET_TARGET = 0x0A,
    SWJ_PINS = 0x10, SWJ_CLOCK = 0x11, SWJ_SEQUENCE = 0x12, SWD_CONFIGURE = 0x13,
    SWD_SEQUENCE = 0x1D, QUEUE_COMMANDS = 0x7E, EXECUTE_COMMANDS = 0x7F,
    INVALIDA = 0xFF
};
// Los bits de la respuesta de una transferencia
enum : uint8_t { ACK_OK = 1, ACK_WAIT = 2, ACK_FAULT = 4, ACK_NADA = 7,
                 ERROR_PROTOCOLO = 0x08, NO_COINCIDE = 0x10 };

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
inline void pon32(std::vector<uint8_t>& v, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}
inline void pon16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}
// Un paquete con su cabecera
inline std::string enmarca(const std::vector<uint8_t>& carga, uint8_t tipo = RESPUESTA) {
    std::string s;
    for (unsigned i = 0; i < 4; ++i) s.push_back(char(uint8_t(FIRMA >> (8 * i))));
    s.push_back(char(uint8_t(carga.size()))); s.push_back(char(uint8_t(carga.size() >> 8)));
    s.push_back(char(tipo)); s.push_back(0);
    s.append(reinterpret_cast<const char*>(carga.data()), carga.size());
    return s;
}
// Saca un paquete completo de `rx`. 1: hay uno en `carga`; 0: falta; -1: el
// marco no vale (y lo que haya se descarta)
inline int desenmarca(std::string& rx, std::vector<uint8_t>& carga, std::string& error) {
    if (rx.size() < CABECERA) return 0;
    const uint8_t* h = reinterpret_cast<const uint8_t*>(rx.data());
    if (le32(h) != FIRMA) { error = "firma que no es \"DAP\""; rx.clear(); return -1; }
    if (h[6] != ORDEN) { error = "un paquete que no es una orden"; rx.clear(); return -1; }
    const unsigned n = le16(h + 4);
    if (n > PAQUETE) { error = "un paquete de mas de 1024 bytes"; rx.clear(); return -1; }
    if (rx.size() < CABECERA + n) return 0;
    carga.assign(h + CABECERA, h + CABECERA + n);
    rx.erase(0, CABECERA + n);
    return 1;
}

} // namespace cmsis_dap

// ---------------------------------------------------------------------------
// La sonda
// ---------------------------------------------------------------------------
SC_MODULE(SondaCmsisDap), public SondaRed {
    // `reset(true)` pone NRST a masa y `reset(false)` lo suelta. `nrst`, si se
    // da, es el nodo que se lee para contestar DAP_SWJ_Pins.
    SondaCmsisDap(sc_core::sc_module_name nm, analog_net_if& swclk, analog_net_if& swdio,
                  unsigned puerto, std::function<void(bool)> reset = {},
                  analog_net_if* nrst = nullptr, double vdd = 3.3)
        : sc_core::sc_module(nm), clk_(&swclk), dio_(&swdio), nrst_(nrst), puerto_(puerto),
          reset_(std::move(reset)), vdd_(vdd) {
        id_clk_ = clk_->register_driver("dap_clk");
        id_dio_ = dio_->register_driver("dap_dio");
        clk_->set_hiz(id_clk_);
        dio_->set_hiz(id_dio_);
        pon_hz(1e6);
        SC_HAS_PROCESS(SondaCmsisDap);
        SC_THREAD(run);
    }
    ~SondaCmsisDap() override {
        red::cerrar(cli_);
        red::cerrar(srv_);
    }

    void set_enabled(bool on) override {
        activo_ = on;
        if (on && !anunciado_ && abrir()) {
            anunciado_ = true;
            std::printf("[cmsis-dap] escuchando en localhost:%u\n", puerto_);
            std::fflush(stdout);
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void set_verbose(bool v) override { verboso_ = v; }
    unsigned puerto() const override { return puerto_; }
    const char* protocolo() const override { return "CMSIS-DAP"; }
    void set_poll(sc_core::sc_time t) { poll_ = t; }
    void set_espera_us(long us) { espera_us_ = us; }

    // --- Lo que se ve desde fuera ---------------------------------------------
    bool escuchando() const { return red::valido(srv_); }
    bool conectado() const override { return red::valido(cli_); }
    unsigned conexiones() const { return n_conex_; }
    uint64_t ordenes() const { return n_ordenes_; }
    uint64_t transferencias() const { return n_transf_; }
    uint64_t esperas() const { return n_wait_; }       // ACK WAIT recibidos
    unsigned resets() const { return n_resets_; }
    double hz() const { return hz_; }

private:
    using Carga = std::vector<uint8_t>;

    analog_net_if *clk_, *dio_, *nrst_;
    int id_clk_ = -1, id_dio_ = -1;
    unsigned puerto_;
    std::function<void(bool)> reset_;
    double vdd_;
    double hz_ = 1e6;
    sc_core::sc_time medio_, cuarto_;
    sc_core::sc_time poll_{100, sc_core::SC_US};
    sc_core::sc_time poll_activo_{20, sc_core::SC_US};
    long espera_us_ = 200;
    unsigned pasos_activo_ = 500;
    bool activo_ = false, anunciado_ = false, verboso_ = false;
    sc_core::sc_event ev_;
    red::socket_t srv_ = red::invalido(), cli_ = red::invalido();
    std::string rx_, tx_;
    // La configuración que pide el anfitrión
    unsigned idle_ = 0, wait_retry_ = 100, match_retry_ = 0, turn_ = 1;
    bool fase_datos_ = false, conectada_ = false, srst_ = false;
    uint32_t match_mask_ = 0xFFFFFFFFu;
    unsigned n_conex_ = 0, n_resets_ = 0;
    uint64_t n_ordenes_ = 0, n_transf_ = 0, n_wait_ = 0;

    bool abrir() {
        if (red::valido(srv_)) return true;
        if (!puerto_ || !red::arranca()) return false;
        srv_ = red::escucha_local(puerto_);
        if (!red::valido(srv_)) {
            std::printf("[cmsis-dap] no se pudo abrir el puerto %u\n", puerto_);
            std::fflush(stdout);
            return false;
        }
        return true;
    }

    // --- La capa física -----------------------------------------------------
    void pon_hz(double hz) {
        if (hz < 1e3) hz = 1e3;
        if (hz > 50e6) hz = 50e6;
        hz_ = hz;
        medio_ = sc_core::sc_time(0.5 / hz, sc_core::SC_SEC);
        cuarto_ = sc_core::sc_time(0.25 / hz, sc_core::SC_SEC);
    }
    void clk(bool v) { clk_->set_drive(id_clk_, v ? float(vdd_) : 0.0f, 30.0f); }
    void gobierna(bool v) { dio_->set_drive(id_dio_, v ? float(vdd_) : 0.0f, 30.0f); }
    void suelta_dio() { dio_->set_hiz(id_dio_); }
    bool lee_dio() const { return dio_->voltage() > 0.5 * vdd_; }
    // Un ciclo: la sonda cambia SWDIO con SWCLK bajo y el objetivo lo muestrea
    // en la subida; al revés cuando contesta. Devuelve lo leído en la subida.
    bool ciclo(int nivel) {                  // -1: la línea, suelta
        if (nivel >= 0) gobierna(nivel != 0); else suelta_dio();
        wait(medio_);
        clk(true);
        wait(cuarto_);
        const bool v = lee_dio();
        wait(cuarto_);
        clk(false);
        return v;
    }
    void pon_srst(bool activo) {
        if (activo && !srst_) ++n_resets_;
        if (activo != srst_ && reset_) reset_(activo);
        srst_ = activo;
    }
    void suelta_todo() {
        clk_->set_hiz(id_clk_);
        suelta_dio();
        pon_srst(false);
        conectada_ = false;
    }

    static bool paridad(uint32_t v) {
        unsigned n = 0;
        for (unsigned i = 0; i < 32; ++i) n += (v >> i) & 1u;
        return (n & 1u) != 0;
    }

    // Un paquete SWD. `peticion` son los bits 0-3 de la de CMSIS-DAP
    // (APnDP, RnW, A2, A3). Devuelve el ACK, o ACK|ERROR_PROTOCOLO si la
    // paridad del dato leído no cuadra.
    uint8_t paquete(uint8_t peticion, uint32_t& dato) {
        const bool ap = peticion & 1u, lee = peticion & 2u;
        const unsigned a2 = (peticion >> 2) & 1u, a3 = (peticion >> 3) & 1u;
        const unsigned par = (ap ? 1u : 0u) + (lee ? 1u : 0u) + a2 + a3;
        ciclo(1);
        ciclo(ap); ciclo(lee); ciclo(int(a2)); ciclo(int(a3));
        ciclo(int(par & 1u));
        ciclo(0);
        ciclo(1);
        for (unsigned i = 0; i < turn_; ++i) ciclo(-1);
        unsigned ack = 0;
        for (unsigned i = 0; i < 3; ++i) if (ciclo(-1)) ack |= 1u << i;
        ++n_transf_;
        if (ack == cmsis_dap::ACK_OK) {
            if (lee) {
                uint32_t v = 0;
                for (unsigned i = 0; i < 32; ++i) if (ciclo(-1)) v |= 1u << i;
                const bool p = ciclo(-1);
                for (unsigned i = 0; i < turn_; ++i) ciclo(-1);
                dato = v;
                reposo();
                return p == paridad(v) ? uint8_t(cmsis_dap::ACK_OK)
                                       : uint8_t(cmsis_dap::ACK_OK | cmsis_dap::ERROR_PROTOCOLO);
            }
            for (unsigned i = 0; i < turn_; ++i) ciclo(-1);
            for (unsigned i = 0; i < 32; ++i) ciclo(int((dato >> i) & 1u));
            ciclo(paridad(dato) ? 1 : 0);
            reposo();
            return cmsis_dap::ACK_OK;
        }
        if (ack == cmsis_dap::ACK_WAIT || ack == cmsis_dap::ACK_FAULT) {
            if (ack == cmsis_dap::ACK_WAIT) ++n_wait_;
            if (fase_datos_) for (unsigned i = 0; i < 33; ++i) ciclo(-1);
            for (unsigned i = 0; i < turn_; ++i) ciclo(-1);
            reposo();
            return uint8_t(ack);
        }
        // Nadie contesta (o basura): la sonda de ARM deja pasar el tiempo de
        // un paquete entero antes de seguir
        for (unsigned i = 0; i < turn_ + 33; ++i) ciclo(-1);
        reposo();
        return cmsis_dap::ACK_NADA;
    }
    // Los ciclos de reposo que pide TransferConfigure, con la línea a cero,
    // y al menos dos: los que da también swd_port.h, y con los que el DP del
    // modelo termina de procesar el paquete
    void reposo() {
        const unsigned n = idle_ > 2 ? idle_ : 2;
        for (unsigned i = 0; i < n; ++i) ciclo(0);
        suelta_dio();
    }
    // Con los reintentos de WAIT que ha pedido el anfitrión (OpenOCD pide 64),
    // ESPERANDO entre uno y otro, cada vez el doble hasta un milisegundo. Un
    // WAIT del modelo dura lo que el bus esté ocupado -el borrado de un
    // sector del F407 lo tiene 1 ms-, y 64 paquetes seguidos son menos que
    // eso: OpenOCD no reintenta por su cuenta y daría el borrado por fallido.
    // Con la espera, 64 reintentos cubren unos 60 ms.
    uint8_t con_reintentos(uint8_t peticion, uint32_t& dato) {
        uint8_t r = 0;
        double pausa_us = 2.0;
        for (unsigned i = 0; i <= wait_retry_; ++i) {
            r = paquete(peticion, dato);
            if ((r & 7u) != cmsis_dap::ACK_WAIT) break;
            wait(sc_core::sc_time(pausa_us, sc_core::SC_US));
            if (pausa_us < 1000.0) pausa_us *= 2.0;
        }
        return r;
    }
    // Leer un registro del AP es pedirlo y recogerlo después en RDBUFF: la
    // sonda lo hace por el anfitrión, que recibe el valor bueno
    uint8_t lee_reg(uint8_t peticion, uint32_t& v) {
        uint8_t r = con_reintentos(peticion, v);
        if ((r & 7u) != cmsis_dap::ACK_OK || !(peticion & 1u)) return r;
        if (r & cmsis_dap::ERROR_PROTOCOLO) return r;
        return con_reintentos(0x0E, v);                // DP RDBUFF (A=0xC, RnW)
    }

    // --- Las órdenes ---------------------------------------------------------------
    static void info_texto(Carga& o, const char* t) {
        const std::size_t n = std::char_traits<char>::length(t);
        o.push_back(uint8_t(n + 1));
        for (std::size_t i = 0; i < n; ++i) o.push_back(uint8_t(t[i]));
        o.push_back(0);
    }
    void info(uint8_t id, Carga& o) {
        switch (id) {
        case 0x01: info_texto(o, "mcu-sim"); break;
        case 0x02: info_texto(o, "mcu-sim CMSIS-DAP (TCP)"); break;
        case 0x03: info_texto(o, "MCUSIM01"); break;
        case 0x04: info_texto(o, "2.1.0"); break;
        case 0x09: info_texto(o, "1.0"); break;
        case 0xF0: o.push_back(1); o.push_back(0x01); break;   // solo SWD
        case 0xFE: o.push_back(1); o.push_back(4); break;      // paquetes en vuelo
        case 0xFF: o.push_back(2); cmsis_dap::pon16(o, uint16_t(cmsis_dap::PAQUETE)); break;
        case 0xFB: case 0xFC: case 0xFD:
            o.push_back(4); cmsis_dap::pon32(o, 0); break;
        default: o.push_back(0); break;                        // no lo sabe
        }
    }

    // Una orden de `in` desde `k`; deja la respuesta en `o` y avanza `k`.
    // false si la orden está cortada (se contesta DAP_Invalid).
    bool una(const Carga& in, std::size_t& k, Carga& o) {
        auto quedan = [&](std::size_t n) { return k + n <= in.size(); };
        if (!quedan(1)) return false;
        const uint8_t c = in[k++];
        ++n_ordenes_;
        switch (c) {
        case cmsis_dap::INFO:
            if (!quedan(1)) return false;
            o.push_back(c); info(in[k++], o); return true;
        case cmsis_dap::HOST_STATUS:
            if (!quedan(2)) return false;
            k += 2; o.push_back(c); o.push_back(0); return true;
        case cmsis_dap::CONNECT: {
            if (!quedan(1)) return false;
            const uint8_t p = in[k++];
            o.push_back(c);
            if (p == 0 || p == 1) {
                conectada_ = true;
                clk(false); gobierna(true);
                o.push_back(1);                                // SWD
            } else {
                o.push_back(0);                                // JTAG: no
            }
            return true;
        }
        case cmsis_dap::DISCONNECT:
            clk_->set_hiz(id_clk_); suelta_dio(); conectada_ = false;
            o.push_back(c); o.push_back(0); return true;
        case cmsis_dap::TRANSFER_CONFIGURE:
            if (!quedan(5)) return false;
            idle_ = in[k];
            wait_retry_ = cmsis_dap::le16(&in[k + 1]);
            match_retry_ = cmsis_dap::le16(&in[k + 3]);
            k += 5;
            o.push_back(c); o.push_back(0); return true;
        case cmsis_dap::TRANSFER: return transfer(in, k, o);
        case cmsis_dap::TRANSFER_BLOCK: return transfer_block(in, k, o);
        case cmsis_dap::TRANSFER_ABORT:
            return true;                                       // no tiene respuesta
        case cmsis_dap::WRITE_ABORT: {
            if (!quedan(5)) return false;
            uint32_t v = cmsis_dap::le32(&in[k + 1]);
            k += 5;
            const uint8_t r = con_reintentos(0x00, v);        // DP ABORT
            o.push_back(c); o.push_back((r & 7u) == cmsis_dap::ACK_OK ? 0 : 0xFF);
            return true;
        }
        case cmsis_dap::DELAY: {
            if (!quedan(2)) return false;
            const unsigned us = cmsis_dap::le16(&in[k]);
            k += 2;
            if (us) wait(sc_core::sc_time(double(us), sc_core::SC_US));
            o.push_back(c); o.push_back(0); return true;
        }
        case cmsis_dap::RESET_TARGET:
            // Sin secuencia propia del dispositivo: OpenOCD resetea con
            // DAP_SWJ_Pins (SRST) o por el núcleo
            o.push_back(c); o.push_back(0); o.push_back(0); return true;
        case cmsis_dap::SWJ_PINS: {
            if (!quedan(6)) return false;
            const uint8_t sal = in[k], sel = in[k + 1];
            const uint32_t espera = cmsis_dap::le32(&in[k + 2]);
            k += 6;
            if (sel & 0x01) clk(sal & 0x01);
            if (sel & 0x02) gobierna(sal & 0x02);
            if (sel & 0x80) pon_srst(!(sal & 0x80));           // nRESET a 0: reset
            // Al menos un microsegundo: lo que se acaba de poner tiene que
            // llegar al nodo antes de leerlo
            wait(sc_core::sc_time(double(espera < 1 ? 1 : (espera < 10000 ? espera : 10000)),
                                  sc_core::SC_US));
            uint8_t ent = 0;
            if (clk_->voltage() > 0.5 * vdd_) ent |= 0x01;
            if (lee_dio()) ent |= 0x02;
            const bool rst_alto = nrst_ ? nrst_->voltage() > 0.5 * vdd_ : !srst_;
            if (rst_alto) ent |= 0x80;
            ent |= 0x20;                                        // nTRST: no hay
            o.push_back(c); o.push_back(ent); return true;
        }
        case cmsis_dap::SWJ_CLOCK:
            if (!quedan(4)) return false;
            pon_hz(double(cmsis_dap::le32(&in[k])));
            k += 4;
            o.push_back(c); o.push_back(0); return true;
        case cmsis_dap::SWJ_SEQUENCE: {
            if (!quedan(1)) return false;
            unsigned n = in[k++];
            if (n == 0) n = 256;
            if (!quedan((n + 7) / 8)) return false;
            for (unsigned i = 0; i < n; ++i) ciclo(int((in[k + i / 8] >> (i % 8)) & 1u));
            k += (n + 7) / 8;
            o.push_back(c); o.push_back(0); return true;
        }
        case cmsis_dap::SWD_CONFIGURE:
            if (!quedan(1)) return false;
            turn_ = (in[k] & 3u) + 1u;
            fase_datos_ = (in[k] & 4u) != 0;
            ++k;
            o.push_back(c); o.push_back(0); return true;
        case cmsis_dap::SWD_SEQUENCE: {
            if (!quedan(1)) return false;
            const unsigned nseq = in[k++];
            Carga leido;
            for (unsigned s = 0; s < nseq; ++s) {
                if (!quedan(1)) return false;
                const uint8_t inf = in[k++];
                unsigned n = inf & 0x3Fu;
                if (n == 0) n = 64;
                const unsigned nb = (n + 7) / 8;
                if (inf & 0x80u) {                              // entrada
                    std::vector<uint8_t> b(nb, 0);
                    for (unsigned i = 0; i < n; ++i) if (ciclo(-1)) b[i / 8] |= uint8_t(1u << (i % 8));
                    leido.insert(leido.end(), b.begin(), b.end());
                } else {
                    if (!quedan(nb)) return false;
                    for (unsigned i = 0; i < n; ++i) ciclo(int((in[k + i / 8] >> (i % 8)) & 1u));
                    k += nb;
                }
            }
            o.push_back(c); o.push_back(0);
            o.insert(o.end(), leido.begin(), leido.end());
            return true;
        }
        case cmsis_dap::EXECUTE_COMMANDS: case cmsis_dap::QUEUE_COMMANDS: {
            if (!quedan(1)) return false;
            const unsigned n = in[k++];
            o.push_back(cmsis_dap::EXECUTE_COMMANDS); o.push_back(uint8_t(n));
            for (unsigned i = 0; i < n; ++i) if (!una(in, k, o)) return false;
            return true;
        }
        default:
            // JTAG, SWO, UART, los de fabricante: no los hay
            if (verboso_)
                std::printf("[cmsis-dap] orden que esta sonda no atiende: 0x%02X\n", unsigned(c));
            k = in.size();
            o.push_back(cmsis_dap::INVALIDA);
            return true;
        }
    }

    // DAP_Transfer: [indice][n]{[peticion][dato si escribe o compara]}
    bool transfer(const Carga& in, std::size_t& k, Carga& o) {
        if (k + 2 > in.size()) return false;
        ++k;                                                   // DAP index: uno
        const unsigned n = in[k++];
        o.push_back(cmsis_dap::TRANSFER);
        const std::size_t pos_n = o.size();
        o.push_back(0); o.push_back(0);
        unsigned hechas = 0;
        uint8_t r = cmsis_dap::ACK_OK;
        bool error = false;
        for (unsigned t = 0; t < n; ++t) {
            if (k >= in.size()) return false;
            const uint8_t p = in[k++];
            const bool lee = p & 0x02, compara = p & 0x10, mascara = p & 0x20;
            uint32_t d = 0;
            if (!lee || compara) {
                if (k + 4 > in.size()) return false;
                d = cmsis_dap::le32(&in[k]);
                k += 4;
            }
            if (error) continue;                               // se saltan, sin hacerse
            if (!lee && mascara) { match_mask_ = d; ++hechas; continue; }
            if (lee && compara) {
                uint32_t v = 0;
                unsigned intentos = 0;
                for (;;) {
                    r = lee_reg(p & 0x0F, v);
                    if ((r & 7u) != cmsis_dap::ACK_OK || (r & cmsis_dap::ERROR_PROTOCOLO)) break;
                    if ((v & match_mask_) == (d & match_mask_)) break;
                    if (intentos++ >= match_retry_) { r |= cmsis_dap::NO_COINCIDE; break; }
                }
                if (r != cmsis_dap::ACK_OK) { error = true; continue; }
                ++hechas;
                continue;
            }
            if (lee) {
                uint32_t v = 0;
                r = lee_reg(p & 0x0F, v);
                if (r != cmsis_dap::ACK_OK) { error = true; continue; }
                cmsis_dap::pon32(o, v);
            } else {
                r = con_reintentos(p & 0x0F, d);
                if (r != cmsis_dap::ACK_OK) { error = true; continue; }
            }
            ++hechas;
        }
        o[pos_n] = uint8_t(hechas);
        o[pos_n + 1] = r;
        return true;
    }

    // DAP_TransferBlock: [indice][n:16][peticion]{[dato]}. Las lecturas del
    // AP van aplazadas: la primera solo pide, cada una recoge la anterior, y
    // la última se recoge en RDBUFF
    bool transfer_block(const Carga& in, std::size_t& k, Carga& o) {
        if (k + 4 > in.size()) return false;
        ++k;
        const unsigned n = cmsis_dap::le16(&in[k]);
        k += 2;
        const uint8_t p = in[k++] & 0x0F;
        const bool lee = p & 0x02, ap = p & 0x01;
        o.push_back(cmsis_dap::TRANSFER_BLOCK);
        const std::size_t pos = o.size();
        o.push_back(0); o.push_back(0); o.push_back(0);
        unsigned hechas = 0;
        uint8_t r = cmsis_dap::ACK_OK;
        if (lee && ap && n) {
            uint32_t v = 0;
            r = con_reintentos(p, v);                          // pide la primera
            for (unsigned i = 1; i <= n && r == cmsis_dap::ACK_OK; ++i) {
                r = con_reintentos(i < n ? p : uint8_t(0x0E), v);
                if (r != cmsis_dap::ACK_OK) break;
                cmsis_dap::pon32(o, v);
                ++hechas;
            }
        } else {
            const std::size_t k0 = k;
            if (!lee && k0 + 4u * n > in.size()) return false;
            for (unsigned i = 0; i < n; ++i) {
                uint32_t d = lee ? 0u : cmsis_dap::le32(&in[k0 + 4u * i]);
                r = con_reintentos(p, d);
                if (r != cmsis_dap::ACK_OK) break;
                if (lee) cmsis_dap::pon32(o, d);
                ++hechas;
            }
            if (!lee) k = k0 + 4u * n;      // los datos que no se escribieron, tambien
        }
        o[pos] = uint8_t(hechas);
        o[pos + 1] = uint8_t(hechas >> 8);
        o[pos + 2] = r;
        return true;
    }

    // Una orden entera, con su respuesta (vacía si no tiene)
    Carga atiende(const Carga& in) {
        Carga o;
        std::size_t k = 0;
        if (!una(in, k, o)) { o.clear(); o.push_back(cmsis_dap::INVALIDA); }
        return o;
    }

    // --- El socket ---------------------------------------------------------------
    void aceptar() {
        if (red::valido(cli_) || !red::valido(srv_)) return;
        cli_ = red::acepta(srv_);
        if (!red::valido(cli_)) return;
        red::no_bloqueante(cli_);
        red::sin_nagle(cli_);
        red::sin_sigpipe(cli_);
        ++n_conex_;
        rx_.clear(); tx_.clear();
        std::printf("[cmsis-dap] cliente conectado\n");
        std::fflush(stdout);
    }
    void desconectar() {
        red::cerrar(cli_);
        suelta_todo();
        std::printf("[cmsis-dap] cliente desconectado (%llu ordenes, %llu transferencias SWD, "
                    "%llu WAIT)\n", (unsigned long long)n_ordenes_,
                    (unsigned long long)n_transf_, (unsigned long long)n_wait_);
        std::fflush(stdout);
    }
    bool recibe() {
        char b[4096];
        for (;;) {
            const long r = red::recibir(cli_, b, sizeof b);
            if (r > 0) { rx_.append(b, size_t(r)); continue; }
            if (r == 0) return false;
            return red::reintentar();
        }
    }
    bool envia() {
        size_t k = 0;
        while (k < tx_.size()) {
            const long r = red::enviar(cli_, tx_.data() + k, tx_.size() - k);
            if (r > 0) { k += size_t(r); continue; }
            if (r < 0 && red::reintentar()) { red::detalle::espera_escribible(cli_, 100); continue; }
            return false;
        }
        tx_.clear();
        return true;
    }

    void run() {
        unsigned activo = 0;
        for (;;) {
            if (!activo_ || !red::valido(srv_)) { wait(poll_, ev_); continue; }
            if (!red::valido(cli_)) {
                aceptar();
                if (!red::valido(cli_)) { wait(poll_, ev_); continue; }
            }
            if (!recibe()) { desconectar(); activo = 0; continue; }
            Carga orden;
            std::string error;
            const int hay = cmsis_dap::desenmarca(rx_, orden, error);
            if (hay < 0) {
                std::printf("[cmsis-dap] %s: se cierra la conexion\n", error.c_str());
                std::fflush(stdout);
                desconectar();
                continue;
            }
            if (hay == 0) {
                // Lo mismo que la sonda de OpenOCD: mientras el anfitrión
                // calla, el tiempo simulado sigue al de pared
                if (activo == pasos_activo_ && espera_us_ > 0) {
                    const auto t0 = std::chrono::steady_clock::now();
                    const bool llega = red::espera_legible_us(cli_, espera_us_);
                    const long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0).count();
                    if (us > 0) wait(sc_core::sc_time(double(us), sc_core::SC_US), ev_);
                    if (llega) continue;
                }
                if (activo) { --activo; wait(poll_activo_, ev_); }
                else wait(poll_, ev_);
                continue;
            }
            const Carga resp = atiende(orden);
            if (!resp.empty()) tx_ += cmsis_dap::enmarca(resp);
            if (!tx_.empty() && !envia()) { desconectar(); activo = 0; continue; }
            activo = pasos_activo_;
        }
    }
};

} // namespace stm32
#endif // STM32_VERIF_SONDA_CMSIS_DAP_H
