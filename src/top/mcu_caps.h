// =============================================================================
// mcu_caps.h — El DESCRIPTOR de un microcontrolador
//
// Junta en una sola pieza lo que distingue a un chip de otro de la misma
// familia: los rasgos del núcleo (core/core_caps.h), los del mapa de memoria
// (mem/mem_caps.h) y los límites de reloj. Es lo que hasta ahora estaba
// repartido entre constantes globales de `common/ahb_types.h`, argumentos de
// constructor y números escritos al vuelo.
//
// LO QUE ESTO ES Y LO QUE NO ES. Con un descriptor se construye un STM32F407VG,
// y con otro se construiría un F405 —el mismo silicio sin Ethernet ni cámara—,
// un F415 o un F407 de otro encapsulado. Lo que NO se construye cambiando
// números es un chip de otra familia: un F446 tiene otro árbol de reloj y
// periféricos que aquí no existen, y describirlo con un `McuCaps` distinto no
// lo convertiría en un F446, lo convertiría en un F407 con etiqueta falsa. La
// frontera exacta está en el campo `familia`: dos chips con la misma familia se
// distinguen con este struct; dos familias distintas necesitan modelo nuevo.
//
// Por eso este fichero declara los once descriptores de la familia F405/407 —el
// mismo silicio con otro encapsulado, otra Flash o sin Ethernet— y dos juegos de
// laboratorio que la suite usa para comprobar que las piezas se recombinan
// (T127). No hay aquí ningún chip que el proyecto afirme modelar y no modele.
//
// Y desde la fase 2 del plan del F446 hay un duodécimo, el `STM32F446RE`, que
// es de OTRA familia y lo dice: su descriptor lleva `familia = "STM32F446"` y
// lo construye otra clase (`soc/stm32f446.h`), no esta tabla. Lo que está aquí
// es lo que un descriptor puede describir de él —Flash, RAM, encapsulado,
// posiciones de vector, bloques ausentes, IDCODE—; lo que no puede, que es su
// árbol de reloj, está en esa clase y en lo que todavía le falta, dicho en voz
// alta. [doc/stm32f407vg_vs_446re.md, §6.3 y §13]
// =============================================================================
#ifndef STM32_TOP_MCU_CAPS_H
#define STM32_TOP_MCU_CAPS_H

#include <string>
#include <vector>
#include "../core/core_caps.h"
#include "../mem/mem_caps.h"
#include "../pins/encapsulado.h"
#include "../bus/conectividad.h"
#include "../rcc/reloj_caps.h"

namespace stm32 {

// Los topes de reloj por dominio y los rasgos del árbol viven en
// `rcc/reloj_caps.h`, con el subsistema que los usa. Los topes no son
// decoración: el RCC avisa cuando el firmware programa un árbol que los pasa, y
// ese aviso es de los que ahorran una tarde —un F401 a 168 MHz no existe, y el
// modelo tiene que decirlo—.

// Los periféricos que NO lleva todo el mundo. Son tres, y no es una lista
// abierta a propósito: el resto del juego —los catorce temporizadores, los seis
// puertos serie, los tres ADC, el bxCAN, el SDIO, los dos OTG— está en TODOS
// los F405/F407, del LQFP64 al UFBGA176, y ponerlos como opción sugeriría que
// hay chips de esta familia sin ellos. No los hay.
//
//   eth   Ethernet MAC. Es LA diferencia entre un F405 y un F407, junto con
//         la cámara. [DS8626, tabla 2]
//   dcmi  Interfaz de cámara paralelo. La otra mitad de esa diferencia.
//   fsmc  Controlador de memoria externa. Lo llevan todos MENOS el LQFP64,
//         que sencillamente no tiene pines donde sacar el bus.
//
// Ojo con lo que NO hace falta modelar aquí: el datasheet dice que en LQFP100
// y WLCSP90 el FSMC está «restringido». Esa restricción es que no salen todas
// las líneas de dirección y datos, y eso YA lo dice el encapsulado —los pads
// que no están, no están—. Un booleano más sería describir dos veces lo mismo
// y arriesgarse a que las dos descripciones no coincidan.
//
// Los dos ÚLTIMOS los trajo el F446 en la fase 2 del plan, y son de la misma
// clase que los tres primeros —bloques que unas piezas llevan y otras no—:
//
//   rng     Generador de números aleatorios. No existe en el F446: ni un solo
//           símbolo `RNG` en `stm32f446xx.h` [CMSIS].
//   i2sext  Los bloques de extensión I2S2ext/I2S3ext, los que dan el I2S
//           full-duplex en el F407. Desaparecen en el F446 [AN4658], y uno de
//           sus dos huecos —`0x4000_4000`— lo ocupa allí otro periférico
//           distinto, el SPDIF-RX. Es la trampa de direcciones de vs_446re §5.3.
//
// Y los SEIS que trajo la fase 4, que son la otra cara: bloques que el F446
// tiene y el F407 no. Van uno por uno, y no como un «es un F446», porque cada
// uno enciende una cosa distinta —una entrada del decodificador, un módulo, un
// vector— y porque la familia F446 tiene miembros que no los llevan todos.
//
//   spi4      una quinta instancia del bloque SPI, en 0x4001_3400
//   i2s1      la mitad I2S del SPI1, que en el F407 es un SPI puro
//   sai       SAI1 y SAI2, los dos bloques de audio
//   quadspi   memoria externa serie, que comparte puerto de matriz con el FMC
//   fmpi2c1   el I2C moderno: OTRO IP, no el de siempre con más velocidad
//   cec       HDMI-CEC
//   spdifrx   receptor S/PDIF
struct Periferia {
    bool eth;
    bool dcmi;
    bool fsmc;
    bool rng;
    bool i2sext;
    bool spi4;
    bool i2s1;
    bool sai;
    bool quadspi;
    bool fmpi2c1;
    bool cec;
    bool spdifrx;

    // ¿Lleva este chip alguno de los bloques que el F446 añadió? Es lo que el
    // RCC necesita saber para abrir -o no- los siete bits de ENR que van con
    // ellos, y preguntarlo así evita que el RCC tenga que conocer la lista.
    bool alguno_f446() const {
        return spi4 || sai || quadspi || fmpi2c1 || cec || spdifrx;
    }
};

struct McuCaps {
    const char*  nombre;       // "STM32F407VG", el que se escribe en el XML
    const char*  familia;      // "STM32F4": la frontera de lo recombinable
    CoreCaps     nucleo;
    MemCaps      memoria;
    LimitesReloj reloj;        // los topes por dominio (rcc/reloj_caps.h)
    // Qué tiene el árbol de reloj de este chip: cuántos PLL, si el principal
    // saca R, si hay registros de selección dedicados y si hay over-drive. Es
    // lo que NO cabía en un descriptor hasta la fase 3, y lo que hace que
    // quepa es que ahora el modelo IMPLEMENTA cada uno de esos rasgos.
    ArbolReloj   arbol;
    Encapsulado  enc;          // qué pads salen al plástico
    Periferia    perif;        // qué bloques lleva este miembro de la familia
    // Qué maestro de la matriz alcanza a qué esclavo. Va en el descriptor, y no
    // dentro de la matriz, porque es un rasgo del CHIP: el F446 tiene siete
    // maestros y no ocho, y eso se dice con una fila a cero.
    Conectividad conn;
    // Lo que un depurador lee en `DBGMCU_IDCODE` para saber con qué habla. No
    // es adorno: con el valor equivocado, STM32CubeIDE no da un error claro,
    // da un «Could not verify ST device» — el mensaje que costó media sesión
    // diagnosticar. [RM0090 §32.6.1, RM0390 §33.6.1]
    uint32_t     idcode;
};

// ---------------------------------------------------------------------------
// LA FAMILIA STM32F405/407, ENTERA
//
// Once referencias, y todas salen del MISMO silicio. Lo que las distingue son
// tres cosas y ninguna más [DS8626, tabla 2]:
//
//   EL DÍGITO 5 O 7. Un F405 es un F407 SIN Ethernet y SIN cámara. Eso es todo:
//   mismo núcleo, misma memoria, mismos temporizadores, mismos puertos serie.
//   (El F415/F417, que aquí no están, son los mismos con el acelerador
//   criptográfico; no se declaran porque ese bloque no está modelado y un
//   descriptor no lo haría aparecer.)
//
//   LA LETRA DEL ENCAPSULADO. R = LQFP64, O = WLCSP90, V = LQFP100,
//   Z = LQFP144, I = LQFP176/UFBGA176. Decide qué pads salen al plástico, y de
//   rebote si hay bus externo: en el LQFP64 no hay dónde sacarlo.
//
//   LA ÚLTIMA LETRA, EL TAMAÑO DE FLASH. E = 512 KB (ocho sectores),
//   G = 1 MB (doce). La RAM no cambia: los 192+4 KB están en todas.
//
// Lo que se gana con esto es muy concreto para un alumno: un firmware enlazado
// para 1 MB deja de caber en un `...E` y el modelo lo dice; `PE2` deja de
// existir en un LQFP64 y el modelo lo dice; y un programa que inicializa el
// Ethernet en un F405 se encuentra con que ese periférico no está, igual que en
// la tarjeta.
// ---------------------------------------------------------------------------

// Las dos geometrías de Flash de la familia. La de 1 MB ya estaba; la de
// 512 KB son sus ocho primeros sectores, y es una tabla y no media tabla
// porque el sector 7 termina donde termina la Flash.
inline constexpr FlashSector SECTORES_512K[] = {
    {0x08000000, 0x04000}, {0x08004000, 0x04000},   // 0,1 : 16 KB
    {0x08008000, 0x04000}, {0x0800C000, 0x04000},   // 2,3 : 16 KB
    {0x08010000, 0x10000},                          // 4   : 64 KB
    {0x08020000, 0x20000}, {0x08040000, 0x20000},   // 5,6 : 128 KB
    {0x08060000, 0x20000}                           // 7   : 128 KB
};
inline constexpr MapaFlash FLASH_512K {
    addr::FLASH_BASE, 0x00080000u,                  // 512 KB
    SECTORES_512K, 8,
    addr::SYSMEM_BASE, addr::SYSMEM_SIZE,
    addr::OTP_BASE,    addr::OTP_SIZE,
    addr::OPT_BASE,    addr::OPT_SIZE,
    30e6, 5
};
inline constexpr MemCaps MEM_512K { FLASH_512K, RAM_STM32F407VG };

// Los tres juegos de periféricos que hay en la familia. El RNG y los bloques de
// extensión del I2S los llevan los once, del LQFP64 al UFBGA176.
// Los siete ultimos campos —los del F446— van escritos a mano aunque valgan lo
// mismo que su valor por defecto: aqui son documentacion. Un `false` explicito
// dice «este chip NO lleva el bloque»; un hueco no dice nada, y dentro de tres
// periféricos nadie recordara si el hueco era una decision o un olvido.
//                                    eth    dcmi   fsmc   rng   i2sext
//                                    spi4   i2s1   sai    qspi  fmpi2c cec    spdif
inline constexpr Periferia PERIF_F407 {
    true,  true,  true,  true, true,
    false, false, false, false, false, false, false };
inline constexpr Periferia PERIF_F405 {
    false, false, true,  true, true,
    false, false, false, false, false, false, false };
inline constexpr Periferia PERIF_F405_R64 {
    false, false, false, true, true,
    false, false, false, false, false, false, false };

// El identificador de la familia F405/407/415/417 en `DBGMCU_IDCODE`:
// DEV_ID = 0x413, REV_ID = 0x1001. [RM0090, §32.6.1]
constexpr uint32_t IDCODE_STM32F40X = 0x10016413u;

// El constructor que evita repetir once veces los mismos cinco campos. Todos
// los miembros comparten núcleo, RAM y topes de reloj; lo que se pasa es lo
// único que cambia.
constexpr McuCaps mcu_f4(const char* nombre, const MemCaps& mem,
                         const Encapsulado& enc, const Periferia& per) {
    return McuCaps{ nombre, "STM32F4", CORE_STM32F407VG, mem,
                    RELOJ_STM32F407VG, ARBOL_STM32F4, enc, per,
                    CONN_STM32F407VG, IDCODE_STM32F40X };
}

// --- STM32F405: sin Ethernet y sin camara -----------------------------------
inline constexpr McuCaps MCU_STM32F405RG =
    mcu_f4("STM32F405RG", MEM_STM32F407VG, ENC_LQFP64,  PERIF_F405_R64);
inline constexpr McuCaps MCU_STM32F405OG =
    mcu_f4("STM32F405OG", MEM_STM32F407VG, ENC_WLCSP90, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405VG =
    mcu_f4("STM32F405VG", MEM_STM32F407VG, ENC_LQFP100, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405ZG =
    mcu_f4("STM32F405ZG", MEM_STM32F407VG, ENC_LQFP144, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405OE =
    mcu_f4("STM32F405OE", MEM_512K,        ENC_WLCSP90, PERIF_F405);

// --- STM32F407: con Ethernet y camara ---------------------------------------
inline constexpr McuCaps MCU_STM32F407VE =
    mcu_f4("STM32F407VE", MEM_512K,        ENC_LQFP100, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407VG =
    mcu_f4("STM32F407VG", MEM_STM32F407VG, ENC_LQFP100, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407ZE =
    mcu_f4("STM32F407ZE", MEM_512K,        ENC_LQFP144, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407ZG =
    mcu_f4("STM32F407ZG", MEM_STM32F407VG, ENC_LQFP144, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407IE =
    mcu_f4("STM32F407IE", MEM_512K,        ENC_LQFP176, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407IG =
    mcu_f4("STM32F407IG", MEM_STM32F407VG, ENC_LQFP176, PERIF_F407);

// ---------------------------------------------------------------------------
// EL STM32F446RE — otra FAMILIA, y por eso otro `familia` y otra clase
//
// Aquí está la parte del F446 que un descriptor SÍ puede describir: cuánta
// Flash, cuánta RAM, qué pads salen, cuántas posiciones de vector, qué bloques
// faltan y qué lee un depurador. Y aquí se acaba: lo que un descriptor NO puede
// describir —el tercer PLL, el divisor R, los cinco registros de selección de
// reloj que en el F407 no existen y el acoplamiento con el over-drive— es
// justamente lo que obliga a que haya una CLASE `Stm32F446` (soc/stm32f446.h) y
// no una fila más en esta tabla. [vs_446re, §6.3 y §13]
//
// Meterlo aquí y no en un fichero aparte es deliberado: este fichero es el
// catálogo de LO QUE ESTE PROGRAMA SABE NOMBRAR, y `--mcu`, `<mcu tipo=>` y la
// ayuda lo leen entero. Un F446 invisible para el catálogo sería un chip que el
// modelo construye y el usuario no puede pedir.
// ---------------------------------------------------------------------------

// La RAM: los mismos 112 + 16 KB de siempre y los 4 de backup, **sin CCM**.
// Verificado por tres caminos [vs_446re, §4.2]: `CCMDATARAM_BASE` está en
// `stm32f407xx.h` y no en `stm32f446xx.h`; PINDATA no declara `<CCMRam>` para el
// F446R; y RM0390 §2.2.3 habla de «up to two blocks: SRAM1 and SRAM2».
//
// La base de la CCM se conserva con tamaño cero, y no es un descuido: el
// tamaño es lo que dice que no está, y la base es lo que dice DÓNDE no está.
// Con las dos cosas, `0x1000_0000` en un F446 es espacio RESERVADO —tocarlo da
// error de bus— y no «la CCM a la que este maestro no llega», que es otra cosa.
inline constexpr MapaRam RAM_STM32F446 {
    addr::SRAM1_BASE,   addr::SRAM1_SIZE,      // 112 KB
    addr::SRAM2_BASE,   addr::SRAM2_SIZE,      //  16 KB
    addr::CCM_BASE,     0u,                    //  sin CCM
    addr::BKPSRAM_BASE, addr::BKPSRAM_SIZE     //   4 KB
};
inline constexpr MemCaps MEM_STM32F446RE { FLASH_512K, RAM_STM32F446 };

// Los topes del F446 están en `rcc/reloj_caps.h`, y desde la fase 3 son DOS
// juegos: 168 / 42 / 84 sin over-drive y 180 / 45 / 90 con él. Quién de los dos
// rige lo dice el PWR en tiempo de ejecución, no esta tabla.

// Lo que el F446 no lleva: Ethernet, RNG y los bloques de extensión del I2S. La
// cámara SÍ la lleva —es fácil suponer lo contrario porque el F405 no la tiene—
// y el bus externo existe en el die pero **no en el LQFP64**: DS10693 tabla 2
// pone `No` en la columna RE de la fila «FMC memory controller».
//                                  eth    dcmi  fsmc   rng    i2sext
inline constexpr Periferia PERIF_F446RE { false, true, false, false, false,
//                                  spi4  i2s1  sai   qspi  fmpi2c1 cec   spdif
                                    true, true, true, true, true,   true, true };

// DEV_ID = 0x421, REV_ID = 0x1000 (revisión A) [RM0390, §33.6.1, verificado en
// la fase 0]. El JTAG ID del boundary-scan, que es otro registro y no este, es
// 0x0641_3041.
constexpr uint32_t IDCODE_STM32F446 = 0x10000421u;

inline constexpr McuCaps MCU_STM32F446RE {
    "STM32F446RE", "STM32F446", CORE_STM32F446, MEM_STM32F446RE,
    RELOJ_STM32F446, ARBOL_STM32F446, ENC_LQFP64_F446, PERIF_F446RE,
    CONN_STM32F446, IDCODE_STM32F446
};

// ---------------------------------------------------------------------------
// EL CATÁLOGO
//
// Una lista y dos funciones. Lo que esta lista dice es qué chips se pueden
// NOMBRAR; quién los construye lo dice `FabricaMcu`, que despacha por el campo
// `familia`. Mientras solo hubo una familia las dos cosas coincidían y la
// factoría era andamio sin obra; con el F446 ya no coinciden, y por eso existe:
// once de estas doce entradas son EL MISMO MODELO con distintos rasgos, y la
// duodécima es otra clase. [doc/stm32f407vg_multi_mcu.md, §6.1]
// ---------------------------------------------------------------------------
inline const McuCaps* const CATALOGO_MCU[] = {
    &MCU_STM32F405RG, &MCU_STM32F405OG, &MCU_STM32F405VG, &MCU_STM32F405ZG,
    &MCU_STM32F405OE,
    &MCU_STM32F407VE, &MCU_STM32F407VG, &MCU_STM32F407ZE, &MCU_STM32F407ZG,
    &MCU_STM32F407IE, &MCU_STM32F407IG,
    &MCU_STM32F446RE
};
inline constexpr unsigned N_CATALOGO_MCU =
    sizeof(CATALOGO_MCU) / sizeof(CATALOGO_MCU[0]);

// Busca por nombre SIN distinguir mayúsculas: quien escribe `--mcu stm32f405rg`
// en una consola no está describiendo nada, está pidiendo. Devuelve nullptr si
// no existe, y el que llama tiene que decirlo — nunca montar otro en su lugar.
inline const McuCaps* mcu_por_nombre(const std::string& n) {
    for (const McuCaps* m : CATALOGO_MCU) {
        const char* a = m->nombre;
        size_t i = 0;
        for (; i < n.size() && a[i]; ++i) {
            char x = a[i], y = n[i];
            if (x >= 'a' && x <= 'z') x = char(x - 'a' + 'A');
            if (y >= 'a' && y <= 'z') y = char(y - 'a' + 'A');
            if (x != y) break;
        }
        if (i == n.size() && a[i] == '\0') return m;
    }
    return nullptr;
}

inline std::string mcus_como_texto() {
    std::string s;
    for (const McuCaps* m : CATALOGO_MCU) {
        if (!s.empty()) s += ", ";
        s += m->nombre;
    }
    return s;
}

} // namespace stm32
#endif // STM32_TOP_MCU_CAPS_H
