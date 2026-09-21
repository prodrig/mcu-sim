// ===========================================================================
// common/huella_fw.h — las imagenes de firmware que carga el banco son LAS QUE
// dice verif/fw/huellas.txt, y esto lo comprueba.
//
// POR QUE. Los `.bin` de verif/fw se versionan desde que se descubrio que el
// tiempo simulado total no dependia de la maquina sino del BINARIO: dos
// compiladores cruzados distintos producen dos CoreMark distintos, y
// `t17_coremark()` sondea el final cada 2 ms, asi que la diferencia sale
// cuantizada a 2 ms y parece un problema de plataforma. Vease T-22 en
// doc/todo.md.
//
// Versionarlos quita la causa. Esto es la defensa de segundo orden: si alguien
// regenera un firmware con otra cadena -y `make fw407` lo hace sin preguntar-,
// la suite lo dice en su primer grupo, en vez de dejarlo salir como una
// diferencia de picosegundos veinte grupos mas abajo.
//
// La comprobacion no cuesta tiempo simulado: es lectura de ficheros del
// anfitrion, fuera del planificador de SystemC.
//
// El manifiesto es la UNICA fuente de verdad. Aqui no hay ninguna huella
// escrita a mano, a proposito: dos sitios que hay que actualizar a la vez son
// un sitio que se queda viejo.
// ===========================================================================
#ifndef MCU_SIM_COMMON_HUELLA_FW_H
#define MCU_SIM_COMMON_HUELLA_FW_H

#include <cstdio>
#include <cstring>
#include <string>

namespace huella_fw {

// FNV-1a de 32 bits sobre el contenido de un fichero. Devuelve 0 si no se
// puede abrir, y 0 no es un valor valido para este uso: el que llama distingue
// el caso mirando si el fichero existe, no por el valor.
inline unsigned huella_fichero(const char* ruta) {
    std::FILE* f = std::fopen(ruta, "rb");
    if (!f) return 0u;
    unsigned h = 2166136261u;
    int c;
    while ((c = std::fgetc(f)) != EOF) { h ^= unsigned(c); h *= 16777619u; }
    std::fclose(f);
    return h;
}

// El resultado de contrastar una suite entera contra el manifiesto.
struct Veredicto {
    int         comprobadas = 0;   // lineas del manifiesto de ESTA suite
    int         ausentes    = 0;   // .bin que no estan
    int         discrepan   = 0;   // .bin que estan y no coinciden
    std::string detalle;           // que ha pasado, listo para imprimir

    // Ojo con `comprobadas > 0`: sin esa condicion, un manifiesto que no se
    // pueda abrir daria por buena la suite entera. Una comprobacion que no
    // puede fallar no comprueba nada.
    bool ok() const { return comprobadas > 0 && ausentes == 0 && discrepan == 0; }
};

// Lee `manifiesto` y verifica las lineas cuya primera columna sea `suite`
// ("407", "446" o "417"). Las rutas del manifiesto son relativas a `raiz`, que
// es el directorio donde vive el propio manifiesto.
inline Veredicto verifica(const char* manifiesto, const char* raiz,
                          const char* suite) {
    Veredicto v;
    std::FILE* f = std::fopen(manifiesto, "r");
    if (!f) {
        v.detalle = std::string("no encuentro el manifiesto ") + manifiesto;
        return v;
    }
    char linea[512];
    while (std::fgets(linea, sizeof linea, f)) {
        char col_suite[16] = {0}, ruta[256] = {0};
        unsigned bytes = 0, esperada = 0;
        if (std::sscanf(linea, "%15s %255s %u %x",
                        col_suite, ruta, &bytes, &esperada) != 4) continue;
        if (col_suite[0] == '#') continue;
        if (std::strcmp(col_suite, suite) != 0) continue;

        v.comprobadas++;
        const std::string completa = std::string(raiz) + "/" + ruta;

        std::FILE* b = std::fopen(completa.c_str(), "rb");
        if (!b) {
            v.ausentes++;
            if (v.ausentes <= 3)
                v.detalle += std::string("\n         falta ") + ruta;
            continue;
        }
        std::fclose(b);

        const unsigned obtenida = huella_fichero(completa.c_str());
        if (obtenida != esperada) {
            v.discrepan++;
            char aviso[384];
            std::snprintf(aviso, sizeof aviso,
                          "\n         %s: huella 0x%08X, se esperaba 0x%08X",
                          ruta, obtenida, esperada);
            if (v.discrepan <= 3) v.detalle += aviso;
        }
    }
    std::fclose(f);

    if (v.comprobadas == 0)
        v.detalle = std::string("el manifiesto no tiene ninguna linea de la suite ")
                  + suite;
    else if (v.ok())
        v.detalle = "todas coinciden con verif/fw/huellas.txt";
    else if (v.ausentes + v.discrepan > 3)
        v.detalle += "\n         (y mas; el resto no se lista)";

    return v;
}

}  // namespace huella_fw

#endif  // MCU_SIM_COMMON_HUELLA_FW_H
