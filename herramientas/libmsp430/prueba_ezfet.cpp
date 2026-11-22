// =============================================================================
// prueba_ezfet.cpp — Un cliente mínimo de libmsp430 para la eZ-FET emulada
// (M11 de doc/analisis_msp430.md): lo que hacen UniFlash o CCS al programar,
// por la API pública de [MSPDS] (DLL430_v3/include/MSP430.h).
//
//   prueba_ezfet PUERTO FICHERO.hex [NOMBRE] [DIR VALOR]
//
// Abre la sonda (sin actualizar su firmware: si lo pide, falla), identifica el
// chip (y lo compara con NOMBRE, si se da), borra, programa y verifica el
// fichero (Intel HEX o TI-TXT, lo que lee libmsp430), lee la palabra DIR y la
// compara con VALOR, y deja el programa corriendo. Cada paso imprime una línea
// «[OK  ] ...» o «[FALLO] ...»; el código de salida es el número de fallos.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "MSP430.h"
#include "MSP430_Debug.h"

static int fallos = 0;
static void comprueba(bool ok, const std::string& que) {
    std::printf("  [%s] %s", ok ? "OK  " : "FALLO", que.c_str());
    if (!ok) std::printf(": %s", MSP430_Error_String(MSP430_Error_Number()));
    std::printf("\n");
    std::fflush(stdout);
    if (!ok) ++fallos;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s PUERTO FICHERO.hex [NOMBRE] [DIR VALOR]\n", argv[0]);
        return 2;
    }
    int32_t version = 0;
    const bool ini = MSP430_Initialize(argv[1], &version) == STATUS_OK;
    comprueba(ini, std::string("libmsp430 abre la eZ-FET en ") + argv[1]);
    if (!ini) return 1;
    comprueba(version > 0, "sin pedir actualizar el firmware (version " + std::to_string(version) + ")");
    if (version <= 0) { MSP430_Close(0); return fallos; }
    comprueba(MSP430_VCC(3300) == STATUS_OK, "VCC a 3,3 V");
    const bool abre = MSP430_OpenDevice("DEVICE_UNKNOWN", "", 0, 0, DEVICE_UNKNOWN) == STATUS_OK;
    comprueba(abre, "abre el objetivo por SBW");
    if (abre) {
        DEVICE_T d;
        std::memset(&d, 0, sizeof d);
        const bool id = MSP430_GetFoundDevice(reinterpret_cast<uint8_t*>(&d), sizeof d) == STATUS_OK;
        const std::string nombre(reinterpret_cast<const char*>(d.string));
        comprueba(id && (argc < 4 || nombre == argv[3]), "identifica " + nombre);
        comprueba(MSP430_ProgramFile(argv[2], ERASE_ALL, 1) == STATUS_OK,
                  std::string("borra, programa y verifica ") + argv[2]);
        comprueba(MSP430_VerifyFile(argv[2]) == STATUS_OK, "lo verifica otra vez");
        if (argc >= 6) {
            const long a = std::strtol(argv[4], nullptr, 0), v = std::strtol(argv[5], nullptr, 0);
            uint8_t b[2] = {0, 0};
            const bool lee = MSP430_Read_Memory(int32_t(a), b, 2) == STATUS_OK;
            const unsigned w = unsigned(b[0] | (b[1] << 8));
            char t[96];
            std::snprintf(t, sizeof t, "lee 0x%04lX = 0x%04X (se esperaba 0x%04lX)", a, w, v);
            comprueba(lee && long(w) == v, t);
        }
        comprueba(MSP430_Reset(RST_RESET, 0, 1) == STATUS_OK, "reset y suelta el objetivo");
    }
    MSP430_Close(0);
    return fallos;
}
