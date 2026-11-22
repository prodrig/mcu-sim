// =============================================================================
// sc_main_ezfet.cpp — LA SUITE DE LA eZ-FET EMULADA (M11b de doc/analisis_msp430.md)
//
// La eZ-FET lite de las LaunchPad como lo que es: un MSP430F5528 (el modelo
// del F5529) que ejecuta el firmware de TI, cableado por SBW al objetivo. Las
// imágenes son de la copia de la MSP Debug Stack del usuario (no se
// versionan): de $MSPDS, o de doc/refs/msp430/MSPDebugStack_OS_Package_3_15_1_1;
// sin ellas, la suite se omite.
//
//   Z1  el F5528 arranca el firmware de TI (núcleo, HAL, HIL, canal y DC-DC)
//       y se enumera como la eZ-FET: 2047:0013 «MSP Tools Driver», el CDC de
//       depuración en el EP2 y el del backchannel en el EP4
//   Z2  el protocolo V3 por el CDC, con las primeras órdenes de libmsp430 tal
//       y como las manda (grabadas de una sesión real con la eZ-FET emulada):
//       la versión, con el identificador de la eZ-FET lite (0xAAAB), y el
//       arranque de SBW hasta el JTAG ID del G2553 (0x89)
//   Z3  lo mismo con la FR5969: su JTAG ID, 0x99
//
// Lo de libmsp430 entera (identificar, programar y verificar el G2553 y la
// FR5969; mspdebug tilib) es `make ezfet-tilib`: necesita compilarla.
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "common/asan_opciones.h"
#include "soc/msp430/g2553/soc_g2553.h"
#include "soc/msp430/fr5969/soc_fr5969.h"
#include "soc/msp430/ezfet/ezfet.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}

static std::string imagenes() {
    const char* e = std::getenv("MSPDS");
    const std::string d = e ? e : "../doc/refs/msp430/MSPDebugStack_OS_Package_3_15_1_1";
    return ezfet::busca(d).empty() ? std::string() : d;
}

// Las órdenes de libmsp430 al abrir la sonda, con su respuesta [grabadas]
struct Paso { std::vector<uint8_t> orden, resp; };
static const std::vector<Paso>& sesion() {
    static const std::vector<Paso> p = {
        {{0x03, 0x92, 0x00, 0x00}, {}},                                     // reset del protocolo: sin respuesta
        {{0x06, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, {}},             // la versión (se mira aparte)
        {{0x03, 0x91, 0x01, 0x00, 0x11, 0x81, 0x02, 0x00, 0x07, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0A, 0x40, 0x00, 0x00},
         {0x03, 0x93, 0x02, 0x00, 0xFE, 0x6C}},
        {{0x03, 0x91, 0x02, 0x00, 0x05, 0x81, 0x03, 0x00, 0x52, 0x00}, {0x03, 0x93, 0x03, 0x00, 0xFF, 0x6C}},
        {{0x03, 0x91, 0x03, 0x00, 0x06, 0x8F, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x05, 0x92, 0x04, 0x00, 0xFF, 0xFF, 0x01, 0x92}},
        {{0x05, 0x81, 0x05, 0x00, 0x10, 0x00}, {}},                         // la tensión (se mira aparte)
        {{0x03, 0x91, 0x05, 0x00, 0x06, 0x81, 0x06, 0x00, 0x04, 0x00, 0x01, 0x00}, {0x04, 0x93, 0x06, 0x00, 0x01, 0x00, 0xFC, 0x6C}},
        {{0x03, 0x91, 0x06, 0x00, 0x05, 0x81, 0x07, 0x00, 0x0C, 0x00}, {}}, // el JTAG ID
    };
    return p;
}

SC_MODULE(TbEzFet) {
    SocG2553 g2{"g2553"};
    SocFR5969 fr5{"fr5969"};
    std::string dir = imagenes();
    std::unique_ptr<EzFet> ez_g2, ez_fr5;

    SC_CTOR(TbEzFet) {
        if (!dir.empty()) {
            ez_g2.reset(new EzFet("ezfet_g2", g2.test, g2.rst, dir));
            ez_fr5.reset(new EzFet("ezfet_fr5", fr5.test, fr5.rst, dir));
        }
        SC_THREAD(run);
    }

    // Una respuesta entera: el primer byte da la longitud (HalResponse: +3 si es
    // impar, +4 si es par)
    static bool entera(const std::string& r) {
        if (r.empty()) return false;
        const size_t n = uint8_t(r[0]) + ((uint8_t(r[0]) & 1u) ? 3u : 4u);
        return r.size() >= n;
    }
    std::string orden(EzFet& ez, const std::vector<uint8_t>& o, bool espera = true) {
        CanalMemoria* m = ez.usb().memoria();
        m->borra_recibido();
        m->empuja(std::string(o.begin(), o.end()));
        if (!espera) { wait(20, SC_MS); return std::string(); }
        for (int i = 0; i < 2000 && !entera(m->recibido()); ++i) wait(100, SC_US);
        return m->recibido();
    }
    static std::vector<uint8_t> v(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

    // Z2/Z3: la sesión; devuelve el JTAG ID (o -1)
    int sesion_v3(EzFet& ez, const char* quien) {
        const auto& p = sesion();
        int jtag = -1;
        bool iguales = true;
        for (size_t k = 0; k < p.size(); ++k) {
            const std::string r = orden(ez, p[k].orden, k != 0);
            if (k == 1) {
                // 0x2B bytes: ... el identificador de herramienta, tres veces, en 8-13
                const bool ok = r.size() >= 46 && uint8_t(r[0]) == 0x2B && uint8_t(r[1]) == 0x93 &&
                                uint8_t(r[8]) == 0xAB && uint8_t(r[9]) == 0xAA;
                check(ok, std::string(quien) + ": la versión del firmware, con el identificador de la eZ-FET lite (0xAAAB)");
            } else if (k == 5) {
                iguales = iguales && r.size() >= 8 && uint8_t(r[1]) == 0x93;
            } else if (k + 1 == p.size()) {
                if (r.size() >= 8 && uint8_t(r[1]) == 0x93) jtag = uint8_t(r[4]);
            } else if (!p[k].resp.empty()) {
                iguales = iguales && v(r) == p[k].resp;
            }
        }
        check(iguales, std::string(quien) + ": la configuración y el arranque de SBW, respuesta a respuesta como en la sesión grabada");
        return jtag;
    }

    void run() {
        if (dir.empty()) {
            std::printf("  [OMITE] testezfet: no están las imágenes de la eZ-FET (MSPDS=carpeta de la MSP Debug Stack)\n");
            sc_stop();
            return;
        }
        g2.carga("verif/g2553/elf/ej_blink.elf");
        fr5.carga("verif/fr5969/elf/fr59_blink.elf");
        g2.reset(true); fr5.reset(true);
        wait(1, SC_US);
        g2.reset(false); fr5.reset(false);

        grupo("Z1 el F5528 con el firmware de TI se enumera como la eZ-FET");
        for (int i = 0; i < 3000 && !(ez_g2->usb().listo() && ez_fr5->usb().listo()); ++i) wait(1, SC_MS);
        PuenteUsbCdc& u = ez_g2->usb();
        check(u.listo() && u.vid() == 0x2047 && u.pid() == 0x0013 && u.producto() == "MSP Tools Driver",
              "2047:0013 «MSP Tools Driver» (t=" + sc_time_stamp().to_string() + ")");
        check(u.describir().find("CDC-ACM") != std::string::npos && ez_fr5->usb().listo(),
              "los dos, con su CDC de depuración (el primero del dispositivo compuesto)");
        check(ez_g2->f5528.f_mclk() > 24e6 && ez_g2->f5528.f_mclk() < 26e6, "el núcleo pone el F5528 a 25 MHz");

        grupo("Z2 el protocolo V3 hasta el JTAG ID del G2553");
        const int j2 = sesion_v3(*ez_g2, "G2553");
        char t[64];
        std::snprintf(t, sizeof t, "el JTAG ID del G2553 por SBW: 0x%02X", unsigned(j2 & 0xFF));
        check(j2 == 0x89, t);

        grupo("Z3 y el de la FR5969");
        const int j5 = sesion_v3(*ez_fr5, "FR5969");
        std::snprintf(t, sizeof t, "el JTAG ID de la FR5969 por SBW: 0x%02X", unsigned(j5 & 0xFF));
        check(j5 == 0x99, t);

        std::printf("\nTOTAL EZFET: %u comprobaciones OK, %u fallos | tiempo simulado %s\n", g_ok, g_fallos,
                    sc_time_stamp().to_string().c_str());
        sc_stop();
    }
};

int sc_main(int, char**) {
    TbEzFet tb("tb");
    sc_start();
    return g_fallos ? 1 : 0;
}
