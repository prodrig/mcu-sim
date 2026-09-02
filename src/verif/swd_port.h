// =============================================================================
// swd_port.h — Maestro SWD a nivel de bit (el lado de la SONDA)
//
// Es el otro extremo del cable de un ST-LINK o un J-Link: dos hilos, SWCLK y
// SWDIO, soldados a PA14 y PA13. Habla el protocolo SWD tal cual, con su reset
// de linea, su secuencia de conmutacion desde JTAG, sus paquetes de peticion
// con paridad, su turnaround y su ACK.
//
// El detalle que importa electricamente: SWDIO es BIDIRECCIONAL de verdad. El
// maestro gobierna el nodo analogico mientras manda, y lo SUELTA -alta
// impedancia- durante el turnaround y mientras contesta el objetivo. Si los dos
// gobernasen a la vez habria una pelea de etapas de salida, y el modelo de pads
// la denunciaria.
//
// Y las peculiaridades de ADIv5 que un maestro TIENE que respetar, todas
// implementadas aqui porque sin ellas una sesion real se cuelga:
//
//   * ENCENDIDO. Antes de tocar el AP hay que pedir CDBGPWRUPREQ y
//     CSYSPWRUPREQ en CTRL/STAT y ESPERAR sus acuses. Sin eso el AP contesta
//     FAULT.
//   * WAIT. Un ACK de espera (010) no es un error: hay que REINTENTAR el mismo
//     paquete.
//   * FAULT. Un ACK de fallo (100) quiere decir que hay un bit pegajoso en
//     CTRL/STAT. La unica forma de limpiarlo es escribir en ABORT, y hasta que
//     no se hace el DAP no vuelve a hablar.
//   * LECTURA APLAZADA. El valor de una lectura del AP llega en la SIGUIENTE
//     transaccion, o en RDBUFF. Leer una vez y creerse el resultado es el error
//     mas comun al escribir un programador desde cero.
//   * FRONTERA DE 1 KiB. El auto-incremento de TAR no la cruza: hay que
//     reescribir TAR en cada una.
//
// Este fichero lo comparten la sonda del banco de pruebas (ext_parts.h) y el
// stub de GDB (gdb_stub.h): el protocolo es el mismo, cambia quien lo manda.
// =============================================================================
#ifndef STM32_VERIF_SWD_PORT_H
#define STM32_VERIF_SWD_PORT_H

#include <systemc>
#include <cstdint>
#include "../common/analog_net.h"

namespace stm32 {

// ===========================================================================
// LA SONDA DE DEPURACION
//
// Es el otro extremo del cable de un ST-LINK o un J-Link: dos hilos, SWCLK y
// SWDIO, soldados a PA14 y PA13. Habla el protocolo SWD A NIVEL DE BIT, con su
// reset de linea, su secuencia de conmutacion desde JTAG, sus paquetes de
// peticion con paridad, su turnaround y su ACK.
//
// El detalle que importa: SWDIO es BIDIRECCIONAL de verdad. La sonda gobierna
// el nodo analogico mientras manda, y lo SUELTA -alta impedancia- durante el
// turnaround y mientras contesta el objetivo. Si los dos gobernasen a la vez
// habria una pelea de etapas de salida, y el modelo de pads la denunciaria.
// ===========================================================================
class SwdProbe {
public:
    SwdProbe(analog_net_if& swclk, analog_net_if& swdio, double hz = 1e6,
             double vdd = 3.3)
        : clk_(&swclk), dio_(&swdio), tb_(1.0 / hz), vdd_(vdd) {
        id_clk_ = clk_->register_driver("swd_clk");
        id_dio_ = dio_->register_driver("swd_dio");
        clk_->set_hiz(id_clk_);
        dio_->set_hiz(id_dio_);
    }
    ~SwdProbe() { if (conectada_) desconectar(); }

    void conectar()    { conectada_ = true;  clk_bajo(); soltar(); }
    void desconectar() { conectada_ = false; clk_->set_hiz(id_clk_); soltar(); }
    void set_speed(double hz) { tb_ = 1.0 / hz; }

    // --- La secuencia de arranque de cualquier sonda -----------------------
    // Reset de linea, palabra magica 0xE79E para pasar de JTAG a SWD, otro
    // reset de linea, y lectura del IDCODE. Sin esto el objetivo ni contesta.
    uint32_t conectar_swd() {
        conectar();
        reset_linea();
        secuencia(0xE79Eu, 16);
        reset_linea();
        pulsos(2, false);
        uint32_t id = 0;
        leer_dp(0x0, id);
        // Limpieza de cualquier bit pegajoso que hubiera quedado de antes, y
        // ENCENDIDO de los dominios de depuracion y del sistema. Sin sus
        // acuses el AP no contesta, asi que hay que esperarlos.
        escribir_dp(0x0, 0x1Eu);                      // ABORT: limpiar todo
        escribir_dp(0x4, (1u << 28) | (1u << 30));
        uint32_t st = 0;
        for (unsigned i = 0; i < 32; ++i) {
            leer_dp(0x4, st);
            if ((st & (1u << 29)) && (st & (1u << 31))) break;
        }
        encendido_ = (st & (1u << 29)) && (st & (1u << 31));
        return id;
    }
    bool encendido() const { return encendido_; }

    // El procedimiento de recuperacion ante un FAULT: leer CTRL/STAT para ver
    // que bit se quedo pegado, y limpiarlo por ABORT. Es lo unico que devuelve
    // la palabra al DAP.
    uint32_t recuperar() {
        uint32_t st = 0;
        leer_dp(0x4, st);
        escribir_dp(0x0, 0x1Eu);                      // STKCMPCLR|STKERRCLR|
        ++n_recuperaciones_;                          // WDERRCLR|ORUNERRCLR
        return st;
    }

    // --- Acceso a los registros del DP y del AP -----------------------------
    // Con el tratamiento que exige ADIv5: un WAIT se REINTENTA, y un FAULT se
    // limpia por ABORT antes de volver a intentarlo.
    bool leer_dp(unsigned a, uint32_t& v)  { return con_reintentos(false, false, a, v); }
    bool escribir_dp(unsigned a, uint32_t v) { return con_reintentos(true, false, a, v); }
    bool leer_ap(unsigned a, uint32_t& v)  { return con_reintentos(false, true, a, v); }
    bool escribir_ap(unsigned a, uint32_t v) { return con_reintentos(true, true, a, v); }

    // Los reintentos ante un WAIT no son cuatro: son los que hagan falta hasta
    // agotar el plazo. Un borrado de sector de Flash tiene al DAP ocupado
    // milisegundos enteros, y durante todo ese tiempo contesta WAIT.
    void set_reintentos(unsigned n) { max_reintentos_ = n; }
    bool con_reintentos(bool escritura, bool ap, unsigned a, uint32_t& v) {
        for (unsigned i = 0; i < max_reintentos_; ++i) {
            const int r = paquete(escritura, ap, a, v);
            if (r == ACK_OK)   return true;
            if (r == ACK_WAIT) { ++n_wait_; continue; }   // reintentar tal cual
            if (r == ACK_FAULT && ap) {
                // Solo tiene sentido recuperarse de un fallo del AP: el DP en
                // si no se bloquea.
                uint32_t st = 0;
                paquete(false, false, 0x4, st);
                uint32_t cero = 0x1Eu;
                paquete(true, false, 0x0, cero);
                ++n_recuperaciones_;
                continue;
            }
            return false;
        }
        return false;
    }
    unsigned recuperaciones() const { return n_recuperaciones_; }
    unsigned esperas() const { return n_wait_; }

    // Una lectura del AP llega con un ciclo de retraso: la sonda pide, y
    // recoge el dato en RDBUFF. Es como funciona el DAP de verdad.
    bool leer_ap_real(unsigned a, uint32_t& v) {
        uint32_t basura = 0;
        if (!leer_ap(a, basura)) return false;
        return leer_dp(0xC, v);                       // RDBUFF
    }

    // --- Memoria del sistema, por el AHB-AP ---------------------------------
    bool mem_read32(uint32_t addr, uint32_t& v) {
        if (!escribir_ap(0x4, addr)) return false;    // TAR
        return leer_ap_real(0xC, v);                  // DRW
    }
    bool mem_write32(uint32_t addr, uint32_t v) {
        if (!escribir_ap(0x4, addr)) return false;
        return escribir_ap(0xC, v);
    }
    // Un bloque con auto-incremento. La sonda tiene que TROCEARLO en la
    // frontera de 1 KiB, porque el auto-incremento de TAR no la cruza.
    unsigned mem_read_block(uint32_t addr, uint32_t* dst, unsigned n) {
        escribir_ap(0x0, 0x23000052u);                // CSW: palabra, incremento
        unsigned k = 0;
        while (k < n) {
            const uint32_t a = addr + 4u * k;
            unsigned hasta_frontera = (0x400u - (a & 0x3FFu)) / 4u;
            unsigned lote = n - k;
            if (lote > hasta_frontera) lote = hasta_frontera;
            escribir_ap(0x4, a);
            uint32_t basura = 0;
            if (!leer_ap(0xC, basura)) break;
            unsigned j = 0;
            for (; j < lote; ++j) {
                uint32_t v = 0;
                if (j + 1 < lote) { if (!leer_ap(0xC, v)) break; }
                else              { if (!leer_dp(0xC, v)) break; }
                dst[k + j] = v;
            }
            k += j;
            if (j < lote) break;
        }
        escribir_ap(0x0, 0x23000042u);                // CSW: sin incremento
        return k;
    }
    unsigned mem_write_block(uint32_t addr, const uint32_t* src, unsigned n) {
        escribir_ap(0x0, 0x23000052u);
        unsigned k = 0;
        while (k < n) {
            const uint32_t a = addr + 4u * k;
            unsigned hasta_frontera = (0x400u - (a & 0x3FFu)) / 4u;
            unsigned lote = n - k;
            if (lote > hasta_frontera) lote = hasta_frontera;
            escribir_ap(0x4, a);
            unsigned j = 0;
            for (; j < lote; ++j) if (!escribir_ap(0xC, src[k + j])) break;
            k += j;
            if (j < lote) break;
        }
        escribir_ap(0x0, 0x23000042u);
        return k;
    }

    // --- Control del nucleo, por los registros de Core Debug ----------------
    static constexpr uint32_t DHCSR = 0xE000EDF0u, DCRSR = 0xE000EDF4u,
                              DCRDR = 0xE000EDF8u, DEMCR = 0xE000EDFCu;
    static constexpr uint32_t LLAVE = 0xA05F0000u;

    bool halt() {
        return mem_write32(DHCSR, LLAVE | 0x3u);      // C_DEBUGEN | C_HALT
    }
    bool resume() { return mem_write32(DHCSR, LLAVE | 0x1u); }
    bool step()   { return mem_write32(DHCSR, LLAVE | 0x5u); }  // C_STEP
    bool is_halted() {
        uint32_t v = 0;
        if (!mem_read32(DHCSR, v)) return false;
        return (v & (1u << 17)) != 0;                 // S_HALT
    }
    // Los registros del nucleo van por la pareja DCRSR/DCRDR.
    bool leer_reg(unsigned sel, uint32_t& v) {
        if (!mem_write32(DCRSR, sel & 0x7Fu)) return false;
        return mem_read32(DCRDR, v);
    }
    bool escribir_reg(unsigned sel, uint32_t v) {
        if (!mem_write32(DCRDR, v)) return false;
        return mem_write32(DCRSR, (1u << 16) | (sel & 0x7Fu));
    }

    unsigned acks_ok() const { return n_ok_; }
    unsigned acks_mal() const { return n_mal_; }
    // Los tres ACK del protocolo, mas un codigo propio para la paridad mala.
    enum : int { ACK_OK = 1, ACK_WAIT = 2, ACK_FAULT = 4, ACK_PARIDAD = -1 };

private:
    // --- Capa fisica --------------------------------------------------------
    void clk_alto() { clk_->set_drive(id_clk_, float(vdd_), 30.0f); }
    void clk_bajo() { clk_->set_drive(id_clk_, 0.0f, 30.0f); }
    void gobernar(bool v) { dio_->set_drive(id_dio_, v ? float(vdd_) : 0.0f, 30.0f); }
    void soltar()   { dio_->set_hiz(id_dio_); }
    bool leer_dio() const { return dio_->voltage() > 0.5 * vdd_; }

    // Un ciclo de reloj. La sonda cambia SWDIO en el flanco de BAJADA y el
    // objetivo lo muestrea en el de SUBIDA; al reves cuando contesta.
    // Devuelve lo que habia en la linea en el flanco de subida.
    bool ciclo(int nivel) {          // -1: soltar la linea
        if (nivel >= 0) gobernar(nivel != 0); else soltar();
        sc_core::wait(sc_core::sc_time(tb_ * 0.5, sc_core::SC_SEC));
        clk_alto();                                   // flanco de subida
        sc_core::wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
        const bool v = leer_dio();
        sc_core::wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
        clk_bajo();                                   // flanco de bajada
        return v;
    }
    void pulsos(unsigned n, bool nivel) { for (unsigned i = 0; i < n; ++i) ciclo(nivel ? 1 : 0); }
    // Ciclos de REPOSO entre paquetes: la linea a cero. No son un adorno, es
    // lo que separa un paquete del siguiente para el objetivo.
    void reposo() { pulsos(2, false); soltar(); }
    void reset_linea() { pulsos(56, true); pulsos(2, false); }
    void secuencia(uint32_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i) ciclo(int((v >> i) & 1u));
    }

    static bool paridad(uint32_t v) {
        unsigned n = 0;
        for (unsigned i = 0; i < 32; ++i) n += (v >> i) & 1u;
        return (n & 1u) != 0;
    }

    // --- Un paquete SWD completo -------------------------------------------
    // Devuelve el ACK recibido: OK (001), WAIT (010) o FAULT (100). El que
    // llama decide que hacer con cada uno, que es como funciona ADIv5.
    int paquete(bool escritura, bool ap, unsigned a, uint32_t& dato) {
        const unsigned a2 = (a >> 2) & 3u;
        unsigned n = (ap ? 1u : 0u) + (escritura ? 0u : 1u) +
                     (a2 & 1u) + ((a2 >> 1) & 1u);
        // Peticion: arranque, APnDP, RnW, A[2:3], paridad, parada, park.
        ciclo(1);                                     // arranque
        ciclo(ap ? 1 : 0);
        ciclo(escritura ? 0 : 1);
        ciclo(int(a2 & 1u));
        ciclo(int((a2 >> 1) & 1u));
        ciclo(int(n & 1u));                           // paridad
        ciclo(0);                                     // parada
        ciclo(1);                                     // park
        ciclo(-1);                                    // turnaround: linea libre
        // ACK, tres bits con el menos significativo por delante.
        unsigned ack = 0;
        for (unsigned i = 0; i < 3; ++i) if (ciclo(-1)) ack |= 1u << i;
        if (ack != ACK_OK) {
            ++n_mal_;
            ciclo(-1);                                // turnaround
            reposo();
            return int(ack);
        }
        ++n_ok_;
        if (!escritura) {
            uint32_t v = 0;
            for (unsigned i = 0; i < 32; ++i) if (ciclo(-1)) v |= 1u << i;
            const bool p = ciclo(-1);
            ciclo(-1);                                // turnaround
            dato = v;
            reposo();
            return (p == paridad(v)) ? ACK_OK : ACK_PARIDAD;
        }
        ciclo(-1);                                    // turnaround
        for (unsigned i = 0; i < 32; ++i) ciclo(int((dato >> i) & 1u));
        ciclo(paridad(dato) ? 1 : 0);
        reposo();
        return ACK_OK;
    }

    analog_net_if *clk_, *dio_;
    double tb_, vdd_;
    int id_clk_ = -1, id_dio_ = -1;
    bool conectada_ = false;
    unsigned n_ok_ = 0, n_mal_ = 0, n_recuperaciones_ = 0, n_wait_ = 0;
    unsigned max_reintentos_ = 2000;
    bool     encendido_ = false;
};


} // namespace stm32
#endif // STM32_VERIF_SWD_PORT_H
