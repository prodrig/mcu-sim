// =============================================================================
// sonda_openocd.h — Una sonda SWD para OpenOCD, por `remote_bitbang`
//
// Es la tercera forma de colgarse de los pines de depuración, y la única en la
// que el protocolo SWD NO lo escribimos nosotros: OpenOCD lo genera bit a bit
// en el ordenador, con su propio código ADIv5, y esta pieza solo lo lleva a
// los dos hilos. Así, lo que OpenOCD sabe hacer -identificar el chip, copiar
// su algoritmo de Flash a la SRAM y ejecutarlo en el núcleo simulado,
// `program ... verify reset`, `mdw`, los hilos de un RTOS en GDB- se ejercita
// contra el modelo con código que no es nuestro [doc/analisis_msp432.md, D3 y
// G3]. Sirve igual para un STM32: lo único que cambia es el `.cfg` del
// objetivo.
//
//     openocd -c "adapter driver remote_bitbang"
//             -c "remote_bitbang host localhost" -c "remote_bitbang port 3335"
//             -c "transport select swd" -f target/ti/msp432.cfg
//             -c "program blinky.out verify reset exit"
//
// (una sola linea; aqui partida para leerla)
//
// EL PROTOCOLO (OpenOCD de git, src/jtag/drivers/remote_bitbang.c; la 0.12.0
// no tiene SWD en este controlador: vease doc/fuentes.md): un carácter
// ASCII por orden, por un socket TCP.
//
//   d e f g   SWCLK y SWDIO: 'd' + (SWCLK << 1 | SWDIO)
//   O o       SWDIO de salida (la sonda gobierna) / de entrada (la suelta)
//   c         leer SWDIO: se contesta '0' o '1'
//   r s t u   reset: 'r' + (TRST << 1 | SRST). SRST es NRST, a masa; TRST no
//             existe en SWD y se ignora
//   R         leer TDO (JTAG): no hay JTAG; se contesta '0' para no colgar
//   B b       el LED de la sonda: se ignora
//   Z z       dormir 1 ms / 1 us (solo con `remote_bitbang use_remote_sleep
//             on`): se duerme en tiempo SIMULADO
//   Q         OpenOCD se va
//
// Y la secuencia de un bit, para entender los tiempos: OpenOCD escribe
// (SWCLK=0, dato), lee si toca, y escribe (SWCLK=1, dato). Cada escritura
// dura aquí medio periodo simulado (250 ns a 2 MHz, como la sonda del stub de
// pines), así que un bit es un periodo y el DAP del modelo ve los flancos con
// tiempo entre ellos, como con un cable.
//
// EL TIEMPO. OpenOCD habla en ráfagas: manda un paquete, se para a leer el
// ACK ('c'), espera la respuesta, sigue. Cada lectura de un bit es un viaje
// de ida y vuelta por el socket, así que entre dos órdenes pasa RELOJ DE
// PARED -decenas de microsegundos, a veces milisegundos-. Una placa de verdad
// sigue corriendo durante ese tiempo; aquí, en cada hueco, el tiempo
// SIMULADO avanza lo mismo que ha pasado en el reloj de pared mientras se
// esperaba la orden siguiente (`espera_us` como mucho, 200 us). Mientras
// OpenOCD habla, el modelo va, más o menos, a tiempo real: SWD es síncrono y
// no le importa cuánto tarda la sonda entre dos flancos. Si no llega nada,
// la simulación avanza a pasos cortos (`poll_activo`, 20 us simulados)
// mirando el socket en cada uno; y si sigue sin llegar nada, al paso normal
// (`poll`).
//
// Sin ese avance, los algoritmos no acaban a tiempo: OpenOCD los arranca y
// sondea DHCSR sin pausa, y si el simulado solo avanza lo que duran los bits
// del SWD, sus plazos -en reloj de pared- vencen antes. El de stm32f2x
// programaba diez medias palabras por sondeo y se pasaba de los 10 s que le
// da OpenOCD; y el CRC de `verify_image` acababa en 3 ms simulados que
// OpenOCD tardaba más de 20 s en ver: lo abandonaba a medias, con el LR
// apuntando al BKPT del algoritmo, y el `reset run` siguiente se paraba.
//
// NO es parte del MCU: vive fuera, como el stub de pines (gdb_stub.h), y no ve
// nada del modelo que no pase por SWCLK, SWDIO y NRST.
// =============================================================================
#ifndef STM32_VERIF_SONDA_OPENOCD_H
#define STM32_VERIF_SONDA_OPENOCD_H

#include <systemc>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include "../common/analog_net.h"
#include "../common/red.h"
#include "sonda_red.h"

namespace stm32 {

SC_MODULE(SondaOpenOcd), public SondaRed {
    // `reset(true)` pone NRST a masa y `reset(false)` lo suelta; lo da quien
    // monta la sonda (en sim, mcu_if::reset_pin). Vacío: SRST no hace nada.
    SondaOpenOcd(sc_core::sc_module_name nm, analog_net_if& swclk, analog_net_if& swdio,
                 unsigned puerto, std::function<void(bool)> reset = {},
                 double swd_hz = 2e6, double vdd = 3.3)
        : sc_core::sc_module(nm), clk_(&swclk), dio_(&swdio), puerto_(puerto),
          reset_(std::move(reset)), semi_(sc_core::sc_time(0.5 / swd_hz, sc_core::SC_SEC)),
          vdd_(vdd) {
        id_clk_ = clk_->register_driver("ocd_clk");
        id_dio_ = dio_->register_driver("ocd_dio");
        clk_->set_hiz(id_clk_);
        dio_->set_hiz(id_dio_);
        SC_HAS_PROCESS(SondaOpenOcd);
        SC_THREAD(run);
    }
    ~SondaOpenOcd() override {
        red::cerrar(cli_);
        red::cerrar(srv_);
    }

    // Se abre al arrancar la simulación, no al construirla (como el stub).
    void set_enabled(bool on) override {
        activo_ = on;
        if (on && !anunciado_ && abrir()) {
            anunciado_ = true;
            std::printf("[openocd] remote_bitbang escuchando en localhost:%u\n", puerto_);
            std::fflush(stdout);
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // Cada cuánto tiempo SIMULADO se mira el socket cuando OpenOCD calla
    void set_poll(sc_core::sc_time t) { poll_ = t; }
    // Cuánto RELOJ DE PARED se espera a la siguiente ráfaga antes de dejar
    // correr la simulación, y el paso corto mientras OpenOCD está activo
    void set_espera_us(long us) { espera_us_ = us; }
    void set_poll_activo(sc_core::sc_time t) { poll_activo_ = t; }
    void set_verbose(bool v) override { verboso_ = v; }
    const char* protocolo() const override { return "remote_bitbang"; }

    // --- Estado observable ---------------------------------------------------
    bool escuchando() const { return red::valido(srv_); }
    bool conectado() const override { return red::valido(cli_); }
    unsigned puerto() const override { return puerto_; }
    unsigned conexiones() const { return n_conex_; }
    uint64_t flancos() const { return n_flancos_; }     // de SWCLK
    uint64_t lecturas() const { return n_lect_; }        // 'c'
    unsigned resets() const { return n_resets_; }        // SRST puesto
    unsigned desconocidos() const { return n_raros_; }

private:
    analog_net_if* clk_;
    analog_net_if* dio_;
    int id_clk_ = -1, id_dio_ = -1;
    unsigned puerto_;
    std::function<void(bool)> reset_;
    sc_core::sc_time semi_;
    double vdd_;
    sc_core::sc_time poll_{100, sc_core::SC_US};
    sc_core::sc_time poll_activo_{20, sc_core::SC_US};
    long espera_us_ = 200;
    unsigned pasos_activo_ = 500;       // pasos cortos antes del paso normal
    bool activo_ = false, anunciado_ = false, verboso_ = false;
    sc_core::sc_event ev_;
    red::socket_t srv_ = red::invalido(), cli_ = red::invalido();
    std::string rx_, tx_;
    bool clk_v_ = false, dio_v_ = true, dio_sal_ = false, srst_ = false;
    unsigned n_conex_ = 0, n_resets_ = 0, n_raros_ = 0;
    uint64_t n_flancos_ = 0, n_lect_ = 0;

    bool abrir() {
        if (red::valido(srv_)) return true;
        if (!puerto_ || !red::arranca()) return false;
        srv_ = red::escucha_local(puerto_);
        if (!red::valido(srv_)) {
            std::printf("[openocd] no se pudo abrir el puerto %u\n", puerto_);
            std::fflush(stdout);
            return false;
        }
        return true;
    }

    // --- Los pines ---------------------------------------------------------------
    void pon_clk(bool v) {
        if (v != clk_v_) ++n_flancos_;
        clk_v_ = v;
        clk_->set_drive(id_clk_, v ? float(vdd_) : 0.0f, 30.0f);
    }
    void pon_dio() {
        if (dio_sal_) dio_->set_drive(id_dio_, dio_v_ ? float(vdd_) : 0.0f, 30.0f);
        else dio_->set_hiz(id_dio_);
    }
    bool lee_dio() const { return dio_->voltage() > 0.5 * vdd_; }
    void pon_srst(bool v) {
        if (v && !srst_) ++n_resets_;
        if (v != srst_ && reset_) reset_(v);
        srst_ = v;
    }
    // Al irse OpenOCD, los pines quedan como sin sonda: sueltos, y NRST libre
    void suelta() {
        clk_->set_hiz(id_clk_);
        dio_sal_ = false;
        pon_dio();
        pon_srst(false);
        clk_v_ = false;
    }

    // --- El socket ------------------------------------------------------------------
    void aceptar() {
        if (red::valido(cli_) || !red::valido(srv_)) return;
        cli_ = red::acepta(srv_);
        if (!red::valido(cli_)) return;
        red::no_bloqueante(cli_);
        red::sin_nagle(cli_);
        red::sin_sigpipe(cli_);
        ++n_conex_;
        rx_.clear(); tx_.clear();
        // Al conectarse, la sonda GOBIERNA SWDIO (a uno): OpenOCD manda la
        // secuencia JTAG->SWD sin pedir antes 'O', y con la linea suelta el
        // objetivo no la veria (el primer DPIDR seria basura)
        pon_clk(false);
        dio_v_ = true; dio_sal_ = true; pon_dio();
        std::printf("[openocd] cliente conectado\n");
        std::fflush(stdout);
    }
    void desconectar() {
        red::cerrar(cli_);
        suelta();
        std::printf("[openocd] cliente desconectado (%llu flancos de SWCLK, %llu lecturas)\n",
                    (unsigned long long)n_flancos_, (unsigned long long)n_lect_);
        std::fflush(stdout);
    }
    // Lee lo que haya, sin esperar. false si el cliente se ha ido.
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

    // Una orden. false: el cliente se va ('Q')
    bool orden(char c) {
        switch (c) {
        case 'd': case 'e': case 'f': case 'g': {
            const unsigned v = unsigned(c - 'd');
            dio_v_ = (v & 1u) != 0;
            pon_dio();
            pon_clk((v & 2u) != 0);
            wait(semi_);
            return true;
        }
        case 'O': dio_sal_ = true;  pon_dio(); return true;
        case 'o': dio_sal_ = false; pon_dio(); return true;
        case 'c': ++n_lect_; tx_.push_back(lee_dio() ? '1' : '0'); return true;
        case 'R': tx_.push_back('0'); return true;
        case 'r': case 's': case 't': case 'u':
            pon_srst(((c - 'r') & 1) != 0);
            return true;
        case 'B': case 'b': return true;
        case 'Z': wait(1, sc_core::SC_MS); return true;
        case 'z': wait(1, sc_core::SC_US); return true;
        case 'Q': return false;
        // Las de JTAG ('0'..'7') no tienen sentido en SWD
        default:
            if (++n_raros_ == 1) {
                std::printf("[openocd] orden de remote_bitbang que esta sonda no atiende: "
                            "'%c' (0x%02X). Es una sonda SWD: falta `transport select swd`?\n",
                            (c >= 32 && c < 127) ? c : '?', unsigned(uint8_t(c)));
                std::fflush(stdout);
            }
            if (c >= '0' && c <= '7') return true;
            return true;
        }
    }

    void run() {
        unsigned activo = 0;          // pasos cortos que quedan
        for (;;) {
            if (!activo_ || !red::valido(srv_)) { wait(poll_, ev_); continue; }
            if (!red::valido(cli_)) {
                aceptar();
                if (!red::valido(cli_)) { wait(poll_, ev_); continue; }
            }
            if (!recibe()) { desconectar(); activo = 0; continue; }
            if (rx_.empty()) {
                if (activo == pasos_activo_ && espera_us_ > 0) {
                    const auto t0 = std::chrono::steady_clock::now();
                    const bool llega = red::espera_legible_us(cli_, espera_us_);
                    const long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0).count();
                    // Lo que ha pasado en el reloj de pared, en el simulado
                    if (us > 0) wait(sc_core::sc_time(double(us), sc_core::SC_US), ev_);
                    if (llega) continue;               // la transacción sigue
                }
                if (activo) { --activo; wait(poll_activo_, ev_); }
                else wait(poll_, ev_);
                continue;
            }
            std::string lote;
            lote.swap(rx_);
            bool sigue = true;
            for (char c : lote) {
                if (!orden(c)) { sigue = false; break; }
            }
            if (!tx_.empty() && !envia()) sigue = false;
            if (!sigue) { desconectar(); activo = 0; continue; }
            activo = pasos_activo_;
        }
    }
};

} // namespace stm32
#endif // STM32_VERIF_SONDA_OPENOCD_H
