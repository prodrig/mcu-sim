// =============================================================================
// sc_main_gui.cpp — el banco de la frontera con mcu-sim-gui (fase 1 de su plan)
//
// QUINTO EJECUTABLE, y por la razón de siempre: lo que aquí se prueba hace
// cosas EN EL TIEMPO —un proceso que muestrea cada `periodo_ns`, otro que
// aplica órdenes en su instante— y hacerlo dentro de `test407` movería su
// invariante. Este banco tiene el suyo, en `verif/invariantes.txt`.
//
// La otra mitad del criterio de la fase no está aquí sino en `test407`: que su
// línea de `verif/invariantes.txt` NO cambie —ni picosegundos ni recuento—
// teniendo una frontera construida y sin activar, que es como estará en `sim`
// cada vez que se ejecute sin `--gui`. Lo que no gasta tiempo también está
// aquí (G0) y no allí, por la decisión D-12 del puente UART: lo que no necesita
// aquel banco no mueve sus cifras.
//
// LA PLACA, mínima y sin chip: lo que aquí falle es de la frontera o de las
// tres piezas, y de nada más. Un LED con su resistencia colgado de un nodo que
// sujeta abajo una resistencia de 100 k, y un pulsador que al cerrarse lleva
// ese nodo a 3,3 V. Pulsar enciende el LED, que es lo que el enunciado quiere
// ver en pantalla, sin firmware de por medio. Y un cristal suelto, para el
// tercer tipo de pieza. Y en un nodo aparte, un pulsador normalmente cerrado,
// que es donde `pulsado` y «el contacto conduce» dejan de ser lo mismo.
//
// QUÉ SE PRUEBA, grupo a grupo:
//
//   G0  lo que no gasta tiempo: lo que declara cada pieza, que el mando hace
//       lo mismo que el método de siempre, la validación de una orden y una
//       frontera sin activar, que no acepta nada;
//   G1  el catálogo de esta placa, que es contra lo que se dan las órdenes;
//   G2  el ejemplo de `doc/protocolo.md` §5, a escala de milisegundos: cuatro
//       órdenes con deltas, enviadas ANTES de arrancar, y las instantáneas que
//       las ven pasar. Es la parte reproducible al picosegundo;
//   G3  órdenes EN MARCHA: el primer tiempo es relativo al instante en que se
//       encolan, y un delta de cero es «en el mismo instante y en este orden»;
//   G4  una orden y una muestra en el MISMO instante: la muestra la ve;
//   G5  las que llegan tarde y las que no se pueden aplicar: RES_TARDE,
//       RES_PIEZA, RES_MANDO, RES_RANGO. Ninguna se descarta en silencio;
//   G6  la suscripción: se rechaza entera si un id no existe, se apaga con
//       periodo 0, y las muestras caen en la rejilla del periodo desde t = 0;
//   G7  el atasco: con la cola llena las instantáneas se tiran, y la
//       siguiente que entra dice cuántas;
//   G8  una orden ANTERIOR que llega mientras el aplicador espera a otra.
//
//   make testgui
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../common/asan_opciones.h"
#include "../common/analog_net.h"
#include "../parts/ext_parts.h"
#include "../parts/frontera_gui.h"

using namespace sc_core;
using namespace stm32;
using namespace mcusim::proto;

namespace {

unsigned g_ok = 0, g_fallos = 0;

void grupo(const char* t) { std::printf("--- %s ---\n", t); }

bool check(bool c, const std::string& q) {
    if (c) { ++g_ok;     std::printf("  [OK  ] %s\n", q.c_str()); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
void check_eq(uint64_t got, uint64_t esp, const std::string& q) {
    if (got == esp) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); }
    else { ++g_fallos;
           std::printf("  [FALLO] %s (obtenido %llu, esperado %llu)\n", q.c_str(),
                       (unsigned long long)got, (unsigned long long)esp); }
}
void check_near(double got, double esp, double tol, const std::string& q) {
    const bool ok = std::fabs(got - esp) <= tol * std::fabs(esp);
    if (ok) { ++g_ok; std::printf("  [OK  ] %s (%.6g)\n", q.c_str(), got); }
    else { ++g_fallos;
           std::printf("  [FALLO] %s (obtenido %.6g, esperado %.6g)\n", q.c_str(), got, esp); }
}

constexpr uint64_t MS = 1000000;    // en ns, que es la unidad del protocolo
constexpr uint64_t US = 1000;

SC_MODULE(TbGui) {
    // --- La placa ----------------------------------------------------------
    AnalogNet n_led{"n_led"}, n_osc{"n_osc"};
    Rpull     pd{n_led, 0.0, 100e3};              // sujeta el nodo abajo
    Button    btn{n_led, 10.0, 3.3, false};       // pulsado lleva el nodo a 3,3 V
    Led       led{"led", n_led, true, 2.0, 330.0};
    Crystal   xtal{n_osc};
    AnalogNet n_nc{"n_nc"};
    Rpull     pd_nc{n_nc, 0.0, 100e3};
    Button    btn_nc{n_nc, 10.0, 3.3, true};      // normalmente CERRADO

    // Capacidad 4 a proposito: es lo que deja provocar el atasco de G7 sin
    // esperar a llenar una cola de 128.
    gui::FronteraGui fr{"fr", 4};
    // Y otra que no se activa nunca, que es como esta en `sim` sin `--gui`
    gui::FronteraGui fr_quieta{"fr_quieta"};

    // Los indices, que se fijan al activar
    uint16_t i_btn = 0, i_led = 0, i_xt = 0, i_pd = 0;
    uint16_t o_enc = 0, o_cor = 0, o_pul = 0, o_xt = 0;

    SC_CTOR(TbGui) { SC_THREAD(run); }

    // Lo que se hace ANTES de sc_start(): activar, suscribirse y mandar la
    // secuencia de G2. Es exactamente lo que hará el saludo de la fase 3.
    void prepara() {
        const gui::Catalogo cat(ExtPartBase::inventario());
        i_btn = uint16_t(cat.indice_de(&btn));
        i_led = uint16_t(cat.indice_de(&led));
        i_xt  = uint16_t(cat.indice_de(&xtal));
        i_pd  = uint16_t(cat.indice_de(&pd));
        o_enc = uint16_t(cat.id_obs(i_led, 0));
        o_cor = uint16_t(cat.id_obs(i_led, 1));
        o_pul = uint16_t(cat.id_obs(i_btn, 0));
        o_xt  = uint16_t(cat.id_obs(i_xt, 0));
        prev_suscripcion_ = fr.suscribe(700 * US, {o_enc});   // sin activar: no
        fr.activa(cat);
        suscripcion_ = fr.suscribe(700 * US, {o_enc, o_pul});
        // El ejemplo de doc/protocolo.md §5, en ms en vez de en s: pulsa en
        // 1,00; suelta en 1,50; pulsa en 4,00; suelta en 4,22. El periodo de
        // 0,7 ms esta elegido para que ninguna caiga en una muestra: la
        // coincidencia se prueba aparte, en G4, donde se busca a proposito.
        encolada_ = fr.encola({ {1000 * US, i_btn, 0, 1.f},
                                { 500 * US, i_btn, 0, 0.f},
                                {2500 * US, i_btn, 0, 1.f},
                                { 220 * US, i_btn, 0, 0.f} }, false);
    }

private:
    bool prev_suscripcion_ = true, suscripcion_ = false, encolada_ = false;

    static uint64_t ahora() {
        return uint64_t(sc_time_stamp().value() / sc_time(1, SC_NS).value());
    }
    void hasta(uint64_t t_ns) {
        const uint64_t a = ahora();
        if (t_ns > a) wait(sc_time(double(t_ns - a), SC_NS));
    }
    std::vector<gui::Instantanea> saca_inst() {
        std::vector<gui::Instantanea> v(fr.instantaneas.begin(), fr.instantaneas.end());
        fr.instantaneas.clear();
        return v;
    }
    std::vector<OrdenHecha> saca_hechas() {
        std::vector<OrdenHecha> v(fr.hechas.begin(), fr.hechas.end());
        fr.hechas.clear();
        return v;
    }
    // El valor de un id_obs en una instantanea, o -1 si no esta.
    static float valor_en(const gui::Instantanea& in, uint16_t id) {
        for (const Muestra& m : in.muestras) if (m.id == id) return m.valor;
        return -1.f;
    }


    // -------------------------------------------------------------------
    // G0 — Lo que no gasta tiempo. Todo son consultas a piezas ya montadas y
    // pulsaciones que se deshacen en el mismo instante: aqui, en t = 0, antes
    // de que corra nada mas, y sin tocar la frontera activa.
    // -------------------------------------------------------------------
    void g0_sin_tiempo() {
        grupo("G0 Observables y mandos, sin gastar tiempo");
        const uint64_t t0 = ahora();

        // --- Lo que declara cada una de las tres piezas --------------------
        check_eq(led.n_observables(), 2, "un Led declara dos observables");
        {
            const Observable a = led.observable(0), b = led.observable(1);
            check(std::string(a.nombre) == "encendido" && std::string(a.unidad).empty() &&
                  a.min == 0.f && a.max == 1.f && a.interesante,
                  "el primero es `encendido`, sin unidad, de 0 a 1, y es el que sugiere");
            check(std::string(b.nombre) == "corriente" && std::string(b.unidad) == "mA" &&
                  b.max == 25.f && !b.interesante,
                  "el segundo es `corriente`, en mA hasta los 25 del pad, y no lo sugiere");
        }
        check_eq(led.n_mandos(), 0, "y un Led no se puede tocar: no tiene mandos");
        check(led.valor_observable(0) == (led.on() ? 1.f : 0.f),
              "`encendido` dice lo mismo que on()");
        check(led.valor_observable(1) == float(led.current() * 1000.0),
              "y `corriente` lo mismo que current(), en mA");

        check_eq(btn.n_observables(), 1, "un Button declara un observable...");
        check(std::string(btn.observable(0).nombre) == "pulsado", "...que es `pulsado`");
        check_eq(btn.n_mandos(), 1, "y un mando...");
        {
            const Mando m = btn.mando(0);
            check(std::string(m.nombre) == "pulsar" && m.tipo == Mando::Boton &&
                  m.min == 0.f && m.max == 1.f,
                  "...que es `pulsar`, de tipo boton, de 0 a 1");
        }

        check_eq(xtal.n_observables(), 1, "un Crystal declara un observable");
        check(std::string(xtal.observable(0).nombre) == "presente",
              "`presente`, y no `frecuencia`: la frecuencia es del RCC, no del cristal");
        check(xtal.valor_observable(0) == 1.f && xtal.present(),
              "y vale 1, que es lo mismo que present()");

        check(pd.n_observables() == 0 && pd.n_mandos() == 0,
              "una pieza que no declara nada (Rpull) compila tal cual: cero y cero");

        // --- El mando hace lo mismo que el metodo de siempre ---------------
        check(!btn.pressed() && btn.valor_observable(0) == 0.f, "el pulsador esta suelto");
        btn.acciona(0, 1.f);
        check(btn.pressed() && btn.cerrado() && btn.valor_observable(0) == 1.f,
              "acciona(pulsar, 1) lo pulsa: el dedo baja y el contacto cierra");
        btn.acciona(0, 0.f);
        check(!btn.pressed() && !btn.cerrado() && btn.valor_observable(0) == 0.f,
              "acciona(pulsar, 0) lo suelta");
        btn.acciona(0, 0.7f);
        check(btn.pressed(), "lo que no es ni 0 ni 1 se decide por la mitad: 0,7 pulsa");
        btn.acciona(0, 0.f);
        check(btn_nc.cerrado() && btn_nc.valor_observable(0) == 0.f,
              "un NC suelto conduce, y `pulsado` vale 0");
        btn_nc.acciona(0, 1.f);
        check(btn_nc.valor_observable(0) == 1.f && !btn_nc.cerrado(),
              "pulsado, `pulsado` vale 1 y el contacto se ABRE: es el dedo, no el contacto");
        btn_nc.acciona(0, 0.f);
        check(btn_nc.cerrado(), "y al soltarlo vuelve a su reposo, conduciendo");

        // --- Validar una orden no la aplica --------------------------------
        {
            const gui::Catalogo cat(ExtPartBase::inventario());
            float v = 0.f;
            Orden o{0, uint16_t(cat.n_piezas()), 0, 1.f};
            check(cat.valida(o, v) == RES_PIEZA, "una orden a una pieza que no existe: RES_PIEZA");
            o.pieza = i_led;
            check(cat.valida(o, v) == RES_MANDO, "a un mando que la pieza no tiene: RES_MANDO");
            o.pieza = i_btn; o.valor = 2.f;
            check(cat.valida(o, v) == RES_RANGO && v == 1.f,
                  "un valor por encima del rango: RES_RANGO, recortado al maximo");
            o.valor = -1.f;
            check(cat.valida(o, v) == RES_RANGO && v == 0.f, "y por debajo, al minimo");
            o.valor = std::nanf("");
            check(cat.valida(o, v) == RES_RANGO && v == 0.f,
                  "un NaN no esta en ningun rango: al minimo, que es el reposo");
            o.valor = 1.f;
            check(cat.valida(o, v) == RES_OK && v == 1.f, "uno dentro del rango: RES_OK");
            check(!btn.pressed(), "y nada de eso ha tocado el pulsador");
            check(cat.id_obs(i_led, 2) == -1 && cat.id_obs(i_pd, 0) == -1 &&
                  cat.id_obs(cat.n_piezas(), 0) == -1,
                  "un observable que no existe es -1: en el LED, en una pieza muda o "
                  "en una pieza que no hay");
            check(cat.pieza_de_obs(o_cor) == i_led && cat.indice_de_obs(o_cor) == 1u,
                  "de un id_obs se vuelve a su pieza y a su indice");
            const std::string xml = cat.xml();
            check(xml.find("<observable idx=\"1\" id_obs=\"" + std::to_string(o_cor) +
                           "\" nombre=\"corriente\" unidad=\"mA\" min=\"0\" max=\"25\" "
                           "interesante=\"no\"/>") != std::string::npos,
                  "un observable sale en el XML con el formato de doc/protocolo.md §3");
            check(xml.find("<mando idx=\"0\" nombre=\"pulsar\" tipo=\"boton\" min=\"0\" "
                           "max=\"1\"/>") != std::string::npos,
                  "y un mando tambien");
            std::size_t n = 0, p = 0;
            while ((p = xml.find("<observable ", p)) != std::string::npos) { ++n; ++p; }
            check_eq(n, cat.n_observables(), "y hay tantos <observable> como id_obs");
        }

        // --- Una frontera sin activar no acepta nada -----------------------
        check(!fr_quieta.activa(), "una frontera recien construida no esta activa");
        check(!fr_quieta.suscribe(1 * MS, {}), "no acepta suscripciones...");
        check(!fr_quieta.encola({Orden{0, i_btn, 0, 1.f}}, true), "...ni ordenes...");
        check(fr_quieta.aplica(Orden{0, i_btn, 0, 1.f}).resultado == RES_PIEZA &&
              !btn.pressed(),
              "...y una orden aplicada a mano no encuentra pieza: no tiene catalogo");

        check(ahora() == t0, "y todo esto no ha gastado un picosegundo");
    }

    void run() {
        g0_sin_tiempo();

        // ---------------------------------------------------------------
        grupo("G1 El catalogo de esta placa");
        {
            const gui::Catalogo& cat = fr.catalogo();
            check(!prev_suscripcion_, "antes de activar no se acepta una suscripcion");
            check(fr.activa() && suscripcion_ && encolada_,
                  "activada, suscrita y con la secuencia encolada antes de arrancar");
            check_eq(cat.n_piezas(), 6, "seis piezas: dos resistencias, dos pulsadores, el LED y el cristal");
            check_eq(cat.n_observables(), 5,
                     "y cinco observables: dos del LED, uno por pulsador y uno del cristal");
            check(o_cor == o_enc + 1, "los dos del LED son seguidos");
            const std::string xml = cat.xml();
            check(xml.find("<pieza idx=\"" + std::to_string(i_led) +
                           "\" id=\"" + led.pieza() + "\" tipo=\"Led\">") != std::string::npos,
                  "el LED sale con su indice, su identificador y su tipo");
            check(xml.find("tipo=\"Rpull\"/>") != std::string::npos,
                  "y la resistencia, muda, sale vacia");
            check_eq(fr.ordenes_pendientes(), 4, "las cuatro ordenes estan en la cola");
            check(fr.instantaneas.empty() && fr.hechas.empty() && ahora() == 0,
                  "y en t = 0 no ha salido nada");
        }

        // ---------------------------------------------------------------
        grupo("G2 Cuatro ordenes con deltas, enviadas antes de arrancar");
        hasta(2500 * US);
        std::vector<gui::Instantanea> in = saca_inst();
        hasta(5 * MS);
        for (const gui::Instantanea& i : saca_inst()) in.push_back(i);
        {
            const std::vector<OrdenHecha> h = saca_hechas();
            check_eq(h.size(), 4, "las cuatro se han aplicado, y cada una ha dejado su eco");
            const uint64_t esp_t[4] = {1000 * US, 1500 * US, 4000 * US, 4220 * US};
            const float    esp_v[4] = {1.f, 0.f, 1.f, 0.f};
            bool bien = h.size() == 4;
            for (std::size_t k = 0; bien && k < 4; ++k)
                bien = h[k].t_sim_ns == esp_t[k] && h[k].valor == esp_v[k] &&
                       h[k].pieza == i_btn && h[k].mando == 0 && h[k].resultado == RES_OK;
            check(bien, "en 1,00 / 1,50 / 4,00 / 4,22 ms, con su valor y RES_OK: los "
                        "deltas se acumulan desde el instante absoluto de la primera");
            check(!btn.pressed() && !led.on(), "y al final el pulsador esta suelto y el LED apagado");
        }
        {
            check_eq(in.size(), 7, "siete instantaneas en 5 ms con un periodo de 0,7 ms");
            // pulsado en [1,00, 1,50) y en [4,00, 4,22): solo 1,4 y 4,2 caen dentro
            const float esp[7] = {0, 1, 0, 0, 0, 1, 0};
            bool t_bien = in.size() == 7, v_bien = t_bien, forma = t_bien;
            for (std::size_t k = 0; t_bien && k < 7; ++k) {
                t_bien = t_bien && in[k].cab.t_sim_ns == (k + 1) * 700 * US;
                v_bien = v_bien && valor_en(in[k], o_pul) == esp[k]
                                && valor_en(in[k], o_enc) == esp[k];
                forma  = forma && in[k].cab.n == 2 && in[k].muestras.size() == 2 &&
                         in[k].muestras[0].id == o_enc && in[k].muestras[1].id == o_pul &&
                         in[k].cab.perdidas == 0;
            }
            check(t_bien, "en los multiplos exactos de 0,7 ms");
            check(forma, "cada una con las dos muestras pedidas, en el orden pedido, sin perdidas");
            check(v_bien, "y `pulsado` y `encendido` valen 1 justo en 1,4 y 4,2 ms: el "
                          "boton se ve hundido y el LED encendido mientras dura la pulsacion");
        }

        // ---------------------------------------------------------------
        grupo("G3 Ordenes en marcha: relativas, y un delta de cero");
        {
            const uint64_t t0 = ahora();
            check(fr.encola({ {300 * US, i_btn, 0, 1.f}, {0, i_btn, 0, 0.f} }, true),
                  "se encola en marcha");
            hasta(t0 + 500 * US);
            const std::vector<OrdenHecha> h = saca_hechas();
            check(h.size() == 2 && h[0].t_sim_ns == t0 + 300 * US &&
                  h[1].t_sim_ns == t0 + 300 * US,
                  "la primera, 0,3 ms despues de encolarla; la segunda, en el mismo instante");
            check(h.size() == 2 && h[0].valor == 1.f && h[1].valor == 0.f && !btn.pressed(),
                  "y en el orden en que venian: pulsa y suelta, y queda suelto");
        }
        saca_inst();

        // ---------------------------------------------------------------
        grupo("G4 Una orden y una muestra en el mismo instante");
        {
            // Ahora es 5,5 ms; la siguiente muestra cae en 5,6 ms (8 x 0,7)
            const uint64_t t_m = (ahora() / (700 * US) + 1) * 700 * US;
            check_eq(t_m, 5600 * US, "la siguiente muestra es la de 5,6 ms");
            fr.encola({ {t_m - ahora(), i_btn, 0, 1.f} }, true);
            hasta(t_m + 50 * US);
            const std::vector<gui::Instantanea> v = saca_inst();
            check(v.size() == 1 && v[0].cab.t_sim_ns == t_m && valor_en(v[0], o_pul) == 1.f,
                  "la muestra de 5,6 ms ve pulsado el boton que se pulso en 5,6 ms: "
                  "las ordenes de un instante van antes que su muestra");
            check(led.on(), "y 50 us despues el LED luce, que es la consecuencia electrica");
            // La corriente, con el nodo resuelto a mano: tres ramas Thevenin
            // {3,3 V, 10}, {2,0 V, 330} y {0 V, 100 k}.
            const double g = 1 / 10.0 + 1 / 330.0 + 1 / 100e3;
            const double v_n = (3.3 / 10.0 + 2.0 / 330.0) / g;
            const double i_ma = (v_n - 2.0) / 330.0 * 1000.0;
            check_near(fr.catalogo().valor(o_cor), i_ma, 1e-3,
                       "`corriente` es la del nodo resuelto a mano, en mA");
            fr.encola({ {0, i_btn, 0, 0.f} }, true);
            wait(1, SC_US);
            const std::vector<OrdenHecha> h = saca_hechas();
            check(h.size() == 2 && h[0].t_sim_ns == t_m && h[1].t_sim_ns == t_m + 50 * US,
                  "un delta de cero en marcha se aplica en el instante en que se encola");
        }

        // ---------------------------------------------------------------
        grupo("G5 Tarde, y las que no se pueden aplicar");
        {
            const uint64_t t0 = ahora();
            // Un mensaje «de antes de arrancar» que llega con la simulacion ya
            // en marcha: su instante absoluto ya paso.
            fr.encola({ {1 * MS, i_btn, 0, 1.f}, {0, i_btn, 0, 0.f} }, false);
            fr.encola({ {0, 999,   0, 1.f},       // no hay tal pieza
                        {0, i_led, 0, 1.f},       // el LED no tiene mandos
                        {0, i_btn, 3, 1.f},       // ni el boton un cuarto mando
                        {0, i_btn, 0, 7.f},       // fuera de rango por arriba
                        {0, i_btn, 0, -3.f} }, true);
            wait(1, SC_US);
            const std::vector<OrdenHecha> h = saca_hechas();
            check_eq(h.size(), 7, "las siete dejan eco: ninguna se descarta en silencio");
            if (h.size() == 7) {
                check(h[0].resultado == RES_TARDE && h[1].resultado == RES_TARDE &&
                      h[0].t_sim_ns == t0 && h[1].t_sim_ns == t0,
                      "las de un instante ya pasado se aplican al sacarlas, con RES_TARDE");
                check(h[0].valor == 1.f && h[1].valor == 0.f,
                      "y en su orden: pulsa y suelta");
                check(h[2].resultado == RES_PIEZA, "una pieza que no existe: RES_PIEZA");
                check(h[3].resultado == RES_MANDO && h[4].resultado == RES_MANDO,
                      "un mando que no existe, en una pieza muda o en una con mandos: RES_MANDO");
                check(h[5].resultado == RES_RANGO && h[5].valor == 1.f,
                      "un 7 se recorta a 1 y se aplica: RES_RANGO");
                check(h[6].resultado == RES_RANGO && h[6].valor == 0.f,
                      "un -3, a 0");
            }
            check_eq(fr.aplicadas(), 4 + 2 + 2 + 4,
                     "aplicadas de verdad: todas menos las tres que no tenian donde");
            check(!btn.pressed(), "y el pulsador acaba suelto");
        }

        // ---------------------------------------------------------------
        grupo("G6 La suscripcion");
        {
            const std::vector<uint16_t> antes = fr.suscritos();
            check(!fr.suscribe(1 * MS, {o_pul, uint16_t(fr.catalogo().n_observables())}),
                  "un id_obs que no existe hace que se rechace entera...");
            check(fr.suscritos() == antes && fr.periodo_ns() == 700 * US,
                  "...y la anterior sigue en pie");
            check(fr.suscribe(0, {o_pul}), "con periodo 0 se acepta...");
            saca_inst();
            const uint64_t n0 = fr.tomadas();
            wait(3, SC_MS);
            check(fr.instantaneas.empty() && fr.tomadas() == n0, "...y apaga las instantaneas");

            // Ahora es t0 + ~3 ms, fuera de la rejilla de 1 ms
            const uint64_t t0 = ahora();
            check(t0 % MS != 0, "la suscripcion nueva llega fuera de la rejilla");
            check(fr.suscribe(1 * MS, {o_xt}), "se suscribe al cristal, cada 1 ms");
            const uint64_t t1 = (t0 / MS + 1) * MS;
            hasta(t1 + 1500 * US);
            xtal.set_enabled(false);                       // se desuelda
            hasta(t1 + 2500 * US);
            const std::vector<gui::Instantanea> v = saca_inst();
            check(v.size() == 3 && v[0].cab.t_sim_ns == t1 &&
                  v[1].cab.t_sim_ns == t1 + MS && v[2].cab.t_sim_ns == t1 + 2 * MS,
                  "las muestras caen en los milisegundos enteros desde t = 0, no "
                  "contados desde la suscripcion");
            check(v.size() == 3 && valor_en(v[0], o_xt) == 1.f &&
                  valor_en(v[1], o_xt) == 1.f && valor_en(v[2], o_xt) == 0.f,
                  "y `presente` pasa a 0 en cuanto se desuelda el cristal");
            xtal.set_enabled(true);
        }

        // ---------------------------------------------------------------
        grupo("G7 El atasco: se tiran y se cuentan");
        {
            saca_inst();
            const uint64_t t0 = (ahora() / MS + 1) * MS;   // primera muestra
            hasta(t0 + 6 * MS + 500 * US);                  // siete muestras, sin vaciar
            check_eq(fr.instantaneas.size(), 4, "en la cola caben cuatro");
            check_eq(fr.perdidas(), 3, "las otras tres se han tirado");
            const std::vector<gui::Instantanea> v = saca_inst();
            check(v.size() == 4 && v[3].cab.t_sim_ns == t0 + 3 * MS,
                  "se tiran las NUEVAS: las que quedan son las cuatro primeras");
            hasta(t0 + 8 * MS + 500 * US);
            const std::vector<gui::Instantanea> w = saca_inst();
            check(w.size() == 2 && w[0].cab.perdidas == 3 && w[1].cab.perdidas == 0,
                  "la primera que entra despues dice que se perdieron tres; la "
                  "siguiente, ninguna");
        }

        // ---------------------------------------------------------------
        grupo("G8 Una orden anterior llega mientras se espera otra");
        {
            fr.suscribe(0, {});
            const uint64_t t0 = ahora();
            fr.encola({ {5 * MS, i_btn, 0, 0.f} }, true);    // suelta en t0 + 5
            wait(100, SC_US);
            fr.encola({ {900 * US, i_btn, 0, 1.f} }, true);  // pulsa en t0 + 1
            hasta(t0 + 2 * MS);
            check(btn.pressed(), "la que se encolo despues, pero va antes, ya se ha aplicado");
            hasta(t0 + 6 * MS);
            const std::vector<OrdenHecha> h = saca_hechas();
            check(h.size() == 2 && h[0].t_sim_ns == t0 + 1 * MS && h[0].valor == 1.f &&
                  h[1].t_sim_ns == t0 + 5 * MS && h[1].valor == 0.f,
                  "las dos en su instante, y en el orden del tiempo, no en el de llegada");
            check(!btn.pressed() && fr.ordenes_pendientes() == 0, "y no queda ninguna");
        }

        check(fr_quieta.tomadas() == 0 && fr_quieta.instantaneas.empty() &&
              fr_quieta.hechas.empty() && fr_quieta.aplicadas() == 0,
              "y la frontera sin activar no ha tomado una muestra ni aplicado una "
              "orden en todo el banco");

        std::printf("\n=====================================================\n");
        std::printf("TOTAL GUI : %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        std::printf("=====================================================\n");
        sc_stop();
    }
};

} // namespace

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbGui tb("tb");
    tb.prepara();
    sc_start();
    // En ps y a mano, no con to_string(): este banco acaba en una cifra
    // redonda y to_string() la escribiria en us, que es una unidad que
    // ci/comprueba_invariante.sh no lee.
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() /
                                     sc_time(1, SC_PS).value()));
    return g_fallos ? 1 : 0;
}
