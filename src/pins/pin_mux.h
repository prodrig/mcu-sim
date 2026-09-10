// =============================================================================
// pin_mux.h — Multiplexor de funciones alternativas y ruta analógica
//
// Contiene los pads de los 9 puertos GPIO (A..I, 16 pines cada uno; bonded
// según el pinout LQFP100 [IR, §2.1]) y encamina:
//   * GPIO  -> pad          : configuración (MODER/OTYPER/OSPEEDR/PUPDR) y, en
//     modo GPIO de salida, el dato del ODR;
//   * AF    <-> pad         : por REGISTRO en elaboración (connect_af). Cuando
//     el pin está en modo AF y AFRL/AFRH selecciona esa función, el periférico
//     gobierna out/oe y recibe la entrada; en cualquier otro caso recibe su
//     valor de reposo [IR, §3.3.3];
//   * ruta analógica        : ADC/DAC/HSE/LSE reciben el AnalogNet del pin.
//
// Reglas de escritura (SystemC solo admite un escritor por sc_signal):
//   * cada pad tiene UN proceso de salida, que elige entre GPIO y AF;
//   * cada señal de entrada de periférico tiene UN proceso, compartido por
//     todos los pines que puedan encaminarla.
// [plan P5/P7; IR, §3, §15.4]
// =============================================================================
#ifndef STM32_PINS_PIN_MUX_H
#define STM32_PINS_PIN_MUX_H

// `sc_spawn()` (procesos dinámicos) NO forma parte de la API que `<systemc>`
// expone por omisión: hay que pedirla con esta macro ANTES de incluirlo, y por
// eso el Makefile la pasa en la línea de órdenes. La alternativa —incluir
// <sysc/kernel/sc_spawn.h> a mano— funcionaba, pero es meter mano en la
// distribución interna de cabeceras de SystemC, que es justo lo que cambia de
// una versión mayor a la siguiente. Si esto falla, falta -DSC_INCLUDE_DYNAMIC_PROCESSES.
#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#  error "compila con -DSC_INCLUDE_DYNAMIC_PROCESSES: pin_mux.h usa sc_spawn()"
#endif
#include <systemc>
#include <array>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "../common/analog_net.h"
#include "../common/nombres_nodo.h"
#include "af_types.h"
#include "pad.h"

namespace stm32 {

constexpr unsigned N_GPIO_PORTS = 9;   // A..I
constexpr unsigned N_PORT_PINS  = 16;

// ---------------------------------------------------------------------------
// EL CABLEADO: qué pads NO crean su propio nodo.
//
// Por omisión cada pad crea su `AnalogNet` y lo ata a su `sc_port`. Eso vale
// mientras el pad sea el único dueño del punto eléctrico, que es el caso de
// casi todo: un LED, un pulsador o un cristal se cuelgan del nodo del pad y ya
// está. Deja de valer en cuanto DOS pads son el mismo punto —un puente de placa
// entre dos pines, o el mismo hilo compartido por dos MCUs—, porque entonces el
// nodo no pertenece a ninguno de los dos: pertenece al circuito.
//
// Y no se puede arreglar después. `Pad::net` es un `sc_port` y un `sc_port` no
// se reata, así que la elección tiene que estar tomada CUANDO SE CONSTRUYE el
// MCU. De ahí este objeto: la placa lo rellena leyendo su fichero —leer no
// construye, que es lo que lo hace posible— y se lo pasa al constructor.
//
// Un `Cableado` vacío deja el modelo exactamente como estaba: el bucle del
// constructor no encuentra nada y cada pad crea su nodo. El coste para una
// placa sin puentes es cero.
// [doc/stm32f407vg_multi_mcu.md, §4.3 y §4.5]
// ---------------------------------------------------------------------------
class Cableado {
public:
    void une(unsigned port, unsigned pin, analog_net_if& n) {
        m_[clave(port, pin)] = &n;
    }
    analog_net_if* busca(unsigned port, unsigned pin) const {
        const auto it = m_.find(clave(port, pin));
        return it == m_.end() ? nullptr : it->second;
    }
    bool     vacio() const { return m_.empty(); }
    unsigned size()  const { return unsigned(m_.size()); }

private:
    static unsigned clave(unsigned p, unsigned i) { return p * N_PORT_PINS + i; }
    std::map<unsigned, analog_net_if*> m_;
};

// "PD12" -> ("", 3, 12);  "u0.PD12" -> ("u0", 3, 12). Falso si no es el nombre
// de un pad de puerto. Es la traducción inversa de `nombre_nodo()`, y la
// necesita quien lee la placa: el XML habla de `PD12` o de `u0.PD12`, y el
// constructor del MCU habla de (puerto, pin).
inline bool pad_desde_nombre(const std::string& s, std::string& mcu,
                             unsigned& port, unsigned& pin) {
    const size_t p = s.rfind('.');
    if (p == std::string::npos) {
        mcu.clear();
    } else {
        if (p == 0 || p + 1 >= s.size()) return false;
        mcu = s.substr(0, p);
    }
    const std::string t = (p == std::string::npos) ? s : s.substr(p + 1);
    if (t.size() < 3 || t.size() > 4 || t[0] != 'P') return false;
    if (t[1] < 'A' || t[1] >= char('A' + N_GPIO_PORTS)) return false;
    for (size_t k = 2; k < t.size(); ++k)
        if (t[k] < '0' || t[k] > '9') return false;
    if (t.size() == 4 && t[2] == '0') return false;      // "PA05" no existe
    const unsigned i = unsigned(std::atoi(t.c_str() + 2));
    if (i >= N_PORT_PINS) return false;
    port = unsigned(t[1] - 'A');
    pin  = i;
    return true;
}
// Sin prefijo: para quien ya sabe que solo hay un MCU.
inline bool pad_desde_nombre(const std::string& s, unsigned& port, unsigned& pin) {
    std::string mcu;
    return pad_desde_nombre(s, mcu, port, pin) && mcu.empty();
}

SC_MODULE(PinMux), public af_sel_if {
    // Nodos analógicos y pads de todos los pines de puerto.
    //
    // `net` son los que este mux POSEE y destruirá; vale nullptr en los pines
    // cuyo nodo pone la placa (véase Cableado). `nodo` es el que usa el pad, y
    // ese está siempre: es el que hay que mirar para todo lo demás.
    std::array<std::array<AnalogNet*,     N_PORT_PINS>, N_GPIO_PORTS> net{};
    std::array<std::array<analog_net_if*, N_PORT_PINS>, N_GPIO_PORTS> nodo{};
    std::array<std::array<Pad*,           N_PORT_PINS>, N_GPIO_PORTS> pad{};

    // Bundles hacia los puertos GPIO (los conecta el top).
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> gpio_drive;  // GPIO -> mux
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> pad_drive;   // mux  -> pad
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din;     // pad  -> GPIO
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din_ok;
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_oor;

    // -----------------------------------------------------------------------
    // Modo Standby: TODOS los pines de puerto pasan a alta impedancia, sin
    // pull-up ni pull-down y con el buffer de entrada desconectado, porque el
    // dominio de 1,2 V que los gobierna está apagado [IR, §14.5.2]. Se salvan
    // los que no dependen de él: NRST (que no es un pin de puerto), el pin
    // WKUP cuando está habilitado -su circuito vive en el dominio VDD- y los
    // de depuración si DBGMCU manda mantenerlos.
    // -----------------------------------------------------------------------
    sc_core::sc_in<bool> standby{"standby"};
    sc_core::sc_in<bool> wkup_en{"wkup_en"};      // PWR_CSR.EWUP
    sc_core::sc_in<bool> dbg_pins{"dbg_pins"};    // DBGMCU_CR.DBG_STANDBY

    // `cab` dice qué pines reciben su nodo de la placa en vez de crearlo. Vacío
    // —que es lo normal— deja el comportamiento de siempre.
    explicit PinMux(sc_core::sc_module_name nm_mod,
                    const Cableado& cab = Cableado())
        : sc_core::sc_module(nm_mod),
          gpio_drive("gpio_drive", N_GPIO_PORTS * N_PORT_PINS),
          pad_drive("pad_drive",   N_GPIO_PORTS * N_PORT_PINS),
          pad_din("pad_din",       N_GPIO_PORTS * N_PORT_PINS),
          pad_din_ok("pad_din_ok", N_GPIO_PORTS * N_PORT_PINS),
          pad_oor("pad_oor",       N_GPIO_PORTS * N_PORT_PINS) {
        // Hay un PinMux por MCU, así que este es el sitio donde contarlos sin
        // que nadie tenga que acordarse. Lo usa nombre_nodo() para decidir si
        // un pad se llama `PA5` o `u0.PA5`.
        ++n_mcus();
        char nm[16];
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                const unsigned k = idx(p, i);
                af_sel_[p][i] = AF_NONE;
                // El nodo: el que ponga la placa si este pin va a un punto
                // compartido, y si no uno propio. Es la única decisión que hay
                // que tomar aquí y no se puede tomar después.
                if (analog_net_if* compartido = cab.busca(p, i)) {
                    nodo[p][i] = compartido;
                } else {
                    std::snprintf(nm, sizeof nm, "net_%c%u", 'A' + p, i);
                    net[p][i]  = new AnalogNet(nm);
                    nodo[p][i] = net[p][i];
                }
                std::snprintf(nm, sizeof nm, "pad_%c%u", 'A' + p, i);
                pad[p][i] = new Pad(nm);
                pad[p][i]->drive(pad_drive[k]);
                pad[p][i]->din(pad_din[k]);
                pad[p][i]->din_valid(pad_din_ok[k]);
                pad[p][i]->out_of_range(pad_oor[k]);
                pad[p][i]->net(*nodo[p][i]);
                pad[p][i]->bonded = is_bonded_lqfp100(p, i);
                // PC13/PC14/PC15 pasan por el conmutador de potencia del dominio
                // de backup: 3 mA máximos y 2 MHz [IR, §2.1 nota 2].
                if (p == 2 && i >= 13) pad[p][i]->i_max = 3e-3;
            }
    }

    ~PinMux() override {
        // Los nodos compartidos NO son de este mux: los destruye quien los creó,
        // y tiene que hacerlo después. `delete nullptr` es válido y aquí es
        // justo lo que hace falta.
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) { delete pad[p][i]; delete net[p][i]; }
    }

    // --- API de registro (elaboración, la llama el top) ---------------------
    // Función alternativa digital: (puerto, pin, af) -> señales del periférico.
    void connect_af(unsigned port, unsigned pin, uint8_t af, const AfEndpoint& ep) {
        af_tab_[key(port, pin, af)] = ep;
        if (ep.in) in_users_[ep.in].push_back(Src{port, pin, af, ep.idle_in});
    }
    // Registra la misma AF en todos los pines de todos los puertos (EVENTOUT).
    void connect_af_all(uint8_t af, const AfEndpoint& ep) {
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) connect_af(p, i, af, ep);
    }
    // Ruta analógica (ADC/DAC/HSE/LSE): devuelve el nodo del pin, sea propio o
    // compartido con otro pad.
    analog_net_if& analog(unsigned port, unsigned pin) { return *nodo[port][pin]; }
    // ¿Este pin comparte su nodo con otro? Lo pregunta la validación, que
    // necesita saber que no es el dueño de lo que hay al otro lado.
    bool comparte_nodo(unsigned port, unsigned pin) const {
        return net[port][pin] == nullptr;
    }

    // --- Selección de AF (la publica el puerto GPIO) ------------------------
    void set_af(unsigned port, unsigned pin, uint8_t af) override {
        if (af_sel_[port][pin] == af) return;
        af_sel_[port][pin] = af;
        af_ev_[idx(port, pin)].notify(sc_core::SC_ZERO_TIME);
    }
    uint8_t af_of(unsigned port, unsigned pin) const { return af_sel_[port][pin]; }

    // Corriente total absorbida/entregada por el encapsulado a través de los
    // pines de puerto. El límite acumulado en VDD/VSS es 240 mA [IR, §2.4].
    double total_pin_current() const {
        double s = 0.0;
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i)
                if (pad[p][i]->bonded) s += std::fabs(double(pad[p][i]->current()));
        return s;
    }

    // Pines soldados en LQFP100: puertos A..E completos y PH0/PH1 [IR, §2.1].
    static bool is_bonded_lqfp100(unsigned port, unsigned pin) {
        if (port <= 4) return true;                       // A..E
        if (port == 7) return pin <= 1;                   // PH0, PH1
        return false;                                     // F, G, resto H, I
    }

    static unsigned idx(unsigned p, unsigned i) { return p * N_PORT_PINS + i; }

    // -----------------------------------------------------------------------
    // Elaboración de los procesos del mux: uno por pad (salida) y uno por
    // señal de entrada de periférico. Se crean aquí porque las AF se registran
    // durante el enlazado del top, después del constructor.
    // -----------------------------------------------------------------------
    void end_of_elaboration() override {
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) spawn_out_proc(p, i);
        for (const auto& kv : in_users_) spawn_in_proc(kv.first, kv.second);
    }

private:
    struct Src { unsigned port, pin; uint8_t af; bool idle; };

    static unsigned key(unsigned p, unsigned i, uint8_t af) {
        return (p << 12) | (i << 4) | af;
    }

    const AfEndpoint* find_af(unsigned p, unsigned i, uint8_t af) const {
        if (af == AF_NONE) return nullptr;
        auto it = af_tab_.find(key(p, i, af));
        return (it == af_tab_.end()) ? nullptr : &it->second;
    }

    // ---- Salida: GPIO o periférico, según MODER/AFRx ----------------------
    void spawn_out_proc(unsigned p, unsigned i) {
        const unsigned k = idx(p, i);
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&gpio_drive[k].value_changed_event());
        o.set_sensitivity(&af_ev_[k]);
        for (uint8_t a = 0; a < 16; ++a) {
            const AfEndpoint* ep = find_af(p, i, a);
            if (!ep) continue;
            if (ep->out) o.set_sensitivity(&ep->out->value_changed_event());
            if (ep->oe)  o.set_sensitivity(&ep->oe->value_changed_event());
        }
        o.set_sensitivity(&standby.value_changed_event());
        o.set_sensitivity(&wkup_en.value_changed_event());
        o.set_sensitivity(&dbg_pins.value_changed_event());
        sc_core::sc_spawn([this, p, i, k] { drive_pad(p, i, k); },
                          ("mux_out_" + std::to_string(k)).c_str(), &o);
        drive_pad(p, i, k);           // valor inicial coherente con el reset
    }

    // ¿Sobrevive este pin al apagado del dominio de 1,2 V? [IR, §14.5.2]
    bool pin_de_standby(unsigned p, unsigned i) const {
        if (p == 0 && i == 0 && wkup_en.read()) return true;      // PA0-WKUP
        if (!dbg_pins.read()) return false;
        if (p == 0 && (i == 13 || i == 14 || i == 15)) return true;  // SWDIO/CLK/JTDI
        if (p == 1 && (i == 3 || i == 4)) return true;               // SWO/NJTRST
        return false;
    }

    void drive_pad(unsigned p, unsigned i, unsigned k) {
        if (standby.read() && !pin_de_standby(p, i)) {
            PadDrive z;                          // oe=0, sin pull, buffer fuera
            z.analog = true;
            pad_drive[k].write(z);
            return;
        }
        PadDrive d = gpio_drive[k].read();       // configuración del puerto
        const uint8_t a = af_sel_[p][i];
        if (a != AF_NONE) {
            const AfEndpoint* ep = find_af(p, i, a);
            if (ep) {                            // el periférico manda
                d.out = ep->out ? ep->out->read() : false;
                d.oe  = ep->oe  ? ep->oe->read()  : false;
            } else {                             // AF no modelada: alta impedancia
                d.out = false;
                d.oe  = false;
            }
        }
        pad_drive[k].write(d);
    }

    // ---- Entrada: un proceso por señal de periférico ----------------------
    void spawn_in_proc(sc_core::sc_signal<bool>* sig, const std::vector<Src>& src) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        for (const Src& s : src) {
            o.set_sensitivity(&pad_din[idx(s.port, s.pin)].value_changed_event());
            o.set_sensitivity(&af_ev_[idx(s.port, s.pin)]);
        }
        sc_core::sc_spawn([this, sig, src] { feed_in(sig, src); },
                          sc_core::sc_gen_unique_name("mux_in"), &o);
        feed_in(sig, src);
    }

    void feed_in(sc_core::sc_signal<bool>* sig, const std::vector<Src>& src) {
        for (const Src& s : src)
            if (af_sel_[s.port][s.pin] == s.af) {
                sig->write(pad_din[idx(s.port, s.pin)].read());
                return;
            }
        sig->write(src.empty() ? true : src.front().idle);
    }

    std::map<unsigned, AfEndpoint> af_tab_;
    std::map<sc_core::sc_signal<bool>*, std::vector<Src>> in_users_;
    std::array<std::array<uint8_t, N_PORT_PINS>, N_GPIO_PORTS> af_sel_{};
    sc_core::sc_event af_ev_[N_GPIO_PORTS * N_PORT_PINS];
};

} // namespace stm32
#endif // STM32_PINS_PIN_MUX_H
