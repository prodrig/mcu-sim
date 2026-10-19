// =============================================================================
// xds110.h — La sonda XDS110 de las LaunchPad MSP432, como pieza de la placa
//
// Fase X1 de `doc/analisis_xds110.md`. En la MSP-EXP432P401R (el XDS110-ET) y
// en la MSP-EXP432E401Y (el XDS110) la sonda va en la misma placa, separada
// del chip por J101, un bloque 2x10 de puentes: los pines impares son del
// chip y los pares, de la sonda. Esta pieza es EL LADO DE LA SONDA de J101,
// y por eso los puentes hacen lo que hacen en la placa:
//
//   * 3V3: el LDO de la sonda, que alimenta todo el lado del objetivo -el
//     chip, los LED, la BoosterPack- con 500 mA de límite [SLAU597F §2.4.1].
//     Es una `Fuente` por dentro: el adaptador del chip deja de alimentarlo
//     cuando la ve en su nodo, así que SIN EL PUENTE 3V3 EL CHIP NO ARRANCA;
//   * TMS_SWDIO, TCK_SWCLK y RST: la sonda de depuración, si se pide
//     (`--openocd` o `--cmsis-dap`, o `sonda=` aquí), va por estos tres pines
//     y no por los del chip. Sin los puentes TMS/TCK OpenOCD no encuentra el
//     DAP; sin RST no hay SRST;
//   * TDO_SWO: la traza SWO, con un receptor UART (formato NRZ, el único del
//     XDS110) que la lleva a un canal del puente serie: TCP en crudo, RFC 2217
//     o memoria, como el `PuenteSerie` -y por las mismas razones-. Con
//     `swo_formato="itm"` (omisión) va lo que el firmware escribe en un
//     puerto de estímulo del ITM -el printf de ITM_SendChar-; con "crudo",
//     los bytes de la línea tal cual, para un decodificador externo.
//
// El *backchannel* (RXD/TXD de J101) sigue siendo un `PuenteSerie` aparte en
// las dos placas: ya existía y no cambia nada que esté dentro o fuera.
//
// `et="si"` dice que es el XDS110-ET (con EnergyTrace, X3). Hoy solo se
// guarda y se dice; la medida es de la fase X3.
// =============================================================================
#ifndef STM32_PARTS_XDS110_H
#define STM32_PARTS_XDS110_H

#include <systemc>
#include <memory>
#include <string>
#include "ext_parts.h"
#include "canal_host.h"
#include "motor_uart.h"
#include "../common/serie_destino.h"
#include "../verif/sonda_openocd.h"
#include "../verif/sonda_cmsis_dap.h"

namespace stm32 {

SC_MODULE(Xds110), public ExtPartBase, public LineaSerie {
    struct Config {
        bool        et = false;
        double      v = 3.3;
        double      limite_ma = 500.0;
        std::string sonda;              // "", "openocd" o "cmsis-dap"
        unsigned    puerto = 0;
        bool        con_swo = false;
        serie::Destino swo;             // a donde va la traza
        double      swo_bitrate = 1e6;
        bool        swo_itm = true;     // "itm" (omision) o "crudo"
        unsigned    swo_puerto = 0;     // el puerto de estimulo, con "itm"
    };

    // Los nodos que no se den, no se conectan: una placa puede no sacar el
    // SWO, o no darle el 3V3 a la sonda
    Xds110(sc_core::sc_module_name nm, analog_net_if* swdio, analog_net_if* swclk,
           analog_net_if* rst, analog_net_if* swo, analog_net_if* v3v3, const Config& c)
        : sc_core::sc_module(nm), ExtPartBase("Xds110", nm), cfg_(c), rst_(rst), swo_(swo) {
        if (v3v3) {
            ldo_.reset(new Fuente("ldo", *v3v3, c.v, 0.1, c.limite_ma));
            adopta(*ldo_, "pin", "3v3");
            retira(ldo_.get());
            marca_riel("3v3");
        }
        add_ref_opt("swdio", swdio);
        add_ref_opt("swclk", swclk);
        if (rst) { id_rst_ = add_pin("rst", *rst, "xds_srst"); rst->set_hiz(id_rst_); }
        if (swo) add_ref("swo", *swo);
        // La sonda, colgada de los pines de la sonda
        if (!c.sonda.empty() && swdio && swclk && c.puerto) {
            auto srst = [this](bool v) { pon_srst(v); };
            if (c.sonda == "openocd")
                sonda_.reset(new SondaOpenOcd("sonda", *swclk, *swdio, c.puerto, srst));
            else
                sonda_.reset(new SondaCmsisDap("sonda", *swclk, *swdio, c.puerto, srst, rst));
        }
        // La traza
        if (swo && c.con_swo) {
            rx_.reset(new ReceptorUart(*swo, c.swo_bitrate, c.v));
            if (!c.swo.valido) {
                error_canal_ = c.swo.error;
            } else {
                switch (c.swo.modo) {
                case serie::Modo::rfc2217: tcp_ = new CanalRfc2217(c.swo.puerto, 4096, this); break;
                case serie::Modo::tcp: tcp_ = new CanalTcp(c.swo.puerto, 4096); break;
                case serie::Modo::tcp_cliente: tcp_ = new CanalTcp(c.swo.host, c.swo.puerto, 4096); break;
                case serie::Modo::rfc2217_cliente:
                    tcp_ = new CanalRfc2217Cliente(c.swo.host, c.swo.puerto, 4096, this); break;
                case serie::Modo::memoria: break;
                }
                if (tcp_) {
                    canal_.reset(tcp_);
                    if (!tcp_->abierto())
                        error_canal_ = "no se puede escuchar en localhost:" +
                                       std::to_string(c.swo.puerto) + " para el SWO: ¿lo tiene "
                                       "otro programa?";
                } else {
                    mem_ = new CanalMemoria(4096);
                    canal_.reset(mem_);
                }
            }
            SC_HAS_PROCESS(Xds110);
            SC_THREAD(hilo_swo);
            if (tcp_) SC_THREAD(hilo_canal);
        }
    }

    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (ldo_) ldo_->set_enabled(on);
        if (!on) pon_srst(false);
    }

    // --- Lo que mira el simulador ---------------------------------------------
    const Config& config() const { return cfg_; }
    SondaRed* sonda() { return sonda_.get(); }
    bool tiene_ldo() const { return ldo_ != nullptr; }
    double corriente() const { return ldo_ ? ldo_->corriente_exacta() : 0.0; }   // A
    bool sobrecorriente() const { return ldo_ && ldo_->sobrecorriente(); }
    bool swo_ok() const { return error_canal_.empty(); }
    const std::string& error_swo() const { return error_canal_; }
    bool swo_por_red() const { return tcp_ != nullptr; }
    CanalTcp* canal_swo_tcp() { return tcp_; }
    // Lo que ha llegado por la traza, si va a memoria
    std::string texto_swo() const { return mem_ ? mem_->recibido() : std::string(); }
    uint64_t bytes_swo() const { return n_bytes_; }
    uint64_t mensajes_itm() const { return n_itm_; }
    unsigned resets() const { return n_srst_; }
    std::string describir() const {
        std::string s = cfg_.et ? "XDS110-ET" : "XDS110";
        if (ldo_) {
            char b[64];
            std::snprintf(b, sizeof b, ", 3V3 de su LDO (%g mA)", cfg_.limite_ma);
            s += b;
        }
        if (sonda_)
            s += ", sonda " + std::string(sonda_->protocolo()) + " en el puerto " +
                 std::to_string(sonda_->puerto());
        if (canal_) {
            char b[96];
            std::snprintf(b, sizeof b, ", SWO a %.0f bit/s (%s",
                          cfg_.swo_bitrate, cfg_.swo_itm ? "ITM, puerto " : "crudo");
            s += b;
            if (cfg_.swo_itm) s += std::to_string(cfg_.swo_puerto);
            s += ") -> " + canal_->describir();
        }
        return s;
    }

    // --- Observables ----------------------------------------------------------
    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"corriente", "mA", 0.f, float(cfg_.limite_ma), true};
        if (i == 1) return {"sobrecorriente", "", 0.f, 1.f, true, true};
        return {"depurando", "", 0.f, 1.f, true};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) return float(corriente() * 1000.0);
        if (i == 1) return sobrecorriente() ? 1.f : 0.f;
        return sonda_ && sonda_->conectado() ? 1.f : 0.f;
    }

    // --- LineaSerie: el SWO por RFC 2217 ----------------------------------------
    // La velocidad de la traza la pone el firmware (TPIU_ACPR), no el
    // terminal: se contesta con la configurada y no se cambia nada
    uint32_t pide_baudios(uint32_t) override { return uint32_t(cfg_.swo_bitrate); }
    uint8_t  pide_datos(uint8_t) override { return 8; }
    uint8_t  pide_paridad(uint8_t) override { return 1; }
    uint8_t  pide_parada(uint8_t) override { return 1; }
    uint8_t  pide_control(uint8_t) override { return 1; }
    uint8_t  estado_modem() const override { return 0; }
    std::string firma() const override { return "mcu-sim XDS110 SWO"; }

private:
    void pon_srst(bool activo) {
        if (!rst_ || id_rst_ < 0) return;
        if (activo && !srst_) ++n_srst_;
        srst_ = activo;
        if (activo) rst_->set_drive(id_rst_, 0.0f, 10.0f);
        else        rst_->set_hiz(id_rst_);
    }

    // La traza: un receptor UART 8N1 y, encima, el desempaquetado del ITM
    // [ARMv7-M ARM, D4.2]: una cabecera con el puerto y el tamaño (1, 2 o 4
    // bytes) y la carga con el byte bajo por delante. Los paquetes de
    // sincronización, desbordamiento y marcas de tiempo se saltan.
    void hilo_swo() {
        for (;;) {
            const TramaUart t = rx_->recibe();
            if (t.error_trama) continue;
            const uint8_t b = uint8_t(t.dato);
            ++n_bytes_;
            if (!cfg_.swo_itm) { emite(&b, 1); continue; }
            if (pend_ == 0) {
                if ((b & 0x03u) == 0) continue;                   // protocolo / sinc.
                if (b & 0x04u) {                                   // fuente hardware (DWT)
                    p_ = 0xFFu; pend_ = (b & 3u) == 3 ? 4u : (b & 3u); continue;
                }
                p_ = (b >> 3) & 0x1Fu;
                pend_ = (b & 3u) == 3 ? 4u : (b & 3u);
                n_ = 0;
                continue;
            }
            --pend_;
            if (p_ == cfg_.swo_puerto) carga_[n_++] = b;
            if (pend_ == 0 && p_ == cfg_.swo_puerto) { emite(carga_, n_); ++n_itm_; }
        }
    }
    void emite(const uint8_t* b, std::size_t n) {
        if (canal_ && n) canal_->escribir(b, n);
    }
    // Los canales por red se sondean en tiempo simulado, como los del puente
    // serie: lo que llega del terminal se tira (la traza solo sale)
    void hilo_canal() {
        uint8_t basura[256];
        for (;;) {
            canal_->sondear();
            while (canal_->leer(basura, sizeof basura)) {}
            wait(1, sc_core::SC_MS);
        }
    }

    Config cfg_;
    analog_net_if* rst_;
    analog_net_if* swo_;
    int id_rst_ = -1;
    bool srst_ = false;
    unsigned n_srst_ = 0;
    std::unique_ptr<Fuente> ldo_;
    std::unique_ptr<SondaRed> sonda_;
    std::unique_ptr<ReceptorUart> rx_;
    std::unique_ptr<CanalHost> canal_;
    CanalTcp* tcp_ = nullptr;
    CanalMemoria* mem_ = nullptr;
    std::string error_canal_;
    unsigned pend_ = 0, p_ = 0, n_ = 0;
    uint8_t carga_[4] = {};
    uint64_t n_bytes_ = 0, n_itm_ = 0;
};

} // namespace stm32
#endif // STM32_PARTS_XDS110_H
