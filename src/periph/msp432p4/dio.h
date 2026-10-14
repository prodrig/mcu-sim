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
// Lo que no hace en T1: ningún periférico está conectado todavía, así que un
// pin en función de periférico se queda en alta impedancia y avisa una vez;
// las funciones de los cristales (PJ.0-PJ.3) sí, porque las usa el CS. Que
// escribir IES pueda levantar IFG, como en el MSP430, no se modela. [P-16 k]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_DIO_H
#define STM32_PERIPH_MSP432P4_DIO_H

#include <array>
#include <functional>
#include <string>
#include <vector>
#include "../../common/periph_base.h"
#include "../../common/analog_net.h"
#include "../../pins/pad.h"
#include "../../pins/pin_mux.h"            // Cableado
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

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
        SC_METHOD(aplica_proc); sensitive << aplica_ev_;
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

    analog_net_if& nodo_de(unsigned p, unsigned i) { return *nodo[p][i]; }
    bool nivel(unsigned p, unsigned i) const { return s_din_[p][i].read(); }
    uint8_t out(unsigned p) const { return out_[p]; }
    uint8_t dir(unsigned p) const { return dir_[p]; }
    unsigned avisos_funcion() const { return n_func_; }

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

    // Lo que cada pad tiene que hacer, desde los registros.
    void aplica_proc() {
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned i = 0; i < NB; ++i) {
                const uint8_t m = uint8_t(1u << i);
                PadDrive d;
                const unsigned func = ((sel1_[p] & m) ? 2u : 0u) | ((sel0_[p] & m) ? 1u : 0u);
                if (func == 0) {
                    d.oe   = (dir_[p] & m) != 0;
                    d.out  = (out_[p] & m) != 0;
                    d.pupd = (!d.oe && (ren_[p] & m)) ? (d.out ? 1 : 2) : 0;
                    d.speed = (alta_corriente(p, i) && (ds_[p] & m)) ? 1 : 0;
                } else if (p == p4::PUERTO_J && i < 4 && func == 1) {
                    d.analog = true;          // LFXIN/LFXOUT, HFXOUT/HFXIN
                } else {
                    // Función de periférico sin periférico: alta impedancia
                    ++n_func_;
                    if (!avisado_func_) {
                        avisado_func_ = true;
                        SC_REPORT_WARNING("msp432p4/dio",
                            "un pin esta en funcion de periferico y ese periferico "
                            "todavia no esta modelado (T3): queda en alta impedancia");
                    }
                }
                if (!(s_drive_[p][i].read() == d)) s_drive_[p][i].write(d);
            }
        if (al_cambiar_cristales)
            al_cambiar_cristales(funcion(p4::PUERTO_J, 0) == 1, funcion(p4::PUERTO_J, 3) == 1);
    }
    unsigned funcion(unsigned p, unsigned i) const {
        const uint8_t m = uint8_t(1u << i);
        return ((sel1_[p] & m) ? 2u : 0u) | ((sel0_[p] & m) ? 1u : 0u);
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

// -----------------------------------------------------------------------------
// EL PMAP: a qué pin va cada función, en P2, P3 y P7
//
// Con llave (0x2D52 en KEYID; se lee 0x96A5) y con cerrojo: tras configurarlo
// queda bloqueado, y solo se puede volver a tocar si PMAPRECFG lo permitía.
// En T1 se guardan los valores -no hay periféricos a los que llevarlos- y los
// valores por omisión de cada pin no están cargados todavía: llegan con los
// periféricos, en T3. [H: PMAP_KEYID_VAL = 0x2D52; P-16 k]
// -----------------------------------------------------------------------------
class PmapP4 : public BusSlave {
public:
    static constexpr uint16_t KEY = 0x2D52, KEY_LEIDA = 0x96A5;
    explicit PmapP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::PMAP_B, 0x400) {
        SC_HAS_PROCESS(PmapP4);
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    bool abierto() const { return abierto_; }
    uint8_t mapa(unsigned puerto1, unsigned pin) const {
        return (puerto1 >= 1 && puerto1 <= 7 && pin < 8) ? map_[puerto1 - 1][pin] : 0;
    }
    unsigned rechazadas() const { return n_rech_; }

protected:
    uint8_t map_[7][8] = {};
    bool abierto_ = false, recfg_ = false, configurado_ = false;
    uint16_t key_ = 0;
    unsigned n_rech_ = 0;
    void reset_proc() {
        if (rst_n.read()) return;
        for (auto& f : map_) for (uint8_t& v : f) v = 0;
        abierto_ = recfg_ = configurado_ = false;
        key_ = 0;
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
            map_[(off - 0x08) / 8][(off - 0x08) % 8] = v;
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

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_DIO_H
