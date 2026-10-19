// =============================================================================
// gpio.h — Los puertos del MSP432E4 (T6)
//
// Quince puertos de ocho pines, A..Q sin I ni O, cada uno con 4 KB en el AHB a
// partir de 0x4005_8000 [H; hw_gpio.h]. La cabecera y DriverLib del E4 solo
// nombran esa apertura; la APB de otros Tiva no está (el análisis de §2 decía
// que había las dos; P-17 a). Los registros son los de Tiva:
//
//   DATA    0x000-0x3FC. EL DATO ESTÁ ENMASCARADO POR LA DIRECCIÓN: los bits
//           [9:2] de la dirección dicen qué pines toca la lectura o la
//           escritura. GPIOPinWrite(base, PIN_1, x) escribe en base + (0x02<<2),
//           y los demás pines no se enteran. Leer en 0x000 da 0.
//   DIR     1 = salida
//   IS, IBE, IEV, IM, RIS, MIS, ICR   interrupciones: nivel o flanco, uno o los
//           dos flancos, cuál, máscara, en bruto, enmascarada y borrar
//   AFSEL   el pin lo lleva un periférico; CUÁL lo dice PCTL, cuatro bits por
//           pin [DL: pin_map.h, GPIOPinConfigure]
//   DR2R, DR4R, DR8R, DR12R   la fuerza: 2, 4, 8 o 12 mA
//   ODR     drenador abierto
//   PUR, PDR   las resistencias internas
//   DEN     SIN ÉL EL PIN NO ES DIGITAL: no saca nada ni lee nada. Es el olvido
//           más común en un Tiva, y el modelo lo avisa una vez por pin, cuando
//           se escribe el dato de una salida sin DEN
//   AMSEL   modo analógico (ADC, comparadores)
//   LOCK, CR   los pines protegidos: AFSEL, PUR, PDR y DEN de un pin con su bit
//           de CR a 0 no se pueden cambiar; CR solo se escribe con LOCK abierto
//           (0x4C4F434B). Protegidos: PC0-PC3 (JTAG/SWD) y PD7 (NMI)
//           [1F: datasheets de Tiva; GPIOUnlockPin de DriverLib; P-17 a]
//   SI      en P y Q, cada pin tiene su interrupción; SI.SUM las junta en la
//           del pin 0
//
// LOS PINES SON LOS DEL MODELO ELÉCTRICO, como en el P4: un `Pad` con su nodo.
// Las funciones de periférico las registra el SoC con `conecta_funcion(p, i,
// pctl, f)`. PC0-PC3 salen del reset en su función 1, JTAG/SWD, con DEN: es
// por donde se cuelga la sonda SWD, como en PA13/PA14 de un STM32. Quitarles
// AFSEL (con LOCK/CR abiertos) deja la placa sin depurador: lo mismo que en
// el silicio.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_GPIO_H
#define STM32_PERIPH_MSP432E4_GPIO_H

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#  error "compila con -DSC_INCLUDE_DYNAMIC_PROCESSES: gpio.h usa sc_spawn()"
#endif
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../../common/periph_base.h"
#include "../../common/analog_net.h"
#include "../../pins/pad.h"
#include "../../pins/pin_mux.h"            // Cableado
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

// Una función de periférico vista desde el pin: la misma idea que FuncionP4
struct FuncionE4 {
    sc_core::sc_signal<bool>* out = nullptr;   // lo que saca el módulo
    sc_core::sc_signal<bool>* oe  = nullptr;   // ¿lo saca? nullptr: lo dice GPIODIR
    sc_core::sc_signal<bool>* in  = nullptr;   // la entrada del módulo (la escribe el GPIO)
    bool idle_in   = true;                      // lo que ve el módulo sin pin
    bool analogica = false;
    const char* nombre = "";
};

class GpioE4 : public BusSlave {
public:
    static constexpr unsigned NP = e4::N_PUERTOS, NB = e4::N_PINES;
    // Una línea por puerto (la del pin 0 en P y Q) y siete más para P1-P7 y
    // otras siete para Q1-Q7
    static constexpr unsigned N_IRQ = NP + 14;
    static constexpr uint32_t LLAVE = 0x4C4F434Bu;
    static unsigned linea(unsigned p, unsigned i) {
        if ((p == e4::PP || p == e4::PQ) && i > 0) return NP + (p == e4::PP ? 0u : 7u) + (i - 1);
        return p;
    }

    sc_core::sc_vector<sc_core::sc_out<bool>> irq;

    std::array<std::array<Pad*, NB>, NP>           pad{};
    std::array<std::array<analog_net_if*, NB>, NP> nodo{};
    std::array<std::array<AnalogNet*, NB>, NP>     propio{};

    GpioE4(sc_core::sc_module_name nm, const Encapsulado& enc,
           const Cableado& cab = Cableado())
        : BusSlave(nm, e4::GPIO_B, 0x1000u * NP), irq("irq", N_IRQ), enc_(enc) {
        SC_HAS_PROCESS(GpioE4);
        char b[32];
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) {
                const std::string canon = e4::canonico_e4(p, i);
                analog_net_if* n = cab.busca(p, i);
                if (!n) {
                    std::snprintf(b, sizeof b, "net_%s", canon.c_str() + 1);
                    propio[p][i] = new AnalogNet(b);
                    propio[p][i]->nombre_esq = canon;
                    n = propio[p][i];
                }
                nodo[p][i] = n;
                std::snprintf(b, sizeof b, "pad_%s", canon.c_str() + 1);
                Pad* q = new Pad(b);
                q->vdd = 3.3;
                q->ft_5v = false;            // [1F: el E4 no tolera 5 V; P-17 a]
                q->r_pull = 20e3;            // [1F: P-17 a]
                // 2, 4, 8 y 12 mA [modelo: P-17 a]
                q->r_on_speed[0] = 100.0; q->r_on_speed[1] = 50.0;
                q->r_on_speed[2] = 25.0;  q->r_on_speed[3] = 17.0;
                q->bonded = enc.bonded(p, i);
                q->net(*n);
                q->drive(s_drive_[p][i]);
                q->din(s_din_[p][i]);
                q->din_valid(s_dok_[p][i]);
                q->out_of_range(s_oor_[p][i]);
                pad[p][i] = q;
            }
        SC_METHOD(entrada_proc);
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) sensitive << s_din_[p][i];
        dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }
    ~GpioE4() override {
        for (auto& f : pad) for (Pad* q : f) delete q;
        for (auto& f : propio) for (AnalogNet* n : f) delete n;
    }

    // --- El registro de funciones (elaboración) -------------------------------
    void conecta_funcion(unsigned p, unsigned i, unsigned pctl, const FuncionE4& f) {
        fijas_[clave(p, i, pctl)] = f;
    }
    // La función analógica de un pin (AMSEL): ADC, comparador, USB...
    void conecta_analogica(unsigned p, unsigned i, const char* nombre) {
        analogicas_[p * NB + i] = nombre;
    }

    analog_net_if& nodo_de(unsigned p, unsigned i) { return *nodo[p][i]; }
    bool nivel(unsigned p, unsigned i) const { return s_din_[p][i].read(); }
    uint8_t data(unsigned p) const { return data_[p]; }
    uint8_t dir(unsigned p) const { return dir_[p]; }
    uint8_t den(unsigned p) const { return den_[p]; }
    uint8_t afsel(unsigned p) const { return afsel_[p]; }
    uint32_t pctl(unsigned p) const { return pctl_[p]; }
    uint8_t amsel(unsigned p) const { return amsel_[p]; }
    unsigned avisos_den() const { return n_den_; }
    unsigned avisos_funcion() const { return n_func_; }
    unsigned rechazadas_cr() const { return n_cr_; }
    // ¿Está el pin en esa función de periférico ahora mismo?
    bool en_funcion(unsigned p, unsigned i, unsigned pf) const {
        return ((afsel_[p] >> i) & 1u) && ((pctl_[p] >> (4 * i)) & 0xFu) == pf;
    }
    bool es_analogico(unsigned p, unsigned i) const { return (amsel_[p] >> i) & 1u; }

    void end_of_elaboration() override {
        BusSlave::end_of_elaboration();
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) lanza_salida(p, i);
        std::map<sc_core::sc_signal<bool>*, Fuentes> ent;
        for (const auto& kv : fijas_)
            if (kv.second.in) {
                Fuentes& f = ent[kv.second.in];
                f.idle = kv.second.idle_in;
                f.fijas.push_back({kv.first >> 8, (kv.first >> 4) & 0xFu, kv.first & 0xFu});
            }
        for (const auto& kv : ent) lanza_entrada(kv.first, kv.second);
    }

protected:
    const Encapsulado enc_;
    sc_core::sc_signal<PadDrive> s_drive_[NP][NB];
    sc_core::sc_signal<bool>     s_din_[NP][NB], s_dok_[NP][NB], s_oor_[NP][NB];
    uint8_t data_[NP] = {}, dir_[NP] = {}, is_[NP] = {}, ibe_[NP] = {}, iev_[NP] = {},
            im_[NP] = {}, ris_[NP] = {}, afsel_[NP] = {}, dr2_[NP] = {}, dr4_[NP] = {},
            dr8_[NP] = {}, dr12_[NP] = {}, odr_[NP] = {}, pur_[NP] = {}, pdr_[NP] = {},
            slr_[NP] = {}, den_[NP] = {}, cr_[NP] = {}, amsel_[NP] = {}, adcctl_[NP] = {},
            dmactl_[NP] = {}, si_[NP] = {}, ultimo_[NP] = {};
    uint32_t pctl_[NP] = {};
    bool abierto_[NP] = {};
    bool avisado_den_[NP][NB] = {};
    bool avisado_func_ = false;
    unsigned n_den_ = 0, n_func_ = 0, n_cr_ = 0;
    sc_core::sc_event aplica_ev_, pub_ev_;
    std::map<unsigned, FuncionE4> fijas_;
    std::map<unsigned, const char*> analogicas_;

    struct Fija { unsigned p, i, pctl; };
    struct Fuentes { std::vector<Fija> fijas; bool idle = true; };

    static unsigned clave(unsigned p, unsigned i, unsigned pctl) {
        return (p << 8) | (i << 4) | pctl;
    }

    void valores_de_reset() {
        for (unsigned p = 0; p < NP; ++p) {
            data_[p] = dir_[p] = is_[p] = ibe_[p] = iev_[p] = im_[p] = ris_[p] = 0;
            afsel_[p] = dr4_[p] = dr8_[p] = dr12_[p] = odr_[p] = pur_[p] = pdr_[p] = 0;
            slr_[p] = den_[p] = amsel_[p] = adcctl_[p] = dmactl_[p] = si_[p] = 0;
            dr2_[p] = 0xFF;
            pctl_[p] = 0;
            cr_[p] = 0xFF;
            abierto_[p] = false;
        }
        // JTAG/SWD en PC0-PC3, y los dos puertos con pines protegidos
        afsel_[e4::PC] = 0x0F; den_[e4::PC] = 0x0F; pur_[e4::PC] = 0x0F;
        pctl_[e4::PC] = 0x00001111u;
        cr_[e4::PC] = 0xF0; cr_[e4::PD] = 0x7F;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        aplica_ev_.notify(sc_core::SC_ZERO_TIME);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- La salida de cada pad ---------------------------------------------------
    void lanza_salida(unsigned p, unsigned i) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&aplica_ev_);
        std::set<sc_core::sc_signal<bool>*> sens;
        for (unsigned f = 1; f < 16; ++f) {
            const auto it = fijas_.find(clave(p, i, f));
            if (it == fijas_.end()) continue;
            if (it->second.out) sens.insert(it->second.out);
            if (it->second.oe)  sens.insert(it->second.oe);
        }
        for (sc_core::sc_signal<bool>* s : sens) o.set_sensitivity(&s->value_changed_event());
        char nm[24];
        std::snprintf(nm, sizeof nm, "sal_%u_%u", p, i);
        sc_core::sc_spawn([this, p, i] { aplica_pin(p, i); }, nm, &o);
        aplica_pin(p, i);
    }

    void aplica_pin(unsigned p, unsigned i) {
        const uint8_t m = uint8_t(1u << i);
        PadDrive d;
        const bool digital = (den_[p] & m) != 0;
        if (amsel_[p] & m) {
            d.analog = true;
        } else {
            if (!(afsel_[p] & m)) {
                d.oe  = (dir_[p] & m) != 0;
                d.out = (data_[p] & m) != 0;
            } else {
                const unsigned pf = (pctl_[p] >> (4 * i)) & 0xFu;
                const auto it = fijas_.find(clave(p, i, pf));
                if (it != fijas_.end() && !it->second.analogica) {
                    const FuncionE4& f = it->second;
                    d.oe  = f.out && (f.oe ? f.oe->read() : (dir_[p] & m) != 0);
                    d.out = f.out ? f.out->read() : false;
                } else if (it == fijas_.end() && digital) {
                    ++n_func_;
                    if (!avisado_func_) {
                        avisado_func_ = true;
                        const std::string s = e4::canonico_e4(p, i) +
                            " esta en una funcion de periferico (AFSEL, PCTL = " +
                            std::to_string(pf) + ") que no esta modelada, o que ese "
                            "pin no tiene: queda en alta impedancia";
                        SC_REPORT_WARNING("msp432e4/gpio", s.c_str());
                    }
                }
            }
            // Sin DEN el pin no es digital: ni saca ni lee
            if (!digital) d.oe = false;
            d.od = (odr_[p] & m) != 0;
            d.pupd = (pur_[p] & m) ? 1 : (pdr_[p] & m) ? 2 : 0;
            d.speed = (dr12_[p] & m) ? 3 : (dr8_[p] & m) ? 2 : (dr4_[p] & m) ? 1 : 0;
        }
        if (!(s_drive_[p][i].read() == d)) s_drive_[p][i].write(d);
    }

    // --- Las entradas de los módulos ---------------------------------------------
    void lanza_entrada(sc_core::sc_signal<bool>* sig, const Fuentes& fu) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&aplica_ev_);
        for (const Fija& f : fu.fijas) o.set_sensitivity(&s_din_[f.p][f.i].value_changed_event());
        sc_core::sc_spawn([this, sig, fu] { alimenta(sig, fu); },
                          sc_core::sc_gen_unique_name("ent"), &o);
        alimenta(sig, fu);
    }
    void alimenta(sc_core::sc_signal<bool>* sig, const Fuentes& fu) {
        for (const Fija& f : fu.fijas)
            if (en_funcion(f.p, f.i, f.pctl) && (den_[f.p] >> f.i) & 1u) {
                sig->write(s_din_[f.p][f.i].read());
                return;
            }
        sig->write(fu.idle);
    }

    void avisa_den(unsigned p, unsigned i) {
        if (avisado_den_[p][i] || !enc_.bonded(p, i)) return;
        avisado_den_[p][i] = true;
        ++n_den_;
        const std::string s = e4::canonico_e4(p, i) +
            " es salida y se le escribe el dato, pero no es digital (GPIODEN = 0): no "
            "saca nada. Pon su bit de DEN (GPIOPinTypeGPIOOutput lo hace)";
        SC_REPORT_WARNING("msp432e4/gpio", s.c_str());
    }

    uint8_t lee_pines(unsigned p) const {
        uint8_t v = 0;
        for (unsigned i = 0; i < NB; ++i)
            if (((den_[p] >> i) & 1u) && s_din_[p][i].read()) v |= uint8_t(1u << i);
        return v;
    }

    // Flancos y niveles -> RIS
    void entrada_proc() {
        bool cambio = false;
        for (unsigned p = 0; p < NP; ++p) {
            const uint8_t ahora = lee_pines(p);
            const uint8_t sube = uint8_t(ahora & ~ultimo_[p]);
            const uint8_t baja = uint8_t(~ahora & ultimo_[p]);
            ultimo_[p] = ahora;
            const uint8_t flanco = uint8_t(~is_[p]);
            const uint8_t f = uint8_t(flanco & ((ibe_[p] & (sube | baja)) |
                                                (~ibe_[p] & ((sube & iev_[p]) | (baja & ~iev_[p])))));
            if (f) { ris_[p] |= f; cambio = true; }
            if (is_[p]) cambio = true;
        }
        if (cambio) pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // Por nivel, RIS sigue al pin mientras esté en el nivel (y no se borra)
    uint8_t ris(unsigned p) const {
        const uint8_t niv = uint8_t(is_[p] & ~((lee_pines(p) ^ iev_[p])));
        return uint8_t((ris_[p] & ~is_[p]) | niv);
    }
    void pub_proc() {
        bool l[N_IRQ] = {};
        for (unsigned p = 0; p < NP; ++p) {
            const uint8_t mis = uint8_t(ris(p) & im_[p]);
            if (p == e4::PP || p == e4::PQ) {
                if (si_[p] & 1u) l[p] = mis != 0;
                else for (unsigned i = 0; i < NB; ++i) if (mis & (1u << i)) l[linea(p, i)] = true;
            } else {
                l[p] = mis != 0;
            }
        }
        for (unsigned k = 0; k < N_IRQ; ++k) if (irq[k].read() != l[k]) irq[k].write(l[k]);
    }

    // --- Los registros ---------------------------------------------------------
    uint32_t reg_read(uint32_t off) override {
        const unsigned p = off >> 12;
        const uint32_t r = off & 0xFFFu;
        if (p >= NP) return 0;
        if (r < 0x400) {
            const uint8_t mask = uint8_t(r >> 2);
            const uint8_t sal = uint8_t(dir_[p] & ~afsel_[p]);
            const uint8_t v = uint8_t((data_[p] & sal) | (lee_pines(p) & ~sal));
            return v & mask;
        }
        switch (r) {
            case 0x400: return dir_[p];
            case 0x404: return is_[p];
            case 0x408: return ibe_[p];
            case 0x40C: return iev_[p];
            case 0x410: return im_[p];
            case 0x414: return ris(p);
            case 0x418: return uint8_t(ris(p) & im_[p]);
            case 0x420: return afsel_[p];
            case 0x500: return dr2_[p];
            case 0x504: return dr4_[p];
            case 0x508: return dr8_[p];
            case 0x50C: return odr_[p];
            case 0x510: return pur_[p];
            case 0x514: return pdr_[p];
            case 0x518: return slr_[p];
            case 0x51C: return den_[p];
            case 0x520: return abierto_[p] ? 0u : 1u;
            case 0x524: return cr_[p];
            case 0x528: return amsel_[p];
            case 0x52C: return pctl_[p];
            case 0x530: return adcctl_[p];
            case 0x534: return dmactl_[p];
            case 0x538: return si_[p];
            case 0x53C: return dr12_[p];
            case 0xFC0: return (p == e4::PP || p == e4::PQ) ? 1u : 0u;   // EDE: una IRQ por pin
            // La identificación PrimeCell [H: GPIO_PeriphID/PCellID]
            case 0xFE0: return 0x61; case 0xFE4: return 0x00;
            case 0xFE8: return 0x18; case 0xFEC: return 0x01;
            case 0xFF0: return 0x0D; case 0xFF4: return 0xF0;
            case 0xFF8: return 0x05; case 0xFFC: return 0xB1;
            default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t ben) override {
        const unsigned p = off >> 12;
        const uint32_t r = off & 0xFFFu;
        if (p >= NP) return;
        const uint8_t b = uint8_t(v);
        bool aplica = false, pub = false;
        // Lo protegido por CR: solo cambian los bits con CR a 1
        auto protegido = [&](uint8_t& reg) {
            const uint8_t nuevo = uint8_t((reg & ~cr_[p]) | (b & cr_[p]));
            if ((reg ^ b) & ~cr_[p]) ++n_cr_;
            reg = nuevo;
            aplica = true;
        };
        if (r < 0x400) {
            if (!(ben & 1u)) return;
            const uint8_t mask = uint8_t(r >> 2);
            data_[p] = uint8_t((data_[p] & ~mask) | (b & mask));
            aplica = true;
            // Escribir el dato de un pin de salida que no es digital: el olvido
            // de DEN. Se dice aquí y no al poner DIR, porque DriverLib pone DIR
            // antes que DEN y el aviso saldría siempre
            const uint8_t mudos = uint8_t(mask & dir_[p] & ~afsel_[p] & ~den_[p]);
            for (unsigned i = 0; i < NB; ++i)
                if ((mudos >> i) & 1u) avisa_den(p, i);
        } else switch (r) {
            case 0x400: dir_[p] = b; aplica = true; break;
            case 0x404: is_[p] = b; pub = true; break;
            case 0x408: ibe_[p] = b; pub = true; break;
            case 0x40C: iev_[p] = b; pub = true; break;
            case 0x410: im_[p] = b; pub = true; break;
            case 0x41C: ris_[p] &= uint8_t(~b); pub = true; break;
            case 0x420: protegido(afsel_[p]); break;
            // Un bit de DRxR a 1 pone los de los otros tres a 0 [1F]
            case 0x500: dr2_[p] |= b; dr4_[p] &= ~b; dr8_[p] &= ~b; dr12_[p] &= ~b; aplica = true; break;
            case 0x504: dr4_[p] |= b; dr2_[p] &= ~b; dr8_[p] &= ~b; dr12_[p] &= ~b; aplica = true; break;
            case 0x508: dr8_[p] |= b; dr2_[p] &= ~b; dr4_[p] &= ~b; dr12_[p] &= ~b; aplica = true; break;
            case 0x53C: dr12_[p] |= b; dr2_[p] &= ~b; dr4_[p] &= ~b; dr8_[p] &= ~b; aplica = true; break;
            case 0x50C: odr_[p] = b; aplica = true; break;
            // PUR y PDR se excluyen: poner uno quita el otro
            case 0x510: { uint8_t t = pur_[p]; protegido(t); pdr_[p] &= ~(t & ~pur_[p]); pur_[p] = t; break; }
            case 0x514: { uint8_t t = pdr_[p]; protegido(t); pur_[p] &= ~(t & ~pdr_[p]); pdr_[p] = t; break; }
            case 0x518: slr_[p] = b; break;
            case 0x51C: protegido(den_[p]); pub = true; break;
            case 0x520: abierto_[p] = (v == LLAVE); break;
            case 0x524: if (abierto_[p]) cr_[p] = b; else ++n_cr_; break;
            case 0x528: amsel_[p] = b; aplica = true; break;
            case 0x52C: {
                uint32_t mm = 0;
                for (unsigned k = 0; k < 4; ++k) if (ben & (1u << k)) mm |= 0xFFu << (8 * k);
                pctl_[p] = (pctl_[p] & ~mm) | (v & mm);
                aplica = true;
                break;
            }
            case 0x530: adcctl_[p] = b; break;
            case 0x534: dmactl_[p] = b; break;
            case 0x538: si_[p] = uint8_t(b & 1u); pub = true; break;
            default: break;
        }
        if (aplica) aplica_ev_.notify(sc_core::SC_ZERO_TIME);
        if (pub) pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_GPIO_H
