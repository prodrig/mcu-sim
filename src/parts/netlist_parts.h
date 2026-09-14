// =============================================================================
// netlist_parts.h — El CATÁLOGO de piezas: qué sabe construir la factoría
//
// (Escrito en el paso 2 con los creadores puestos a mano; reescrito en el paso 3
//  sobre la factoría, que era lo previsto. `netlist.h` no se tocó.)
//
// `netlist.h` sabe de nodos, instancias y conexiones y no conoce ni una sola
// clase de pieza: es la máquina. Este fichero es la otra mitad, la que sabe que
// un `Led` se construye con un nodo y un `CanTransceiver` con dos nodos y una
// referencia a otra instancia. Tiene dos partes:
//
//   EL REGISTRO DE LA FACTORÍA. Una entrada por tipo, con la macro de
//   auto-registro. Es el único sitio del proyecto donde una cadena escrita por
//   una persona se convierte en un objeto. A un creador le da igual de dónde
//   venga la declaración: la instancia es la misma estructura la escriba un
//   ayudante de aquí abajo o la lea el lector de XML.
//
//   Cada entrada lleva además SU AYUDA, en la misma llamada: lo que responde
//   `sim --help Led`. Va ahí y no en una tabla aparte porque una tabla aparte
//   se queda vieja —se añade un parámetro al creador y nadie se acuerda de la
//   tabla— y una ayuda que miente es peor que ninguna. Aquí no hay dos sitios
//   que puedan discrepar: hay uno, y la macro no deja registrar sin él.
//
//   LOS AYUDANTES TIPADOS. Ya no construyen: declaran terminales, parámetros y
//   referencias. Siguen valiendo la pena porque el compilador comprueba lo que
//   un fichero no puede — que el terminal se llame "anodo" y no "anode".
// =============================================================================
#ifndef STM32_PARTS_NETLIST_PARTS_H
#define STM32_PARTS_NETLIST_PARTS_H

#include "netlist.h"
#include "part_factory.h"
#include "ext_parts.h"

namespace stm32 {


// Los nodos de un bus indexado: d0, d1, d2... hasta que falte uno.
inline std::vector<analog_net_if*> nets_bus(const Instancia& d, NodeMap& n,
                                            const char* pref) {
    std::vector<analog_net_if*> v;
    char t[24];
    const unsigned k = d.n_indexados(pref);
    for (unsigned i = 0; i < k; ++i) {
        std::snprintf(t, sizeof t, "%s%u", pref, i);
        v.push_back(&n[d.nodo_de(t)]);
    }
    return v;
}

inline Instancia& conecta(Instancia& i, std::initializer_list<Conexion> pines) {
    for (const Conexion& c : pines) i.pin(c.pin.c_str(), c.nodo);
    return i;
}

// ---------------------------------------------------------------------------
// EL REGISTRO DE LA FACTORÍA
//
// Aquí es donde una cadena se convierte en un objeto, y es el único sitio del
// proyecto donde eso pasa. Cada entrada recibe la instancia ya declarada -sus
// terminales, sus parámetros y sus referencias- y monta la pieza con ella.
//
// Nótese que ninguno de estos creadores sabe de dónde viene la declaración. Da
// exactamente igual que la haya escrito un ayudante tipado de más abajo o que
// venga de un fichero XML: la instancia es la misma estructura, y por eso el
// lector del paso 3 no necesitó ni una línea nueva aquí.
// ---------------------------------------------------------------------------

REGISTRA_PARTE(Led,
    Ayuda("Diodo con su resistencia en serie. NO ES LINEAL: mientras la "
          "tension aplicada no supera vf no conduce, y se modela con dos "
          "estados -conduciendo, con equivalente {vf, r}, o en corte, en alta "
          "impedancia- reevaluados cada vez que cambia la tension del pin.")
      .ejemplo("<componente tipo=\"Led\" id=\"LD4\" a_vss=\"si\" vf=\"2.0\" r=\"680\">\n"
               "  <pin nombre=\"anodo\" nodo=\"PD12\"/>\n"
               "</componente>")
      .pin("anodo o catodo", "uno de los dos",
           "La patilla que va soldada al pin. Son EL MISMO TERMINAL CON DOS "
           "NOMBRES: el que se escriba no cambia la fisica -eso lo decide "
           "a_vss-, pero permite que el fichero diga la verdad. Con a_vss=si "
           "lo que toca el pin es el anodo; con a_vss=no, el catodo. Declarar "
           "los dos es un error.")
      .atr("a_vss", "si",
           "El montaje. si: anodo al pin y catodo a masa, LUCE CON EL PIN "
           "ALTO, y conduce cuando V > vf. no: anodo a vdd y catodo al pin, "
           "LUCE CON EL PIN BAJO -el montaje de las placas Discovery y "
           "Nucleo-, y conduce cuando V < vdd - vf.")
      .atr("vdd", "3.3",
           "Tension del extremo que NO toca el pin. Solo interviene con "
           "a_vss=no; con el anodo al pin ese extremo es masa y vdd no se "
           "usa. Es lo que permite colgar el LED de una alimentacion distinta "
           "de la del MCU.")
      .atr("vf", "2.0",
           "Tension directa del diodo, en voltios. Un LED rojo ronda 1,8; uno "
           "azul o blanco, 3,0.")
      .atr("r", "330",
           "Resistencia en serie, en ohmios. Es quien fija la corriente: con "
           "vf=2,0 y r=330 salen unos 3,4 mA.")
      .nota("EL CASO DEL LED AZUL. Con vf=3,0 sobre 3,3 V no queda margen para "
            "la resistencia y el LED apenas luce; es un problema real de "
            "placa, no del modelo. La solucion de siempre es colgarlo de los "
            "5 V con el catodo al pin (a_vss=\"no\" vdd=\"5.0\" vf=\"3.0\" "
            "r=\"220\"), de modo que el MCU lo enciende poniendo el pin a "
            "cero. Cuidado con lo que este modelo NO dice: un LED en corte "
            "queda en alta impedancia, asi que con vdd=5.0 la pieza nunca "
            "presenta 5 V en el pin, y en una placa real ese pin si se iria "
            "cerca de los 5 V -por encima del maximo de un pad que no sea "
            "tolerante-. El modelo no avisa de eso.")
      .cpp("on() dice si luce y current() la corriente; `sim` los imprime al "
           "terminar: \"LED LD4 en PD12: encendido (3.11 V, 3.38 mA)\"."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        // La patilla que va al pin se puede llamar `anodo` o `catodo`, segun el
        // montaje. Es un nombre, no un cambio de fisica -eso lo decide
        // `a_vss`-, pero escribir `catodo` cuando lo que va al pin es el catodo
        // es lo que haria cualquiera con un esquematico delante.
        const bool tiene_a = !d.nodo_de("anodo").empty();
        const bool tiene_c = !d.nodo_de("catodo").empty();
        if (tiene_a && tiene_c) {
            SC_REPORT_ERROR("netlist",
                (d.id + ": un LED tiene UNA patilla en el pin; estan declaradas "
                 "'anodo' y 'catodo'").c_str());
            return nullptr;
        }
        const char* term = tiene_c ? "catodo" : "anodo";
        return new Led(d.id.c_str(), n[d.nodo_de(term)], d.si("a_vss", true),
                       d.num("vf", 2.0), d.num("r", 330.0), d.num("vdd", 3.3),
                       term);
    });

REGISTRA_PARTE(Button,
    Ayuda("Pulsador. SIN PULSAR DEJA EL PIN ABIERTO, asi que el nivel de "
          "reposo tiene que darlo alguien: el pull interno del MCU o un Rpull "
          "externo. Sin ninguno de los dos el pin queda indeterminado, que es "
          "lo que pasa en una placa real y lo que el modelo reproduce.")
      .ejemplo("<nodo id=\"PA0\" bus=\"si\"/>\n"
               "<componente tipo=\"Button\" id=\"B1\" v_cerrado=\"3.3\" r_cerrado=\"10\">\n"
               "  <pin nombre=\"pin\" nodo=\"PA0\"/>\n"
               "</componente>\n"
               "<componente tipo=\"Rpull\" id=\"R35\" v=\"0\" r=\"100000\">\n"
               "  <pin nombre=\"a\" nodo=\"PA0\"/>\n"
               "</componente>")
      .pin("pin", "obligatorio", "El pin del pulsador.")
      .atr("r_cerrado", "10",
           "Resistencia del contacto cerrado, en ohmios. Cerrado, la pieza "
           "gobierna el nodo con {v_cerrado, r_cerrado}.")
      .atr("v_cerrado", "0",
           "LA TENSION A LA QUE LLEVA EL PIN AL CERRARSE. Cero es el pulsador "
           "a masa de siempre; 3.3 es el pulsador a VDD. No todos van a masa, "
           "y la diferencia se nota en el firmware: en la STM32F4-Discovery el "
           "boton de usuario lleva PA0 a VDD y es una resistencia externa la "
           "que lo sujeta abajo, y por eso el codigo que genera STM32CubeIDE "
           "configura PA0 como EXTI por flanco de SUBIDA y sin pull interno. "
           "Descrito con un pulsador a masa ese flanco no llegaria nunca y el "
           "firmware pareceria roto sin estarlo.")
      .atr("normalmente", "abierto",
           "El REPOSO DEL CONTACTO: abierto (suelto no conduce, pulsado "
           "conduce) o cerrado (suelto CONDUCE, y pulsarlo lo ABRE). "
           "Cualquier otra palabra es un error, no un abierto silencioso.")
      .nota("normalmente=\"cerrado\" NO ES UNA RAREZA: ES LO QUE HAY EN "
            "SEGURIDAD. Un final de carrera, una seta de emergencia o un "
            "detector de puerta se cablean NC a proposito, para que un cable "
            "cortado se vea igual que una pulsacion y la maquina pare. "
            "Descrito como NA, el montaje parece funcionar hasta el dia en que "
            "se corta el cable, que es justo el dia que importa.")
      .nota("Lo que conduce no es \"pulsado\" sino \"pulsado XOR normalmente "
            "cerrado\":\n"
            "    normalmente    suelto    pulsado    desoldado\n"
            "    abierto        abierto   CIERRA     abierto\n"
            "    cerrado        CIERRA    abre       abierto\n"
            "La ultima columna es la misma para los dos, y es lo razonable: "
            "una pieza que no esta no cierra ningun contacto. Al volver a "
            "soldarla recupera su reposo, que en un NC es conduciendo.")
      .cpp("press() y release() son lo que hace el DEDO; cerrado() es lo que "
           "hace el CONTACTO, y pressed() lo que hace el dedo. En un NC son "
           "opuestos, y confundirlos es el error facil."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        // `normalmente` solo admite dos palabras, y escribir cualquier otra es
        // un error y no un "abierto" silencioso: un pulsador de seguridad
        // descrito al reves solo se nota el dia en que se corta un cable.
        const std::string rep = d.txt("normalmente", "abierto");
        if (rep != "abierto" && rep != "cerrado") {
            SC_REPORT_ERROR("netlist",
                ("Button '" + d.id + "': normalmente=\"" + rep + "\" no vale; "
                 "solo \"abierto\" (por omision) o \"cerrado\"").c_str());
            return nullptr;
        }
        return new Button(n[d.nodo_de("pin")], d.num("r_cerrado", 10.0),
                          d.num("v_cerrado", 0.0), rep == "cerrado");
    });

REGISTRA_PARTE(Crystal,
    Ayuda("Cristal de cuarzo con sus condensadores de carga. Lo que el pin "
          "OSC_IN ve del lazo oscilador es una red de polarizacion: una "
          "impedancia de 1 Mohm hacia vdd/2. Su presencia es LA CONDICION "
          "PARA QUE EL HSE O EL LSE ARRANQUEN, y quitarlo es lo que debe "
          "detectar el CSS.")
      .ejemplo("<componente tipo=\"Crystal\" id=\"X2\" vdd=\"3.3\">\n"
               "  <pin nombre=\"osc_in\" nodo=\"PH0\"/>\n"
               "</componente>")
      .pin("osc_in", "obligatorio", "PH0 para el HSE, PC14 para el LSE.")
      .atr("vdd", "3.3", "La pieza polariza el pin a vdd/2.")
      .nota("NO LLEVA LA FRECUENCIA, y no es un olvido: la del HSE es un dato "
            "del arbol de reloj y se configura en el RCC (PLLM/PLLN). El "
            "cristal solo dice que HAY un cristal. Con conectada=\"no\" se "
            "describe la placa que trae el zocalo vacio -el LSE de la "
            "Discovery, sin ir mas lejos-: el oscilador no arranca nunca, "
            "LSERDY se queda a cero y un firmware que lo espere se cuelga, "
            "exactamente igual que con la tarjeta en la mano.")
      .nota("Cristal y oscilador externo no son la misma pieza: si la senal la "
            "trae otro chip por el pin -el MCO de un ST-LINK, por ejemplo- lo "
            "que va es un ExtClock y HSEBYP en el RCC, no un Crystal."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Crystal(n[d.nodo_de("osc_in")], d.num("vdd", 3.3));
    });

REGISTRA_PARTE(Rpull,
    Ayuda("Una rama resistiva entre el nodo y una tension fija. Es el "
          "equivalente Thevenin {v, r} y nada mas, asi que sirve para el "
          "pull-up y el pull-down de la placa, para polarizar una entrada y "
          "para cargar un pad y medir cuanta corriente entrega. CONDUCE "
          "SIEMPRE DESDE QUE SE CONSTRUYE: es el unico componente cuyo efecto "
          "no depende de ningun proceso, y por eso es la forma mas directa de "
          "sujetar un nodo que si no quedaria flotante.")
      .ejemplo("<componente tipo=\"Rpull\" id=\"R35\" v=\"0\" r=\"100000\">\n"
               "  <pin nombre=\"a\" nodo=\"PA0\"/>\n"
               "</componente>")
      .pin("a", "obligatorio", "El nodo al que se conecta.")
      .atr("v", "3.3",
           "La tension del otro extremo. ES UNA TENSION CUALQUIERA, no una "
           "eleccion entre VDD y masa, y no tiene por que existir en el MCU: "
           "3.3 es el pull-up al mismo rail que el chip, 5 el pull-up a un "
           "rail de 5 V como el de un bus I2C mixto, 0 un pull-down y 1.8 una "
           "polarizacion a media escala para una entrada de ADC.")
      .atr("r", "10000", "El valor de la resistencia, en ohmios.")
      .nota("Por que no se llama PullUp: lo que hace es una rama resistiva a "
            "un potencial, y de ahi salen tanto los pulls como las cargas de "
            "prueba -v=\"0\" r=\"1000\" es una carga de 1 kohm con la que "
            "medir la corriente que da un pad-."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        // `v` es una tension cualquiera, no una eleccion entre VDD y masa: 5 V
        // para un pull-up a un rail de 5 V, 1,8 para polarizar una entrada, 0
        // para un pull-down.
        return new Rpull(n[d.nodo_de("a")], d.num("v", 3.3), d.num("r", 10e3));
    });

REGISTRA_PARTE(Driver,
    Ayuda("Salida digital externa generica: otro chip de la placa gobernando "
          "ese pin. NACE EN ALTA IMPEDANCIA y solo conduce cuando se le manda "
          "desde C++. Sirve para dos cosas: dar un nivel a una entrada del "
          "MCU, y provocar a proposito el conflicto con una salida push-pull "
          "para ver la sobrecorriente en el pad.")
      .ejemplo("<componente tipo=\"Driver\" id=\"U3\" vdd=\"3.3\" r_out=\"25\">\n"
               "  <pin nombre=\"pin\" nodo=\"PB6\"/>\n"
               "</componente>")
      .pin("pin", "obligatorio", "El pin que gobierna.")
      .atr("vdd", "3.3", "La tension de su nivel alto.")
      .atr("r_out", "25",
           "Su impedancia de salida, en ohmios. Bajarla lo hace mas fuerte en "
           "un conflicto.")
      .cpp("set(nivel) para un cero o un uno, set_volts(v, r) para una tension "
           "cualquiera con su resistencia, y hiz() para soltarlo."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Driver(n[d.nodo_de("pin")], d.num("vdd", 3.3), d.num("r_out", 25.0));
    });

REGISTRA_PARTE(SignalLink,
    Ayuda("Una pista de placa entre dos pines: lo que hay entre el TX de un "
          "puerto serie y el RX del otro. Lee la tension del origen, la "
          "digitaliza CON EL MISMO UMBRAL DE HISTERESIS QUE UN PAD (sube a "
          "0,55*VDD, baja a 0,45*VDD) y la reproduce en el destino con 50 "
          "ohmios.")
      .ejemplo("<componente tipo=\"SignalLink\" id=\"pista1\">\n"
               "  <pin nombre=\"origen\"  nodo=\"u0.PD12\"/>\n"
               "  <pin nombre=\"destino\" nodo=\"u1.PB4\"/>\n"
               "</componente>")
      .pin("origen", "obligatorio",
           "Solo se lee: no lleva driver, y en el netlist sale marcado como "
           "pasivo.")
      .pin("destino", "obligatorio", "El que la pista gobierna.")
      .nota("ES UNIDIRECCIONAL POR CONSTRUCCION, que es lo que hace falta en "
            "un enlace full-duplex donde cada hilo tiene un unico emisor. Un "
            "hilo compartido de verdad -medio duplex, colector abierto- no se "
            "modela con esta pieza, sino conectando los dos pines al mismo "
            "nodo con <nodo une=\"PB9 PD3\">. Frente a ese nodo compartido la "
            "pista tiene dos ventajas y una carencia: se puede desoldar en "
            "marcha (conectada=\"no\") y no carga el origen, pero el conflicto "
            "entre los dos extremos es invisible.")
      .nota("Sirve igual entre dos pines del MISMO MCU: llevar la salida PWM "
            "de un temporizador a la entrada de captura de otro es como se "
            "mide con un temporizador lo que genera el de al lado.")
      .nota("Sin atributos: 3,3 V y 50 ohmios fijos."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new SignalLink(d.id.c_str(), n[d.nodo_de("origen")],
                                            n[d.nodo_de("destino")]);
    });

REGISTRA_PARTE(ExtClock,
    Ayuda("Oscilador de encapsulado o generador de senal: una onda cuadrada "
          "de 0 a 3,3 V con 50 ohmios de salida. Es la pieza que va cuando la "
          "senal de reloj la trae otro chip por el pin -el MCO de un ST-LINK "
          "alimentando el HSE en modo BYPASS, por ejemplo-, frente al Crystal, "
          "que es el cuarzo del lazo oscilador del propio MCU.")
      .ejemplo("<componente tipo=\"ExtClock\" id=\"OSC1\" hz=\"8e6\">\n"
               "  <pin nombre=\"out\" nodo=\"PH0\"/>\n"
               "</componente>")
      .pin("out", "obligatorio", "El pin al que entrega la onda.")
      .atr("hz", "0",
           "Frecuencia en hercios. CON 0 LA PIEZA NO CONDUCE: queda en alta "
           "impedancia y no gasta tiempo de simulacion, que es lo razonable al "
           "arrancar; se enciende luego desde C++.")
      .nota("CUIDADO CON LAS UNIDADES: hz=\"8M\" son OCHO HERCIOS, no ocho "
            "megahercios, y la placa se monta sin quejarse. Escribase 8e6. Es "
            "el fallo silencioso mas caro de este formato.")
      .cpp("set_freq(hz) enciende, apaga (con 0) y cambia la frecuencia en "
           "marcha."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new ExtClock(d.id.c_str(), n[d.nodo_de("out")], d.num("hz", 0.0));
    });

REGISTRA_PARTE(I2cWire,
    Ayuda("El hilo de un bus I2C: CORTOCIRCUITA ENTRE SI N PINES y les pone un "
          "pull-up comun. Es de colector abierto -nadie fuerza un uno, solo se "
          "tira de la linea a cero (30 ohmios) o se suelta- y todo eso ocurre "
          "de verdad en el nodo. Es lo que permite que el I2C1 del MCU y el "
          "I2C3 hablen por el mismo bus, o que una EEPROM este colgada de los "
          "mismos hilos que el maestro.")
      .ejemplo("<componente tipo=\"I2cWire\" id=\"scl_bus\" r_pull=\"4700\">\n"
               "  <pin nombre=\"l0\" nodo=\"PB6\"/>\n"
               "  <pin nombre=\"l1\" nodo=\"PA8\"/>\n"
               "</componente>")
      .pin("l0, l1, ...", "al menos uno",
           "Los pines que quedan unidos. Es un BUS INDEXADO: se cuenta desde "
           "cero y sin huecos, asi que declarar l0, l1 y l3 da un hilo de DOS "
           "lineas, no de cuatro.")
      .atr("r_pull", "4700",
           "El pull-up, en ohmios. Subirlo hace la linea mas lenta al "
           "soltarla, que es el efecto real de un pull-up flojo.")
      .nota("HACE FALTA UNA INSTANCIA POR LINEA: una para SCL y otra para SDA. "
            "Y el nodo de cada linea querra bus=\"si\", porque por definicion "
            "tendra varios conductores."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new I2cWire(d.id.c_str(), nets_bus(d, n, "l"), 3.3,
                           d.num("r_pull", 4700.0));
    });

REGISTRA_PARTE(I2cEeprom,
    Ayuda("Una 24Cxx colgada de los pines: 64 bytes, direccionamiento de un "
          "byte de puntero, escritura y lectura secuenciales, y su bit de "
          "reconocimiento. Habla el protocolo de verdad sobre el nodo, no una "
          "simplificacion por encima.")
      .ejemplo("<componente tipo=\"I2cEeprom\" id=\"U5\" dir=\"0x50\">\n"
               "  <pin nombre=\"scl\" nodo=\"PB6\"/>\n"
               "  <pin nombre=\"sda\" nodo=\"PB7\"/>\n"
               "</componente>")
      .pin("scl", "obligatorio", "El reloj del bus.")
      .pin("sda", "obligatorio", "Los datos del bus, bidireccional.")
      .atr("dir", "0x50 (80)",
           "Direccion de esclavo de 7 bits. Se puede escribir en hexadecimal "
           "(0x50) o en decimal (80).")
      .cpp("peek/poke para ver y poner memoria, set_ack(false) para provocar "
           "un NACK y set_stretch_us() para que retenga el reloj."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new I2cEeprom(d.id.c_str(), n[d.nodo_de("scl")], n[d.nodo_de("sda")],
                             uint8_t(d.num("dir", 0x50)));
    });

REGISTRA_PARTE(I2cExtMaster,
    Ayuda("Otro microcontrolador en la misma placa, hablando como MAESTRO del "
          "bus. Sirve para dos cosas que sin el no se pueden probar: el MCU "
          "como ESCLAVO, y la PERDIDA DE ARBITRAJE cuando los dos arrancan a "
          "la vez.")
      .ejemplo("<componente tipo=\"I2cExtMaster\" id=\"M2\" f_scl=\"100e3\">\n"
               "  <pin nombre=\"scl\" nodo=\"PB6\"/>\n"
               "  <pin nombre=\"sda\" nodo=\"PB7\"/>\n"
               "</componente>")
      .pin("scl", "obligatorio", "El reloj, que pone el maestro.")
      .pin("sda", "obligatorio", "Los datos, bidireccional.")
      .atr("f_scl", "100000",
           "Frecuencia de reloj del bus, en Hz. Respeta el estiramiento del "
           "esclavo.")
      .cpp("request_write y request_read piden las transacciones."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new I2cExtMaster(d.id.c_str(), n[d.nodo_de("scl")],
                                n[d.nodo_de("sda")], d.num("f_scl", 100e3));
    });

REGISTRA_PARTE(SwoReceiver,
    Ayuda("Analizador de traza colgado del pin SWO. SOLO ESCUCHA, NUNCA "
          "CONDUCE. En modo NRZ el pin es una linea serie asincrona "
          "corriente, asi que esto es un receptor de UART con el "
          "desempaquetado del protocolo ITM encima.")
      .ejemplo("<componente tipo=\"SwoReceiver\" id=\"TRAZA\" bitrate=\"2000000\">\n"
               "  <pin nombre=\"swo\" nodo=\"PB3\"/>\n"
               "</componente>")
      .pin("swo", "obligatorio", "Normalmente PB3.")
      .atr("bitrate", "1000000",
           "Velocidad en bits por segundo. TIENE QUE COINCIDIR CON LA QUE "
           "PROGRAME EL TPIU o la trama se desincroniza.")
      .nota("Capturar el SWO es una de las cosas que el simulador deja hacer y "
            "la STM32F4-Discovery real no: en esa tarjeta PB3 no va al "
            "ST-LINK, sale a los conectores de expansion.")
      .cpp("bytes(), mensajes() y texto(puerto) -que es el printf del ITM-."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new SwoReceiver(d.id.c_str(), n[d.nodo_de("swo")],
                               d.num("bitrate", 1e6));
    });

REGISTRA_PARTE(SdCard,
    Ayuda("Una tarjeta SD v2.0 A NIVEL DE PIN: habla el protocolo de verdad, "
          "bit a bit, con su CRC7 en los comandos y su CRC16 por cada linea de "
          "datos. No entiende de registros del MCU; solo ve CK, CMD y D0-D3. "
          "Implementa el arranque completo: CMD0, CMD8, CMD55, ACMD41, CMD2, "
          "CMD3, CMD9, CMD7, CMD16, ACMD6 -que es lo que pone el bus a cuatro "
          "hilos-, CMD17 y CMD24.")
      .ejemplo("<componente tipo=\"SdCard\" id=\"SD1\">\n"
               "  <pin nombre=\"ck\"   nodo=\"PC12\"/>\n"
               "  <pin nombre=\"cmd\"  nodo=\"PD2\"/>\n"
               "  <pin nombre=\"dat0\" nodo=\"PC8\"/>\n"
               "</componente>")
      .pin("ck", "obligatorio", "El reloj, que pone el host. Pasivo.")
      .pin("cmd", "obligatorio",
           "Bidireccional. Lleva el pull-up de 47 kohm del zocalo.")
      .pin("dat0", "recomendado",
           "Con solo dat0 la tarjeta funciona en modo de un hilo.")
      .pin("dat1, dat2, dat3", "opcionales",
           "Hacen falta para el bus de cuatro hilos. Cada uno con su pull-up "
           "de 47 kohm.")
      .nota("Sin atributos en el XML. conectada=\"no\" es el zocalo vacio, y "
            "SE VAN CON ELLA LOS PULL-UPS, que es precisamente lo que "
            "distingue un zocalo vacio de uno ocupado.")
      .cpp("peek/poke, commands(), bus_width(), y tres averias a proposito "
           "-break_resp_crc, break_data_crc y set_mute- para comprobar que el "
           "host levanta CCRCFAIL, DCRCFAIL y CTIMEOUT de verdad."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        auto opt = [&](const char* t) -> analog_net_if* {
            const std::string& s = d.nodo_de(t);
            return s.empty() ? nullptr : &n[s];
        };
        return new SdCard(d.id.c_str(), n[d.nodo_de("ck")], n[d.nodo_de("cmd")],
                          opt("dat0"), opt("dat1"), opt("dat2"), opt("dat3"));
    });

REGISTRA_PARTE(CameraSensor,
    Ayuda("Un sensor CMOS de los de una placa con OV7670 o MT9V034. Genera "
          "PIXCLK, conduce los datos EN EL FLANCO CONTRARIO AL QUE MUESTREA EL "
          "DCMI -asi se cumple el tiempo de establecimiento- y marca los "
          "bordes con VSYNC y HSYNC, o sin ellos, metiendo los codigos de "
          "sincronismo en el propio flujo (BT.656). Su rasgo esencial: NO SE "
          "PUEDE PARAR. Si el DCMI no vacia su FIFO a tiempo, los datos se "
          "pierden; un modelo de sensor que esperase seria un modelo inutil.")
      .ejemplo("<componente tipo=\"CameraSensor\" id=\"CAM\">\n"
               "  <pin nombre=\"pixclk\" nodo=\"PA6\"/>\n"
               "  <pin nombre=\"hsync\"  nodo=\"PA4\"/>\n"
               "  <pin nombre=\"vsync\"  nodo=\"PB7\"/>\n"
               "  <pin nombre=\"d0\"     nodo=\"PC6\"/>  <!-- d1, d2, ... -->\n"
               "</componente>")
      .pin("pixclk", "obligatorio", "El reloj de pixel, que pone el sensor.")
      .pin("hsync, vsync", "obligatorios", "Los sincronismos.")
      .pin("d0 ... dN", "al menos uno",
           "Los hilos de datos, bus indexado sin huecos. LOS QUE EL "
           "ENCAPSULADO NO SACA SENCILLAMENTE NO SE DECLARAN, y el DCMI leera "
           "lo que haya en unas entradas que nadie gobierna, que es justo lo "
           "que pasa en la placa.")
      .nota("Sin atributos en el XML. Por omision: 16x8 pixeles de 8 bits a "
            "6 MHz.")
      .cpp("set_formato, set_pixclk, las polaridades, el blanking, el "
           "sincronismo embebido y el patron; los cuadros se piden con "
           "emitir(n)."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new CameraSensor(d.id.c_str(), n[d.nodo_de("pixclk")],
                                n[d.nodo_de("hsync")], n[d.nodo_de("vsync")],
                                nets_bus(d, n, "d"));
    });

REGISTRA_PARTE(ExtSram,
    Ayuda("SRAM asincrona de 64 KB en el bus externo (FSMC). No tiene reloj y "
          "no negocia nada: obedece. Comparte los hilos de datos y varias "
          "senales de control con la ExtNand, y EN ESTE ENCAPSULADO COMPARTE "
          "TAMBIEN EL CHIP SELECT (PD7 es NE1 y NCE2 a la vez), asi que no "
          "pueden estar puestas las dos: una de ellas ira con "
          "conectada=\"no\".")
      .ejemplo("<componente tipo=\"ExtSram\" id=\"U7\">\n"
               "  <pin nombre=\"d0\"  nodo=\"PD14\"/>  <!-- d1 ... d15 -->\n"
               "  <pin nombre=\"ne\"  nodo=\"PD7\"/>\n"
               "  <pin nombre=\"noe\" nodo=\"PD4\"/>\n"
               "  <pin nombre=\"nwe\" nodo=\"PD5\"/>\n"
               "  <pin nombre=\"nl\"  nodo=\"PB7\"/>\n"
               "</componente>")
      .pin("d0 ... d15", "obligatorios",
           "Los hilos de datos. Bidireccionales: la memoria conduce al leer y "
           "escucha al escribir.")
      .pin("a16 ... a23", "opcionales",
           "Las direcciones altas que el encapsulado saca. EMPIEZAN EN 16 A "
           "PROPOSITO: sin A0-A15 este chip solo puede usar el bus "
           "multiplexado, y la parte baja de la direccion la engancha de los "
           "propios hilos de datos con NL.")
      .pin("ne", "obligatorio", "Chip select, activo a cero. Pasivo.")
      .pin("noe", "obligatorio", "Output enable. Pasivo.")
      .pin("nwe", "obligatorio", "Write enable; GUARDA EN SU FLANCO DE SUBIDA.")
      .pin("nl", "obligatorio",
           "Address latch; engancha la direccion baja en su flanco de subida.")
      .pin("nbl0, nbl1", "opcionales", "Byte lanes: cual de los dos bytes se escribe.")
      .pin("nwait", "opcional",
           "Si se conecta, la memoria puede pedir tiempo. Es lo unico del bus "
           "externo que no decide el controlador.")
      .nota("Sin atributos en el XML. Por omision, 16 bits y multiplexado.")
      .cpp("set_ancho() y set_mux() ajustan el bus; lee/escribe ven el "
           "contenido."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        auto opt = [&](const char* t) -> analog_net_if* {
            const std::string& s = d.nodo_de(t);
            return s.empty() ? nullptr : &n[s];
        };
        // Las direcciones empiezan en A16: es lo unico que este encapsulado
        // saca, y por eso el bus multiplexado no es opcional aqui.
        std::vector<analog_net_if*> a;
        char t[8];
        for (unsigned k = 16; ; ++k) {
            std::snprintf(t, sizeof t, "a%u", k);
            if (d.nodo_de(t).empty()) break;
            a.push_back(&n[d.nodo_de(t)]);
        }
        return new ExtSram(d.id.c_str(), nets_bus(d, n, "d"), a,
                           n[d.nodo_de("ne")], n[d.nodo_de("noe")],
                           n[d.nodo_de("nwe")], n[d.nodo_de("nl")],
                           opt("nbl0"), opt("nbl1"), opt("nwait"));
    });

REGISTRA_PARTE(ExtNand,
    Ayuda("NAND flash de 16 paginas de 512 bytes en el bus externo (FSMC). Una "
          "NAND NO TIENE BUS DE DIRECCIONES: tiene ocho hilos por los que van "
          "mandatos, direcciones y datos, y dos senales -CLE y ALE- que dicen "
          "cual de las tres cosas viaja en cada ciclo. El FSMC saca CLE y ALE "
          "por A16 y A17, de modo que ESCRIBIR EN UNA DIRECCION U OTRA DEL "
          "BANCO es lo que elige el tipo de ciclo. Entiende leer "
          "identificacion (0x90), leer pagina (0x00...0x30), programar "
          "(0x80...0x10) y leer estado (0x70).")
      .ejemplo("<componente tipo=\"ExtNand\" id=\"U8\" conectada=\"no\">\n"
               "  <pin nombre=\"d0\"  nodo=\"PD14\"/>  <!-- d1 ... d7 -->\n"
               "  <pin nombre=\"cle\" nodo=\"PD11\"/>\n"
               "  <pin nombre=\"ale\" nodo=\"PD12\"/>\n"
               "  <pin nombre=\"nce\" nodo=\"PD7\"/>\n"
               "</componente>")
      .pin("d0 ... d7", "obligatorios", "Los ocho hilos, compartidos con la SRAM.")
      .pin("cle", "obligatorio", "Command latch enable (A16). Pasivo.")
      .pin("ale", "obligatorio", "Address latch enable (A17). Pasivo.")
      .pin("nce", "obligatorio", "Chip enable. Pasivo.")
      .pin("noe, nwe", "obligatorios", "Output y write enable. Pasivos.")
      .pin("rb", "opcional", "Ready/Busy. Si se conecta, la NAND lo gobierna.")
      .nota("Sin atributos en el XML. Comparte el chip select con la ExtSram "
            "en este encapsulado, asi que las dos no pueden estar puestas a la "
            "vez."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        const std::string& rb = d.nodo_de("rb");
        return new ExtNand(d.id.c_str(), nets_bus(d, n, "d"),
                           n[d.nodo_de("cle")], n[d.nodo_de("ale")],
                           n[d.nodo_de("nce")], n[d.nodo_de("noe")],
                           n[d.nodo_de("nwe")], rb.empty() ? nullptr : &n[rb]);
    });

REGISTRA_PARTE(UsbHostRig,
    Ayuda("Un PC al otro lado del cable USB: da los 5 V de VBUS, pone los dos "
          "15 kohm a masa -que es lo que convierte a este extremo en "
          "anfitrion- y hace el reset de bus con un SE0 largo. TODO LO "
          "ELECTRICO VA POR LOS NODOS con tensiones de verdad: la conexion, la "
          "velocidad y el reset salen del divisor resistivo, no de una "
          "variable booleana. Los paquetes, en cambio, cruzan como paquetes.")
      .ejemplo("<componente tipo=\"UsbHostRig\" id=\"PC\" conectada=\"no\">\n"
               "  <pin nombre=\"dm\"   nodo=\"PA11\"/>\n"
               "  <pin nombre=\"dp\"   nodo=\"PA12\"/>\n"
               "  <pin nombre=\"vbus\" nodo=\"PA9\"/>\n"
               "  <pin nombre=\"id\"   nodo=\"PA10\"/>\n"
               "</componente>")
      .pin("dm, dp", "obligatorios", "El par diferencial.")
      .pin("vbus", "obligatorio", "Los 5 V.")
      .pin("id", "obligatorio",
           "A masa = cable A = el que lo tiene enchufado es el anfitrion.")
      .nota("Sin atributos. conectada=\"no\" es EL CABLE DESENCHUFADO, y es lo "
            "razonable al arrancar.")
      .cpp("set_vbus, reset_bus, resume, setup/in/out, sofs -el latido de "
           "1 ms, sin el cual todo dispositivo se suspende- y "
           "conectar_dispositivo para engancharlo al OTG del MCU."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new UsbHostRig(d.id.c_str(), n[d.nodo_de("dm")], n[d.nodo_de("dp")],
                              n[d.nodo_de("vbus")], n[d.nodo_de("id")]);
    });

REGISTRA_PARTE(UsbDeviceRig,
    Ayuda("Un pendrive al otro lado del cable: pone su 1,5 kohm en D+ cuando "
          "lo enchufan y SE ENTERA DEL RESET PORQUE LO VE EN EL CABLE, no "
          "porque nadie se lo diga.")
      .ejemplo("<componente tipo=\"UsbDeviceRig\" id=\"PEN\" conectada=\"no\">\n"
               "  <pin nombre=\"dm\"   nodo=\"PB14\"/>\n"
               "  <pin nombre=\"dp\"   nodo=\"PB15\"/>\n"
               "  <pin nombre=\"vbus\" nodo=\"PB13\"/>\n"
               "</componente>")
      .pin("dm, dp", "obligatorios", "El par diferencial.")
      .pin("vbus", "obligatorio",
           "El interruptor de 5 V de la placa. NO LO DA EL MCU -el pin de VBUS "
           "es una entrada de sensado-, lo da un conmutador externo.")
      .nota("Sin atributos. El 1,5 kohm es LA DECLARACION DE EXISTENCIA, y en "
            "cual de los dos hilos se pone es lo que dice la velocidad: D+ "
            "para Full Speed, D- para Low Speed.")
      .cpp("enchufar, alimentacion_placa, direccion, resets y "
           "set_baja_velocidad."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new UsbDeviceRig(d.id.c_str(), n[d.nodo_de("dm")],
                                n[d.nodo_de("dp")], n[d.nodo_de("vbus")]);
    });

REGISTRA_PARTE(EthPhy,
    Ayuda("El integrado que va entre el MAC y el conector RJ45. Hace tres "
          "cosas que el MAC no puede hacer solo: PONE LOS RELOJES del camino "
          "de datos -el MAC no genera nada, los sigue-, habla MDIO bit a bit "
          "con los registros de la norma, y hace de buzon de tramas con su "
          "CRC-32. Dieciocho terminales para MII; de ellos, nueve son los de "
          "RMII.")
      .ejemplo("<componente tipo=\"EthPhy\" id=\"PHY\">\n"
               "  <pin nombre=\"mdc\"  nodo=\"PC1\"/>\n"
               "  <pin nombre=\"mdio\" nodo=\"PA2\"/>\n"
               "  <pin nombre=\"txd0\" nodo=\"PB12\"/>  <!-- ... -->\n"
               "</componente>")
      .pin("mdc", "obligatorio", "Reloj del MDIO, que pone el MAC. Pasivo.")
      .pin("mdio", "obligatorio",
           "Datos del MDIO, bidireccional. LLEVA EL PULL-UP DE 10 kohm DE LA "
           "PLACA, sin el cual preguntar a una direccion donde no hay nadie "
           "devolveria basura en vez de todo unos.")
      .pin("tx_clk", "obligatorio", "25 MHz en MII a 100 Mbit/s.")
      .pin("rx_clk", "obligatorio",
           "En RMII es el REF_CLK de 50 MHz que sirve para los dos sentidos.")
      .pin("tx_en", "obligatorio", "Lo gobierna el MAC. Pasivo.")
      .pin("txd0 ... txd3", "segun el modo",
           "Los gobierna el MAC. Pasivos. Dos en RMII, cuatro en MII.")
      .pin("rxd0 ... rxd3", "segun el modo", "Los gobierna el PHY.")
      .pin("rx_dv", "obligatorio", "RX_DV en MII, CRS_DV en RMII.")
      .pin("rx_er, crs, col", "obligatorios",
           "Solo MII, pero se declaran igual.")
      .nota("CUIDADO CON LOS PINES: en el LQFP100 estan los dieciocho, pero "
            "TODOS ESTAN COGIDOS. MII_CRS es PA0-WKUP, asi que con MII "
            "cableado el MCU pierde el pin de despertar desde Standby; "
            "MII_RXD0/1 son PC4/PC5 = ADC12_IN14/15, y MII_COL es PA3 = "
            "OTG_HS_ULPI_D0.")
      .nota("Sin atributos en el XML.")
      .cpp("set_rmii (modo), set_cien (velocidad), set_enlace (estado del "
           "enlace) y set_dir (direccion de MDIO, 0 por omision); enviar() "
           "inyecta tramas y recibidas() ve lo capturado."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new EthPhy(d.id.c_str(), n[d.nodo_de("mdc")], n[d.nodo_de("mdio")],
                          n[d.nodo_de("tx_clk")], n[d.nodo_de("rx_clk")],
                          n[d.nodo_de("tx_en")], nets_bus(d, n, "txd"),
                          nets_bus(d, n, "rxd"), n[d.nodo_de("rx_dv")],
                          n[d.nodo_de("rx_er")], n[d.nodo_de("crs")],
                          n[d.nodo_de("col")]);
    });

REGISTRA_PARTE(CanWire,
    Ayuda("El hilo de un bus CAN, con su terminador. UN BUS CAN NO ES UNA "
          "SENAL: ES UN CABLE EN Y. El estado dominante gana al recesivo "
          "porque un cero de 20 ohmios gana a un terminador de 1 kohm, y de "
          "ahi -no de un &&- salen el arbitraje y el asentimiento. Recesivo = "
          "alto. Un bus se monta con tres piezas y EL ORDEN IMPORTA: el hilo "
          "primero, porque los otros dos lo refieren.")
      .ejemplo("<nodo id=\"n_can\" externo=\"si\" bus=\"si\"/>\n"
               "<componente tipo=\"CanWire\" id=\"bus1\" r_term=\"120\">\n"
               "  <pin nombre=\"bus\" nodo=\"n_can\"/>\n"
               "</componente>")
      .pin("bus", "obligatorio",
           "El nodo del hilo. Casi siempre un nodo externo=\"si\" y "
           "bus=\"si\", porque no es un pin de ningun MCU y tiene varios "
           "conductores por definicion.")
      .atr("vdd", "3.3",
           "Nivel recesivo, y umbral: dominante es por debajo de vdd/2.")
      .atr("r_term", "1000",
           "El terminador, en ohmios. Un bus real lleva 120. conectada=\"no\" "
           "es desenchufar el cable, y DEJA EL HILO FLOTANDO."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        // El nodo lo ha creado ya el netlist: el hilo solo pone su terminador
        // encima. Esa es la diferencia con el constructor histórico, y es lo
        // que permite que otro componente se refiera al mismo nodo por nombre.
        return new CanWire(n[d.nodo_de("bus")], d.num("vdd", 3.3),
                           d.num("r_term", 1000.0), d.id.c_str());
    });

REGISTRA_PARTE(CanTransceiver,
    Ayuda("El chip que va al lado del microcontrolador: convierte los dos "
          "pines digitales en el estado del hilo y al reves. Necesita EL "
          "OBJETO del hilo -para leer si el bus esta dominante y a que "
          "tension-, no solo el punto electrico, y por eso ademas del terminal "
          "bus lleva una referencia.")
      .ejemplo("<componente tipo=\"CanTransceiver\" id=\"U2\">\n"
               "  <pin nombre=\"txd\" nodo=\"PD1\"/>\n"
               "  <pin nombre=\"rxd\" nodo=\"PD0\"/>\n"
               "  <pin nombre=\"bus\" nodo=\"n_can\"/>\n"
               "  <ref nombre=\"hilo\" componente=\"bus1\"/>\n"
               "</componente>")
      .pin("txd", "obligatorio",
           "El pin CAN_TX del MCU. LLEVA PULL-UP DE 10 kohm, como los chips "
           "reales: sin el, un TXD flotante -el MCU aun sin configurar- "
           "atascaria el bus entero en dominante.")
      .pin("rxd", "obligatorio",
           "El pin CAN_RX. El transceptor lo gobierna push-pull a 50 ohmios.")
      .pin("bus", "recomendado",
           "El nodo del hilo. La construccion no lo usa -para eso esta la "
           "referencia-, pero SIN EL EL NETLIST QUEDA INCOMPLETO y la "
           "comprobacion de ida y vuelta lo detecta.")
      .ref("hilo",
           "El id del CanWire. TIENE QUE ESTAR DECLARADO ANTES: los "
           "componentes se construyen en el orden del fichero, y una "
           "referencia hacia delante se rechaza al validar.")
      .atr("vdd", "3.3", "Tension de la logica del transceptor.")
      .nota("TIENE DOS INTERRUPTORES DISTINTOS, y conviene no confundirlos: "
            "conectada=\"no\" es no soldarlo a la placa; set_standby(false) "
            "desde C++ es el modo de bajo consumo, en el que deja de gobernar "
            "el hilo pero sigue escuchando."),
    [](const Instancia& d, NodeMap& n, Netlist& red) -> ExtPartBase* {
        CanWire* w = red.como<CanWire>(d.ref_de("hilo"));
        if (!w) {
            SC_REPORT_ERROR("netlist",
                (d.id + ": el hilo CAN '" + d.ref_de("hilo") +
                 "' no existe o todavia no se ha construido").c_str());
            return nullptr;
        }
        return new CanTransceiver(d.id.c_str(), n[d.nodo_de("txd")],
                                  n[d.nodo_de("rxd")], *w, d.num("vdd", 3.3));
    });

REGISTRA_PARTE(CanNode,
    Ayuda("Otro controlador colgado del mismo hilo, que habla el protocolo de "
          "verdad -relleno de bits, CRC15, asentimiento- con las mismas "
          "funciones que el periferico del MCU. NO TOCA NINGUN PIN DEL MCU: "
          "todos sus terminales son del nodo externo, que es algo que un "
          "netlist tiene que poder describir porque hay componentes de placa "
          "que el MCU ni ve. SIN EL NO SE PUEDE PROBAR CASI NADA: sin nadie "
          "que asienta, un bus CAN no entrega nada.")
      .ejemplo("<componente tipo=\"CanNode\" id=\"otro\" bitrate=\"500e3\">\n"
               "  <pin nombre=\"bus\" nodo=\"n_can\"/>\n"
               "  <ref nombre=\"hilo\" componente=\"bus1\"/>\n"
               "</componente>")
      .pin("bus", "obligatorio", "El nodo del hilo.")
      .ref("hilo", "El id del CanWire, declarado antes.")
      .atr("bitrate", "500000",
           "Velocidad en bits por segundo. TIENE QUE COINCIDIR CON LA DEL "
           "bxCAN o no habra mas que errores de bit.")
      .cpp("send(), received(), last(), y set_ack(false) para dejar de asentir "
           "y provocar el error de reconocimiento."),
    [](const Instancia& d, NodeMap&, Netlist& red) -> ExtPartBase* {
        CanWire* w = red.como<CanWire>(d.ref_de("hilo"));
        if (!w) {
            SC_REPORT_ERROR("netlist",
                (d.id + ": el hilo CAN '" + d.ref_de("hilo") +
                 "' no existe o todavia no se ha construido").c_str());
            return nullptr;
        }
        return new CanNode(d.id.c_str(), *w, d.num("bitrate", 500e3));
    });

// ---------------------------------------------------------------------------
// LOS AYUDANTES TIPADOS
//
// Ya no construyen nada: DECLARAN. Terminales con su nombre, parámetros y
// referencias, y `Netlist::add` le pone el creador consultando la factoría.
// Siguen valiendo la pena porque el compilador comprueba lo que un XML no
// puede: que el terminal se llame "anodo" y no "anode", y que los parámetros
// que la pieza necesita estén.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Piezas de una patilla
// ---------------------------------------------------------------------------

// Un LED con su resistencia en serie. `a_vss` distingue el montaje: ánodo al
// pin y cátodo a masa (se enciende en alto), o ánodo a `vdd` y cátodo al pin
// (se enciende en bajo, que es lo que hacen las placas de evaluación de ST).
//
// `vdd` es la tensión del extremo que NO toca el pin, y solo interviene en el
// segundo montaje. Vale 3,3 por omisión, pero no tiene por qué ser la del MCU:
// un LED azul con vf = 3,0 no luce con 3,3 V y se cuelga de los 5 V.
inline Instancia& led(Netlist& nl, const char* id, const std::string& nodo,
                      bool a_vss = true, double vf = 2.0, double r = 330.0,
                      double vdd = 3.3) {
    Instancia& i = nl.add("Led", id);
    i.pin(a_vss ? "anodo" : "catodo", nodo)
     .par("a_vss", a_vss ? "si" : "no").par("vf", vf).par("r", r).par("vdd", vdd);
    return i;
}

inline Instancia& pulsador(Netlist& nl, const char* id, const std::string& nodo,
                           double r_cerrado = 10.0, double v_cerrado = 0.0,
                           bool normalmente_cerrado = false) {
    Instancia& i = nl.add("Button", id);
    i.pin("pin", nodo).par("r_cerrado", r_cerrado);
    if (v_cerrado != 0.0) i.par("v_cerrado", v_cerrado);
    if (normalmente_cerrado) i.par("normalmente", "cerrado");
    return i;
}

inline Instancia& cristal(Netlist& nl, const char* id, const std::string& nodo,
                          double vdd = 3.3) {
    Instancia& i = nl.add("Crystal", id);
    i.pin("osc_in", nodo).par("vdd", vdd);
    return i;
}

// Una rama resistiva del nodo a una tensión fija. `a_voltios` es una tensión
// cualquiera, no una elección entre VDD y masa: 5 para un pull-up a un raíl de
// 5 V, 1.8 para polarizar una entrada, 0 para un pull-down.
inline Instancia& rpull(Netlist& nl, const char* id, const std::string& nodo,
                        double a_voltios = 3.3, double ohmios = 10e3) {
    Instancia& i = nl.add("Rpull", id);
    i.pin("a", nodo).par("v", a_voltios).par("r", ohmios);
    return i;
}

inline Instancia& driver(Netlist& nl, const char* id, const std::string& nodo,
                         double vdd = 3.3, double r_out = 25.0) {
    Instancia& i = nl.add("Driver", id);
    i.pin("pin", nodo).par("vdd", vdd).par("r_out", r_out);
    return i;
}

// Una pista de placa entre dos pines. El origen solo se lee: por eso en el
// netlist sale marcado `pasivo` y aquí se declara igual que el destino.
inline Instancia& pista(Netlist& nl, const char* id, const std::string& origen,
                        const std::string& destino) {
    Instancia& i = nl.add("SignalLink", id);
    i.pin("origen", origen).pin("destino", destino);
    return i;
}

// ---------------------------------------------------------------------------
// Piezas de MUCHAS patillas
//
// Para estas, la lista de terminales se pasa como lista de pares
// {terminal, nodo}. No es un capricho de estilo: un constructor de doce
// argumentos posicionales es justo lo que el netlist viene a eliminar, y así el
// sitio de la llamada ya tiene la forma que tendrá el XML.
// ---------------------------------------------------------------------------

// Reloj externo de encapsulado. `hz` a cero es lo normal al arrancar: el banco
// lo enciende cuando le hace falta.
inline Instancia& reloj_ext(Netlist& nl, const char* id, const std::string& nodo,
                            double hz = 0.0) {
    Instancia& i = nl.add("ExtClock", id);
    i.pin("out", nodo).par("hz", hz);
    return i;
}

// El hilo de un bus I2C: colector abierto con su pull-up, sobre N pines que
// quedan cortocircuitados entre sí, como en la placa.
inline Instancia& hilo_i2c(Netlist& nl, const char* id,
                           std::initializer_list<Conexion> lineas,
                           double r_pull = 4700.0) {
    Instancia& i = nl.add("I2cWire", id);
    conecta(i, lineas).par("r_pull", r_pull);
    return i;
}

inline Instancia& eeprom_i2c(Netlist& nl, const char* id, const std::string& scl,
                             const std::string& sda, unsigned dir = 0x50) {
    Instancia& i = nl.add("I2cEeprom", id);
    i.pin("scl", scl).pin("sda", sda).par("dir", double(dir));
    return i;
}

inline Instancia& maestro_i2c(Netlist& nl, const char* id, const std::string& scl,
                              const std::string& sda, double f_scl = 100e3) {
    Instancia& i = nl.add("I2cExtMaster", id);
    i.pin("scl", scl).pin("sda", sda).par("f_scl", f_scl);
    return i;
}

inline Instancia& analizador_swo(Netlist& nl, const char* id,
                                 const std::string& nodo, double bitrate) {
    Instancia& i = nl.add("SwoReceiver", id);
    i.pin("swo", nodo).par("bitrate", bitrate);
    return i;
}

// Terminales: ck, cmd, dat0..dat3. El zócalo trae sus pull-ups.
inline Instancia& tarjeta_sd(Netlist& nl, const char* id,
                             std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("SdCard", id);
    conecta(i, pines);
    return i;
}

// Terminales: pixclk, hsync, vsync, d0..dN. Los hilos que el encapsulado no
// saca sencillamente no se declaran: el DCMI leerá lo que haya, que es lo que
// pasa en la placa.
inline Instancia& sensor_imagen(Netlist& nl, const char* id,
                                std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("CameraSensor", id);
    conecta(i, pines);
    return i;
}

// Terminales: d0..d15, a16..a23, ne, noe, nwe, nl, nbl0, nbl1, nwait.
inline Instancia& sram_ext(Netlist& nl, const char* id,
                           std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("ExtSram", id);
    conecta(i, pines);
    return i;
}

// Terminales: d0..d7, cle, ale, nce, noe, nwe, rb.
inline Instancia& nand_ext(Netlist& nl, const char* id,
                           std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("ExtNand", id);
    conecta(i, pines);
    return i;
}

// Terminales: dm, dp, vbus, id.
inline Instancia& aparejo_usb_host(Netlist& nl, const char* id,
                                   std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("UsbHostRig", id);
    conecta(i, pines);
    return i;
}

// Terminales: dm, dp, vbus.
inline Instancia& aparejo_usb_disp(Netlist& nl, const char* id,
                                   std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("UsbDeviceRig", id);
    conecta(i, pines);
    return i;
}

// Terminales: mdc, mdio, tx_clk, rx_clk, tx_en, txd0..3, rxd0..3, rx_dv,
// rx_er, crs, col. Dieciocho para MII; de ellos, nueve son los de RMII.
inline Instancia& phy_eth(Netlist& nl, const char* id,
                          std::initializer_list<Conexion> pines) {
    Instancia& i = nl.add("EthPhy", id);
    conecta(i, pines);
    return i;
}

// ---------------------------------------------------------------------------
// El bus CAN
//
// Es el grupo que obligó a que el netlist supiera hacer tres cosas que las
// piezas de una patilla no exigen: NODOS QUE NO SON PINES (el hilo),
// REFERENCIAS ENTRE INSTANCIAS (un transceptor necesita su hilo, no solo el
// nodo) y ORDEN DE CONSTRUCCIÓN (el hilo antes que quien se cuelga de él).
// ---------------------------------------------------------------------------

// El hilo. Su terminal `bus` NO es un pin del MCU: es un nodo externo, y el
// creador lo da de alta en el mapa si no existía.
inline Instancia& hilo_can(Netlist& nl, const char* id, const std::string& nodo,
                           double vdd = 3.3, double r_term = 1000.0) {
    nl.nodo_externo(nodo);           // el hilo no es un pin: hay que crearlo
    Instancia& i = nl.add("CanWire", id);
    i.pin("bus", nodo).par("vdd", vdd).par("r_term", r_term);
    return i;
}

// El transceptor. `hilo` es el IDENTIFICADOR de la instancia del hilo, no un
// nodo: es una referencia entre componentes, que un netlist tiene que admitir
// igual que admite conexiones a nodos.
inline Instancia& transceptor_can(Netlist& nl, const char* id,
                                  const std::string& txd, const std::string& rxd,
                                  const char* hilo, double vdd = 3.3) {
    // El hilo se declara ANTES, y de él sale el nodo al que va el terminal
    // `bus` del transceptor: la declaración queda completa —los tres terminales
    // con su nodo— aunque para construirlo haga falta además el OBJETO del hilo.
    const Instancia* w = nl.busca(hilo);
    Instancia& i = nl.add("CanTransceiver", id);
    i.pin("txd", txd).pin("rxd", rxd);
    if (w) i.pin("bus", w->nodo_de("bus"));
    i.ref("hilo", hilo).par("vdd", vdd);
    return i;
}

// Otro controlador colgado del mismo hilo. No tiene ningún pin del MCU: todos
// sus terminales son del nodo externo. Un netlist tiene que poder describir eso
// —hay componentes de placa que el MCU ni ve— y por eso el `bus` va como
// conexión y no como parámetro.
inline Instancia& nodo_can(Netlist& nl, const char* id, const std::string& nodo,
                           const char* hilo, double bitrate = 500e3) {
    Instancia& i = nl.add("CanNode", id);
    i.pin("bus", nodo).ref("hilo", hilo).par("bitrate", bitrate);
    return i;
}

} // namespace stm32
#endif // STM32_PARTS_NETLIST_PARTS_H
