// =============================================================================
// dio.h — Los puertos del MSP432P4 (DIO) y el mapeo de puertos (PMAP)
//
// Once puertos de 8 bits, P1..P10 y PJ, con los registros del MSP430 agrupados
// por PAREJAS de 16 bits: PA = P1 (byte bajo) + P2 (byte alto), PB = P3 + P4,
// ..., PE = P9 + P10, y PJ solo [H: DIO_PORT_*_Type, P1..P10, PJ].
//
//   IN    lo que se lee en el pin (siempre, también en salida)
//   OUT   lo que se saca; con REN = 1 y DIR = 0 elige pull-up (1) o pull-down (0)
//   DIR   1 = salida
//   REN   resistencia interna
//   DS    alta corriente, solo en los pines que la tienen (P2.0-P2.3 en el P401R)
//   SEL0/SEL1  la función: 00 GPIO, y las otras tres, de periférico
//   SELC  escribir 1 complementa SEL0 y SEL1 a la vez (para cambiar de 01 a 10
//         sin pasar por 00 ni por 11)
//   IES, IE, IFG  interrupciones por flanco, solo en P1..P6
//   IV    el vector: 2·(n+1) del pin pendiente de número más bajo; LEERLO
//         baja ese flag, que es como se atiende un puerto en el MSP430
//
// LOS PINES SON LOS DEL MODELO ELÉCTRICO: cada uno es un `Pad`, el mismo de los
// STM32, con su nodo analógico. Un LED, un pulsador o un cristal de la placa
// se cuelgan de ellos exactamente igual. [pins/pad.h]
//
// LAS FUNCIONES DE PERIFÉRICO (T3). Cada pin tiene hasta tres, una por valor
// de SEL1:SEL0 (01 primaria, 10 secundaria, 11 terciaria), y el SoC las
// registra en la elaboración con `conecta_funcion()`. Lo que el pin hace con
// una función depende de cómo es esa función [1F: tablas de pines del
// datasheet, SLAS826 §6.12; P-16 p]:
//
//   el MÓDULO decide    la eUSCI gobierna la dirección de sus pines: PxDIR no
//                       cuenta (FuncionP4::oe apunta a una señal del módulo)
//   PxDIR decide        Timer_A, relojes...: con DIR = 1 el pin saca la salida
//                       del módulo (TA0.1); con DIR = 0 es su entrada (CCI1A).
//                       FuncionP4::oe vale nullptr
//   analógica           ADC14, COMP_E, cristales: buffers digitales fuera
//
// EN P2, P3 Y P7 LA PRIMARIA LA ELIGE EL PMAP: el valor de PxMAPy dice qué
// señal va a ese pin (`conecta_mapeable()`). Los valores de reset son los de
// la tabla «Default Mapping» del datasheet: P2 lleva la UCA1 y TA0.1-TA0.4,
// P3 la UCA2 y la UCB2, y P7 SMCLK/DMAE0, C0OUT/TA0CLK, C1OUT/TA1CLK, TA0.0 y
// TA1.4-TA1.1. [SLAS826 §6.9.2; TI-Drivers, *MSP432.h, para la codificación]
//
// UNA FUNCIÓN QUE NADIE HA REGISTRADO -un periférico que no está- deja el pin
// en alta impedancia y avisa una vez, como en T1. Que escribir IES pueda
// levantar IFG, como en el MSP430, no se modela. [P-16 k]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_DIO_H
#define STM32_PERIPH_MSP432P4_DIO_H

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#  error "compila con -DSC_INCLUDE_DYNAMIC_PROCESSES: dio.h usa sc_spawn()"
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
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

// -----------------------------------------------------------------------------
// Una función de periférico vista desde el pin.
// -----------------------------------------------------------------------------
struct FuncionP4 {
    sc_core::sc_signal<bool>* out = nullptr;   // lo que saca el módulo
    sc_core::sc_signal<bool>* oe  = nullptr;   // ¿lo saca? nullptr: lo dice PxDIR
    sc_core::sc_signal<bool>* in  = nullptr;   // la entrada del módulo (la escribe el DIO)
    bool idle_in   = true;                      // lo que ve el módulo sin pin
    bool analogica = false;
    const char* nombre = "";
};

// -----------------------------------------------------------------------------
// EL PMAP: a qué pin va cada función, en P2, P3 y P7
//
// Con llave (0x2D52 en KEYID; se lee 0x96A5) y con cerrojo: tras configurarlo
// queda bloqueado, y solo se puede volver a tocar si PMAPRECFG lo permitía.
// [H: PMAP_KEYID_VAL = 0x2D52; P-16 k]
// -----------------------------------------------------------------------------
class PmapP4 : public BusSlave {
public:
    static constexpr uint16_t KEY = 0x2D52, KEY_LEIDA = 0x96A5;
    // Los valores de PxMAPy [H: PMAP_* «Pre-defined bitfield values»]
    enum : uint8_t {
        PM_NONE = 0, PM_UCA0CLK = 1, PM_UCA0RXD = 2, PM_UCA0TXD = 3, PM_UCB0CLK = 4,
        PM_UCB0SDA = 5, PM_UCB0SCL = 6, PM_UCA1STE = 7, PM_UCA1CLK = 8,
        PM_UCA1RXD = 9, PM_UCA1TXD = 10, PM_UCA2STE = 11, PM_UCA2CLK = 12,
        PM_UCA2RXD = 13, PM_UCA2TXD = 14, PM_UCB2STE = 15, PM_UCB2CLK = 16,
        PM_UCB2SDA = 17, PM_UCB2SCL = 18, PM_TA0CCR0A = 19, PM_TA0CCR1A = 20,
        PM_TA0CCR2A = 21, PM_TA0CCR3A = 22, PM_TA0CCR4A = 23, PM_TA1CCR1A = 24,
        PM_TA1CCR2A = 25, PM_TA1CCR3A = 26, PM_TA1CCR4A = 27, PM_TA0CLK = 28,
        PM_TA1CLK = 29, PM_SMCLK = 30, PM_ANALOG = 31
    };
    // ¿Este puerto (índice 0 = P1) tiene mapeo? P2, P3 y P7 en el P401R
    static bool mapeable(unsigned p) { return p == 1 || p == 2 || p == 6; }

    // Al cambiar un mapa, el DIO tiene que volver a mirar sus pines
    std::function<void()> al_cambiar;

    explicit PmapP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::PMAP_B, 0x400) {
        SC_HAS_PROCESS(PmapP4);
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }
    bool abierto() const { return abierto_; }
    // `puerto1` empieza en 1: P2 es el 2
    uint8_t mapa(unsigned puerto1, unsigned pin) const {
        return (puerto1 >= 1 && puerto1 <= 7 && pin < 8) ? map_[puerto1 - 1][pin] : 0;
    }
    unsigned rechazadas() const { return n_rech_; }

protected:
    uint8_t map_[7][8] = {};
    bool abierto_ = false, recfg_ = false, configurado_ = false;
    uint16_t key_ = 0;
    unsigned n_rech_ = 0;

    void valores_de_reset() {
        // La tabla «Default Mapping» [SLAS826 §6.9.2; segunda fuente: las
        // definiciones de pines de TI-Drivers y Energia, P-16 p]
        static const uint8_t P2[8] = { PM_UCA1STE, PM_UCA1CLK, PM_UCA1RXD, PM_UCA1TXD,
                                       PM_TA0CCR1A, PM_TA0CCR2A, PM_TA0CCR3A, PM_TA0CCR4A };
        static const uint8_t P3[8] = { PM_UCA2STE, PM_UCA2CLK, PM_UCA2RXD, PM_UCA2TXD,
                                       PM_UCB2STE, PM_UCB2CLK, PM_UCB2SDA, PM_UCB2SCL };
        static const uint8_t P7[8] = { PM_SMCLK, PM_TA0CLK, PM_TA1CLK, PM_TA0CCR0A,
                                       PM_TA1CCR4A, PM_TA1CCR3A, PM_TA1CCR2A, PM_TA1CCR1A };
        for (auto& f : map_) for (uint8_t& v : f) v = 0;
        for (unsigned i = 0; i < 8; ++i) { map_[1][i] = P2[i]; map_[2][i] = P3[i]; map_[6][i] = P7[i]; }
        abierto_ = recfg_ = configurado_ = false;
        key_ = 0;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        if (al_cambiar) al_cambiar();
    }
    uint8_t lee8(uint32_t off) const {
        if (off == 0x00) return uint8_t(KEY_LEIDA & 0xFF);
        if (off == 0x01) return uint8_t(KEY_LEIDA >> 8);
        if (off == 0x02) return uint8_t((abierto_ ? 0u : 1u) | (recfg_ ? 2u : 0u));
        if (off == 0x03) return 0;
        if (off >= 0x08 && off < 0x40) return map_[(off - 0x08) / 8][(off - 0x08) % 8];
        return 0;
    }
    void escribe8(uint32_t off, uint8_t v) {
        if (off == 0x00) { key_ = uint16_t((key_ & 0xFF00) | v); return; }
        if (off == 0x01) {
            key_ = uint16_t((key_ & 0x00FF) | (uint16_t(v) << 8));
            // Abrir solo si nunca se cerró, o si se permitió reconfigurar
            if (key_ == KEY && (!configurado_ || recfg_)) abierto_ = true;
            else if (key_ != KEY) { if (abierto_) configurado_ = true; abierto_ = false; }
            return;
        }
        if (off == 0x02) { if (abierto_) recfg_ = (v & 2u) != 0; return; }
        if (off >= 0x08 && off < 0x40) {
            if (!abierto_) { ++n_rech_; return; }
            uint8_t& m = map_[(off - 0x08) / 8][(off - 0x08) % 8];
            if (m == (v & 0x1Fu)) return;
            m = uint8_t(v & 0x1Fu);
            if (al_cambiar) al_cambiar();
        }
    }
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        if (!check_common(gp)) return;
        const uint32_t off = uint32_t(gp.get_address()) - base();
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < gp.get_data_length(); ++i) {
            if (gp.is_read()) d[i] = lee8(off + i);
            else if (byte_enabled(gp, i)) escribe8(off + i, d[i]);
        }
        t += cycles(1);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

class DioP4 : public BusSlave {
public:
    static constexpr unsigned NP = p4::N_PUERTOS, NB = p4::N_PINES;
    static constexpr unsigned N_INT = 6;              // P1..P6 interrumpen

    sc_core::sc_vector<sc_core::sc_out<bool>> irq;    // PORT1_IRQn..PORT6_IRQn

    // Los pads y sus nodos. `nodo` es el que usa el pad: el propio, o el de la
    // placa si el `Cableado` lo une a otro.
    std::array<std::array<Pad*, NB>, NP>           pad{};
    std::array<std::array<analog_net_if*, NB>, NP> nodo{};
    std::array<std::array<AnalogNet*, NB>, NP>     propio{};

    // Los pines de los cristales cambian de función: el CS tiene que saberlo.
    std::function<void(bool lf, bool hf)> al_cambiar_cristales;
    // El PMAP, para la primaria de P2, P3 y P7 (lo pone el SoC)
    const PmapP4* pmap = nullptr;

    DioP4(sc_core::sc_module_name nm, const Encapsulado& enc,
          const Cableado& cab = Cableado())
        : BusSlave(nm, p4::DIO_B, 0x400), irq("irq", N_INT), enc_(enc) {
        SC_HAS_PROCESS(DioP4);
        char b[32];
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) {
                const std::string canon = p4::canonico_p4(p, i);
                std::string sc = canon;                 // `P1.0` -> `P1_0`
                for (char& c : sc) if (c == '.') c = '_';
                analog_net_if* n = cab.busca(p, i);
                if (!n) {
                    std::snprintf(b, sizeof b, "net_%s", sc.c_str() + 1);
                    propio[p][i] = new AnalogNet(b);
                    propio[p][i]->nombre_esq = canon;
                    n = propio[p][i];
                }
                nodo[p][i] = n;
                std::snprintf(b, sizeof b, "pad_%s", sc.c_str() + 1);
                Pad* q = new Pad(b);
                q->vdd = 3.3;
                q->ft_5v = false;            // el MSP432 no tolera 5 V
                q->r_pull = 30e3;            // [1F: datasheet, RPULL; P-16 k]
                q->r_on_speed[0] = 50.0;     // E/S normal (~6 mA)  [1F]
                q->r_on_speed[1] = 12.0;     // alta corriente (~20 mA) [1F]
                q->bonded = enc.bonded(p, i);
                q->net(*n);
                q->drive(s_drive_[p][i]);
                q->din(s_din_[p][i]);
                q->din_valid(s_dok_[p][i]);
                q->out_of_range(s_oor_[p][i]);
                pad[p][i] = q;
            }
        // Los cristales: LFXIN/LFXOUT y HFXOUT/HFXIN, primarias de PJ.0-PJ.3
        for (unsigned i = 0; i < 4; ++i) {
            FuncionP4 f; f.analogica = true; f.nombre = "cristal";
            conecta_funcion(p4::PUERTO_J, i, 1, f);
        }
        SC_METHOD(cristales_proc); sensitive << aplica_ev_;
        SC_METHOD(entrada_proc);
        for (unsigned p = 0; p < N_INT; ++p)
            for (unsigned i = 0; i < NB; ++i) sensitive << s_din_[p][i];
        dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    ~DioP4() override {
        for (auto& f : pad) for (Pad* q : f) delete q;
        for (auto& f : propio) for (AnalogNet* n : f) delete n;
    }

    // --- El registro de funciones (elaboración, lo llama el SoC) ---------------
    // `func`: 1 primaria, 2 secundaria, 3 terciaria
    void conecta_funcion(unsigned p, unsigned i, unsigned func, const FuncionP4& f) {
        fijas_[clave(p, i, func)] = f;
    }
    // La señal que lleva el valor `pm` de PxMAPy
    void conecta_mapeable(unsigned pm, const FuncionP4& f) {
        if (pm < 32) mapeadas_[pm] = f;
    }
    // El PMAP ha cambiado: hay que volver a mirar los pines
    void mapa_cambiado() {
        aplica_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    analog_net_if& nodo_de(unsigned p, unsigned i) { return *nodo[p][i]; }
    bool nivel(unsigned p, unsigned i) const { return s_din_[p][i].read(); }
    uint8_t out(unsigned p) const { return out_[p]; }
    uint8_t dir(unsigned p) const { return dir_[p]; }
    unsigned avisos_funcion() const { return n_func_; }
    // La función que tiene el pin ahora: 0 GPIO, 1-3 de periférico
    unsigned funcion(unsigned p, unsigned i) const {
        const uint8_t m = uint8_t(1u << i);
        return ((sel1_[p] & m) ? 2u : 0u) | ((sel0_[p] & m) ? 1u : 0u);
    }
    // ¿Qué función de periférico hay en ese pin con esa selección? nullptr si
    // ninguna (o PM_NONE en un pin mapeable).
    const FuncionP4* funcion_de(unsigned p, unsigned i, unsigned func) const {
        if (func == 0) return nullptr;
        if (func == 1 && PmapP4::mapeable(p)) {
            const unsigned v = pmap ? pmap->mapa(p + 1, i) : 0u;
            if (v == 0 || v >= 32) return nullptr;
            const FuncionP4& f = mapeadas_[v];
            return (f.out || f.in || f.analogica) ? &f : nullptr;
        }
        const auto it = fijas_.find(clave(p, i, func));
        return it == fijas_.end() ? nullptr : &it->second;
    }

    // Se crean aquí porque las funciones se registran DESPUÉS del constructor.
    void end_of_elaboration() override {
        BusSlave::end_of_elaboration();
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) lanza_salida(p, i);
        // Las entradas de los módulos: un proceso por señal, con todos los
        // pines que pueden llevarla
        std::map<sc_core::sc_signal<bool>*, Fuentes> ent;
        for (const auto& kv : fijas_)
            if (kv.second.in) {
                Fuentes& f = ent[kv.second.in];
                f.idle = kv.second.idle_in;
                f.fijas.push_back({kv.first >> 8, (kv.first >> 4) & 0xFu, kv.first & 0xFu});
            }
        for (unsigned v = 1; v < 32; ++v)
            if (mapeadas_[v].in) {
                Fuentes& f = ent[mapeadas_[v].in];
                f.idle = mapeadas_[v].idle_in;
                f.valores.insert(v);
            }
        for (const auto& kv : ent) lanza_entrada(kv.first, kv.second);
    }

protected:
    const Encapsulado enc_;
    sc_core::sc_signal<PadDrive> s_drive_[NP][NB];
    sc_core::sc_signal<bool>     s_din_[NP][NB], s_dok_[NP][NB], s_oor_[NP][NB];
    uint8_t out_[NP] = {}, dir_[NP] = {}, ren_[NP] = {}, ds_[NP] = {},
            sel0_[NP] = {}, sel1_[NP] = {}, ies_[NP] = {}, ie_[NP] = {},
            ifg_[NP] = {}, ultimo_[NP] = {};
    bool repulso_[N_INT] = {};
    bool avisado_func_ = false;
    unsigned n_func_ = 0;
    sc_core::sc_event aplica_ev_, pub_ev_;
    std::map<unsigned, FuncionP4> fijas_;
    FuncionP4 mapeadas_[32];

    struct Fija { unsigned p, i, func; };
    struct Fuentes { std::vector<Fija> fijas; std::set<unsigned> valores; bool idle = true; };

    static unsigned clave(unsigned p, unsigned i, unsigned func) {
        return (p << 8) | (i << 4) | func;
    }
    // ¿Qué pines tienen alta corriente? P2.0-P2.3 [1F: datasheet; P-16 k]
    static bool alta_corriente(unsigned p, unsigned i) { return p == 1 && i < 4; }

    void reset_proc() {
        if (rst_n.read()) return;
        for (unsigned p = 0; p < NP; ++p) {
            out_[p] = dir_[p] = ren_[p] = ds_[p] = sel0_[p] = sel1_[p] = 0;
            ies_[p] = ie_[p] = ifg_[p] = 0;
        }
        aplica_ev_.notify(sc_core::SC_ZERO_TIME);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- La salida de cada pad: un proceso por pin -----------------------------
    void lanza_salida(unsigned p, unsigned i) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&aplica_ev_);
        std::set<sc_core::sc_signal<bool>*> sens;
        for (unsigned func = 1; func <= 3; ++func) {
            const auto it = fijas_.find(clave(p, i, func));
            if (it == fijas_.end()) continue;
            if (it->second.out) sens.insert(it->second.out);
            if (it->second.oe)  sens.insert(it->second.oe);
        }
        if (PmapP4::mapeable(p))
            for (const FuncionP4& f : mapeadas_) {
                if (f.out) sens.insert(f.out);
                if (f.oe)  sens.insert(f.oe);
            }
        for (sc_core::sc_signal<bool>* s : sens) o.set_sensitivity(&s->value_changed_event());
        char nm[24];
        std::snprintf(nm, sizeof nm, "sal_%u_%u", p, i);
        sc_core::sc_spawn([this, p, i] { aplica_pin(p, i); }, nm, &o);
        aplica_pin(p, i);
    }

    // Lo que tiene que hacer el pad, desde los registros y el módulo.
    void aplica_pin(unsigned p, unsigned i) {
        const uint8_t m = uint8_t(1u << i);
        PadDrive d;
        const unsigned func = funcion(p, i);
        const bool dir = (dir_[p] & m) != 0;
        const bool ren = (ren_[p] & m) != 0;
        const bool o   = (out_[p] & m) != 0;
        if (func == 0) {
            d.oe   = dir;
            d.out  = o;
        } else if (const FuncionP4* f = funcion_de(p, i, func)) {
            if (f->analogica) {
                d.analog = true;
            } else {
                d.oe  = f->out && (f->oe ? f->oe->read() : dir);
                d.out = f->out ? f->out->read() : false;
            }
        } else {
            // Función de periférico sin periférico: alta impedancia
            ++n_func_;
            if (!avisado_func_) {
                avisado_func_ = true;
                SC_REPORT_WARNING("msp432p4/dio",
                    "un pin esta en funcion de periferico y ese periferico no "
                    "esta modelado: queda en alta impedancia");
            }
        }
        if (!d.analog) {
            d.pupd = (!d.oe && ren) ? (o ? 1 : 2) : 0;
            d.speed = (alta_corriente(p, i) && (ds_[p] & m)) ? 1 : 0;
        }
        if (!(s_drive_[p][i].read() == d)) s_drive_[p][i].write(d);
    }

    // --- Las entradas de los módulos: un proceso por señal ------------------------
    void lanza_entrada(sc_core::sc_signal<bool>* sig, const Fuentes& fu) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&aplica_ev_);
        std::set<unsigned> pines;
        for (const Fija& f : fu.fijas) pines.insert(f.p * NB + f.i);
        if (!fu.valores.empty())
            for (unsigned p = 0; p < NP; ++p)
                if (PmapP4::mapeable(p))
                    for (unsigned i = 0; i < NB; ++i) pines.insert(p * NB + i);
        for (unsigned k : pines) o.set_sensitivity(&s_din_[k / NB][k % NB].value_changed_event());
        sc_core::sc_spawn([this, sig, fu] { alimenta(sig, fu); },
                          sc_core::sc_gen_unique_name("ent"), &o);
        alimenta(sig, fu);
    }
    void alimenta(sc_core::sc_signal<bool>* sig, const Fuentes& fu) {
        for (const Fija& f : fu.fijas)
            if (funcion(f.p, f.i) == f.func) { sig->write(s_din_[f.p][f.i].read()); return; }
        if (!fu.valores.empty() && pmap)
            for (unsigned p = 0; p < NP; ++p) {
                if (!PmapP4::mapeable(p)) continue;
                for (unsigned i = 0; i < NB; ++i)
                    if (funcion(p, i) == 1 && fu.valores.count(pmap->mapa(p + 1, i))) {
                        sig->write(s_din_[p][i].read());
                        return;
                    }
            }
        sig->write(fu.idle);
    }

    void cristales_proc() {
        if (al_cambiar_cristales)
            al_cambiar_cristales(funcion(p4::PUERTO_J, 0) == 1, funcion(p4::PUERTO_J, 3) == 1);
    }

    // Flancos en P1..P6 -> IFG
    void entrada_proc() {
        bool cambio = false;
        for (unsigned p = 0; p < N_INT; ++p) {
            uint8_t ahora = 0;
            for (unsigned i = 0; i < NB; ++i) if (s_din_[p][i].read()) ahora |= uint8_t(1u << i);
            const uint8_t sube = uint8_t(ahora & ~ultimo_[p]);
            const uint8_t baja = uint8_t(~ahora & ultimo_[p]);
            // IES = 0: flanco de subida; IES = 1: de bajada
            const uint8_t f = uint8_t((sube & ~ies_[p]) | (baja & ies_[p]));
            if (f) { ifg_[p] |= f; cambio = true; }
            ultimo_[p] = ahora;
        }
        if (cambio) pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // Las líneas al NVIC. Muestrea por flanco, así que si después de leer IV
    // queda otro flag pendiente hay que volver a subir la línea: se baja un
    // delta y se sube en el siguiente.
    void pub_proc() {
        bool otra = false;
        for (unsigned p = 0; p < N_INT; ++p) {
            const bool quiere = (ifg_[p] & ie_[p]) != 0;
            if (repulso_[p] && quiere) { irq[p].write(false); repulso_[p] = false; otra = true; }
            else { repulso_[p] = false; irq[p].write(quiere); }
        }
        if (otra) pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Los registros, byte a byte ------------------------------------------
    // (puerto, registro) desde un desplazamiento; false si no es de nadie.
    static bool decodifica(uint32_t off, unsigned& p, unsigned& r) {
        if (off < 0xA0u) {
            const unsigned pareja = off / 0x20u, byte = off & 1u;
            p = pareja * 2u + byte;
            r = (off % 0x20u) & ~1u;
            return true;
        }
        if (off >= 0x120u && off < 0x140u) {
            if (off & 1u) return false;               // PJ no tiene byte alto
            p = p4::PUERTO_J;
            r = (off - 0x120u) & ~1u;
            return true;
        }
        return false;
    }
    uint8_t iv(unsigned p) const {
        const uint8_t pend = uint8_t(ifg_[p] & ie_[p]);
        for (unsigned i = 0; i < NB; ++i) if (pend & (1u << i)) return uint8_t(2u * (i + 1u));
        return 0;
    }
    // El IV del byte bajo de la pareja es el del puerto impar en 0x0E; el del
    // par, en 0x1E. Sus bytes altos leen cero.
    uint8_t lee8(uint32_t off, bool dbg) {
        unsigned p, r;
        if ((off % 0x20u) == 0x0Eu || (off % 0x20u) == 0x1Eu ||
            (off % 0x20u) == 0x0Fu || (off % 0x20u) == 0x1Fu) {
            if (off & 1u) return 0;
            const bool alto = (off % 0x20u) == 0x1Eu;
            if (off >= 0x120u) { if (alto) return 0; p = p4::PUERTO_J; return 0; }
            p = (off / 0x20u) * 2u + (alto ? 1u : 0u);
            if (p >= N_INT) return 0;
            const uint8_t v = iv(p);
            if (v && !dbg) {                          // leerlo baja ese flag
                ifg_[p] &= uint8_t(~(1u << (v / 2u - 1u)));
                repulso_[p] = true;
                pub_ev_.notify(sc_core::SC_ZERO_TIME);
            }
            return v;
        }
        if (!decodifica(off, p, r)) return 0;
        switch (r) {
            case 0x00: {
                uint8_t v = 0;
                for (unsigned i = 0; i < NB; ++i) if (s_din_[p][i].read()) v |= uint8_t(1u << i);
                return v;
            }
            case 0x02: return out_[p];
            case 0x04: return dir_[p];
            case 0x06: return ren_[p];
            case 0x08: return ds_[p];
            case 0x0A: return sel0_[p];
            case 0x0C: return sel1_[p];
            case 0x16: return 0;                      // SELC se lee 0
            case 0x18: return p < N_INT ? ies_[p] : 0;
            case 0x1A: return p < N_INT ? ie_[p]  : 0;
            case 0x1C: return p < N_INT ? ifg_[p] : 0;
            default:   return 0;
        }
    }
    void escribe8(uint32_t off, uint8_t v) {
        unsigned p, r;
        if (!decodifica(off, p, r)) return;
        bool aplica = false, pub = false;
        switch (r) {
            case 0x02: out_[p]  = v; aplica = true; break;
            case 0x04: dir_[p]  = v; aplica = true; break;
            case 0x06: ren_[p]  = v; aplica = true; break;
            case 0x08: ds_[p]   = v; aplica = true; break;
            case 0x0A: sel0_[p] = v; aplica = true; break;
            case 0x0C: sel1_[p] = v; aplica = true; break;
            case 0x16: sel0_[p] ^= v; sel1_[p] ^= v; aplica = true; break;
            case 0x18: if (p < N_INT) ies_[p] = v; break;
            case 0x1A: if (p < N_INT) { ie_[p]  = v; pub = true; } break;
            case 0x1C: if (p < N_INT) { ifg_[p] = v; pub = true; } break;
            default: break;
        }
        if (aplica) aplica_ev_.notify(sc_core::SC_ZERO_TIME);
        if (pub)    pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        if (!check_common(gp)) return;
        const uint32_t off = uint32_t(gp.get_address()) - base();
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < gp.get_data_length(); ++i) {
            if (gp.is_read()) d[i] = lee8(off + i, false);
            else if (byte_enabled(gp, i)) escribe8(off + i, d[i]);
        }
        t += cycles(1);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) override {
        const uint32_t off = uint32_t(gp.get_address()) - base();
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < gp.get_data_length(); ++i) {
            if (gp.is_read()) d[i] = lee8(off + i, true);
            else escribe8(off + i, d[i]);
        }
        return gp.get_data_length();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_DIO_H
