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
// FASE D2 — LA PIEZA. (Llega en el commit siguiente.)
//
//   make testserie
// =============================================================================
#include <systemc>
#include <cstdio>
#include <string>
#include <vector>
#include "../common/asan_opciones.h"
#include "../common/analog_net.h"
#include "../parts/motor_uart.h"

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
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    return g_fallos ? 1 : 0;
}
