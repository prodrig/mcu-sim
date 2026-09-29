// =============================================================================
// puente_serie.h — PuenteSerie: una USART del MCU hasta el ordenador
//
// Fase D2 del plan de `doc/analisis_puente_serie.md` §10 (P-14). Es lo que hace
// el ST-LINK/V2-1 de una Nucleo con su puerto COM virtual: un UART de placa
// colgado de dos pines del MCU, que lleva los bytes a otro sitio. Aquí ese
// otro sitio es un `CanalHost` (parts/canal_host.h): en esta fase, una cola en
// memoria (D2), un puerto TCP en crudo (D3) y, en la D5, RFC 2217.
//
// VA EN LOS PINES (opción M1 del análisis), como el resto de `parts/`. Lee el
// TX del MCU con el mismo receptor que `SwoReceiver` y gobierna su RX con un
// Thevenin de 50 ohmios, así que respeta el mux de funciones alternativas, la
// configuración del GPIO y la tensión del pad. La consecuencia que importa: si
// los baudios o el formato del puente y los del firmware no coinciden, la USART
// del modelo ve BASURA y levanta sus banderas de error, como en la placa. El
// puente no avisa al firmware de nada; el firmware se entera como se entera en
// la placa.
//
// LOS TERMINALES, con los nombres del ADAPTADOR y no del MCU —es como vienen
// serigrafiados en uno de verdad—:
//
//   rx    lee el TX del MCU                       (pasivo: no conduce)
//   tx    gobierna el RX del MCU
//   cts   lee el RTS del MCU    (opcional)        (pasivo)
//   rts   gobierna el CTS del MCU (opcional): bajo = «puedes mandar»
//   dtr   una línea más (opcional): alto en reposo, bajo si el anfitrión la
//         activa. En D5 la moverá RFC 2217; hasta entonces, `set_dtr()`
//
// Hace falta al menos uno de `rx` y `tx`: un puente que solo escucha es un
// analizador, y uno que solo habla es un teclado, y los dos tienen sentido.
//
// LO QUE HACE CON LOS ERRORES. Una trama con error de trama o de paridad SE
// ENTREGA al anfitrión igual —es la basura que el alumno tiene que ver en su
// terminal cuando se equivoca de baudios— y se cuenta, y se avisa UNA vez por
// la consola. Un break no es un byte: se cuenta y no se entrega (en D5 irá como
// LINESTATE).
// =============================================================================
#ifndef STM32_PARTS_PUENTE_SERIE_H
#define STM32_PARTS_PUENTE_SERIE_H

#include <systemc>
#include <cstdio>
#include <memory>
#include <string>
#include "../common/analog_net.h"
#include "../common/formato_uart.h"
#include "../common/serie_destino.h"
#include "part_base.h"
#include "motor_uart.h"
#include "canal_host.h"

namespace stm32 {

SC_MODULE(PuenteSerie), public ExtPartBase {
    // Lo que dice el XML, ya leído y comprobado.
    struct Config {
        serie::Destino destino      = serie::por_omision();
        double         baudios      = serie::BAUDIOS_OMISION;
        bool           baudios_host = false;   // `baudios="host"` (D-3)
        FormatoUart    formato{};
        bool           flujo_rtscts = false;
        bool           muestra      = false;   // imprimir lo que manda el MCU
        std::string    guion;                  // lo que se «teclea» al arrancar
        double         guion_ms     = 10.0;
        std::size_t    cola         = 4096;    // hacia el MCU (§4.2)
        double         vdd          = 3.3;
        double         r_out        = 50.0;
    };

    PuenteSerie(sc_core::sc_module_name nm, analog_net_if* rx, analog_net_if* tx,
                analog_net_if* cts, analog_net_if* rts, analog_net_if* dtr,
                const Config& c)
        : sc_core::sc_module(nm), ExtPartBase("PuenteSerie", nm), cfg_(c) {
        if (rx) {
            add_ref("rx", *rx);
            rx_.reset(new ReceptorUart(*rx, c.baudios, c.vdd));
            rx_->set_formato(c.formato);
        }
        if (tx) {
            const int id = add_pin("tx", *tx, "vcp_tx");
            tx_.reset(new EmisorUart(*tx, id, c.baudios, c.vdd, c.r_out));
            tx_->set_formato(c.formato);
        }
        if (cts) { add_ref("cts", *cts); cts_ = cts; }
        if (rts) { id_rts_ = add_pin("rts", *rts, "vcp_rts"); rts_ = rts; }
        if (dtr) { id_dtr_ = add_pin("dtr", *dtr, "vcp_dtr"); dtr_ = dtr; }
        // El canal, según el destino. RFC 2217 es la fase D5: hasta entonces
        // `sim` no deja montar la pieza con ese destino, y si alguien lo hace
        // desde C++ se queda en memoria y `canal_ok()` dice por qué.
        switch (c.destino.modo) {
        case serie::Modo::tcp:
            tcp_ = new CanalTcp(c.destino.puerto, c.cola);
            canal_.reset(tcp_);
            if (!tcp_->abierto())
                error_canal_ = "no se puede escuchar en localhost:" +
                               std::to_string(c.destino.puerto) + ": ¿lo tiene "
                               "otro programa (otro mcu-sim, un GDB)?";
            break;
        case serie::Modo::rfc2217:
            error_canal_ = "RFC 2217 llega en la fase D5 del plan";
            // sigue: se monta en memoria para que la pieza no quede sin canal
            [[fallthrough]];
        case serie::Modo::memoria:
            mem_ = new CanalMemoria(c.cola);
            canal_.reset(mem_);
            break;
        }
        niveles_de_reposo();
        SC_HAS_PROCESS(PuenteSerie);
        SC_THREAD(hilo_rx);
        SC_THREAD(hilo_tx);
        SC_THREAD(hilo_guion);
        SC_THREAD(hilo_canal);
    }

    // ¿El canal está listo? Si no, `error_canal()` dice por qué, y `sim` no
    // arranca: un puente que no escucha no es un puente.
    bool canal_ok() const { return error_canal_.empty(); }
    const std::string& error_canal() const { return error_canal_; }
    // ¿Va por la red? Entonces la simulación no termina sola (D3).
    bool por_red() const { return tcp_ != nullptr; }

    // --- Conexión ------------------------------------------------------------
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (on) niveles_de_reposo();
        ev_tx_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Lo que se configura en marcha ----------------------------------------
    // Los dos se aplican ENTRE TRAMAS: la que está en la línea termina con lo
    // que tenía (D-4). Es lo que hará `SET-BAUDRATE` en la fase D5.
    void set_baudios(double b) {
        cfg_.baudios = b;
        if (rx_) rx_->set_bitrate(b);
        if (tx_) tx_->set_bitrate(b);
    }
    void set_formato(const FormatoUart& f) {
        cfg_.formato = f;
        if (rx_) rx_->set_formato(f);
        if (tx_) tx_->set_formato(f);
    }
    void set_flujo_rtscts(bool on) { cfg_.flujo_rtscts = on; ev_tx_.notify(sc_core::SC_ZERO_TIME); }
    // `rts` del adaptador: ¿puede mandar el MCU? (bajo = sí)
    void set_rts(bool listo) { rts_listo_ = listo; conduce_rts(); }
    // `dtr` del adaptador: activo = bajo, como en un adaptador TTL.
    void set_dtr(bool activo) { dtr_activo_ = activo; conduce_dtr(); }
    // Un break hacia el MCU: se emite en cuanto acabe la trama en curso.
    void envia_break() { break_pendiente_ = true; ev_tx_.notify(sc_core::SC_ZERO_TIME); }

    double             baudios() const { return cfg_.baudios; }
    const FormatoUart& formato() const { return cfg_.formato; }
    const Config&      config()  const { return cfg_; }
    std::string describir() const {
        return canal_->describir() + ", " +
               std::to_string(long(cfg_.baudios)) + " " + como_texto(cfg_.formato) +
               (cfg_.flujo_rtscts ? ", RTS/CTS" : "");
    }

    // --- El canal en memoria: lo que usa un banco de pruebas ----------------
    CanalMemoria* memoria() { return mem_; }
    CanalTcp*     tcp()     { return tcp_; }
    CanalHost&    canal()   { return *canal_; }
    std::size_t envia(const std::string& s) { return mem_ ? mem_->empuja(s) : 0; }
    const std::string& recibido() const { return mem_->recibido(); }
    void borra_recibido() { if (mem_) mem_->borra_recibido(); }

    // --- Contadores ---------------------------------------------------------
    uint64_t bytes_desde_mcu() const { return n_desde_; }
    uint64_t bytes_hacia_mcu() const { return n_hacia_; }
    uint64_t errores_trama()   const { return n_err_trama_; }
    uint64_t errores_paridad() const { return n_err_par_; }
    uint64_t breaks_desde_mcu() const { return n_breaks_; }
    uint64_t esperas_por_cts() const { return n_esperas_cts_; }

    // Lo que quede a medias en la línea de `muestra`, marcado como tal. Lo
    // llama `sim` al acabar la ventana de --ms: la basura de unos baudios
    // equivocados casi nunca trae un salto de línea, y sin esto no se vería.
    void vacia_muestra() {
        if (!cfg_.muestra || linea_.empty()) return;
        std::printf("  [%s] %s  (sin salto de linea)\n", pieza().c_str(), linea_.c_str());
        std::fflush(stdout);
        linea_.clear();
    }

private:
    // ---- Los niveles de las líneas que el puente gobierna -------------------
    void niveles_de_reposo() {
        if (!conectada_) return;
        if (tx_) tx_->reposo();
        conduce_rts();
        conduce_dtr();
    }
    void conduce_rts() {
        if (rts_ && conectada_)
            rts_->set_drive(id_rts_, rts_listo_ ? 0.0f : float(cfg_.vdd), float(cfg_.r_out));
    }
    void conduce_dtr() {
        if (dtr_ && conectada_)
            dtr_->set_drive(id_dtr_, dtr_activo_ ? 0.0f : float(cfg_.vdd), float(cfg_.r_out));
    }
    // El RTS del MCU, leído en el terminal `cts`: alto = «no me mandes».
    bool mcu_no_listo() const { return cts_ && cts_->voltage() > 0.5 * cfg_.vdd; }

    // ---- Del MCU al anfitrión ----------------------------------------------
    void hilo_rx() {
        if (!rx_) return;
        for (;;) {
            const TramaUart t = rx_->recibe();
            if (!conectada_) continue;
            if (t.es_break) { ++n_breaks_; continue; }
            if (t.error_trama) ++n_err_trama_;
            if (t.error_paridad) ++n_err_par_;
            if (!t.ok() && !avisado_err_) {
                avisado_err_ = true;
                std::fflush(stdout);        // que el aviso salga en su sitio
                std::fprintf(stderr, "  [serie] %s: tramas con error desde el MCU; "
                             "coinciden los baudios (%ld) y el formato (%s) con "
                             "los del firmware?\n", pieza().c_str(),
                             long(cfg_.baudios), como_texto(cfg_.formato).c_str());
            }
            // Los bytes del anfitrión son de ocho bits: de un 9N1 se entregan
            // los ocho de abajo. RFC 2217 no admite más (SET-DATASIZE, 5..8).
            const uint8_t b = uint8_t(t.dato & 0xFFu);
            canal_->escribir(&b, 1);
            ++n_desde_;
            if (cfg_.muestra) muestra(b);
        }
    }

    // ---- Del anfitrión al MCU ----------------------------------------------
    void hilo_tx() {
        if (!tx_) return;
        for (;;) {
            if (!conectada_) { wait(evento_conexion()); continue; }
            if (break_pendiente_) {
                break_pendiente_ = false;
                tx_->emite_break();
                continue;
            }
            // Control de flujo: el adaptador mira el CTS antes de EMPEZAR cada
            // trama. La que está en la línea no se corta.
            if (cfg_.flujo_rtscts && mcu_no_listo()) {
                ++n_esperas_cts_;
                wait(cts_->value_changed_event() | ev_tx_ | evento_conexion());
                continue;
            }
            uint8_t b;
            if (canal_->leer(&b, 1) == 1) {
                tx_->emite(b);
                ++n_hacia_;
                continue;
            }
            // Nada que mandar. El canal en memoria avisa cuando llega algo; los
            // de red no pueden, y los sondea `hilo_canal`, que avisa por ev_tx_.
            const sc_core::sc_event* a = canal_->aviso();
            if (a) wait(*a | ev_tx_ | evento_conexion());
            else   wait(ev_tx_ | evento_conexion());
        }
    }

    // ---- El sondeo de los canales sin aviso (D-9) --------------------------
    // En TIEMPO SIMULADO, como los stubs de GDB. El periodo es ADAPTATIVO: un
    // tiempo de carácter mientras hay tráfico -para no ir más lento que la
    // línea- y un milisegundo en reposo, que es lo que cuesta poco.
    void hilo_canal() {
        if (canal_->aviso()) return;                 // la memoria no se sondea
        uint64_t antes = 0;
        for (;;) {
            if (!conectada_) { wait(evento_conexion()); continue; }
            canal_->sondear();
            if (canal_->pendientes()) ev_tx_.notify(sc_core::SC_ZERO_TIME);
            const uint64_t ahora = n_desde_ + n_hacia_;
            const bool trafico = canal_->pendientes() || ahora != antes;
            antes = ahora;
            if (trafico)
                wait(sc_core::sc_time(0.5 * cfg_.formato.medios_de_trama() / cfg_.baudios,
                                      sc_core::SC_SEC));
            else
                wait(sc_core::sc_time(1, sc_core::SC_MS));
        }
    }

    // ---- `guion=`: lo que el «terminal» teclea al arrancar -----------------
    void hilo_guion() {
        if (cfg_.guion.empty() || !mem_) return;
        wait(sc_core::sc_time(cfg_.guion_ms, sc_core::SC_MS));
        mem_->empuja(cfg_.guion);
    }

    // ---- `muestra="si"`: lo que manda el MCU, en la consola ----------------
    // Línea a línea, con el id de la pieza delante. `\r` se come (los
    // firmwares mandan `\r\n`) y lo que no se imprime sale como \xNN.
    void muestra(uint8_t b) {
        if (b == '\n' || linea_.size() >= 200) {
            std::printf("  [%s] %s\n", pieza().c_str(), linea_.c_str());
            std::fflush(stdout);
            linea_.clear();
            if (b == '\n') return;
        }
        if (b == '\r') return;
        if (b >= 0x20 && b < 0x7F) { linea_ += char(b); return; }
        char h[8];
        std::snprintf(h, sizeof h, "\\x%02X", b);
        linea_ += h;
    }

    Config                        cfg_;
    std::unique_ptr<ReceptorUart> rx_;
    std::unique_ptr<EmisorUart>   tx_;
    std::unique_ptr<CanalHost>    canal_;
    CanalMemoria*                 mem_ = nullptr;
    CanalTcp*                     tcp_ = nullptr;
    std::string                   error_canal_;
    analog_net_if* cts_ = nullptr;
    analog_net_if* rts_ = nullptr;
    analog_net_if* dtr_ = nullptr;
    int  id_rts_ = -1, id_dtr_ = -1;
    bool rts_listo_ = true, dtr_activo_ = false;
    bool break_pendiente_ = false, avisado_err_ = false;
    sc_core::sc_event ev_tx_;
    std::string linea_;
    uint64_t n_desde_ = 0, n_hacia_ = 0, n_err_trama_ = 0, n_err_par_ = 0,
             n_breaks_ = 0, n_esperas_cts_ = 0;
};

} // namespace stm32

#endif // STM32_PARTS_PUENTE_SERIE_H
