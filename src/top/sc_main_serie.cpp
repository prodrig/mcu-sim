// =============================================================================
// sc_main_serie.cpp — el banco del puente UART (P-14)
//
// CUARTO EJECUTABLE, y por la razón de siempre: construir piezas nuevas dentro
// del banco del F407 MUEVE su invariante, porque el orden en que SystemC
// despierta los procesos depende de cuántos módulos hay en la simulación. Este
// banco tiene el suyo, en `verif/invariantes.txt`, y crece con cada fase.
//
// FASE D1 — EL MOTOR, SOLO. `ReceptorUart` y `EmisorUart` sobre un nodo
// suelto, sin chip: lo que aquí falle es del motor y de nada más. Se comprueba
// lo que `SwoReceiver` no ejercita nunca -paridad, 5, 7 y 9 bits, uno y medio y
// dos de parada, break, errores provocados- y, en 8N1, los TIEMPOS exactos:
// que el receptor devuelva la trama en el centro del bit de parada y que el
// emisor tarde lo que tiene que tardar. La otra mitad del criterio de la fase
// no está aquí sino en `test407`: que su invariante no se mueva, que es la
// prueba de que el receptor extraído hace lo mismo que el de antes.
//
// FASE D2 — LA PIEZA. Un STM32F407VG entero, con el firmware `vcp_demo`
// -compilado contra la cabecera de ST, sin una adaptación al modelo- y un
// `PuenteSerie` en memoria colgado de PA2/PA3 (USART2) y de PA1/PA0 (RTS/CTS).
// El firmware saluda, devuelve lo que recibe, cuenta sus errores en un buzón
// de la SRAM y sabe dejar de leer a propósito, que es como se prueba el
// control de flujo. Aquí se comprueba de punta a punta lo que el alumno verá:
// el saludo, el eco, los 255 valores de byte, lo que pasa con los baudios
// equivocados, un cambio de velocidad a mitad de trama, el break y RTS/CTS.
//
// FASE D3 — TCP EN CRUDO. Un segundo puente en los mismos pines, desoldado
// hasta ahora, con `host="tcp:47355"`, y un cliente TCP de verdad en el propio
// banco: eco, los 255 valores, desconexion (lo que manda el MCU se descarta y
// se cuenta), reconexion, sustitucion de un cliente por otro y puerto ocupado.
//
// EL TIEMPO SIMULADO NO DEPENDE DE LA MAQUINA, y es a proposito. Un socket de
// verdad entrega los datos cuando el sistema operativo quiere; si la pieza los
// descubriera en su sondeo en tiempo simulado, cuantos sondeos hicieran falta
// dependeria de la maquina, que es lo que le pasa al stub de GDB en test407 y
// por lo que alli se contrasta el `resto` y no el total. Aqui no: el banco
// PROVOCA el efecto del anfitrion -conectar, mandar, cerrar- y ESPERA EN TIEMPO
// DE PARED, llamando al sondeo del canal, a que el canal lo tenga dentro. Solo
// entonces deja avanzar la simulacion. Mientras espera no corre ningun proceso
// de SystemC -comparten un hilo-, asi que la simulacion ve siempre lo mismo.
//
// FASE D5 — RFC 2217. Un tercer puente en los mismos pines, con
// `host="rfc2217:47357"` y `baudios="host"`, y un terminal RFC 2217 hecho con el
// códec de la D4 (verif/cliente_2217.h). Se prueba todo lo que el terminal
// puede hacer y el efecto que tiene EN LOS PINES Y EN EL FIRMWARE, no en el
// protocolo: los baudios que pide el terminal los ve la USART (con basura si
// no cuadran), el formato cambia lo que el firmware recibe, el break sube LBD,
// DTR y RTS se miden en voltios, el control de flujo evita el desbordamiento,
// los errores del MCU vuelven como LINESTATE y el RTS del MCU como el CTS del
// terminal. Y lo que no debe pasar: órdenes sin negociar COM-PORT, o baudios
// pedidos a un puente que los tiene fijos.
//
//   make testserie
// =============================================================================
#include <systemc>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <thread>
#include "../common/red.h"
#include "../common/asan_opciones.h"
#include "../common/analog_net.h"
#include "../common/huella_fw.h"
#include "../parts/motor_uart.h"
#include "../parts/puente_serie.h"
#include "soc_f4.h"
#include "../verif/image_loader.h"
#include "../verif/cliente_2217.h"

using namespace sc_core;
using namespace stm32;

namespace {

unsigned g_ok = 0, g_fallos = 0;

void grupo(const char* t) { std::printf("--- %s ---\n", t); }

bool check(bool c, const std::string& q) {
    if (c) { ++g_ok;     std::printf("  [OK  ] %s\n", q.c_str()); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
void check_eq(uint32_t got, uint32_t esp, const std::string& q) {
    if (got == esp) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); }
    else { ++g_fallos;
           std::printf("  [FALLO] %s (obtenido 0x%X, esperado 0x%X)\n",
                       q.c_str(), got, esp); }
}

FormatoUart fmt(const char* s) {
    FormatoUart f;
    const std::string e = parsea_formato(s, f);
    if (!e.empty()) std::printf("  [!!] formato de prueba malo: %s\n", e.c_str());
    return f;
}

// Una trama recibida y CUÁNDO se recibió.
struct Recibida { TramaUart t; sc_time cuando; };

// ---------------------------------------------------------------------------
// D1: una línea, un emisor y un receptor
// ---------------------------------------------------------------------------
struct TbMotor : sc_module {
    AnalogNet    linea{"linea"};
    int          drv = -1;
    EmisorUart*  tx  = nullptr;
    ReceptorUart rx{linea, 115200.0};
    std::vector<Recibida> llegadas;
    sc_event     llego;
    sc_event     fin;          // el banco de la pieza espera a que acabe este

    SC_CTOR(TbMotor) {
        drv = linea.register_driver("tx");
        tx  = new EmisorUart(linea, drv, 115200.0);
        SC_THREAD(escucha);
        SC_THREAD(run);  set_stack_size(512 * 1024);
    }
    ~TbMotor() { delete tx; }

    void escucha() {
        for (;;) {
            const TramaUart t = rx.recibe();
            llegadas.push_back({ t, sc_time_stamp() });
            llego.notify(SC_ZERO_TIME);
        }
    }

    void configura(const FormatoUart& f, double baud_tx, double baud_rx) {
        tx->set_formato(f);  tx->set_bitrate(baud_tx);
        rx.set_formato(f);   rx.set_bitrate(baud_rx);
    }
    // Deja la línea en reposo lo bastante para que el receptor se resitúe.
    void pausa() { tx->reposo(); wait(1, SC_MS); }

    // Manda, espera lo que haga falta y devuelve lo que llegó.
    std::vector<Recibida> ida(const std::vector<unsigned>& datos) {
        llegadas.clear();
        for (unsigned d : datos) tx->emite(d);
        wait(1, SC_MS);
        return llegadas;
    }

    // Ida y vuelta en un formato: todo llega, igual y sin errores.
    void ida_y_vuelta(const char* nombre, const std::vector<unsigned>& datos) {
        const FormatoUart f = fmt(nombre);
        configura(f, 115200.0, 115200.0);
        pausa();
        const std::vector<Recibida> r = ida(datos);
        bool igual = r.size() == datos.size();
        bool limpio = true;
        for (size_t i = 0; igual && i < r.size(); ++i) {
            if (r[i].t.dato != (datos[i] & f.mascara())) igual = false;
            if (!r[i].t.ok()) limpio = false;
        }
        check(igual, std::string(nombre) + ": las " + std::to_string(datos.size()) +
                     " tramas llegan y dicen lo mismo");
        check(limpio, std::string(nombre) + ": sin error de trama ni de paridad");
    }

    void run() {
        // --- 0. Arranque con la linea sin gobierno ---------------------------
        grupo("S0 Arranque con la linea sin gobierno");
        {
            // Lo que ya resolvia SwoReceiver: al arrancar, el nodo no lo
            // gobierna nadie y lee bajo, y el receptor NO puede tomarlo por un
            // bit de arranque. Tiene que ser lo primero del banco: es una
            // propiedad del arranque, no de una linea que se suelta despues.
            wait(2, SC_MS);
            check(llegadas.empty(),
                  "con la linea sin gobierno no aparece ninguna trama fantasma");
            tx->reposo();
            wait(200, SC_US);
            const std::vector<Recibida> r = ida({ 0x7E });
            check(r.size() == 1u && r[0].t.ok() && r[0].t.dato == 0x7E,
                  "y en cuanto hay reposo, la primera trama llega bien");
        }

        // --- 1. 8N1 y los tiempos exactos ---------------------------------
        grupo("S1 8N1: lo que ya hacia SwoReceiver, con los tiempos medidos");
        {
            configura(fmt("8N1"), 115200.0, 115200.0);
            pausa();
            // El tiempo de bit, construido EXACTAMENTE como lo construyen el
            // emisor y el receptor: la conversion a picosegundos redondea.
            const double  tb_s = 1.0 / 115200.0;
            const sc_time tb(tb_s, SC_SEC);
            const sc_time t0 = sc_time_stamp();
            llegadas.clear();
            tx->emite(0x55);
            const sc_time dura = sc_time_stamp() - t0;
            check(dura == tb * 10.0 ||
                  dura == tb * 9.0 + sc_time(tb_s * 0.5 * 2, SC_SEC),
                  "el emisor tarda diez tiempos de bit en un 8N1: arranque, "
                  "ocho datos y la parada (" + dura.to_string() + ")");
            wait(1, SC_MS);
            if (check(llegadas.size() == 1u, "llega una trama")) {
                check_eq(llegadas[0].t.dato, 0x55u, "y es 0x55");
                // El receptor devuelve en el CENTRO del bit de parada: 1,5 bits
                // hasta el centro del bit 0 y ocho mas. Si esto cambia, el SWO
                // del F407 cambia con ello.
                const sc_time esp = sc_time(tb_s * 1.5, SC_SEC) + tb * 8.0;
                const sc_time vio = llegadas[0].cuando - t0;
                check(vio == esp,
                      "el receptor la entrega en el centro del bit de parada, "
                      "9,5 bits despues del flanco de arranque (" +
                      vio.to_string() + ")");
            }
        }
        ida_y_vuelta("8N1", { 0x00, 0xFF, 0x55, 0xAA, 0x01, 0x80, 0x0D, 0x0A });

        // --- 2. Los demás formatos -----------------------------------------
        grupo("S2 Los formatos que el SWO no ejercita");
        ida_y_vuelta("7E1", { 0x00, 0x7F, 0x41, 0x2A, 0x55 });
        ida_y_vuelta("7O1", { 0x00, 0x7F, 0x41, 0x2A, 0x55 });
        ida_y_vuelta("8E1", { 0x00, 0xFF, 0x81, 0x7E });
        ida_y_vuelta("8O2", { 0x00, 0xFF, 0x81, 0x7E });
        ida_y_vuelta("8M1", { 0x00, 0xFF, 0x3C });
        ida_y_vuelta("8S1", { 0x00, 0xFF, 0x3C });
        ida_y_vuelta("9N1", { 0x000, 0x1FF, 0x1A5, 0x100 });
        ida_y_vuelta("5N1", { 0x00, 0x1F, 0x15 });
        ida_y_vuelta("6E2", { 0x00, 0x3F, 0x2A });
        ida_y_vuelta("8N1.5", { 0x00, 0xFF, 0x5A });

        // --- 3. Lo que dura la parada ----------------------------------------
        grupo("S3 La parada: 1, 1,5 y 2 bits, medidos");
        for (const char* n : { "8N1", "8N1.5", "8N2" }) {
            const FormatoUart f = fmt(n);
            configura(f, 115200.0, 115200.0);
            pausa();
            const double  tb_s = 1.0 / 115200.0;
            const sc_time esp = sc_time(tb_s, SC_SEC) * 9.0 +
                                sc_time(tb_s * 0.5 * f.medios_de_parada(), SC_SEC);
            const sc_time t0 = sc_time_stamp();
            tx->emite(0xA5);
            check(sc_time_stamp() - t0 == esp,
                  std::string(n) + ": la trama dura " +
                  std::to_string(f.medios_de_trama() / 2.0).substr(0, 4) +
                  " tiempos de bit");
        }
        {
            // Un emisor con dos de parada habla con un receptor que espera
            // uno: el receptor solo mira el PRIMERO. Es lo que permite que un
            // terminal en 8N2 hable con un firmware en 8N1.
            configura(fmt("8N1"), 115200.0, 115200.0);
            tx->set_formato(fmt("8N2"));
            pausa();
            const std::vector<Recibida> r = ida({ 0x31, 0x32, 0x33 });
            check(r.size() == 3u && r[0].t.ok() && r[2].t.dato == 0x33,
                  "un emisor 8N2 y un receptor 8N1 se entienden");
        }

        // --- 4. Los errores, provocados --------------------------------------
        grupo("S4 Los errores, provocados a proposito");
        {
            configura(fmt("8E1"), 115200.0, 115200.0);
            pausa();
            llegadas.clear();
            rx.borra_contadores();
            tx->emite_con_paridad_mala(0x41);
            wait(1, SC_MS);
            check(llegadas.size() == 1u && llegadas[0].t.error_paridad &&
                  !llegadas[0].t.error_trama,
                  "paridad al reves: error de paridad, y SOLO de paridad");
            check(llegadas.size() == 1u && llegadas[0].t.dato == 0x41,
                  "el dato llega igual: la paridad no lo toca");
            check(rx.errores_paridad() == 1u, "y el contador lo apunta");
        }
        {
            // Sin paridad, «paridad mala» no puede estropear nada.
            configura(fmt("8N1"), 115200.0, 115200.0);
            pausa();
            const std::vector<Recibida> r = [&] {
                llegadas.clear(); tx->emite_con_paridad_mala(0x41);
                wait(1, SC_MS); return llegadas; }();
            check(r.size() == 1u && r[0].t.ok() && r[0].t.dato == 0x41,
                  "sin paridad, emite_con_paridad_mala manda la trama normal");
        }
        {
            // Baudios distintos: el emisor a 9600 y el receptor a 115200. Es
            // el fallo mas comun de un alumno y el que el puente tiene que
            // reproducir: tiene que salir BASURA, no el dato.
            configura(fmt("8N1"), 9600.0, 115200.0);
            pausa();
            rx.borra_contadores();
            const std::vector<Recibida> r = ida({ 0x48, 0x6F, 0x6C, 0x61 });
            bool alguna_bien = false;
            for (const Recibida& x : r)
                if (x.t.ok() && (x.t.dato == 0x48 || x.t.dato == 0x6F ||
                                 x.t.dato == 0x6C || x.t.dato == 0x61))
                    alguna_bien = true;
            check(!r.empty(), "con baudios distintos el receptor ve tramas...");
            check(!alguna_bien, "...y ninguna es lo que se mando");
            check(rx.errores_trama() > 0u,
                  "y hay errores de trama, como en la placa");
        }
        {
            // Break: la linea a cero mas de una trama.
            configura(fmt("8N1"), 115200.0, 115200.0);
            pausa();
            rx.borra_contadores();
            llegadas.clear();
            tx->emite_break();
            tx->reposo();
            wait(200, SC_US);
            tx->emite(0x42);
            wait(1, SC_MS);
            check(!llegadas.empty() && llegadas[0].t.es_break &&
                  llegadas[0].t.error_trama,
                  "un break se ve como break, y como error de trama");
            check(!llegadas.empty() && llegadas.back().t.ok() &&
                  llegadas.back().t.dato == 0x42,
                  "y el receptor se recupera: la trama siguiente llega bien");
            check(rx.breaks() == 1u && tx->breaks() == 1u,
                  "un break emitido, un break contado");
        }
        {
            // Un 0x00 NO es un break: su bit de parada es 1.
            configura(fmt("8N1"), 115200.0, 115200.0);
            pausa();
            const std::vector<Recibida> r = ida({ 0x00 });
            check(r.size() == 1u && r[0].t.ok() && !r[0].t.es_break,
                  "un 0x00 es un dato, no un break: su parada es un uno");
        }

        tx->reposo();
        fin.notify(SC_ZERO_TIME);
    }
};

// ---------------------------------------------------------------------------
// D2: un F407 con `vcp_demo` y un PuenteSerie en memoria
// ---------------------------------------------------------------------------
// El buzon del firmware, en el orden de `verif/fw/vcp_demo/main.c`.
enum Buzon : uint32_t {
    B_LISTO = 0, B_RX = 4, B_ECO = 8, B_FE = 12, B_NE = 16, B_PE = 20,
    B_ORE = 24, B_LBD = 28, B_PAUSAS = 32, B_BRR = 36, B_ULTIMO = 40, B_MS = 44
};

struct TbPieza : sc_module {
    SocF4        soc{"soc", DBG_PINES, Cableado(), MCU_STM32F407VG};
    PuenteSerie* vcp = nullptr;
    PuenteSerie* vcp_tcp = nullptr;       // D3: el mismo puente, por TCP
    PuenteSerie* vcp_ocupado = nullptr;   // D3: y uno que no puede escuchar
    PuenteSerie* vcp_2217 = nullptr;      // D5: RFC 2217, con DTR en PB0
    sc_event&    empieza;
    static constexpr unsigned PUERTO = 47355;   // alto: que no choque con nada
    static constexpr unsigned PUERTO_2217 = 47357;
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1, d_pb2 = -1;

    static PuenteSerie::Config config() {
        PuenteSerie::Config c;
        c.destino = serie::parsea("memoria");
        return c;                          // 115200 8N1, sin flujo, callado
    }

    TbPieza(sc_module_name nm, sc_event& ev) : sc_module(nm), empieza(ev) {
        // rx <- PA2 (USART2_TX), tx -> PA3 (USART2_RX), cts <- PA1 (RTS),
        // rts -> PA0 (el CTS, que este firmware no usa: se mira la tension).
        vcp = new PuenteSerie("vcp", &soc.pinmux.analog(0, 2),
                              &soc.pinmux.analog(0, 3),
                              &soc.pinmux.analog(0, 1),
                              &soc.pinmux.analog(0, 0), nullptr, config());
        PuenteSerie::Config ct = config();
        ct.destino = serie::parsea("tcp:" + std::to_string(PUERTO));
        vcp_tcp = new PuenteSerie("vcp_tcp", &soc.pinmux.analog(0, 2),
                                  &soc.pinmux.analog(0, 3),
                                  &soc.pinmux.analog(0, 1),
                                  &soc.pinmux.analog(0, 0), nullptr, ct);
        vcp_tcp->set_enabled(false);      // desoldado hasta la fase D3
        // Y otro en el MISMO puerto, que no puede escuchar (P17). Sin pines.
        vcp_ocupado = new PuenteSerie("vcp_ocupado", nullptr, nullptr, nullptr,
                                      nullptr, nullptr, ct);
        vcp_ocupado->set_enabled(false);
        // D5: el de RFC 2217. Los baudios los fija el terminal.
        PuenteSerie::Config cr = config();
        cr.destino = serie::parsea("rfc2217:" + std::to_string(PUERTO_2217));
        cr.baudios_host = true;
        vcp_2217 = new PuenteSerie("vcp_2217", &soc.pinmux.analog(0, 2),
                                   &soc.pinmux.analog(0, 3),
                                   &soc.pinmux.analog(0, 1),
                                   &soc.pinmux.analog(0, 0),
                                   &soc.pinmux.analog(1, 0), cr);
        vcp_2217->set_enabled(false);
        SC_HAS_PROCESS(TbPieza);
        SC_THREAD(run);  set_stack_size(1024 * 1024);
    }
    ~TbPieza() { delete vcp_2217; delete vcp_ocupado; delete vcp_tcp; delete vcp; }

    // ---- Tiempo de PARED, con la simulacion parada ---------------------------
    // Espera a que se cumpla algo mirando el canal TCP, hasta dos segundos de
    // reloj. No avanza el tiempo simulado: es lo que lo hace determinista.
    bool en_pared(const std::function<bool()>& c, double seg = 2.0) {
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            vcp_tcp->tcp()->sondear();
            if (c()) return true;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                    .count() > seg) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    bool manda(red::socket_t c, const std::string& m) {
        size_t hecho = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (hecho < m.size()) {
            const long n = red::enviar(c, m.data() + hecho, m.size() - hecho);
            if (n > 0) { hecho += size_t(n); continue; }
            if (n < 0 && red::reintentar() &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                    .count() < 2.0) continue;
            return false;
        }
        return true;
    }
    // Lee `n` bytes del cliente, o lo que llegue en dos segundos de reloj.
    std::string lee(red::socket_t c, size_t n) {
        std::string r;
        char b[512];
        en_pared([&] {
            const long k = red::recibir(c, b, sizeof b);
            if (k > 0) r.append(b, size_t(k));
            return r.size() >= n;
        });
        return r;
    }
    // ¿El otro lado ha cerrado? recv devuelve 0.
    bool cerrado_por_el_otro(red::socket_t c) {
        char b[16];
        return en_pared([&] { return red::recibir(c, b, sizeof b) == 0; });
    }

    uint32_t buzon(uint32_t off) { return soc.sram1.peek32(off); }

    // Espera a que se cumpla algo, mirando cada 100 us, como mucho `max`.
    bool espera(const std::function<bool()>& c, const sc_time& max) {
        const sc_time t0 = sc_time_stamp();
        while (!c()) {
            if (sc_time_stamp() - t0 >= max) return false;
            wait(100, SC_US);
        }
        return true;
    }
    bool recibe(const std::string& esp, const sc_time& max) {
        return espera([&] { return vcp->recibido().size() >= esp.size(); }, max) &&
               vcp->recibido() == esp;
    }

    void enciende() {
        d_vdd  = soc.pwr_pads.vdd.register_driver("tb_vdd");
        d_vdda = soc.pwr_pads.vdda.register_driver("tb_vdda");
        d_nrst = soc.pwr_pads.nrst.register_driver("tb_nrst");
        d_bt0  = soc.pwr_pads.boot0.register_driver("tb_boot0");
        d_pb2  = soc.pinmux.analog(1, 2).register_driver("tb_pb2");
        soc.pwr_pads.vdd.set_drive(d_vdd, 0.0f, 1.0f);
        soc.pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        soc.pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);        // arranca de Flash
        soc.pinmux.analog(1, 2).set_drive(d_pb2, 0.0f, 10e3f);
        soc.pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        // Sin la onda cuadrada de los relojes internos, como hace `sim` por
        // omision: son el grueso de los sucesos y aqui no se mira el reloj.
        soc.rcc.set_internal_waveforms(false);
        wait(10, SC_US);
        soc.pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);
        soc.pwr_pads.vdda.set_drive(d_vdda, 3.3f, 0.1f);
        wait(100, SC_US);
    }

    void run() {
        wait(empieza);

        // --- 0. El firmware es el versionado ---------------------------------
        grupo("P0 Imagen de firmware [verif/fw/huellas.txt]");
        {
            const huella_fw::Veredicto v =
                huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "serie");
            std::printf("         %d imagenes: %s\n", v.comprobadas, v.detalle.c_str());
            check(v.ok(), "vcp_demo.bin es la imagen versionada");
        }

        // --- 1. Arranque y saludo -------------------------------------------
        grupo("P1 El firmware arranca y saluda por el puente");
        enciende();
        ImageLoader ld(soc);
        const long n = ld.load_file("verif/fw/vcp_demo/vcp_demo.bin", addr::FLASH_BASE);
        if (!check(n > 0, "vcp_demo.bin se carga en la Flash")) { termina(); return; }
        for (unsigned i = 0; i < 48; i += 4) ld.poke32(addr::SRAM1_BASE + i, 0);
        soc.pwr_pads.nrst.set_hiz(d_nrst);
        check(espera([&] { return buzon(B_LISTO) == 1u; }, sc_time(50, SC_MS)),
              "el firmware llega a su bucle de eco");
        check(recibe("vcp_demo listo\r\n", sc_time(10, SC_MS)),
              "y el saludo llega entero al puente: \"vcp_demo listo\\r\\n\"");
        // 16 MHz / 115200 = 8,68: mantisa 8 y fraccion round(0,68 * 16) = 11
        check_eq(buzon(B_BRR), 0x8Bu, "BRR = 0x8B: el divisor del driver de ST");
        check(vcp->errores_trama() == 0 && vcp->errores_paridad() == 0,
              "sin un error en el lado del puente");

        // --- 2. Eco -----------------------------------------------------------
        grupo("P2 Lo que se teclea vuelve");
        {
            vcp->borra_recibido();
            const std::string m = "Hola, mcu-sim\r\n";
            check(vcp->envia(m) == m.size(), "el mensaje cabe en la cola");
            check(recibe(m, sc_time(20, SC_MS)), "el eco es exactamente el mensaje");
            check_eq(buzon(B_RX), 15u, "el firmware conto quince tramas");
            check_eq(buzon(B_ECO), 15u, "y devolvio las quince");
            check(buzon(B_FE) == 0 && buzon(B_NE) == 0 && buzon(B_PE) == 0 &&
                  buzon(B_ORE) == 0,
                  "sin error de trama, ruido, paridad ni desbordamiento");
            check(vcp->bytes_hacia_mcu() == 15u,
                  "el puente mando quince bytes");
        }

        // --- 3. Transparencia -------------------------------------------------
        grupo("P3 Los 255 valores de byte, incluidos 0x00 y 0xFF");
        {
            vcp->borra_recibido();
            std::string todos;
            for (unsigned v = 0; v < 256; ++v)
                if (v != 0x13u) todos += char(v);    // 0x13 es la orden de pausa
            vcp->envia(todos);
            check(recibe(todos, sc_time(100, SC_MS)),
                  "los 255 bytes vuelven en orden y sin tocar");
            check(buzon(B_FE) == 0 && buzon(B_ORE) == 0, "sin un error");
        }

        // --- 4. Control de flujo ----------------------------------------------
        grupo("P4 RTS/CTS: sin control de flujo, la USART se desborda");
        std::string rafaga;
        for (unsigned i = 0; i < 40; ++i) rafaga += char('A' + (i % 26));
        {
            vcp->borra_recibido();
            const uint32_t ore0 = buzon(B_ORE);
            vcp->envia(std::string(1, '\x13') + rafaga);
            wait(20, SC_MS);
            check_eq(buzon(B_PAUSAS), 1u, "el firmware atendio la pausa");
            check(buzon(B_ORE) > ore0,
                  "mientras no leia, llegaron bytes encima: desbordamiento");
            check(vcp->recibido().size() < rafaga.size(),
                  "y el eco se queda corto: se han perdido bytes (" +
                  std::to_string(vcp->recibido().size()) + " de 40)");
        }
        grupo("P5 RTS/CTS: con control de flujo, el puente espera");
        {
            vcp->set_flujo_rtscts(true);
            vcp->borra_recibido();
            const uint32_t ore0 = buzon(B_ORE);
            vcp->envia(std::string(1, '\x13') + rafaga);
            check(recibe(rafaga, sc_time(40, SC_MS)),
                  "los cuarenta llegan y vuelven, en orden");
            check_eq(buzon(B_PAUSAS), 2u, "con su pausa de por medio");
            check(buzon(B_ORE) == ore0, "sin un solo desbordamiento");
            check(vcp->esperas_por_cts() > 0,
                  "porque el puente espero al RTS del MCU");
            vcp->set_flujo_rtscts(false);
        }

        // --- 6. Baudios equivocados -------------------------------------------
        grupo("P6 Baudios equivocados: basura, como en la placa");
        {
            wait(2, SC_MS);
            vcp->borra_recibido();
            const uint32_t err0 = buzon(B_FE) + buzon(B_NE);
            vcp->set_baudios(9600.0);
            vcp->envia("Hola");
            wait(15, SC_MS);
            check(buzon(B_FE) + buzon(B_NE) > err0,
                  "el puente a 9600 y el firmware a 115200: la USART levanta "
                  "errores de trama o de ruido");
            check(vcp->recibido().find("Hola") == std::string::npos,
                  "y el eco no dice Hola");
            vcp->set_baudios(115200.0);
            wait(5, SC_MS);
            vcp->borra_recibido();
            vcp->envia("ok\r\n");
            check(recibe("ok\r\n", sc_time(20, SC_MS)),
                  "de vuelta a 115200, todo vuelve a funcionar");
        }

        // --- 7. Cambio de velocidad a mitad de trama (D-4) --------------------
        grupo("P7 Un cambio a mitad de trama espera a la siguiente (D-4)");
        {
            // El EMISOR del puente: se le cambian formato y baudios con una Z
            // en la linea. La Z tiene que llegar entera; si el emisor leyera
            // el formato en vivo, le meteria un bit de paridad (un 0, porque
            // 0x5A tiene cuatro unos) donde el MCU espera la parada.
            wait(2, SC_MS);
            const uint32_t rx0 = buzon(B_RX), fe0 = buzon(B_FE);
            vcp->envia("Z");
            wait(30, SC_US);                       // la trama dura 87 us
            FormatoUart f8e1;
            parsea_formato("8E1", f8e1);
            vcp->set_formato(f8e1);
            vcp->set_baudios(57600.0);
            wait(3, SC_MS);
            check(buzon(B_RX) == rx0 + 1 && buzon(B_FE) == fe0 &&
                  buzon(B_ULTIMO) == uint32_t('Z'),
                  "emisor: la Z que ya estaba en la linea llega entera, sin "
                  "la velocidad ni la paridad nuevas");
            vcp->set_formato(FormatoUart{});
            vcp->set_baudios(115200.0);
            wait(5, SC_MS);
        }
        {
            // El RECEPTOR del puente: se le cambian los baudios mientras el MCU
            // esta devolviendo una Y. El flanco de arranque de la Y se busca
            // en PA2, que es por donde sale.
            vcp->borra_recibido();
            const uint64_t et0 = vcp->errores_trama(), ep0 = vcp->errores_paridad();
            vcp->envia("Y");
            const analog_net_if& pa2 = soc.pinmux.analog(0, 2);
            wait(100, SC_US);                      // que la Y llegue al MCU
            const sc_time t0 = sc_time_stamp();
            while (pa2.voltage() > 1.0f && sc_time_stamp() - t0 < sc_time(5, SC_MS))
                wait(sc_time(5, SC_MS), pa2.value_changed_event());
            check(pa2.voltage() < 1.0f, "el MCU empieza a devolver la Y");
            wait(30, SC_US);
            vcp->set_baudios(57600.0);
            wait(3, SC_MS);
            check(vcp->recibido() == "Y" && vcp->errores_trama() == et0 &&
                  vcp->errores_paridad() == ep0,
                  "receptor: la Y que ya estaba en la linea se lee entera a "
                  "115200");
            vcp->set_baudios(115200.0);
            wait(5, SC_MS);
        }

        // --- 8. Break -----------------------------------------------------------
        grupo("P8 Un break del puente es un break LIN para la USART");
        {
            const uint32_t lbd0 = buzon(B_LBD);
            vcp->envia_break();
            check(espera([&] { return buzon(B_LBD) == lbd0 + 1; }, sc_time(5, SC_MS)),
                  "LBD sube una vez, y el firmware lo cuenta");
            vcp->borra_recibido();
            vcp->envia("b\r\n");
            check(recibe("b\r\n", sc_time(20, SC_MS)),
                  "y despues del break la linea sigue funcionando");
        }

        // --- 9. Las lineas que gobierna el puente -------------------------------
        grupo("P9 El rts del puente, en el pin");
        {
            const analog_net_if& pa0 = soc.pinmux.analog(0, 0);
            check(pa0.voltage() < 0.5f, "en reposo, rts bajo: el MCU puede mandar");
            vcp->set_rts(false);
            wait(1, SC_US);
            check(pa0.voltage() > 3.0f, "set_rts(false) lo sube");
            vcp->set_rts(true);
            wait(1, SC_US);
            check(pa0.voltage() < 0.5f, "y set_rts(true) lo vuelve a bajar");
        }

        // --- 10. La cola hacia el MCU -------------------------------------------
        grupo("P10 La cola hacia el MCU tiene limite, y lo dice");
        {
            CanalMemoria c(8);
            check(c.empuja("0123456789") == 8u, "con 8 de cola caben 8 de 10");
            check(c.rechazados() == 2u && c.pendientes() == 8u,
                  "los otros dos se rechazan y se cuentan");
            uint8_t b[16];
            check(c.leer(b, 16) == 8u && b[0] == '0' && b[7] == '7',
                  "y salen en orden, los que cupieron");
            check(c.leer(b, 16) == 0u, "vacia, leer no bloquea: devuelve cero");
        }

        tcp_en_crudo();
        rfc2217();
        termina();
    }

    // -----------------------------------------------------------------------
    // D3: TCP en crudo
    // -----------------------------------------------------------------------
    void tcp_en_crudo() {
        CanalTcp* tcp = vcp_tcp->tcp();
        const std::string dir = "localhost:" + std::to_string(PUERTO);

        grupo("P11 TCP en crudo: el puente escucha y acepta");
        vcp->set_enabled(false);                  // se desuelda el de memoria
        vcp_tcp->set_enabled(true);               // y se suelda el de TCP
        wait(1, SC_MS);
        if (!check(vcp_tcp->canal_ok(), "el puente escucha en " + dir)) {
            std::printf("         %s\n", vcp_tcp->error_canal().c_str());
            return;
        }
        check(vcp_tcp->describir() == "TCP en crudo en " + dir + ", 115200 8N1",
              "y lo dice: \"" + vcp_tcp->describir() + "\"");
        check(!tcp->conectado(), "sin cliente todavia");
        red::socket_t c1 = red::conecta_local(PUERTO);
        check(red::valido(c1), "un cliente se conecta");
        check(en_pared([&] { return tcp->conectado(); }), "y el puente lo acepta");

        grupo("P12 El eco, por TCP");
        {
            const uint32_t eco0 = buzon(B_ECO);
            const std::string m = "Hola por TCP\r\n";
            manda(c1, m);
            check(en_pared([&] { return tcp->pendientes() == m.size(); }),
                  "los catorce bytes estan en el canal antes de simular");
            wait(10, SC_MS);
            check(lee(c1, m.size()) == m, "y vuelven, exactos, por el socket");
            check_eq(buzon(B_ECO) - eco0, uint32_t(m.size()),
                     "el firmware devolvio los catorce");
        }

        grupo("P13 Los 255 valores de byte, por TCP");
        {
            std::string todos;
            for (unsigned v = 0; v < 256; ++v) if (v != 0x13u) todos += char(v);
            manda(c1, todos);
            check(en_pared([&] { return tcp->pendientes() == todos.size(); }),
                  "los 255 bytes estan en el canal");
            wait(60, SC_MS);
            check(lee(c1, todos.size()) == todos,
                  "y vuelven en orden: 0x00, 0xFF y 0x0D no se tocan");
        }

        grupo("P14 Sin cliente, lo que manda el MCU se descarta y se cuenta");
        {
            manda(c1, "x");
            check(en_pared([&] { return tcp->pendientes() == 1u; }), "una x en el canal");
            red::cerrar(c1);
            check(en_pared([&] { return !tcp->conectado(); }),
                  "el cliente cierra, y el puente se entera");
            const uint64_t d0 = tcp->descartados();
            const uint32_t eco0 = buzon(B_ECO);
            wait(5, SC_MS);
            check_eq(buzon(B_ECO) - eco0, 1u,
                     "la x llega al MCU igual: ya estaba dentro");
            check(tcp->descartados() == d0 + 1,
                  "y su eco, sin nadie al otro lado, se descarta y se cuenta");
        }

        grupo("P15 Reconexion en caliente");
        red::socket_t c2 = red::conecta_local(PUERTO);
        check(en_pared([&] { return tcp->conectado(); }), "un cliente nuevo entra");
        {
            manda(c2, "de nuevo\r\n");
            en_pared([&] { return tcp->pendientes() == 10u; });
            wait(5, SC_MS);
            check(lee(c2, 10) == "de nuevo\r\n", "y el eco funciona como antes");
        }

        grupo("P16 Un cliente nuevo sustituye al anterior (D-5)");
        red::socket_t c3 = red::conecta_local(PUERTO);
        {
            check(en_pared([&] { return tcp->sustituidos() == 1u; }),
                  "el tercero entra con el segundo aun conectado");
            check(cerrado_por_el_otro(c2),
                  "y al segundo se le cierra la conexion");
            manda(c3, "tres\r\n");
            en_pared([&] { return tcp->pendientes() == 6u; });
            wait(5, SC_MS);
            check(lee(c3, 6) == "tres\r\n", "el eco va al tercero");
            check(tcp->conexiones() == 3u, "tres conexiones en total");
        }

        grupo("P17 Puerto ocupado");
        {
            CanalTcp otro(PUERTO);
            check(!otro.abierto(),
                  "un segundo canal en el mismo puerto no puede escuchar");
            // Y una PIEZA con el puerto cogido no se da por buena: `sim` mira
            // canal_ok() y no arranca. Esta se construyo en la elaboracion,
            // despues de vcp_tcp y sobre su mismo puerto.
            check(!vcp_ocupado->canal_ok() &&
                  vcp_ocupado->error_canal().find("no se puede escuchar en " + dir) == 0,
                  "un puente sobre un puerto cogido lo dice: \"" +
                  vcp_ocupado->error_canal() + "\"");
        }

        red::cerrar(c2);
        red::cerrar(c3);
        vcp_tcp->set_enabled(false);
        wait(1, SC_MS);
    }

    // -----------------------------------------------------------------------
    // D5: RFC 2217
    // -----------------------------------------------------------------------
    // Espera en tiempo SIMULADO a que el MCU empiece a mandar por PA2.
    bool espera_arranque_mcu() {
        const analog_net_if& pa2 = soc.pinmux.analog(0, 2);
        const sc_time t0 = sc_time_stamp();
        while (pa2.voltage() > 1.0f && sc_time_stamp() - t0 < sc_time(5, SC_MS))
            wait(sc_time(5, SC_MS), pa2.value_changed_event());
        return pa2.voltage() < 1.0f;
    }
    // Lo mismo en PA3, que es por donde el puente le manda al MCU.
    bool espera_arranque_puente() {
        const analog_net_if& pa3 = soc.pinmux.analog(0, 3);
        const sc_time t0 = sc_time_stamp();
        while (pa3.voltage() > 1.0f && sc_time_stamp() - t0 < sc_time(5, SC_MS))
            wait(sc_time(5, SC_MS), pa3.value_changed_event());
        return pa3.voltage() < 1.0f;
    }
    // Manda datos por el terminal y espera a que estén dentro del puente.
    bool teclea(Cliente2217& t, CanalHost& c, const std::string& m) {
        const std::size_t p0 = c.pendientes();
        t.datos(m);
        return t.espera([&] { return c.pendientes() >= p0 + m.size(); });
    }
    // Deja correr la simulación y recoge lo que haya llegado al terminal.
    std::string eco(Cliente2217& t, std::size_t n, const sc_time& max) {
        const sc_time t0 = sc_time_stamp();
        for (;;) {
            t.espera([&] { return t.recibido().size() >= n; }, 0.0);
            if (t.recibido().size() >= n || sc_time_stamp() - t0 >= max) break;
            wait(500, SC_US);
        }
        std::string r = t.recibido();
        t.recibido().clear();
        return r;
    }

    void rfc2217() {
        CanalRfc2217* rfc = vcp_2217->rfc2217();
        CanalHost& canal = vcp_2217->canal();
        const std::string dir = "localhost:" + std::to_string(PUERTO_2217);
        const analog_net_if& pa0 = soc.pinmux.analog(0, 0);
        const analog_net_if& pa3 = soc.pinmux.analog(0, 3);
        const analog_net_if& pb0 = soc.pinmux.analog(1, 0);
        namespace cpo = telnet::cpo;
        using V = std::vector<uint8_t>;

        grupo("P18 RFC 2217: negociacion y estado de modem inicial");
        vcp_2217->set_enabled(true);
        wait(1, SC_MS);
        if (!check(vcp_2217->canal_ok() && rfc, "el puente escucha en " + dir)) {
            std::printf("         %s\n", vcp_2217->error_canal().c_str());
            return;
        }
        check(vcp_2217->describir() == "RFC 2217 en " + dir + ", 115200 8N1",
              "y lo dice: \"" + vcp_2217->describir() + "\"");
        Cliente2217 t;
        t.bomba = [&] { canal.sondear(); };
        check(t.conecta(PUERTO_2217) && t.espera([&] { return canal.conectado(); }),
              "un terminal se conecta");
        t.calma();
        check(t.bytes() == 0u,
              "el servidor es pasivo: al conectarse no manda nada (D-13)");
        check(t.negocia(), "COM-PORT aceptado y BINARY en los dos sentidos");
        check(rfc->negociador().com_port() && rfc->negociador().binario_entrada() &&
              rfc->negociador().binario_salida(),
              "y el puente lo sabe igual");
        {
            // El RTS del MCU esta bajo (RTSE y nada por leer): el CTS del
            // terminal, activo. DSR y DCD siempre. Y los tres «han cambiado»,
            // porque es la primera vez.
            t.espera([&] { return t.sucesos_pendientes() > 0; });
            const auto m = t.saca_todas(cpo::NOTIFY_MODEMSTATE + cpo::RESPUESTA);
            check(m.size() == 1u && m[0] == V{0xBB},
                  "llega un NOTIFY-MODEMSTATE 0xBB: CTS, DSR y DCD, con sus deltas");
        }

        grupo("P19 La firma");
        {
            V r;
            check(t.orden(cpo::SIGNATURE, {}, r) &&
                  std::string(r.begin(), r.end()) == "mcu-sim PuenteSerie vcp_2217",
                  "SIGNATURE vacia: \"" + std::string(r.begin(), r.end()) + "\"");
            t.orden_sin_respuesta(cpo::SIGNATURE, {'t', 'b'});
            check(t.espera([&] { return rfc->firma_cliente() == "tb"; }),
                  "con texto es la del terminal: se guarda...");
            t.calma();
            check(t.sucesos_pendientes() == 0u, "...y no se contesta");
        }

        grupo("P20 Los 255 valores de byte, por RFC 2217 en BINARY");
        {
            std::string todos;
            for (unsigned v = 0; v < 256; ++v) if (v != 0x13u) todos += char(v);
            todos += "\r";
            todos += '\0';                          // un CR NUL de verdad
            check(teclea(t, canal, todos), "los 257 bytes estan en el puente, "
                  "con el IAC ya sin doblar");
            const std::string r = eco(t, todos.size(), sc_time(80, SC_MS));
            check(r == todos, "y vuelven exactos: 0xFF se dobla y se desdobla, y "
                  "el NUL detras del CR no se come");
        }

        grupo("P21 Los baudios, desde el terminal");
        {
            V r;
            check(t.orden(cpo::SET_BAUDRATE, telnet::u32_red(9600), r) &&
                  r == telnet::u32_red(9600),
                  "SET-BAUDRATE 9600 se confirma con 9600");
            check(vcp_2217->baudios() == 9600.0, "y el puente esta a 9600");
            const uint32_t err0 = buzon(B_FE) + buzon(B_NE);
            teclea(t, canal, "Hola");
            wait(15, SC_MS);
            check(buzon(B_FE) + buzon(B_NE) > err0,
                  "la USART, a 115200, levanta errores: el alumno lo ve igual "
                  "que si se equivoca en el terminal de la placa");
            check(eco(t, 4, sc_time(5, SC_MS)).find("Hola") == std::string::npos,
                  "y el eco no dice Hola");
            check(t.orden(cpo::SET_BAUDRATE, telnet::u32_red(0), r) &&
                  r == telnet::u32_red(9600), "SET-BAUDRATE 0 es una consulta: 9600");
            check(t.orden(cpo::SET_BAUDRATE, telnet::u32_red(115200), r) &&
                  r == telnet::u32_red(115200), "vuelta a 115200");
            wait(5, SC_MS);
            t.recibido().clear();
            teclea(t, canal, "ok\r\n");
            check(eco(t, 4, sc_time(20, SC_MS)) == "ok\r\n", "y el eco vuelve a funcionar");
            check(t.orden(cpo::SET_BAUDRATE, telnet::u32_red(20000000), r) &&
                  r == telnet::u32_red(115200),
                  "20 Mbaudios no los hace la USART: se contesta con los que hay");
        }

        grupo("P22 El formato, desde el terminal");
        {
            check(t.orden1(cpo::SET_DATASIZE, 7) == 7, "SET-DATASIZE 7");
            check(t.orden1(cpo::SET_PARITY, 3) == 3, "SET-PARITY 3 (par)");
            check(t.orden1(cpo::SET_STOPSIZE, 2) == 2, "SET-STOPSIZE 2 (dos)");
            check(como_texto(vcp_2217->formato()) == "7E2",
                  "el puente queda en 7E2");
            check(t.orden1(cpo::SET_DATASIZE, 0) == 7 && t.orden1(cpo::SET_PARITY, 0) == 3 &&
                  t.orden1(cpo::SET_STOPSIZE, 0) == 2, "y las consultas lo dicen");
            // Una C (0x43, tres unos) en 7E2 lleva paridad 1 detras de los
            // siete bits: la USART, en 8N1, lee ese uno como su octavo bit.
            teclea(t, canal, "C");
            wait(3, SC_MS);
            check_eq(buzon(B_ULTIMO), 0xC3u,
                     "el firmware, en 8N1, recibe 0xC3: la paridad es su bit 7");
            check(t.orden1(cpo::SET_DATASIZE, 8) == 8 && t.orden1(cpo::SET_PARITY, 1) == 1 &&
                  t.orden1(cpo::SET_STOPSIZE, 1) == 1, "vuelta a 8N1");
            check(t.orden1(cpo::SET_PARITY, 9) == 1, "una paridad que no existe: la que hay");
            wait(5, SC_MS);
            t.recibido().clear();
            teclea(t, canal, "8N1\r\n");
            check(eco(t, 5, sc_time(20, SC_MS)) == "8N1\r\n", "y el eco, bien");
        }

        grupo("P23 Un cambio del terminal a mitad de trama espera (D-4)");
        {
            const uint32_t rx0 = buzon(B_RX), fe0 = buzon(B_FE);
            teclea(t, canal, "Z");
            check(espera_arranque_puente(), "la Z empieza a salir por PA3");
            wait(30, SC_US);
            V r;
            t.orden(cpo::SET_BAUDRATE, telnet::u32_red(57600), r);
            t.orden1(cpo::SET_PARITY, 3);
            wait(3, SC_MS);
            check(buzon(B_RX) == rx0 + 1 && buzon(B_FE) == fe0 &&
                  buzon(B_ULTIMO) == uint32_t('Z'),
                  "la Z que ya estaba en la linea llega entera");
            t.orden(cpo::SET_BAUDRATE, telnet::u32_red(115200), r);
            t.orden1(cpo::SET_PARITY, 1);
            wait(5, SC_MS);
            t.recibido().clear();
        }

        grupo("P24 Con los baudios fijos en el XML, el terminal no manda (D-14)");
        {
            vcp_2217->set_baudios_host(false);
            V r;
            check(t.orden(cpo::SET_BAUDRATE, telnet::u32_red(9600), r) &&
                  r == telnet::u32_red(115200),
                  "SET-BAUDRATE 9600 se contesta con 115200");
            check(vcp_2217->baudios() == 115200.0, "y el puente sigue a 115200");
            check(t.orden1(cpo::SET_DATASIZE, 7) == 8 && t.orden1(cpo::SET_CONTROL, 3) == 1,
                  "ni el formato ni el control de flujo");
            check(t.orden1(cpo::SET_CONTROL, 12) == 12 && !vcp_2217->rts_listo(),
                  "RTS si: es una senal, no configuracion");
            t.orden1(cpo::SET_CONTROL, 11);
            vcp_2217->set_baudios_host(true);
        }

        grupo("P25 Break desde el terminal: LBD en la USART");
        {
            const uint32_t lbd0 = buzon(B_LBD);
            check(t.orden1(cpo::SET_CONTROL, 5) == 5 && vcp_2217->break_activo(),
                  "SET-CONTROL 5 enciende el break");
            wait(1, SC_MS);
            check(pa3.voltage() < 0.5f, "PA3 a cero, sostenido");
            check(t.orden1(cpo::SET_CONTROL, 4) == 5, "la consulta dice que esta encendido");
            check(t.orden1(cpo::SET_CONTROL, 6) == 6, "SET-CONTROL 6 lo apaga");
            wait(1, SC_US);
            check(pa3.voltage() > 3.0f, "PA3 vuelve a reposo");
            check(espera([&] { return buzon(B_LBD) == lbd0 + 1; }, sc_time(5, SC_MS)),
                  "y el firmware conto un break LIN");
            t.recibido().clear();
            teclea(t, canal, "b\r\n");
            check(eco(t, 3, sc_time(20, SC_MS)) == "b\r\n", "la linea sigue viva");
        }

        grupo("P26 DTR y RTS desde el terminal, en voltios");
        {
            check(pb0.voltage() > 3.0f, "DTR inactivo: PB0 alto");
            check(t.orden1(cpo::SET_CONTROL, 8) == 8, "SET-CONTROL 8: DTR activo");
            wait(1, SC_US);
            check(pb0.voltage() < 0.5f, "PB0 baja");
            check(t.orden1(cpo::SET_CONTROL, 7) == 8, "y la consulta lo dice");
            check(t.orden1(cpo::SET_CONTROL, 9) == 9, "SET-CONTROL 9: inactivo");
            wait(1, SC_US);
            check(pb0.voltage() > 3.0f, "PB0 sube");
            check(t.orden1(cpo::SET_CONTROL, 12) == 12, "SET-CONTROL 12: RTS inactivo");
            wait(1, SC_US);
            check(pa0.voltage() > 3.0f, "el CTS del MCU (PA0) sube");
            check(t.orden1(cpo::SET_CONTROL, 10) == 12, "y la consulta lo dice");
            check(t.orden1(cpo::SET_CONTROL, 11) == 11, "SET-CONTROL 11: RTS activo");
            wait(1, SC_US);
            check(pa0.voltage() < 0.5f, "PA0 baja");
        }

        grupo("P27 El control de flujo, desde el terminal");
        {
            check(t.orden1(cpo::SET_CONTROL, 3) == 3, "SET-CONTROL 3: RTS/CTS");
            std::string rafaga;
            for (unsigned i = 0; i < 40; ++i) rafaga += char('A' + (i % 26));
            const uint32_t ore0 = buzon(B_ORE), p0 = buzon(B_PAUSAS);
            t.recibido().clear();
            teclea(t, canal, std::string(1, '\x13') + rafaga);
            check(eco(t, 40, sc_time(40, SC_MS)) == rafaga,
                  "los cuarenta vuelven, en orden");
            check(buzon(B_PAUSAS) == p0 + 1 && buzon(B_ORE) == ore0,
                  "con la pausa del firmware y sin un desbordamiento");
            check(t.orden1(cpo::SET_CONTROL, 0) == 3, "la consulta dice RTS/CTS");
            check(t.orden1(cpo::SET_CONTROL, 2) == 3,
                  "XON/XOFF no lo tiene el adaptador: se queda como estaba");
            check(t.orden1(cpo::SET_CONTROL, 13) == 14,
                  "el de entrada no lo hay: 14, ninguno");
            check(t.orden1(cpo::SET_CONTROL, 1) == 1 && !vcp_2217->config().flujo_rtscts,
                  "SET-CONTROL 1: sin control de flujo");
        }

        grupo("P28 NOTIFY-MODEMSTATE: el RTS del MCU es el CTS del terminal");
        {
            // Una pausa de 5 ms con un byte detras: RXNE se queda puesto y el
            // RTS por hardware sube. El terminal tiene que verlo caer y volver.
            t.saca_todas(cpo::NOTIFY_MODEMSTATE + cpo::RESPUESTA);
            teclea(t, canal, std::string("\x13") + "m");
            wait(12, SC_MS);
            t.espera([] { return false; }, 0.0);
            const auto m = t.saca_todas(cpo::NOTIFY_MODEMSTATE + cpo::RESPUESTA);
            bool cae = false, vuelve = false;
            for (const auto& v : m) {
                if (v == V{0xA1}) cae = true;
                if (cae && v == V{0xB1}) vuelve = true;
            }
            check(cae, "CTS cae (0xA1: DSR y DCD, y el delta de CTS)");
            check(vuelve, "y vuelve (0xB1) cuando el firmware lee");
            check(t.orden1(cpo::NOTIFY_MODEMSTATE, 0) == 0xB0,
                  "el terminal lo pregunta y le dicen 0xB0");
            check(t.orden1(cpo::SET_MODEMSTATE_MASK, 0) == 0, "mascara de modem a cero");
            const uint64_t n0 = rfc->notificaciones_modem();
            teclea(t, canal, std::string("\x13") + "m");
            wait(12, SC_MS);
            t.espera([] { return false; }, 0.0);
            check(rfc->notificaciones_modem() == n0 &&
                  t.saca_todas(cpo::NOTIFY_MODEMSTATE + cpo::RESPUESTA).empty(),
                  "y con ella a cero no se manda nada");
            check(t.orden1(cpo::SET_MODEMSTATE_MASK, 255) == 255, "mascara otra vez a 255");
            t.recibido().clear();
        }

        grupo("P29 NOTIFY-LINESTATE: los errores del MCU, si se piden");
        {
            // Una A a 115200, y con ella ya en la linea, el puente a 230400:
            // la A llega bien al MCU (D-4), pero su eco se lee al doble de
            // velocidad y la parada cae en el bit 3 de la A, que es un cero.
            auto provoca = [&] {
                V r;
                const uint64_t e0 = vcp_2217->errores_trama();
                teclea(t, canal, "A");
                espera_arranque_puente();
                wait(30, SC_US);
                t.orden(cpo::SET_BAUDRATE, telnet::u32_red(230400), r);
                wait(3, SC_MS);
                t.orden(cpo::SET_BAUDRATE, telnet::u32_red(115200), r);
                wait(3, SC_MS);
                t.espera([] { return false; }, 0.0);
                t.recibido().clear();
                return vcp_2217->errores_trama() > e0;
            };
            check(provoca(), "con los baudios cambiados, el puente ve errores de trama");
            check(rfc->notificaciones_linea() == 0u &&
                  t.saca_todas(cpo::NOTIFY_LINESTATE + cpo::RESPUESTA).empty(),
                  "con la mascara de omision (0), el terminal no se entera");
            check(t.orden1(cpo::SET_LINESTATE_MASK, 0x0C) == 0x0C,
                  "SET-LINESTATE-MASK 0x0C: trama y paridad");
            check(provoca(), "otra vez errores...");
            const auto l = t.saca_todas(cpo::NOTIFY_LINESTATE + cpo::RESPUESTA);
            bool trama = !l.empty();
            for (const auto& v : l) trama = trama && v.size() == 1 && (v[0] & 0x08) &&
                                            !(v[0] & ~0x0Cu);
            check(trama && rfc->notificaciones_linea() == l.size(),
                  "...y ahora llegan como NOTIFY-LINESTATE con el bit 3 (" +
                  std::to_string(l.size()) + ")");
            check(t.orden1(cpo::NOTIFY_LINESTATE, 0) == 0,
                  "si lo pregunta, 0: los errores son sucesos, no estado");
            t.orden1(cpo::SET_LINESTATE_MASK, 0);
        }

        grupo("P30 PURGE-DATA 2: lo que va hacia el MCU");
        {
            wait(5, SC_MS);
            const uint32_t rx0 = buzon(B_RX);
            t.datos(std::string(100, 'p'));
            t.manda_crudo(telnet::orden_2217(cpo::PURGE_DATA, {2}));
            V r;
            check(t.espera([&] { return t.saca_todas(cpo::PURGE_DATA + cpo::RESPUESTA)
                                         .size() == 1; }),
                  "PURGE-DATA 2 se confirma");
            check(canal.pendientes() == 0u, "y la cola hacia el MCU esta vacia");
            wait(10, SC_MS);
            check(buzon(B_RX) == rx0, "no le llega ni una p al firmware");
        }

        grupo("P31 FLOWCONTROL-SUSPEND y RESUME, y PURGE-DATA 1");
        {
            t.recibido().clear();
            t.orden_sin_respuesta(cpo::FLOWCONTROL_SUSPEND, {});
            check(t.espera([&] { return rfc->suspendido(); }),
                  "SUSPEND: el puente deja de mandar (y no contesta)");
            teclea(t, canal, "susp\r\n");
            wait(10, SC_MS);
            t.calma();
            check(t.recibido().empty() && rfc->retenidos() == 6u,
                  "el eco se queda en el puente: seis bytes retenidos");
            t.orden_sin_respuesta(cpo::FLOWCONTROL_RESUME, {});
            check(t.espera([&] { return t.recibido() == "susp\r\n"; }),
                  "RESUME: llegan, en orden");
            // Y ahora se tiran: SUSPEND, eco, PURGE 1, RESUME.
            t.recibido().clear();
            t.orden_sin_respuesta(cpo::FLOWCONTROL_SUSPEND, {});
            teclea(t, canal, "tira\r\n");
            wait(10, SC_MS);
            t.orden_sin_respuesta(cpo::PURGE_DATA, {1});
            check(t.espera([&] { return rfc->retenidos() == 0u; }),
                  "PURGE-DATA 1 vacia lo retenido");
            t.calma();
            const bool calla = t.saca_todas(cpo::PURGE_DATA + cpo::RESPUESTA).empty();
            check(calla, "y su confirmacion no sale mientras dure el SUSPEND: "
                  "tampoco las ordenes");
            t.orden_sin_respuesta(cpo::FLOWCONTROL_RESUME, {});
            check(t.espera([&] { return t.saca_todas(cpo::PURGE_DATA + cpo::RESPUESTA)
                                         .size() == 1; }),
                  "su confirmacion llega al reanudar");
            t.calma();
            check(t.recibido().empty(), "y el eco tirado no llega");
        }

        grupo("P32 Sin negociar COM-PORT no se atiende nada; y el NVT");
        {
            Cliente2217 crudo;
            crudo.bomba = [&] { canal.sondear(); };
            check(crudo.conecta(PUERTO_2217) &&
                  crudo.espera([&] { return rfc->sustituidos() == 1u; }),
                  "un cliente sin Telnet sustituye al terminal (D-5)");
            t.calma();
            const uint64_t ign0 = rfc->ordenes_ignoradas();
            crudo.manda_crudo(telnet::orden_2217(cpo::SET_BAUDRATE, telnet::u32_red(9600)));
            check(crudo.espera([&] { return rfc->ordenes_ignoradas() == ign0 + 1; }),
                  "un SET-BAUDRATE sin WILL COM-PORT se ignora");
            check(vcp_2217->baudios() == 115200.0 && crudo.bytes() == 0u,
                  "los baudios no se tocan y no se contesta");
            check(!rfc->negociador().com_port() && rfc->mascara_modem() == 255,
                  "la sesion nueva empieza de cero");
            crudo.manda_crudo("nvt\r\n");
            check(crudo.espera([&] { return canal.pendientes() == 5u; }), "datos en NVT");
            wait(10, SC_MS);
            crudo.espera([&] { return crudo.crudo().size() >= 6; });
            check(crudo.crudo() == std::string("nvt\r\0\n", 6),
                  "sin BINARY, el CR del eco sale como CR NUL, como manda el NVT");
        }

        t.cierra();
        vcp_2217->set_enabled(false);
        wait(1, SC_MS);
    }

    void termina() {
        std::printf("\n=====================================================\n");
        std::printf("TOTAL SERIE : %u comprobaciones OK, %u fallos\n",
                    g_ok, g_fallos);
        std::printf("=====================================================\n");
        sc_stop();
    }
};

} // namespace

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbMotor tb("tb");
    TbPieza tp("tp", tb.fin);
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    return g_fallos ? 1 : 0;
}
