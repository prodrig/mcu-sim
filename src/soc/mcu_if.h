// =============================================================================
// mcu_if.h — LA INTERFAZ DE UN MCU y su factoría por cadena
//
// Hasta la fase 1, `tipo="STM32F407VG"` en el XML **se comprobaba, no se
// despachaba**: `sim_main.cpp` miraba el nombre contra una lista y luego hacía
// `new SocF4(...)` pasara lo que pasara. Eso valía mientras todos los
// tipos aceptados fueran el mismo modelo con distintos rasgos —que es el caso
// de los once miembros de la familia F405/407— y deja de valer el día que haya
// un chip de otra familia, que es otra clase de C++.
//
// Es exactamente el problema que el paso 3 resolvió para las piezas de placa, y
// se resuelve igual: una interfaz y un registro de cadena a creador.
// [doc/multi_mcu.md, §6.1]
//
// QUÉ TIENE QUE SABER HACER UN MCU, visto desde `sim`. Cinco cosas, y son las
// cinco que `sim_main` le pedía al puntero concreto:
//
//   1. DAR DE ALTA SUS NODOS con el prefijo que le toque (`u0.PD12`), para que
//      la placa pueda referirse a sus pines;
//   2. ENTREGAR EL NODO de un pad concreto, que es por donde se engancha la
//      sonda de depuración a PA13/PA14;
//   3. ARRANCAR Y PARAR ELÉCTRICAMENTE: VDD, VDDA, BOOT0 y NRST;
//   4. CARGAR UN FIRMWARE en su Flash;
//   5. y dos interruptores de traza —las ondas de reloj internas y el stub de
//      GDB que lleva dentro, si lo lleva—.
//
// Ni un periférico se entera de nada de esto: son las cuatro cosas de §2 del
// documento de varios MCUs, más el nodo analógico que la sonda necesita.
//
// LO QUE ESTA INTERFAZ NO ES. No es una capa de abstracción sobre el modelo:
// `sim` sigue pudiendo pedir el objeto concreto cuando lo necesita —para el
// informe final de los LEDs, por ejemplo—. Es el punto donde una CADENA se
// convierte en un OBJETO, ni más ni menos, igual que `Fabrica` para las piezas.
// =============================================================================
#ifndef STM32_SOC_MCU_IF_H
#define STM32_SOC_MCU_IF_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../common/analog_net.h"
#include "../core/cortex_m4f.h"   // DebugCaps
#include "mcu_desc.h"

namespace stm32 {

class NodeMap;
class Cableado;   // pins/pin_mux.h
class programador_nv_if;   // common/gdb_rsp.h

class mcu_if {
public:
    virtual ~mcu_if() = default;

    // Qué chip es: su nombre, su familia y su encapsulado. Lo demás -memoria,
    // reloj, periféricos- es del descriptor de SU familia, y lo sabe él.
    // (Hasta T0 era `const McuCaps& caps()`, el descriptor de los STM32F4; un
    // adaptador de ST lo sigue ofreciendo, en `McuF4`.)
    virtual const McuDesc& desc() const = 0;

    // --- 1. Los nodos -------------------------------------------------------
    // `prefijo` vacío deja los nombres desnudos (`PD12`), que es lo que hace
    // que las placas de un solo chip escritas hasta hoy sigan valiendo.
    virtual void registra_nodos(const std::string& prefijo, NodeMap& n) = 0;

    // --- 2. El nodo de un pad ----------------------------------------------
    virtual analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) = 0;
    // Los dos por donde se engancha una sonda SWD de fuera: SWCLK y SWDIO. En
    // un STM32 son PA14 y PA13, pads de puerto con la función AF0; en un
    // MSP432P4 son patillas DEDICADAS, que no son de ningún puerto. Por eso
    // los dice el chip y no `sim` (T0). nullptr: este chip no los saca.
    virtual analog_net_if* nodo_swclk() { return &nodo_analogico(0, 14); }
    virtual analog_net_if* nodo_swdio() { return &nodo_analogico(0, 13); }

    // --- 3. La alimentación -------------------------------------------------
    // El orden importa y es el de la placa real: primero todo a cero con NRST
    // abajo, luego VDD, y solo después se suelta el reset.
    virtual void alimenta(bool encendido) = 0;
    virtual void reset_pin(bool activo) = 0;      // NRST a masa / en alta imp.
    // La alimentación principal la pone la PLACA -el LDO de la sonda XDS110,
    // por el puente 3V3 de J101 (doc/analisis_xds110.md, X1)-: el adaptador
    // deja de sostener ese nodo y solo pone las masas (y la analógica, que
    // las placas no unen). Sin el puente, el chip se queda sin alimentar.
    // Por omisión no hace nada: los chips que no lo saben siguen igual.
    virtual void alimentacion_externa(bool) {}

    // --- 4. El firmware -----------------------------------------------------
    // Devuelve false si el fichero no se puede leer o no cabe.
    virtual bool carga_firmware(const std::string& ruta) = 0;
    // Y el caso de no haberlo: un chip sin firmware no es un chip que no
    // arranca, es un chip que lee basura y se va a un fallo. Para que una placa
    // se pueda montar y mirar sin programa, el modelo le escribe un vector de
    // reset valido y un `wfe` en bucle. Quien sabe donde va eso es el chip -su
    // Flash y su SRAM-, no `sim`, y por eso esta aqui.
    virtual void aparca_en_wfe() = 0;
    // Lo que hace `sim` sin firmware: aparcar, salvo que el chip tenga algo
    // mejor que hacer en blanco (el MSP432E4 con ROM arranca su cargador
    // serie, como un chip nuevo). Devuelve lo que se le dice al usuario.
    virtual std::string sin_firmware() { aparca_en_wfe(); return "el nucleo se aparca en wfe"; }

    // --- 5. Trazas ----------------------------------------------------------
    virtual void set_ondas_reloj(bool on) = 0;
    // El stub que el núcleo lleva dentro en modo `dap`. `tiene_gdb_interno()`
    // dice si existe; encenderlo cuando no lo hay no es un error, es un no-op.
    virtual bool tiene_gdb_interno() const = 0;
    virtual void gdb_interno(bool activo, bool traza) = 0;
    // La secuencia con la que un stub de GDB programa la Flash de este chip
    // (T0). nullptr: la de omisión, la del STM32F405/407. La usa el stub de
    // PINES, que `sim` construye por fuera del chip; el interno ya la tiene.
    virtual programador_nv_if* programador() { return nullptr; }

    // --- 6. Lo que este modelo TODAVIA no hace ------------------------------
    //
    // Un modelo incompleto no es un problema; un modelo incompleto que no lo
    // dice, si. Cada chip devuelve aqui la lista de cosas que el silicio tiene
    // y el modelo aun no, en una frase por linea, y `sim` las imprime al
    // montar la placa. Vacia = el modelo cubre lo que dice cubrir.
    //
    // Es el mismo recurso que `Encapsulado::verificado`: preferimos un aviso
    // repetido a un alumno desarrollando contra algo que no esta.
    virtual std::vector<std::string> limitaciones() const { return {}; }

    // --- 7. Lo que solo tienen algunos chips (T4) ---------------------------
    //
    // Opciones de montaje que no son de todos: la ROM del MSP432P4 (`rom`:
    // "r1", "ninguna" o el fichero de un volcado) y su TLV (`tlv`: un
    // fichero). Se aplican antes de cargar el firmware. Devuelve "" si se
    // aplica, o por que no; un chip que no la tiene lo dice aqui.
    virtual bool admite_opcion(const std::string& clave) const { (void)clave; return false; }
    virtual std::string opcion(const std::string& clave, const std::string& valor) {
        (void)valor;
        return std::string("el ") + desc().nombre + " no tiene " + clave +
               " que se pueda cambiar";
    }
    // Una linea que describe lo que se aplico, para imprimirla al montar.
    virtual std::string describe_opcion(const std::string& clave) const {
        (void)clave;
        return "";
    }
};

// ---------------------------------------------------------------------------
// LA FACTORÍA
//
// Misma receta que `Fabrica` para las piezas de placa: un registro estático de
// cadena a creador que se puebla antes de `main` con una macro de auto-registro.
//
// Diferencia con aquella, y es la que explica por qué no la hubo hasta ahora:
// el creador recibe además el **descriptor** del chip, porque los once miembros
// de la familia F405/407 son la MISMA clase con distintos rasgos. Un tipo nuevo
// de la misma familia es una línea en el catálogo de `mcu_caps.h`; un tipo de
// otra familia es una clase nueva y una llamada a `REGISTRA_MCU`.
// ---------------------------------------------------------------------------
class FabricaMcu {
public:
    // nombre de instancia, rasgos de depuración, cableado de nodos compartidos
    // y el descriptor del chip.
    using Creador = std::function<mcu_if*(const char* nm, DebugCaps,
                                          const Cableado&, const McuDesc&)>;

    static void registra(const std::string& familia, Creador c) {
        mapa()[familia] = std::move(c);
    }
    // Se busca por FAMILIA, no por nombre de pieza: `STM32F407VE` y
    // `STM32F405RG` los construye el mismo creador con descriptores distintos.
    // Es la misma frontera que marca `McuCaps::familia`.
    static const Creador* busca(const std::string& familia) {
        auto it = mapa().find(familia);
        return it == mapa().end() ? nullptr : &it->second;
    }
    static bool conoce(const std::string& familia) { return busca(familia) != nullptr; }

    // Construye el chip que pide el descriptor, o devuelve nullptr si su
    // familia no está registrada. NUNCA construye otra cosa en su lugar: eso
    // sería montar un F407 donde el usuario escribió un F446.
    static mcu_if* crea(const McuDesc& d, const char* nm, DebugCaps dbg,
                        const Cableado& cab) {
        const Creador* c = busca(d.familia);
        return c ? (*c)(nm, dbg, cab, d) : nullptr;
    }

    static std::vector<std::string> familias() {
        std::vector<std::string> v;
        for (const auto& kv : mapa()) v.push_back(kv.first);
        return v;
    }

private:
    static std::map<std::string, Creador>& mapa() {
        static std::map<std::string, Creador> m;
        return m;
    }
};

// Auto-registro, en el ámbito de fichero:
//
//   REGISTRA_MCU(STM32F4, [](const char* nm, DebugCaps d, const Cableado& c,
//                            const McuDesc& desc) -> mcu_if* {
//       return new AdaptadorStm32F4(nm, d, c,
//                                   *static_cast<const McuCaps*>(desc.propio));
//   });
//
// El argumento es el nombre de la FAMILIA, sin comillas.
#define REGISTRA_MCU(FAMILIA, ...)                                             \
    struct RegistroMcu##FAMILIA {                                              \
        RegistroMcu##FAMILIA() {                                               \
            ::stm32::FabricaMcu::registra(#FAMILIA, __VA_ARGS__);              \
        }                                                                      \
    };                                                                         \
    inline const RegistroMcu##FAMILIA g_registro_mcu_##FAMILIA{}

} // namespace stm32
#endif // STM32_SOC_MCU_IF_H
