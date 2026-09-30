// =============================================================================
// puente_serie.h — PuenteSerie: una USART del MCU hasta el ordenador
//
// Fase D2 del plan de `doc/analisis_puente_serie.md` §10 (P-14). Es lo que hace
// el ST-LINK/V2-1 de una Nucleo con su puerto COM virtual: un UART de placa
// colgado de dos pines del MCU, que lleva los bytes a otro sitio. Aquí ese
// otro sitio es un `CanalHost` (parts/canal_host.h): en esta fase, una cola en
// memoria (D2), un puerto TCP en crudo (D3) o RFC 2217 (D5), escuchando o, desde
// la D8, conectándose a un servidor (`tcp-cliente:`, `rfc2217-cliente:`).
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
//         activa (SET-CONTROL 8 y 9 de RFC 2217, o `set_dtr()`)
//
// Hace falta al menos uno de `rx` y `tx`: un puente que solo escucha es un
// analizador, y uno que solo habla es un teclado, y los dos tienen sentido.
//
// LO QUE HACE CON LOS ERRORES. Una trama con error de trama o de paridad SE
// ENTREGA al anfitrión igual —es la basura que el alumno tiene que ver en su
// terminal cuando se equivoca de baudios— y se cuenta, y se avisa UNA vez por
// la consola. Un break no es un byte: se cuenta y no se entrega. Con RFC 2217,
// los dos -errores y breaks- van además como NOTIFY-LINESTATE, si el terminal
// los ha pedido con su máscara.
//
// QUIÉN MANDA EN LA LÍNEA CON RFC 2217 (decisión D-14). Con `baudios="host"`,
// el terminal: sus baudios, su formato y su control de flujo se aplican, entre
// tramas (D-4). Con unos baudios fijos en el XML, el XML: lo que pida el
// terminal se contesta con lo que hay -la RFC confirma siempre lo APLICADO- y
// se avisa una vez por la consola. DTR, RTS y el break no son configuración,
// son señales, y esas las mueve el terminal siempre.
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

SC_MODULE(PuenteSerie), public ExtPartBase, public LineaSerie {
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
        // El canal, según el destino.
        switch (c.destino.modo) {
        case serie::Modo::rfc2217:
            rfc_ = new CanalRfc2217(c.destino.puerto, c.cola, this);
            tcp_ = rfc_;
            break;
        case serie::Modo::tcp:
            tcp_ = new CanalTcp(c.destino.puerto, c.cola);
            break;
        // Los clientes (D8). En RFC 2217 la pieza sigue siendo la LineaSerie:
        // el canal le PREGUNTA la configuración para mandarla al servidor.
        case serie::Modo::tcp_cliente:
            tcp_ = new CanalTcp(c.destino.host, c.destino.puerto, c.cola);
            break;
        case serie::Modo::rfc2217_cliente:
            tcp_ = new CanalRfc2217Cliente(c.destino.host, c.destino.puerto, c.cola, this);
            break;
        case serie::Modo::memoria:
            break;
        }
        if (tcp_) {
            canal_.reset(tcp_);
            if (!tcp_->abierto())
                error_canal_ = "no se puede escuchar en localhost:" +
                               std::to_string(c.destino.puerto) + ": ¿lo tiene "
                               "otro programa (otro mcu-sim, un GDB)?";
        } else {
            mem_ = new CanalMemoria(c.cola);
            canal_.reset(mem_);
        }
        niveles_de_reposo();
        SC_HAS_PROCESS(PuenteSerie);
        SC_THREAD(hilo_rx);
        SC_THREAD(hilo_tx);
        SC_THREAD(hilo_guion);
        SC_THREAD(hilo_canal);
        SC_THREAD(hilo_modem);
    }

    // ¿El canal está listo? Si no, `error_canal()` dice por qué, y `sim` no
    // arranca: un puente que no escucha no es un puente.
    bool canal_ok() const { return error_canal_.empty(); }
    const std::string& error_canal() const { return error_canal_; }
    // ¿Va por la red? Entonces la simulación no termina sola (D3, D5).
    bool por_red() const { return tcp_ != nullptr; }

    // --- Conexión ------------------------------------------------------------
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (on) niveles_de_reposo();
        ev_tx_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Lo que se configura en marcha ----------------------------------------
    // Los dos se aplican ENTRE TRAMAS: la que está en la línea termina con lo
    // que tenía (D-4). Es lo que hacen SET-BAUDRATE y compañía (D5).
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
    // Un break SOSTENIDO: la línea a cero desde que acabe la trama en curso
    // hasta que se apague. Es SET-CONTROL 5 y 6.
    void set_break(bool on) { break_on_ = on; ev_tx_.notify(sc_core::SC_ZERO_TIME); }
    // ¿Manda el terminal en los baudios, el formato y el flujo? (D-3, D-14)
    void set_baudios_host(bool on) { cfg_.baudios_host = on; }

    // --- LineaSerie: lo que pide el terminal por RFC 2217 ----------------------
    uint32_t pide_baudios(uint32_t v) override {
        if (v && v != uint32_t(cfg_.baudios)) {
            if (cfg_.baudios_host && v >= serie::BAUDIOS_MIN && v <= serie::BAUDIOS_MAX)
                set_baudios(double(v));
            else
                rechaza("baudios", std::to_string(v));
        }
        return uint32_t(cfg_.baudios);
    }
    uint8_t pide_datos(uint8_t v) override {
        if (v && v != cfg_.formato.bits) {
            if (cfg_.baudios_host && v >= 5 && v <= 8) {
                FormatoUart f = cfg_.formato; f.bits = v; set_formato(f);
            } else rechaza("bits de datos", std::to_string(v));
        }
        return uint8_t(cfg_.formato.bits > 8 ? 8 : cfg_.formato.bits);
    }
    uint8_t pide_paridad(uint8_t v) override {
        static const Paridad tabla[] = { Paridad::ninguna, Paridad::impar, Paridad::par,
                                         Paridad::marca, Paridad::espacio };
        if (v && !(v <= 5 && tabla[v - 1] == cfg_.formato.paridad)) {
            if (cfg_.baudios_host && v <= 5) {
                FormatoUart f = cfg_.formato; f.paridad = tabla[v - 1]; set_formato(f);
            } else rechaza("paridad", std::to_string(v));
        }
        for (uint8_t i = 0; i < 5; ++i) if (tabla[i] == cfg_.formato.paridad) return uint8_t(i + 1);
        return 1;
    }
    uint8_t pide_parada(uint8_t v) override {
        static const Parada tabla[] = { Parada::uno, Parada::dos, Parada::uno_y_medio };
        if (v && !(v <= 3 && tabla[v - 1] == cfg_.formato.parada)) {
            if (cfg_.baudios_host && v <= 3) {
                FormatoUart f = cfg_.formato; f.parada = tabla[v - 1]; set_formato(f);
            } else rechaza("bits de parada", std::to_string(v));
        }
        for (uint8_t i = 0; i < 3; ++i) if (tabla[i] == cfg_.formato.parada) return uint8_t(i + 1);
        return 1;
    }
    // SET-CONTROL. El control de flujo es configuración, y va como los baudios
    // (D-14). XON/XOFF, y el flujo por DCD, DSR o DTR, no los tiene el
    // adaptador: se contesta con lo que hay. El de ENTRADA (13..18) tampoco: el
    // puente no para nunca al MCU por su cuenta, el RTS lo mueve el terminal.
    uint8_t pide_control(uint8_t v) override {
        switch (v) {
        case 1: case 3:
            if (v == 3 && !cts_) { rechaza("control de flujo", "RTS/CTS sin terminal cts"); break; }
            if ((v == 3) != cfg_.flujo_rtscts) {
                if (cfg_.baudios_host) set_flujo_rtscts(v == 3);
                else rechaza("control de flujo", v == 3 ? "RTS/CTS" : "ninguno");
            }
            break;
        case 4: return break_on_ ? 5 : 6;
        case 5: case 6: set_break(v == 5); return v;
        case 7: return dtr_activo_ ? 8 : 9;
        case 8: case 9: set_dtr(v == 8); return v;
        case 10: return rts_listo_ ? 11 : 12;
        case 11: case 12: set_rts(v == 11); return v;
        case 13: case 14: case 15: case 16: case 18: return 14;
        default: break;                     // 0, 2, 17, 19 y lo desconocido
        }
        return cfg_.flujo_rtscts ? 3 : 1;
    }
    // El anfitrión ve: DSR y DCD siempre (el adaptador está ahí), y CTS
    // cuando el MCU deja mandar (su RTS bajo), o siempre si no hay `cts`.
    uint8_t estado_modem() const override {
        return uint8_t(CanalRfc2217::MODEM_DSR | CanalRfc2217::MODEM_DCD |
                       (mcu_no_listo() ? 0 : CanalRfc2217::MODEM_CTS));
    }
    std::string firma() const override { return "mcu-sim PuenteSerie " + pieza(); }

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
    CanalRfc2217* rfc2217() { return rfc_; }
    bool          break_activo() const { return break_on_; }
    bool          dtr_activo()   const { return dtr_activo_; }
    bool          rts_listo()    const { return rts_listo_; }
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
            if (t.es_break) {
                ++n_breaks_;
                canal_->notifica_linea(CanalRfc2217::LINEA_BREAK);
                continue;
            }
            if (t.error_trama) ++n_err_trama_;
            if (t.error_paridad) ++n_err_par_;
            if (!t.ok())
                canal_->notifica_linea(uint8_t(
                    (t.error_trama ? CanalRfc2217::LINEA_TRAMA : 0) |
                    (t.error_paridad ? CanalRfc2217::LINEA_PARIDAD : 0)));
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
            if (break_on_) {
                tx_->a_cero();
                while (break_on_ && conectada_) wait(ev_tx_ | evento_conexion());
                if (conectada_) tx_->reposo();
                continue;
            }
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

    // ---- Las líneas de módem, hacia el terminal (D5) ------------------------
    // Solo con RFC 2217 y con `cts`: cada vez que el MCU mueve su RTS, el canal
    // mira si el CTS que ve el anfitrión ha cambiado y, si sí, se lo dice.
    void hilo_modem() {
        if (!rfc_ || !cts_) return;
        for (;;) {
            wait(cts_->value_changed_event());
            if (conectada_) canal_->notifica_modem();
        }
    }

    // Lo que pide el terminal y no se hace. Una vez por pieza y por motivo
    // -el XML fija la línea, o el puente no sabe hacerlo-: el terminal lo va a
    // repetir cada vez que se abra.
    void rechaza(const char* que, const std::string& valor) {
        bool& avisado = cfg_.baudios_host ? avisado_no_admite_ : avisado_fijo_;
        if (avisado) return;
        avisado = true;
        std::fflush(stdout);
        std::fprintf(stderr, "  [serie] %s: el terminal pide %s = %s, pero %s; se "
                     "queda con %ld %s%s\n", pieza().c_str(), que, valor.c_str(),
                     cfg_.baudios_host ? "el puente no lo admite"
                                       : "el XML fija la linea (baudios=\"host\" "
                                         "se la dejaria al terminal)",
                     long(cfg_.baudios), como_texto(cfg_.formato).c_str(),
                     cfg_.flujo_rtscts ? ", RTS/CTS" : "");
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
    CanalRfc2217*                 rfc_ = nullptr;
    std::string                   error_canal_;
    analog_net_if* cts_ = nullptr;
    analog_net_if* rts_ = nullptr;
    analog_net_if* dtr_ = nullptr;
    int  id_rts_ = -1, id_dtr_ = -1;
    bool rts_listo_ = true, dtr_activo_ = false;
    bool break_pendiente_ = false, break_on_ = false, avisado_err_ = false,
         avisado_fijo_ = false, avisado_no_admite_ = false;
    sc_core::sc_event ev_tx_;
    std::string linea_;
    uint64_t n_desde_ = 0, n_hacia_ = 0, n_err_trama_ = 0, n_err_par_ = 0,
             n_breaks_ = 0, n_esperas_cts_ = 0;
};

} // namespace stm32

#endif // STM32_PARTS_PUENTE_SERIE_H
