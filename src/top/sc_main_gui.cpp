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
// Y desde la fase 4, EL ENLACE (`parts/enlace_gui.h`), el proceso que atiende
// la conexión con la simulación en marcha, contra un CANAL EN MEMORIA que se
// puede atascar a voluntad. Sin sockets, así que es tan determinista como lo
// demás:
//
//   G9  instantáneas y T_ESTADO por el canal, T_PING, el latido de pared, los
//       avisos de SC_REPORT —los que se ven sí, los silenciados no—, y las
//       suscripciones que no se pueden aplicar;
//   G10 la contrapresión: con el canal atascado las instantáneas se tiran y se
//       cuentan, los avisos esperan sin perderse ninguno, y cuando se
//       acumulan demasiados, T_FIN con M_ERROR y la conexión cerrada, sin que
//       la simulación se entere;
//   G11 la ventana se va: el enlace lo ve, deja de muestrear y suelta SC_REPORT;
//   G12 terminar: lo pendiente sale, luego un T_ESTADO final y T_FIN.
//
// Y desde la fase 5, LAS ÓRDENES POR EL CANAL, con el mismo canal en memoria:
//
//   G13 un T_ORDENES en marcha es relativo a la vuelta que lo lee, y sus ecos
//       salen a medida que se aplican, con el instante real; las que no se
//       pueden aplicar dejan su eco, las de fuera de rango además un T_AVISO
//       detrás; un T_ORDENES malformado no se aplica ni a medias; con el canal
//       atascado los ecos esperan sin perderse, y cuando son demasiados se
//       cierra la conexión, y las órdenes ya aceptadas se siguen aplicando.
//
// La secuencia de antes de arrancar es G2, a nivel de la frontera; por el
// socket de verdad y con `sim`, `make gui-ordenes` (`verif/gui/ordenes.py`).
//
// Y desde la fase 6, EL CONTROL:
//
//   G14 la pausa: mientras dura, el tiempo simulado no se mueve ni un delta y
//       ningún otro proceso corre —este banco tampoco—, pero el enlace sigue
//       contestando, y manda su latido; T_SIGUE la quita. A demanda: T_SIGUE
//       no vale, cada T_PASO para en su instante EXACTO aunque no caiga en la
//       rejilla del sondeo, dos seguidos se suman y uno de cero no se mueve; y
//       si la ventana se va en pausa, la pausa se quita.
//
// Lo que aquí no se puede probar es T_PARA: `sc_stop()` pararía el banco. Eso
// y todo lo demás con el `sim` de verdad, en `make gui-control`.
//
//   make testgui
// =============================================================================
#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>
#include "../common/asan_opciones.h"
#include "../common/analog_net.h"
#include "../parts/ext_parts.h"
#include "../parts/frontera_gui.h"
#include "../parts/enlace_gui.h"

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

    // --- Fase 4: el enlace, por un canal en memoria ------------------------
    // Uno por grupo, porque cada uno acaba perdiendo su conexión. El de G9 y
    // G10 con un tope de salida de 200 bytes y 5 avisos como mucho: son los
    // números que dejan provocar el atasco sin esperar segundos.
    struct CanalMemoria : gui::CanalGui {
        std::string a_gui, a_modelo;    // lo que va hacia cada lado
        long acepta = -1;               // bytes por llamada; -1 sin límite; 0 atascado
        bool cerrado_aqui = false;      // lo cerró el enlace
        bool cerrado_alli = false;      // lo cerró la ventana
        // Fase 6: en pausa, el enlace espera en el canal SIN que corra ningún
        // otro proceso, este banco incluido. Así que lo que la ventana haga
        // durante la pausa lo hace este guion, que corre en cada espera. Con
        // un tope: un guion que no saque de la pausa colgaría el banco.
        std::function<void()> guion;
        int esperas = 0;
        void espera_lectura(int) override {
            if (++esperas > 1000) { cerrado_alli = true; return; }
            if (guion) guion();
        }
        long recibe(char* b, std::size_t n) override {
            if (cerrado_alli || cerrado_aqui) return 0;
            if (a_modelo.empty()) return -1;
            const std::size_t k = std::min(n, a_modelo.size());
            std::memcpy(b, a_modelo.data(), k);
            a_modelo.erase(0, k);
            return long(k);
        }
        long envia(const char* b, std::size_t n) override {
            if (cerrado_alli || cerrado_aqui) return -1;
            const std::size_t k = acepta < 0 ? n : std::min(n, std::size_t(acepta));
            a_gui.append(b, k);
            return long(k);
        }
        void cierra() override { cerrado_aqui = true; }
    };
    gui::FronteraGui fr9{"fr9", 4}, fr11{"fr11"}, fr12{"fr12"}, fr13{"fr13"};
    gui::EnlaceGui   en9{"en9", fr9, 200, 5}, en11{"en11", fr11}, en12{"en12", fr12};
    // El de G13: el mismo tope de 200 bytes, que son cinco ecos, y veinte en
    // la cola como mucho.
    gui::EnlaceGui   en13{"en13", fr13, 200, 20};
    gui::FronteraGui fr14{"fr14"};
    gui::EnlaceGui   en14{"en14", fr14};
    gui::EnlaceGui   en_quieto{"en_quieto", fr_quieta};
    CanalMemoria     c9, c11, c12, c13, c14;
    double           pared_ = 1000.0;           // el reloj de pared, a mano

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
        check_eq(btn.n_mandos(), 3, "y tres mandos...");
        {
            const Mando m = btn.mando(0);
            check(std::string(m.nombre) == "pulsar" && m.tipo == Mando::Boton &&
                  m.min == 0.f && m.max == 1.f,
                  "...`pulsar`, de tipo boton, de 0 a 1...");
            const Mando r = btn.mando(1);
            check(std::string(r.nombre) == "rebote_ms" && r.tipo == Mando::Continuo &&
                  r.min == 0.f && r.max == 20.f,
                  "...y `rebote_ms`, continuo, de 0 a 20 ms");
            const Mando n = btn.mando(2);
            check(std::string(n.nombre) == "rebotes" && n.tipo == Mando::Discreto &&
                  n.min == 1.f && n.max == 9.f && btn.valor_mando(2) == 5.f,
                  "...y `rebotes`, discreto -un desplegable-, de 1 a 9, que vale 5");
            check(btn.valor_mando(0) == 0.f && btn.valor_mando(1) == 0.f,
                  "y cada uno dice lo que vale ahora: suelto, y sin rebote -construido "
                  "en C++ sin decirlo-");
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
        btn.acciona(1, 7.5f);
        check(btn.rebote_ms() == 7.5 && btn.valor_mando(1) == 7.5f,
              "acciona(rebote_ms, 7,5) es pon_rebote_ms(7,5), y valor_mando lo dice");
        btn.acciona(1, 0.f);
        check(btn.rebote_ms() == 0.0, "y con 0 vuelve a ser un contacto ideal");
        btn.acciona(2, 3.4f);
        check(btn.rebotes() == 3u && btn.valor_mando(2) == 3.f,
              "acciona(rebotes, 3,4) es pon_rebotes(3): un discreto se redondea");
        btn.acciona(2, 5.f);

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
                           "max=\"1\" valor=\"0\"/>") != std::string::npos,
                  "y un mando tambien, con lo que vale ahora");
            check(xml.find("<mando idx=\"1\" nombre=\"rebote_ms\" tipo=\"continuo\" "
                           "min=\"0\" max=\"20\" valor=\"0\"/>") != std::string::npos,
                  "y el del rebote, igual: la pantalla nace donde esta el modelo");
            check(xml.find("<mando idx=\"2\" nombre=\"rebotes\" tipo=\"discreto\" "
                           "min=\"1\" max=\"9\" valor=\"5\"/>") != std::string::npos,
                  "y el de cuantos rebotes, con su tipo nuevo: `discreto`");
            {
                float v = 0.f;
                Orden o{0, i_btn, 1, 50.f};
                check(cat.valida(o, v) == RES_RANGO && v == 20.f,
                      "un rebote de 50 ms por la pantalla: RES_RANGO, y se queda en 20");
            }
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


    // -------------------------------------------------------------------
    // La ventana falsa del otro lado del canal: lee con el mismo proto_io.h
    // -------------------------------------------------------------------
    struct Visto { uint16_t tipo; std::string cuerpo; };
    static std::vector<Visto> lee_canal(CanalMemoria& c, Lector& L) {
        L.mete(c.a_gui.data(), c.a_gui.size());
        c.a_gui.clear();
        std::vector<Visto> v;
        Mensaje m;
        while (L.saca(m) == Lector::LEC_MENSAJE) v.push_back({m.tipo, m.texto()});
        return v;
    }
    static void a_modelo(CanalMemoria& c, Emisor& e, uint16_t tipo, const std::string& cuerpo = "") {
        e.mensaje(c.a_modelo, tipo, cuerpo);
    }
    template <class T> static std::string bytes(const T& t) {
        return std::string(reinterpret_cast<const char*>(&t), sizeof t);
    }
    template <class T> static T pod(const std::string& c) {
        T t{};
        if (c.size() >= sizeof t) std::memcpy(&t, c.data(), sizeof t);
        return t;
    }
    static std::size_t cuenta(const std::vector<Visto>& v, uint16_t tipo) {
        std::size_t n = 0;
        for (const Visto& x : v) n += x.tipo == tipo ? 1 : 0;
        return n;
    }
    gui::EnlaceGui::Reloj reloj() { return [this] { return pared_; }; }

    void g9_enlace() {
        grupo("G9 El enlace: instantaneas, estado, latido y avisos por un canal en memoria");
        sc_report_handler::set_actions("prueba/gui", SC_WARNING, SC_DISPLAY);
        sc_report_handler::set_actions("prueba/calla", SC_WARNING, SC_DO_NOTHING);
        const gui::Catalogo cat(ExtPartBase::inventario());
        fr9.activa(cat);
        en9.activa(&c9, Emisor(), Lector(Origen::Pantalla), reloj());
        Lector L(Origen::Modelo);
        Emisor E;
        wait(150, SC_US);
        std::vector<Visto> v = lee_canal(c9, L);
        check(en9.activo() && en9.conectado() && v.empty(),
              "activo y conectado; sin suscripcion y sin que pase el reloj de pared, "
              "no sale nada");

        // --- Suscripcion por el canal, e instantaneas ---------------------
        a_modelo(c9, E, T_SUSCRIBE, gui::cuerpo_suscripcion(1 * MS, {o_pul, o_enc}));
        wait(150, SC_US);                       // la lee en la siguiente vuelta
        const uint64_t t0 = ahora();
        hasta((t0 / MS + 3) * MS + 150 * US);   // tres muestras y una vuelta
        v = lee_canal(c9, L);
        std::vector<CabInstantanea> cabs;
        bool cada_una_con_estado = true, valores = true;
        for (std::size_t k = 0; k < v.size(); ++k)
            if (v[k].tipo == T_INSTANTANEA) {
                cabs.push_back(pod<CabInstantanea>(v[k].cuerpo));
                const Muestra m0 = pod<Muestra>(v[k].cuerpo.substr(sizeof(CabInstantanea)));
                valores = valores && cabs.back().n == 2 && m0.id == o_pul && m0.valor == 0.f;
                cada_una_con_estado = cada_una_con_estado && k + 1 < v.size() &&
                                      v[k + 1].tipo == T_ESTADO;
            }
        check(cabs.size() == 3 && cabs[1].t_sim_ns - cabs[0].t_sim_ns == MS &&
              cabs[2].t_sim_ns - cabs[1].t_sim_ns == MS && cabs[0].perdidas == 0,
              "T_SUSCRIBE por el canal: tres T_INSTANTANEA, una por milisegundo");
        check(valores, "con las dos muestras pedidas y sus valores");
        check(cada_una_con_estado, "y detras de cada una, un T_ESTADO");
        {
            const Estado e = pod<Estado>(v.back().cuerpo);
            check(v.back().tipo == T_ESTADO && e.fase == F_CORRIENDO &&
                  e.t_sim_ns >= cabs.back().t_sim_ns && e.t_sim_ns <= ahora() &&
                  e.t_pared_s == 0.0 && e.deltas > 0,
                  "con la fase, el tiempo simulado, el de pared desde que se activo "
                  "(el reloj falso no se ha movido: 0 s) y los deltas");
        }

        // --- T_PING, y un tipo desconocido -------------------------------------
        a_modelo(c9, E, T_SUSCRIBE, gui::cuerpo_suscripcion(0, {}));
        a_modelo(c9, E, T_PING);
        a_modelo(c9, E, 0x8077, "un tipo que esta version no conoce");
        wait(150, SC_US);
        v = lee_canal(c9, L);
        check(v.size() == 1 && v[0].tipo == T_PONG,
              "T_PING: T_PONG en la siguiente vuelta");
        check(en9.ignorados() == 1,
              "un tipo que esta version no conoce se salta entero, y se cuenta");

        // --- El latido -------------------------------------------------------
        wait(1, SC_MS);
        const bool callado = lee_canal(c9, L).empty();
        pared_ += 0.3;                          // un tercio de segundo de pared
        wait(150, SC_US);
        v = lee_canal(c9, L);
        check(callado && v.size() == 1 && v[0].tipo == T_ESTADO &&
              std::fabs(pod<Estado>(v[0].cuerpo).t_pared_s - 0.3) < 1e-9,
              "sin instantaneas no sale nada... hasta que pasan 250 ms de pared: "
              "un T_ESTADO suelto, que dice 0,3 s");

        // --- Los avisos de SC_REPORT ---------------------------------------
        SC_REPORT_WARNING("prueba/gui", "un aviso del modelo");
        SC_REPORT_WARNING("prueba/calla", "silenciado");
        SC_REPORT_INFO("/OSCI/SystemC", "como el de sc_stop");
        const uint64_t t_av = ahora();
        wait(150, SC_US);
        v = lee_canal(c9, L);
        bool bien = v.size() == 1 && v[0].tipo == T_AVISO;
        const CabAviso ca = bien ? pod<CabAviso>(v[0].cuerpo) : CabAviso{};
        bien = bien && ca.nivel == N_AVISO && ca.t_sim_ns == t_av &&
               v[0].cuerpo.substr(sizeof ca) == "prueba/gui" + std::string("un aviso del modelo");
        check(bien, "un SC_REPORT_WARNING que se ve en la consola llega como T_AVISO: "
                    "nivel, instante, origen y texto");
        check(en9.avisos() == 1,
              "el silenciado (SC_DO_NOTHING) no, y el informativo del nucleo de SystemC "
              "tampoco");

        // --- Suscripciones que no se pueden aplicar -----------------------
        a_modelo(c9, E, T_SUSCRIBE, gui::cuerpo_suscripcion(1 * MS, {o_pul, 777}));
        a_modelo(c9, E, T_SUSCRIBE, "corto");
        wait(150, SC_US);
        v = lee_canal(c9, L);
        bool rechazo = false, malo = false;
        for (const Visto& x : v) if (x.tipo == T_AVISO) {
            rechazo = rechazo || x.cuerpo.find("777") != std::string::npos;
            malo = malo || x.cuerpo.find("no mide") != std::string::npos;
        }
        check(rechazo && fr9.suscritos().empty(),
              "una suscripcion con un observable que no existe: T_AVISO que lo nombra, "
              "y sigue la anterior (ninguna)");
        check(malo, "y un T_SUSCRIBE que no mide lo que dice, otro T_AVISO");
    }

    void g10_contrapresion() {
        grupo("G10 La contrapresion: instantaneas que se tiran, avisos que no");
        Lector L(Origen::Modelo);
        Emisor E;
        E.fija_version(1);
        // Las secuencias siguen donde las dejo G9: el lector de la ventana es
        // nuevo, asi que lo que tenga se descarta leyendo una vez.
        lee_canal(c9, L);
        L = Lector(Origen::Modelo);
        c9.acepta = 0;                          // la ventana deja de leer
        a_modelo(c9, E, T_SUSCRIBE, gui::cuerpo_suscripcion(1 * MS, {o_enc}));
        const uint64_t tomadas0 = fr9.tomadas();
        wait(20, SC_MS);
        check(en9.pendientes_salida() >= 200 && fr9.instantaneas.size() == 4 &&
              fr9.perdidas() > 0,
              "con el canal atascado, la salida llega a su tope (" +
              std::to_string(en9.pendientes_salida()) + " bytes), la cola de la "
              "frontera se llena (4) y empiezan a tirarse: " +
              std::to_string(fr9.perdidas()));
        SC_REPORT_WARNING("prueba/gui", "uno");
        SC_REPORT_WARNING("prueba/gui", "dos");
        SC_REPORT_WARNING("prueba/gui", "tres");
        wait(1, SC_MS);
        check(en9.avisos_en_cola() == 3, "tres avisos esperan: no se tira ninguno");

        c9.acepta = -1;                         // vuelve a leer
        wait(150, SC_US);
        std::vector<Visto> v = lee_canal(c9, L);
        std::vector<std::string> avs;
        std::vector<CabInstantanea> cabs;
        for (const Visto& x : v) {
            if (x.tipo == T_AVISO) avs.push_back(x.cuerpo.substr(sizeof(CabAviso) + 10));
            if (x.tipo == T_INSTANTANEA) cabs.push_back(pod<CabInstantanea>(x.cuerpo));
        }
        check(avs == std::vector<std::string>({"uno", "dos", "tres"}),
              "al volver a leer llegan los tres avisos, en su orden");
        wait(3, SC_MS);
        for (const Visto& x : lee_canal(c9, L))
            if (x.tipo == T_INSTANTANEA) cabs.push_back(pod<CabInstantanea>(x.cuerpo));
        uint64_t perdidas_dichas = 0;
        std::size_t con_perdidas = 0;
        for (const CabInstantanea& c : cabs) {
            perdidas_dichas += c.perdidas;
            con_perdidas += c.perdidas ? 1 : 0;
        }
        check(con_perdidas == 1 && perdidas_dichas == fr9.perdidas(),
              "una sola instantanea dice que faltan " + std::to_string(perdidas_dichas) +
              ", que son exactamente las que se tiraron");
        check(cabs.size() + perdidas_dichas == fr9.tomadas() - tomadas0,
              "y las que llegan mas las que faltan son todas las que se tomaron: " +
              std::to_string(cabs.size()) + " + " + std::to_string(perdidas_dichas));

        // --- Demasiados avisos sin leer --------------------------------------
        c9.acepta = 0;
        wait(5, SC_MS);                         // la salida, otra vez al tope
        for (int k = 0; k < 5; ++k) SC_REPORT_WARNING("prueba/gui", "esperando");
        const bool cinco = en9.avisos_en_cola() == 5 && en9.conectado();
        c9.acepta = -1;                         // para ver que T_FIN va delante
        const uint64_t t_fin = ahora();
        SC_REPORT_WARNING("prueba/gui", "el sexto");
        v = lee_canal(c9, L);
        const Fin f = v.empty() ? Fin{} : pod<Fin>(v[0].cuerpo);
        check(cinco && !v.empty() && v[0].tipo == T_FIN && f.motivo == M_ERROR &&
              f.t_sim_ns == t_fin,
              "con cinco esperando, el sexto aviso cierra la conexion: T_FIN con "
              "M_ERROR, el primero de lo que sale");
        check(!en9.conectado() && c9.cerrado_aqui &&
              en9.cierre().find("avisos") != std::string::npos,
              "el canal queda cerrado, y el enlace dice por que");
        const uint64_t t = ahora();
        wait(2, SC_MS);
        check(ahora() == t + 2 * MS && fr9.suscritos().empty(),
              "y la simulacion sigue, sin nadie a quien muestrear");
    }

    void g11_se_va() {
        grupo("G11 La ventana se va");
        const gui::Catalogo cat(ExtPartBase::inventario());
        fr11.activa(cat);
        en11.activa(&c11, Emisor(), Lector(Origen::Pantalla), reloj());
        Emisor E;
        a_modelo(c11, E, T_SUSCRIBE, gui::cuerpo_suscripcion(1 * MS, {o_enc}));
        wait(2, SC_MS);
        const bool daba = fr11.tomadas() > 0 && en11.instantaneas() > 0;
        c11.cerrado_alli = true;
        wait(150, SC_US);
        const uint64_t n = fr11.tomadas();
        SC_REPORT_WARNING("prueba/gui", "nadie lo lee");
        wait(3, SC_MS);
        check(daba && !en11.conectado() &&
              en11.cierre().find("cerro la conexion") != std::string::npos,
              "el enlace ve que la ventana se ha ido");
        check(fr11.tomadas() == n && fr11.suscritos().empty(),
              "deja de muestrear: no lo va a leer nadie");
        check(en11.avisos() == 0, "y los avisos vuelven a ser solo de la consola");
    }

    void g12_termina() {
        grupo("G12 Terminar: lo pendiente, un T_ESTADO final y T_FIN");
        const gui::Catalogo cat(ExtPartBase::inventario());
        fr12.activa(cat);
        en12.activa(&c12, Emisor(), Lector(Origen::Pantalla), reloj());
        Lector L(Origen::Modelo);
        Emisor E;
        a_modelo(c12, E, T_SUSCRIBE, gui::cuerpo_suscripcion(1 * MS, {o_enc}));
        wait(1500, SC_US);
        lee_canal(c12, L);
        c12.acepta = 0;
        wait(2, SC_MS);
        SC_REPORT_WARNING("prueba/gui", "el ultimo");
        c12.acepta = -1;
        const uint64_t t = ahora();
        en12.termina(M_VENTANA, 0);
        const std::vector<Visto> v = lee_canal(c12, L);
        const std::size_t n = v.size();
        bool antes = n >= 2;
        for (std::size_t k = n >= 2 ? n - 2 : 0; k < n; ++k)
            antes = antes && v[k].tipo != T_AVISO && v[k].tipo != T_INSTANTANEA;
        check(n >= 4 && cuenta(v, T_AVISO) == 1 && cuenta(v, T_INSTANTANEA) >= 2 && antes,
              "termina() vacia todo lo que esperaba -instantaneas y el aviso, en el "
              "orden en que se produjeron- antes de lo ultimo");
        check(n >= 2 && v[n - 2].tipo == T_ESTADO &&
              pod<Estado>(v[n - 2].cuerpo).fase == F_TERMINADA &&
              pod<Estado>(v[n - 2].cuerpo).t_sim_ns == t,
              "despues un T_ESTADO con la fase TERMINADA");
        check(n >= 1 && v[n - 1].tipo == T_FIN && pod<Fin>(v[n - 1].cuerpo).motivo == M_VENTANA &&
              pod<Fin>(v[n - 1].cuerpo).t_sim_ns == t && c12.cerrado_aqui && !en12.conectado(),
              "y T_FIN, con el instante, y el canal cerrado");
        check(!en_quieto.activo() && en_quieto.instantaneas() == 0 && en_quieto.estados() == 0,
              "y el enlace que nunca se activo no ha hecho nada en todo el banco");
    }

    // -------------------------------------------------------------------
    // G13 — Las ordenes por el canal (fase 5)
    // -------------------------------------------------------------------
    static std::vector<Visto> solo(const std::vector<Visto>& v, uint16_t a, uint16_t b) {
        std::vector<Visto> w;
        for (const Visto& x : v) if (x.tipo == a || x.tipo == b) w.push_back(x);
        return w;
    }
    static std::string texto_aviso(const Visto& x) { return x.cuerpo.substr(sizeof(CabAviso)); }

    void g13_ordenes() {
        grupo("G13 Las ordenes por el canal: ecos, rango, malformadas y atasco");
        const gui::Catalogo cat(ExtPartBase::inventario());
        fr13.activa(cat);
        const uint16_t i_nc = uint16_t(cat.indice_de(&btn_nc));
        en13.activa(&c13, Emisor(), Lector(Origen::Pantalla), reloj());
        // Activado en marcha: la primera vuelta es AHORA, y las demas cada
        // 100 us desde aqui. Asi se sabe en que instante lee cada mensaje.
        const uint64_t t_a = ahora();
        Lector L(Origen::Modelo);
        Emisor E;

        // --- En marcha: relativo a la vuelta que lo lee, y a medida ---------
        wait(50, SC_US);
        a_modelo(c13, E, T_ORDENES, gui::cuerpo_ordenes({ {1 * MS,   i_nc, 0, 1.f},
                                                         {500 * US, i_nc, 0, 0.f},
                                                         {0,        i_nc, 0, 1.f},
                                                         {0,        i_nc, 0, 0.f} }));
        const uint64_t t_l = t_a + 100 * US;     // la vuelta que lo lee
        hasta(t_l + 1 * MS + 50 * US);
        std::vector<Visto> v = solo(lee_canal(c13, L), T_ORDEN_HECHA, T_AVISO);
        {
            const OrdenHecha h = v.empty() ? OrdenHecha{} : pod<OrdenHecha>(v[0].cuerpo);
            check(v.size() == 1 && v[0].tipo == T_ORDEN_HECHA && h.t_sim_ns == t_l + 1 * MS &&
                  h.pieza == i_nc && h.mando == 0 && h.valor == 1.f && h.resultado == RES_OK &&
                  btn_nc.valor_observable(0) == 1.f,
                  "la primera, 1 ms despues de la vuelta que leyo el mensaje, y su eco "
                  "sale en cuanto se aplica, con el instante real: no espera a las demas");
        }
        hasta(t_l + 1500 * US + 150 * US);
        v = solo(lee_canal(c13, L), T_ORDEN_HECHA, T_AVISO);
        {
            bool bien = v.size() == 3;
            const float esp[3] = {0.f, 1.f, 0.f};
            for (std::size_t k = 0; bien && k < 3; ++k) {
                const OrdenHecha h = pod<OrdenHecha>(v[k].cuerpo);
                bien = v[k].tipo == T_ORDEN_HECHA && h.t_sim_ns == t_l + 1500 * US &&
                       h.valor == esp[k] && h.resultado == RES_OK;
            }
            check(bien, "las otras tres, 0,5 ms despues y las tres en el MISMO instante "
                        "(delta 0), en el orden del mensaje: suelta, pulsa, suelta");
        }
        check(en13.mensajes_ordenes() == 1 && en13.ordenes() == 4 && en13.ecos() == 4 &&
              !btn_nc.pressed(),
              "un mensaje, cuatro ordenes, cuatro ecos, y el pulsador acaba suelto");

        // --- Las que no se pueden aplicar, y las de fuera de rango ---------
        a_modelo(c13, E, T_ORDENES, gui::cuerpo_ordenes({ {0, 999,   0, 1.f},
                                                         {0, i_led, 0, 1.f},
                                                         {0, i_nc,  0, 7.f},
                                                         {0, i_nc,  0, -3.f} }));
        // Cuatro vueltas: el tope de 200 bytes deja salir los ecos y los
        // avisos en dos tandas, y lo que aqui se mira es el orden.
        wait(400, SC_US);
        v = solo(lee_canal(c13, L), T_ORDEN_HECHA, T_AVISO);
        {
            const uint16_t esp_t[6] = {T_ORDEN_HECHA, T_ORDEN_HECHA, T_ORDEN_HECHA,
                                       T_AVISO, T_ORDEN_HECHA, T_AVISO};
            bool forma = v.size() == 6;
            for (std::size_t k = 0; forma && k < 6; ++k) forma = v[k].tipo == esp_t[k];
            check(forma, "cuatro ecos, y detras de cada uno de los de fuera de rango, "
                         "su T_AVISO");
            if (forma) {
                const OrdenHecha a = pod<OrdenHecha>(v[0].cuerpo), b = pod<OrdenHecha>(v[1].cuerpo),
                                 c = pod<OrdenHecha>(v[2].cuerpo), d = pod<OrdenHecha>(v[4].cuerpo);
                check(a.resultado == RES_PIEZA && a.pieza == 999 && b.resultado == RES_MANDO,
                      "una pieza que no existe: RES_PIEZA; un mando que no existe: "
                      "RES_MANDO. Ninguna se calla");
                check(c.resultado == RES_RANGO && c.valor == 1.f &&
                      d.resultado == RES_RANGO && d.valor == 0.f,
                      "un 7 se recorta a 1 y un -3 a 0, con RES_RANGO");
                const std::string a1 = texto_aviso(v[3]), a2 = texto_aviso(v[5]);
                const CabAviso ca = pod<CabAviso>(v[3].cuerpo);
                check(ca.nivel == N_AVISO && ca.t_sim_ns == c.t_sim_ns &&
                      a1.find(btn_nc.pieza() + ".pulsar") != std::string::npos &&
                      a1.find("pasa del maximo") != std::string::npos &&
                      a1.find("se aplica 1 (rango 0 a 1)") != std::string::npos &&
                      a2.find("no llega al minimo") != std::string::npos,
                      "y el aviso dice la pieza y el mando, de que lado se salio, lo que "
                      "se aplico y el rango: \"" + a1.substr(11) + "\"");
            }
        }

        // --- Malformados: no se aplican ni a medias --------------------------
        {
            const uint64_t aplicadas = fr13.aplicadas();
            a_modelo(c13, E, T_ORDENES, std::string(20, 'x'));
            a_modelo(c13, E, T_ORDENES, "");
            wait(200, SC_US);
            v = solo(lee_canal(c13, L), T_ORDEN_HECHA, T_AVISO);
            check(v.size() == 2 && v[0].tipo == T_AVISO && v[1].tipo == T_AVISO &&
                  texto_aviso(v[0]).find("20 bytes") != std::string::npos &&
                  texto_aviso(v[1]).find("no se aplica ninguna") != std::string::npos &&
                  fr13.aplicadas() == aplicadas && en13.mensajes_ordenes() == 2,
                  "un T_ORDENES de 20 bytes y uno vacio: un T_AVISO cada uno, y ninguna "
                  "orden aplicada");
        }

        // --- Con el canal atascado los ecos esperan ---------------------------
        {
            c13.acepta = 0;
            std::vector<Orden> doce;
            for (int k = 0; k < 12; ++k) doce.push_back({0, i_nc, 0, float((k + 1) % 2)});
            a_modelo(c13, E, T_ORDENES, gui::cuerpo_ordenes(doce));
            wait(300, SC_US);
            SC_REPORT_WARNING("prueba/gui", "detras de las ordenes");
            wait(300, SC_US);
            check(en13.conectado() && en13.pendientes_salida() >= 200 && !fr13.hechas.empty(),
                  "con el canal atascado, la salida al tope y " +
                  std::to_string(fr13.hechas.size()) + " ecos esperando en la frontera");
            c13.acepta = -1;
            wait(500, SC_US);                   // cinco vueltas: 200 bytes por vuelta
            v = solo(lee_canal(c13, L), T_ORDEN_HECHA, T_AVISO);
            bool bien = v.size() == 13;
            for (std::size_t k = 0; bien && k < 12; ++k)
                bien = v[k].tipo == T_ORDEN_HECHA &&
                       pod<OrdenHecha>(v[k].cuerpo).valor == float((k + 1) % 2);
            check(bien && v[12].tipo == T_AVISO &&
                  texto_aviso(v[12]).find("detras de las ordenes") != std::string::npos,
                  "al volver a leer llegan los doce ecos, en su orden, y DESPUES el aviso "
                  "que se produjo despues: no se pierde ninguno");
        }

        // --- Demasiados ecos sin leer ----------------------------------------
        {
            c13.acepta = 0;
            std::vector<Orden> muchas(30, Orden{0, i_nc, 0, 0.f});
            muchas.push_back({1 * MS, i_nc, 0, 1.f});
            a_modelo(c13, E, T_ORDENES, gui::cuerpo_ordenes(muchas));
            wait(300, SC_US);
            check(!en13.conectado() && c13.cerrado_aqui &&
                  en13.cierre().find("ecos de ordenes") != std::string::npos,
                  "con veinte ecos esperando se cierra la conexion, como con los avisos, "
                  "y dice por que");
            wait(1, SC_MS);
            check(btn_nc.pressed(),
                  "y la orden ya aceptada se aplica igual en su instante: el modelo hace "
                  "lo que se le dijo, aunque ya no mire nadie");
            btn_nc.acciona(0, 0.f);
        }
    }

    // -------------------------------------------------------------------
    // G14 — El control (fase 6)
    // -------------------------------------------------------------------
    void g14_control() {
        grupo("G14 El control: la pausa, y los pasos a demanda");
        const gui::Catalogo cat(ExtPartBase::inventario());
        fr14.activa(cat);
        int reanclas = 0;
        en14.al_seguir([&] { ++reanclas; });
        en14.activa(&c14, Emisor(), Lector(Origen::Pantalla), reloj());
        const uint64_t t_a = ahora();            // las vueltas, en t_a + k x 100 us
        Lector L(Origen::Modelo);
        Emisor E;
        std::vector<uint64_t> ts, ds;
        int k = 0;

        // --- La pausa --------------------------------------------------------
        c14.guion = [&] {
            ts.push_back(ahora());
            ds.push_back(uint64_t(sc_delta_count()));
            switch (k++) {
                case 0: a_modelo(c14, E, T_PING); break;
                case 1: pared_ += 0.3; break;          // pasa el tiempo de pared
                case 2: a_modelo(c14, E, T_SIGUE); break;
                default: break;
            }
        };
        wait(50, SC_US);
        a_modelo(c14, E, T_PAUSA);
        const uint64_t t_p = t_a + 100 * US;    // la vuelta que lo lee
        wait(100, SC_US);                       // y este banco, ¿cuando despierta?
        const uint64_t t_despierta = ahora();
        std::vector<Visto> v = lee_canal(c14, L);
        check(ts.size() == 3 && ts[0] == t_p && ts[1] == t_p && ts[2] == t_p &&
              ds[0] == ds[1] && ds[1] == ds[2],
              "en pausa, en las tres esperas del enlace el tiempo simulado es el mismo "
              "y los deltas tambien: no se mueve nada");
        check(t_despierta == t_p + 50 * US && en14.pausas() == 1 && !en14.pausado(),
              "este banco, que esperaba 100 us, no corre hasta que llega T_SIGUE: el "
              "modelo entero estaba quieto");
        {
            bool bien = v.size() == 4 && v[0].tipo == T_ESTADO && v[1].tipo == T_PONG &&
                        v[2].tipo == T_ESTADO && v[3].tipo == T_ESTADO;
            if (bien) {
                const Estado a = pod<Estado>(v[0].cuerpo), b = pod<Estado>(v[2].cuerpo),
                             c = pod<Estado>(v[3].cuerpo);
                bien = a.fase == F_PAUSADA && a.t_sim_ns == t_p &&
                       b.fase == F_PAUSADA && b.t_sim_ns == t_p &&
                       std::fabs(b.t_pared_s - a.t_pared_s - 0.3) < 1e-9 &&
                       c.fase == F_CORRIENDO && c.t_sim_ns == t_p;
            }
            check(bien, "y la ventana ve: T_ESTADO PAUSADA, el T_PONG de su T_PING, el "
                        "latido de pausa a los 0,3 s de pared -mismo instante simulado-, "
                        "y al seguir, T_ESTADO CORRIENDO");
        }
        check(reanclas == 1, "al salir de la pausa se avisa a quien lleve el freno");

        // --- A demanda -------------------------------------------------------
        en14.ritmo(RIT_DEMANDA);                // a la pausa en la siguiente vuelta
        const uint64_t t_d = (ahora() - t_a) / (100 * US) * (100 * US) + t_a + 100 * US;
        ts.clear();
        k = 0;
        c14.guion = [&] {
            ts.push_back(ahora());
            switch (k++) {
                case 0: a_modelo(c14, E, T_SIGUE); break;              // no vale
                case 1: a_modelo(c14, E, T_PASO, bytes(Paso{250 * US})); break;
                case 2: a_modelo(c14, E, T_PASO, bytes(Paso{30 * US}));
                        a_modelo(c14, E, T_PASO, bytes(Paso{20 * US})); break;
                case 3: a_modelo(c14, E, T_PASO, bytes(Paso{0})); break;
                case 4: c14.cerrado_alli = true; break;              // la ventana se va
                default: break;
            }
        };
        wait(1, SC_MS);
        v = lee_canal(c14, L);
        check(ts.size() == 5 && ts[0] == t_d && ts[1] == t_d,
              "con RIT_DEMANDA, a la pausa en la siguiente vuelta; T_SIGUE no la quita");
        check(ts.size() == 5 && ts[2] == t_d + 250 * US,
              "T_PASO de 250 us: a la pausa otra vez en EXACTAMENTE t + 250 us, aunque "
              "el sondeo va de 100 en 100");
        check(ts.size() == 5 && ts[3] == t_d + 300 * US && ts[4] == t_d + 300 * US,
              "dos pasos seguidos, de 30 y 20 us, se suman: t + 300; y uno de cero no "
              "se mueve");
        {
            bool aviso = false;
            std::vector<uint64_t> pausadas;
            for (const Visto& x : v) {
                if (x.tipo == T_AVISO)
                    aviso = aviso || x.cuerpo.find("T_SIGUE con ritmo a demanda") != std::string::npos;
                if (x.tipo == T_ESTADO && pod<Estado>(x.cuerpo).fase == F_PAUSADA)
                    pausadas.push_back(pod<Estado>(x.cuerpo).t_sim_ns);
            }
            check(aviso, "T_SIGUE a demanda: un T_AVISO lo dice");
            check(std::find(pausadas.begin(), pausadas.end(), t_d + 250 * US) != pausadas.end() &&
                  std::count(pausadas.begin(), pausadas.end(), t_d + 300 * US) >= 2,
                  "cada parada dice PAUSADA con su instante, y el paso de cero tambien");
        }
        check(en14.pasos() == 4 && !en14.conectado() && !en14.pausado() &&
              en14.cierre().find("cerro la conexion") != std::string::npos,
              "la ventana se va en pausa: se quita la pausa y la simulacion sigue -este "
              "banco ha despertado-");
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

        g9_enlace();
        g10_contrapresion();
        g11_se_va();
        g12_termina();
        g13_ordenes();
        g14_control();

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
