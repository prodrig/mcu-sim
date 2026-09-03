// =============================================================================
// otg.h — USB On-The-Go: OTG_FS (AHB2) y OTG_HS (AHB1) [IR, §12.15 y §12.23]
//
// El OTG no es un puerto serie con pretensiones. Es una máquina que hace tres
// cosas a la vez y las tres a distinto nivel:
//
//   1. ELECTRICIDAD. Dos hilos, D+ y D-, y una red de resistencias que decide
//      TODO lo demás: quién está enchufado, a qué velocidad, y en qué estado
//      está el cable. El anfitrión pone 15 kohm a masa en los dos hilos; el
//      dispositivo, al querer que lo vean, pone 1,5 kohm a 3,3 V en UNO de
//      ellos -D+ si es Full Speed, D- si es Low Speed-. No hay registro que
//      diga "hay algo enchufado": lo dice el divisor resistivo.
//
//   2. PROTOCOLO. Testigos, datos y acuses (SETUP/IN/OUT, DATA0/DATA1,
//      ACK/NAK/STALL), con su bit de conmutación y sus reintentos.
//
//   3. MEMORIA. Una RAM de FIFOs que el firmware PARTE A MANO. Es la fuente
//      número uno de sufrimiento con este periférico: nadie comprueba que las
//      particiones no se pisen, y cuando se pisan, la corrupción aparece muy
//      lejos del sitio donde se causó.
//
// El modelo hace las tres. La electricidad, en float sobre los AnalogNet de los
// pines -de ahí salen la detección de conexión, la velocidad, el reset, la
// suspensión y el despertar-. El protocolo, a nivel de TRANSACCIÓN (véase
// "Dónde está la frontera del modelo", más abajo). Y la RAM de FIFOs, con
// direcciones de verdad: si dos particiones se solapan, se corrompen.
//
// -----------------------------------------------------------------------------
// LOS "CANALES" DEL OTG SON TRES COSAS DISTINTAS
//
// La pregunta "¿en qué se diferencian los canales?" tiene aquí tres respuestas,
// y conviene no mezclarlas:
//
//   A) LAS DOS INSTANCIAS. OTG_FS y OTG_HS no son dos copias. Difieren en el
//      bus al que cuelgan (AHB2 / AHB1), en si tienen DMA propio (el HS es el
//      OCTAVO MAESTRO de la matriz), en el PHY (integrado / externo por ULPI),
//      en el tamaño de la RAM de FIFOs (1,25 KB / 4 KB), en cuántos endpoints
//      y canales gobiernan (4/8 frente a 6/12) y hasta en cuántas líneas de
//      interrupción sacan (el HS tiene dos dedicadas al endpoint 1).
//
//   B) LOS CANALES DE ANFITRIÓN (0..7 u 0..11). Estos SÍ son copias: mismo
//      juego de registros HCCHARx/HCINTx/HCINTMSKx/HCTSIZx y misma máquina.
//      Lo único que los distingue es a qué FIFO periódica van (bit EPTYP) y,
//      en el HS, que tienen HCDMAx.
//
//   C) LOS ENDPOINTS DE DISPOSITIVO (0..3 o 0..5). Estos NO son copias, y es
//      la trampa más fina del periférico: el ENDPOINT 0 ES DISTINTO. Su MPSIZ
//      no es un número de bytes sino un código de dos bits (0=64, 1=32, 2=16,
//      3=8); su XFRSIZ tiene 7 bits en vez de 19 y su PKTCNT 2 en vez de 10;
//      no se puede deshabilitar; es el único que recibe SETUP; y su FIFO de
//      transmisión no se programa con DIEPTXF0 a secas, sino con un registro
//      que en modo anfitrión es OTRA COSA (HNPTXFSIZ). Un modelo que trate los
//      endpoints como un vector homogéneo se equivoca en los cinco puntos.
//
// -----------------------------------------------------------------------------
// SELECCIÓN DEL TIPO DE OTG
//
//   * en TIEMPO DE COMPILACIÓN, con el alias de plantilla:
//         using OtgFsT     = OtgT<CAPS_OTG_FS>;      // PHY integrado, 320 pal.
//         using OtgHsT     = OtgT<CAPS_OTG_HS>;      // ULPI + DMA, 1024 pal.
//         using OtgHsFsT   = OtgT<CAPS_OTG_HS_FS>;   // núcleo HS sin ULPI
//         using OtgDevT    = OtgT<CAPS_OTG_DEV>;     // solo dispositivo
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         OtgBase u{"u", base, tam, OtgCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA de cada registro, y además
// deciden qué GRUPOS de registros existen: sin rol de anfitrión, todo el bloque
// 0x400 se lee cero; sin OTG, GOTGCTL y GOTGINT también. Misma receta que
// UsartCaps, TimCaps, SpiCaps, I2cCaps, AdcCaps, DacCaps, SdioCaps, CanCaps,
// DcmiCaps y FsmcCaps.
//
// -----------------------------------------------------------------------------
// DÓNDE ESTÁ LA FRONTERA DEL MODELO
//
// Los pines D+/D- llevan tensiones de verdad y de ellas salen los estados de
// línea (J, K, SE0, SE1), la conexión, la velocidad, el reset y la reanudación.
// Lo que NO se modela es la codificación NRZI, el relleno de bits ni el CRC:
// un paquete cruza el cable como paquete, no como una tira de bits a 12 Mbit/s.
// Es una frontera deliberada -simular 480 Mbit/s bit a bit no aporta nada a un
// modelo de MCU- y está dicha aquí para que nadie la descubra por sorpresa.
// =============================================================================
#ifndef STM32_PERIPH_OTG_H
#define STM32_PERIPH_OTG_H

#include <array>
#include <deque>
#include <vector>
#include <cstring>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/periph_base.h"
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// El protocolo, al nivel al que lo modelamos
// ---------------------------------------------------------------------------
enum : uint8_t {
    PID_OUT = 0x1, PID_IN = 0x9, PID_SOF = 0x5, PID_SETUP = 0xD,
    PID_DATA0 = 0x3, PID_DATA1 = 0xB,
    PID_ACK = 0x2, PID_NAK = 0xA, PID_STALL = 0xE, PID_NYET = 0x6,
    PID_NADIE = 0x0                       // nadie contestó: timeout
};

// Lo que hay al otro extremo del cable. En modo ANFITRIÓN el controlador llama
// a esta interfaz; en modo DISPOSITIVO, la implementa él mismo y es el aparejo
// externo quien llama. La simetría no es casual: el OTG es de doble rol.
class usb_dev_if {
public:
    virtual ~usb_dev_if() {}
    // Una transacción completa. Devuelve el PID de respuesta (ACK/NAK/STALL) y,
    // en una IN, deja los datos en `entrada`.
    virtual uint8_t transaccion(uint8_t pid_token, uint8_t addr, uint8_t ep,
                                const std::vector<uint8_t>& salida,
                                std::vector<uint8_t>& entrada) = 0;
    virtual void sof(uint16_t /*trama*/) {}
};

// Estado eléctrico del par diferencial.
enum class UsbLinea : uint8_t { Desconectado, SE0, J, K, SE1 };

// ---------------------------------------------------------------------------
// Los rasgos
// ---------------------------------------------------------------------------
struct OtgCaps {
    bool     hs            = false;   // núcleo High-Speed (480 Mbit/s)
    bool     ulpi          = false;   // interfaz a PHY externo
    bool     phy_fs        = true;    // transceptor Full-Speed integrado
    bool     dma_interno   = false;   // maestro AHB propio (GAHBCFG.DMAEN)
    bool     host          = true;    // rol anfitrión (grupo 0x400)
    bool     device        = true;    // rol dispositivo (grupo 0x800)
    bool     otg           = true;    // ID / SRP / HNP (GOTGCTL, GOTGINT)
    bool     ep1_irq       = false;   // líneas de interrupción propias de EP1
    bool     split         = false;   // transacciones split (HCSPLT)
    unsigned endpoints     = 4;       // endpoints bidireccionales, EP0 incluido
    unsigned canales       = 8;       // canales de anfitrión
    unsigned fifo_palabras = 320;     // RAM de FIFOs, en palabras de 32 bits
    uint32_t cid           = 0x00001200u;
    const char* kind       = "OTG";
};

// --- Las variantes ---------------------------------------------------------
// OTG_FS del F407VG: PHY integrado sobre PA11/PA12, 1,25 KB de FIFO, sin DMA
// propio -sus datos los mueve la CPU o un DMA general- [IR, §12.15].
constexpr OtgCaps caps_otg_fs() {
    OtgCaps c{};
    c.kind = "OTG_FS (PHY integrado, 4 EP, 8 canales, 320 palabras)";
    return c;
}
// OTG_HS del F407VG: PHY externo por ULPI, DMA propio (octavo maestro de la
// matriz), 4 KB de FIFO, 6 endpoints, 12 canales y dos IRQ dedicadas al
// endpoint 1 [IR, §12.23].
constexpr OtgCaps caps_otg_hs() {
    OtgCaps c{};
    c.hs = true; c.ulpi = true; c.dma_interno = true; c.ep1_irq = true;
    c.split = true;
    c.endpoints = 6; c.canales = 12; c.fifo_palabras = 1024;
    c.cid = 0x00001100u;
    c.kind = "OTG_HS (ULPI + DMA propio, 6 EP, 12 canales, 1024 palabras)";
    return c;
}
// El MISMO núcleo HS en una placa que no ha cableado el PHY externo: los pines
// ULPI no existen y el núcleo se queda con su transceptor FS. Sigue teniendo
// DMA, seis endpoints y doce canales, pero los bits de ULPI ya no se guardan.
// Es el caso real de casi todas las placas pequeñas con F407.
constexpr OtgCaps caps_otg_hs_fs() {
    OtgCaps c = caps_otg_hs();
    c.ulpi = false;
    c.kind = "OTG_HS en FS integrado (sin PHY ULPI en la placa)";
    return c;
}
// El mínimo que sigue siendo USB: solo dispositivo, sin OTG, sin anfitrión,
// tres endpoints y una FIFO pequeña. Es como aparece el bloque en los
// derivados que solo se conectan a un PC.
constexpr OtgCaps caps_otg_dev() {
    OtgCaps c{};
    c.host = false; c.otg = false;
    c.endpoints = 3; c.canales = 0; c.fifo_palabras = 256;
    c.cid = 0x00000900u;
    c.kind = "USB solo dispositivo (sin anfitrion ni OTG)";
    return c;
}

inline constexpr OtgCaps CAPS_OTG_FS    = caps_otg_fs();
inline constexpr OtgCaps CAPS_OTG_HS    = caps_otg_hs();
inline constexpr OtgCaps CAPS_OTG_HS_FS = caps_otg_hs_fs();
inline constexpr OtgCaps CAPS_OTG_DEV   = caps_otg_dev();

// ===========================================================================
// El controlador
// ===========================================================================
class OtgBase : public BusSlave, public usb_dev_if {
public:
    // Los rasgos de ESTA instancia. Se declara lo primero porque de él dependen
    // los tamaños de todo lo demás.
    const OtgCaps caps;

    // ---- Interrupciones ----------------------------------------------------
    sc_core::sc_out<bool> irq_global{"irq_global"};    // IRQ 67 (FS) / 77 (HS)
    sc_core::sc_out<bool> wkup_line{"wkup_line"};      // EXTI18 (FS) / EXTI20 (HS)
    sc_core::sc_out<bool> irq_wkup{"irq_wkup"};        // sin uso: va por EXTI
    sc_core::sc_out<bool> irq_ep1_out{"irq_ep1_out"};  // solo HS
    sc_core::sc_out<bool> irq_ep1_in{"irq_ep1_in"};    // solo HS
    // ---- Relojes -----------------------------------------------------------
    sc_core::sc_in<bool>   clk48{"clk48"};             // PLL48CK
    sc_core::sc_in<double> clk48_hz{"clk48_hz"};
    // ---- Maestro AHB del DMA interno (solo HS) -----------------------------
    tlm_utils::simple_initiator_socket<OtgBase> dma_m{"dma_m"};
    // Una variante SIN DMA propio -el OTG_FS lo es- deja este puerto sin
    // enlazar, y SystemC no admite puertos sueltos. En vez de fingir que el FS
    // es maestro de la matriz, se le engancha un tapon que contesta ERROR: si
    // algun dia el modelo intentara usarlo, se sabria en el acto. El tapon solo
    // se CONSTRUYE cuando hace falta: en el HS no existe.
    tlm_utils::simple_target_socket<OtgBase>* dma_nc_ = nullptr;
    // ---- Interfaz ULPI hacia el PHY externo (AF10) -------------------------
    sc_core::sc_signal<bool> ulpi_ck_in{"ulpi_ck_in"}, ulpi_dir_in{"ulpi_dir_in"},
                             ulpi_nxt_in{"ulpi_nxt_in"}, ulpi_stp_out{"ulpi_stp_out"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> ulpi_d_out, ulpi_d_oe, ulpi_d_in;

    OtgBase(sc_core::sc_module_name nm, uint32_t base, uint32_t size,
            const OtgCaps& c)
        : BusSlave(nm, base, size), caps(c),
          ulpi_d_out("ulpi_d_out", 8), ulpi_d_oe("ulpi_d_oe", 8),
          ulpi_d_in("ulpi_d_in", 8) {
        ram_.assign(caps.fifo_palabras, 0);
        ep_.resize(caps.endpoints);
        ch_.resize(caps.canales ? caps.canales : 1);
        txf_.resize(caps.endpoints);
        reset_regs();
        SC_HAS_PROCESS(OtgBase);
        SC_THREAD(motor_proc);
        SC_THREAD(pines_proc);
        SC_METHOD(rst_proc);   sensitive << rst_n;   dont_initialize();
        SC_METHOD(pub_proc);   sensitive << pub_ev_; dont_initialize();
        SC_METHOD(phy_proc);   sensitive << phy_ev_ << clk48_hz << clk_en;
        dont_initialize();
    }
    void before_end_of_elaboration() override {
        if (dma_m.size() == 0) {
            dma_nc_ = new tlm_utils::simple_target_socket<OtgBase>("dma_nc");
            dma_nc_->register_b_transport(this, &OtgBase::bt_sin_dma);
            dma_m.bind(*dma_nc_);
        }
    }
    ~OtgBase() override { delete dma_nc_; }

    // =======================================================================
    // Cableado eléctrico
    // =======================================================================
    // El par diferencial. En el F407 son PA11/PA12 (FS) y PB14/PB15 (HS en su
    // modo FS integrado). Se toma el nodo analógico directamente, como hacen
    // el ADC y el DAC: lo que va por aquí no es una señal digital sino una
    // tensión, y de esa tensión salen la conexión y la velocidad.
    void bind_phy(analog_net_if& dm, analog_net_if& dp) {
        dm_ = &dm; dp_ = &dp;
        id_dm_ = dm_->register_driver("otg_dm");
        id_dp_ = dp_->register_driver("otg_dp");
        id_pu_ = dp_->register_driver("otg_pullup_dp");
        id_pum_ = dm_->register_driver("otg_pullup_dm");
        soltar_par();
    }
    // VBUS: los 5 V del cable. Sin ellos no hay sesión, se programe lo que se
    // programe [IR, §12.15.1].
    void bind_vbus(analog_net_if& v) { vbus_ = &v; }
    // ID: el quinto hilo del conector mini/micro-AB. A masa = cable A = este
    // chip es el anfitrión. Al aire = cable B = este chip es el dispositivo.
    void bind_id(analog_net_if& i) {
        id_net_ = &i;
        id_idpull_ = id_net_->register_driver("otg_id_pu");
        id_net_->set_drive(id_idpull_, 3.3f, 100.0e3f);   // pull-up interno
    }
    // Quién está al otro lado cuando este núcleo hace de anfitrión.
    void conectar_dispositivo(usb_dev_if* d) { dispo_ = d; }

    // =======================================================================
    // Mapa de registros [IR, §12.15.4 / §12.23.2]
    // =======================================================================
    enum : uint32_t {
        R_GOTGCTL = 0x000, R_GOTGINT = 0x004, R_GAHBCFG = 0x008, R_GUSBCFG = 0x00C,
        R_GRSTCTL = 0x010, R_GINTSTS = 0x014, R_GINTMSK = 0x018, R_GRXSTSR = 0x01C,
        R_GRXSTSP = 0x020, R_GRXFSIZ = 0x024, R_DIEPTXF0 = 0x028, R_HNPTXSTS = 0x02C,
        R_GCCFG = 0x038, R_CID = 0x03C, R_HPTXFSIZ = 0x100, R_DIEPTXF1 = 0x104,
        R_HCFG = 0x400, R_HFIR = 0x404, R_HFNUM = 0x408, R_HPTXSTS = 0x410,
        R_HAINT = 0x414, R_HAINTMSK = 0x418, R_HPRT = 0x440, R_HC0 = 0x500,
        R_DCFG = 0x800, R_DCTL = 0x804, R_DSTS = 0x808, R_DIEPMSK = 0x810,
        R_DOEPMSK = 0x814, R_DAINT = 0x818, R_DAINTMSK = 0x81C,
        R_DVBUSDIS = 0x828, R_DVBUSPULSE = 0x82C, R_DIEPEMPMSK = 0x834,
        R_DIEP0 = 0x900, R_DOEP0 = 0xB00, R_PCGCCTL = 0xE00, R_FIFO0 = 0x1000
    };
    // GOTGCTL
    enum : uint32_t {
        OTGCTL_SRQSCS = 1u << 0, OTGCTL_SRQ = 1u << 1, OTGCTL_HNGSCS = 1u << 8,
        OTGCTL_HNPRQ = 1u << 9, OTGCTL_HSHNPEN = 1u << 10, OTGCTL_DHNPEN = 1u << 11,
        OTGCTL_CIDSTS = 1u << 16, OTGCTL_DBCT = 1u << 17,
        OTGCTL_ASVLD = 1u << 18, OTGCTL_BSVLD = 1u << 19
    };
    // GAHBCFG
    enum : uint32_t {
        AHB_GINTMSK = 1u << 0, AHB_HBSTLEN = 0xFu << 1, AHB_DMAEN = 1u << 5,
        AHB_TXFELVL = 1u << 7, AHB_PTXFELVL = 1u << 8
    };
    // GUSBCFG
    enum : uint32_t {
        USB_PHYSEL = 1u << 6, USB_SRPCAP = 1u << 8, USB_HNPCAP = 1u << 9,
        USB_TRDT = 0xFu << 10, USB_PHYLPCS = 1u << 15, USB_ULPIFSLS = 1u << 17,
        USB_ULPIAR = 1u << 18, USB_ULPICSM = 1u << 19, USB_ULPIEVBUSD = 1u << 20,
        USB_ULPIEVBUSI = 1u << 21, USB_TSDPS = 1u << 22, USB_ULPIIPD = 1u << 25,
        USB_FHMOD = 1u << 29, USB_FDMOD = 1u << 30, USB_CTXPKT = 1u << 31
    };
    // GRSTCTL
    enum : uint32_t {
        RST_CSRST = 1u << 0, RST_HSRST = 1u << 1, RST_FCRST = 1u << 2,
        RST_RXFFLSH = 1u << 4, RST_TXFFLSH = 1u << 5, RST_TXFNUM = 0x1Fu << 6,
        RST_AHBIDL = 1u << 31
    };
    // GINTSTS / GINTMSK
    enum : uint32_t {
        INT_CMOD = 1u << 0, INT_MMIS = 1u << 1, INT_OTGINT = 1u << 2,
        INT_SOF = 1u << 3, INT_RXFLVL = 1u << 4, INT_NPTXFE = 1u << 5,
        INT_ESUSP = 1u << 10, INT_USBSUSP = 1u << 11, INT_USBRST = 1u << 12,
        INT_ENUMDNE = 1u << 13, INT_EOPF = 1u << 15, INT_IEPINT = 1u << 18,
        INT_OEPINT = 1u << 19, INT_HPRTINT = 1u << 24, INT_HCINT = 1u << 25,
        INT_PTXFE = 1u << 26, INT_CIDSCHG = 1u << 28, INT_DISCINT = 1u << 29,
        INT_SRQINT = 1u << 30, INT_WKUINT = 1u << 31
    };
    // GCCFG (control del transceptor)
    enum : uint32_t {
        CCFG_PWRDWN = 1u << 16, CCFG_VBUSASEN = 1u << 18,
        CCFG_VBUSBSEN = 1u << 19, CCFG_SOFOUTEN = 1u << 20,
        CCFG_NOVBUSSENS = 1u << 21
    };
    // HPRT: ojo, PENA/PCDET/PENCHNG/POCCHNG se BORRAN escribiendo uno, de modo
    // que un read-modify-write ingenuo apaga el puerto. Es el error clásico.
    enum : uint32_t {
        HPRT_PCSTS = 1u << 0, HPRT_PCDET = 1u << 1, HPRT_PENA = 1u << 2,
        HPRT_PENCHNG = 1u << 3, HPRT_POCA = 1u << 4, HPRT_POCCHNG = 1u << 5,
        HPRT_PRES = 1u << 6, HPRT_PSUSP = 1u << 7, HPRT_PRST = 1u << 8,
        HPRT_PLSTS = 3u << 10, HPRT_PPWR = 1u << 12, HPRT_PTCTL = 0xFu << 13,
        HPRT_PSPD = 3u << 17,
        HPRT_RC_W1 = HPRT_PCDET | HPRT_PENA | HPRT_PENCHNG | HPRT_POCCHNG
    };
    // DCTL / DSTS / DCFG
    enum : uint32_t {
        DCTL_RWUSIG = 1u << 0, DCTL_SDIS = 1u << 1, DCTL_GINSTS = 1u << 2,
        DCTL_GONSTS = 1u << 3, DCTL_SGINAK = 1u << 7, DCTL_CGINAK = 1u << 8,
        DCTL_SGONAK = 1u << 9, DCTL_CGONAK = 1u << 10, DCTL_POPRGDNE = 1u << 11,
        DSTS_SUSPSTS = 1u << 0, DSTS_ENUMSPD = 3u << 1, DSTS_EERR = 1u << 3
    };
    // DIEPCTL / DOEPCTL
    enum : uint32_t {
        EPC_MPSIZ = 0x7FFu << 0, EPC_USBAEP = 1u << 15, EPC_DPID = 1u << 16,
        EPC_NAKSTS = 1u << 17, EPC_EPTYP = 3u << 18, EPC_SNPM = 1u << 20,
        EPC_STALL = 1u << 21, EPC_TXFNUM = 0xFu << 22, EPC_CNAK = 1u << 26,
        EPC_SNAK = 1u << 27, EPC_SD0PID = 1u << 28, EPC_SD1PID = 1u << 29,
        EPC_EPDIS = 1u << 30, EPC_EPENA = 1u << 31
    };
    // DIEPINT / DOEPINT
    enum : uint32_t {
        EPI_XFRC = 1u << 0, EPI_EPDISD = 1u << 1, EPI_TOC = 1u << 3,
        EPI_STUP = 1u << 3, EPI_ITTXFE = 1u << 4, EPI_OTEPDIS = 1u << 4,
        EPI_INEPNE = 1u << 6, EPI_B2BSTUP = 1u << 6, EPI_TXFE = 1u << 7
    };
    // HCCHAR / HCINT
    enum : uint32_t {
        HCC_MPSIZ = 0x7FFu << 0, HCC_EPNUM = 0xFu << 11, HCC_EPDIR = 1u << 15,
        HCC_LSDEV = 1u << 17, HCC_EPTYP = 3u << 18, HCC_MCNT = 3u << 20,
        HCC_DAD = 0x7Fu << 22, HCC_ODDFRM = 1u << 29, HCC_CHDIS = 1u << 30,
        HCC_CHENA = 1u << 31,
        HCI_XFRC = 1u << 0, HCI_CHH = 1u << 1, HCI_STALL = 1u << 3,
        HCI_NAK = 1u << 4, HCI_ACK = 1u << 5, HCI_TXERR = 1u << 7,
        HCI_DTERR = 1u << 10
    };
    // PKTSTS de GRXSTSP en modo dispositivo [IR, §12.15.4]
    enum : uint32_t {
        PKTSTS_GONAK = 1, PKTSTS_OUT_DATA = 2, PKTSTS_OUT_COMP = 3,
        PKTSTS_SETUP_COMP = 4, PKTSTS_SETUP_DATA = 6,
        PKTSTS_IN_DATA = 2, PKTSTS_IN_COMP = 3          // modo anfitrión
    };

    // =======================================================================
    // Observadores para el banco de pruebas
    // =======================================================================
    UsbLinea linea() const { return linea_; }
    bool     modo_host() const { return host_; }
    bool     conectado() const { return (hprt_ & HPRT_PCSTS) != 0; }
    bool     pullup_puesto() const { return pullup_; }
    unsigned velocidad() const { return vel_; }        // 0=HS 1=FS 2=LS
    uint32_t vbus_mv() const { return vbus_mv_; }
    unsigned tramas() const { return n_sof_; }
    unsigned paquetes_in() const { return n_in_; }
    unsigned paquetes_out() const { return n_out_; }
    unsigned dma_escrituras() const { return n_dma_wr_; }
    unsigned dma_lecturas() const { return n_dma_rd_; }
    // ¿Se pisan las particiones de la RAM de FIFOs? El silicio no lo dice; el
    // modelo sí, porque es la avería que más tiempo cuesta encontrar.
    bool fifos_solapadas() const { return solape_; }
    unsigned palabras_ram() const { return unsigned(ram_.size()); }

    // El aparejo externo que hace de anfitrión llama aquí (modo dispositivo).
    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep,
                        const std::vector<uint8_t>& salida,
                        std::vector<uint8_t>& entrada) override;
    void sof(uint16_t trama) override;
    // Y para el reset de bus, que es eléctrico pero tiene efecto lógico.
    void bus_reset_visto();

protected:
    // =======================================================================
    // Estado
    // =======================================================================
    struct Endpoint {
        uint32_t diepctl = 0, diepint = 0, dieptsiz = 0, diepdma = 0;
        uint32_t doepctl = 0, doepint = 0, doeptsiz = 0, doepdma = 0;
        // Lo que queda por mover de la transferencia en curso.
        unsigned in_pkt = 0,  in_bytes = 0;
        unsigned out_pkt = 0, out_bytes = 0;
    };
    struct Canal {
        uint32_t hcchar = 0, hcsplt = 0, hcint = 0, hcintmsk = 0;
        uint32_t hctsiz = 0, hcdma = 0;
        unsigned pkt = 0, bytes = 0;
    };
    // Una FIFO de verdad: un trozo de la RAM compartida, con su cabeza y su
    // cola. Si dos se solapan, se corrompen: exactamente como en el silicio.
    struct Fifo {
        uint32_t inicio = 0, tam = 0, rd = 0, wr = 0, n = 0;
    };

    std::vector<uint32_t> ram_;
    std::vector<Endpoint> ep_;
    std::vector<Canal>    ch_;
    std::vector<Fifo>     txf_;              // FIFO de transmisión por endpoint
    Fifo                  rxf_;
    std::deque<uint32_t>  rx_sts_;           // cola de palabras de estado

    uint32_t gotgctl_ = 0, gotgint_ = 0, gahbcfg_ = 0, gusbcfg_ = 0;
    uint32_t grstctl_ = 0, gintsts_ = 0, gintmsk_ = 0, grxfsiz_ = 0;
    uint32_t dieptxf0_ = 0, gccfg_ = 0, hptxfsiz_ = 0;
    std::array<uint32_t, 8> dieptxf_{};      // DIEPTXF1..n
    uint32_t hcfg_ = 0, hfir_ = 0, hfnum_ = 0, haintmsk_ = 0, hprt_ = 0;
    uint32_t dcfg_ = 0, dctl_ = 0, dsts_ = 0, diepmsk_ = 0, doepmsk_ = 0;
    uint32_t daintmsk_ = 0, diepempmsk_ = 0, dvbusdis_ = 0, dvbuspulse_ = 0;
    uint32_t pcgcctl_ = 0;

    bool     host_ = false;                  // rol actual
    bool     pullup_ = false;                // 1,5 kohm en D+ (dispositivo)
    bool     solape_ = false;
    unsigned vel_ = 1;                       // 0=HS 1=FS 2=LS
    uint32_t vbus_mv_ = 0;
    UsbLinea linea_ = UsbLinea::Desconectado;
    unsigned n_sof_ = 0, n_in_ = 0, n_out_ = 0, n_dma_wr_ = 0, n_dma_rd_ = 0;
    sc_core::sc_time t_actividad_{sc_core::SC_ZERO_TIME};
    sc_core::sc_time t_se0_{sc_core::SC_ZERO_TIME};
    sc_core::sc_time t_sof_{sc_core::SC_ZERO_TIME};

    analog_net_if *dm_ = nullptr, *dp_ = nullptr, *vbus_ = nullptr, *id_net_ = nullptr;
    int id_dm_ = -1, id_dp_ = -1, id_pu_ = -1, id_pum_ = -1, id_idpull_ = -1;
    usb_dev_if* dispo_ = nullptr;

    bool o_irq_ = false, o_wkup_ = false, o_ep1o_ = false, o_ep1i_ = false;
    sc_core::sc_event pub_ev_, phy_ev_, motor_ev_;

    // =======================================================================
    // Máscaras: aquí es donde los rasgos dejan de ser documentación
    // =======================================================================
    uint32_t mask_gahbcfg() const {
        uint32_t m = AHB_GINTMSK | AHB_TXFELVL | AHB_PTXFELVL;
        if (caps.dma_interno) m |= AHB_DMAEN | AHB_HBSTLEN;
        return m;
    }
    uint32_t mask_gusbcfg() const {
        uint32_t m = 0x7u | USB_TRDT | USB_CTXPKT;
        if (caps.otg)  m |= USB_SRPCAP | USB_HNPCAP;
        if (caps.host) m |= USB_FHMOD;
        if (caps.device) m |= USB_FDMOD;
        if (caps.ulpi) m |= USB_ULPIFSLS | USB_ULPIAR | USB_ULPICSM |
                            USB_ULPIEVBUSD | USB_ULPIEVBUSI | USB_TSDPS |
                            USB_ULPIIPD | USB_PHYLPCS;
        return m;
    }
    // PHYSEL es de SOLO LECTURA y vale uno en un núcleo sin PHY externo: el
    // registro no deja elegir lo que la placa no tiene.
    uint32_t gusbcfg_fijo() const { return caps.ulpi ? 0u : USB_PHYSEL; }
    uint32_t mask_gccfg() const {
        uint32_t m = CCFG_PWRDWN | CCFG_SOFOUTEN | CCFG_NOVBUSSENS;
        if (caps.otg) m |= CCFG_VBUSASEN | CCFG_VBUSBSEN;
        return m;
    }
    uint32_t mask_hprt() const {
        if (!caps.host) return 0;
        return HPRT_PCDET | HPRT_PENA | HPRT_PENCHNG | HPRT_POCCHNG |
               HPRT_PRES | HPRT_PSUSP | HPRT_PRST | HPRT_PPWR | HPRT_PTCTL;
    }
    uint32_t mask_dcfg() const {
        // DSPD: en un núcleo FS solo valen los códigos de Full Speed (11) y
        // Low Speed (10); un núcleo HS admite además el 00 (High Speed).
        uint32_t m = 0x7Fu << 4;                  // DAD
        m |= 0x3u;                                // DSPD (siempre dos bits)
        m |= 1u << 2;                             // NZLSOHSK
        m |= 3u << 11;                            // PFIVL
        if (caps.hs) m |= 0x3Fu << 24;            // PERSCHIVL
        return m;
    }
    uint32_t mask_epctl(unsigned ep, bool in) const {
        uint32_t m = EPC_EPTYP | EPC_STALL | EPC_CNAK | EPC_SNAK |
                     EPC_EPDIS | EPC_EPENA | EPC_SD0PID | EPC_SD1PID;
        if (ep == 0) {
            // EL ENDPOINT 0 NO ES UNO MÁS. MPSIZ son DOS bits codificados
            // (0=64, 1=32, 2=16, 3=8), no un número de bytes; el tipo es
            // siempre control y no se puede cambiar; y no se puede
            // deshabilitar [IR, §12.15.4].
            m = 0x3u | EPC_STALL | EPC_CNAK | EPC_SNAK | EPC_EPENA;
            if (in) m |= EPC_TXFNUM;
            return m;
        }
        m |= EPC_MPSIZ | EPC_USBAEP;
        if (in) m |= EPC_TXFNUM;
        else    m |= EPC_SNPM;
        return m;
    }
    uint32_t mask_eptsiz(unsigned ep, bool in) const {
        if (ep == 0) return in ? (0x7Fu | (3u << 19))          // 7 bits + 2
                               : (0x7Fu | (1u << 19) | (3u << 29));
        uint32_t m = 0x7FFFFu | (0x3FFu << 19);
        if (in) m |= 3u << 29;                                 // MCNT
        return m;
    }
    // El límite de una FIFO no es el que quiera el firmware: es la RAM que hay.
    uint32_t tope_fifo() const { return caps.fifo_palabras; }

    // =======================================================================
    // Lectura de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override;
    void     reg_write(uint32_t off, uint32_t v, uint32_t be) override;
    unsigned access_cycles(bool) const override { return 1; }

    // =======================================================================
    // FIFOs
    // =======================================================================
    void reparte_fifos();
    void fifo_push(Fifo& f, uint32_t w) {
        if (!f.tam) return;
        ram_[(f.inicio + f.wr) % ram_.size()] = w;
        f.wr = (f.wr + 1) % f.tam;
        if (f.n < f.tam) ++f.n; else f.rd = (f.rd + 1) % f.tam;
    }
    uint32_t fifo_pop(Fifo& f) {
        if (!f.tam || !f.n) return 0;
        const uint32_t w = ram_[(f.inicio + f.rd) % ram_.size()];
        f.rd = (f.rd + 1) % f.tam;
        --f.n;
        return w;
    }
    void fifo_vacia(Fifo& f) { f.rd = f.wr = f.n = 0; }

    // =======================================================================
    // El motor
    // =======================================================================
    void motor_proc();
    void rst_proc() { if (!rst_n.read()) { reset_regs(); actualiza_pines(); publica(); } }
    void pub_proc() {
        irq_global.write(o_irq_); wkup_line.write(o_wkup_); irq_wkup.write(false);
        irq_ep1_out.write(o_ep1o_); irq_ep1_in.write(o_ep1i_);
    }
    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void phy_proc() { actualiza_pines(); }
    // UN SOLO PROCESO PARA LOS PINES. Los nodos analogicos se enlazan despues
    // de construir el modulo, cuando ya no se puede ampliar la lista estatica
    // de sensibilidad; asi que la lista se arma aqui, en tiempo de ejecucion.
    void pines_proc() {
        for (;;) {
            if (!dm_ && !vbus_ && !id_net_) { sc_core::wait(nunca_); continue; }
            sc_core::sc_event_or_list l;
            if (dm_)     l |= dm_->value_changed_event() | dp_->value_changed_event();
            if (vbus_)   l |= vbus_->value_changed_event();
            if (id_net_) l |= id_net_->value_changed_event();
            sc_core::wait(l);
            vbus_proc(); id_proc(); linea_proc();
        }
    }
    void bt_sin_dma(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
    sc_core::sc_event nunca_;
    void linea_proc();
    void vbus_proc();
    void id_proc();

    void reset_regs();
    void actualiza_pines();
    void soltar_par() {
        if (!dm_) return;
        dm_->set_hiz(id_dm_); dp_->set_hiz(id_dp_);
        dm_->set_hiz(id_pum_); dp_->set_hiz(id_pu_);
    }
    void soltar_par_datos() {
        if (dm_) { dp_->set_hiz(id_dp_); dm_->set_hiz(id_dm_); }
    }
    void conduce_par(UsbLinea l);
    void actualiza_irq();
    void set_int(uint32_t b) { gintsts_ |= b; actualiza_irq(); }

    // Transacciones de anfitrión
    void canal_transaccion(unsigned c);
    // Utilidades del DMA interno
    bool dma_write(uint32_t dir, const uint8_t* d, unsigned n);
    bool dma_read(uint32_t dir, uint8_t* d, unsigned n);

    bool phy_encendido() const {
        if (!(gccfg_ & CCFG_PWRDWN) || !clock_enabled() || !rst_n.read())
            return false;
        // UN TRANSCEPTOR FS INTEGRADO NO ARRANCA SIN LOS 48 MHz. De PLL48CK
        // sale el reloj de bit, y sin el no hay ni pull-up: el PC no ve NADA
        // enchufado y el firmware no tiene ningun registro donde enterarse.
        // Es la averia numero uno de una placa nueva con USB.
        if (!caps.ulpi && clk48_hz.read() < 40.0e6) return false;
        return true;
    }
    unsigned mps_ep0(uint32_t ctl) const {
        static const unsigned t[4] = {64, 32, 16, 8};
        return t[ctl & 3u];
    }
    unsigned mps_de(unsigned ep, uint32_t ctl) const {
        return ep == 0 ? mps_ep0(ctl) : unsigned(ctl & 0x7FFu);
    }
};

// ===========================================================================
// Reset
// ===========================================================================
inline void OtgBase::reset_regs() {
    gotgctl_ = caps.otg ? 0u : 0u;
    gotgint_ = 0;
    gahbcfg_ = 0;
    gusbcfg_ = 0x00000A00u | gusbcfg_fijo();     // TRDT = 2 por reset
    grstctl_ = RST_AHBIDL;
    gintsts_ = 0x04000020u;                      // CIDSCHG-libre; NPTXFE, ...
    gintsts_ = INT_NPTXFE | INT_PTXFE;
    gintmsk_ = 0;
    // LOS VALORES DE RESET DE LA PARTICION NO SON UNA PARTICION VALIDA. Son
    // los que documenta el manual -GRXFSIZ = 0x200, DIEPTXF0 = 0x0200_0200,
    // DIEPTXFx = 0x0200_0400- y piden mucha mas RAM de la que hay, ademas de
    // solaparse entre si. Programarlas TODAS antes de usar el periferico no es
    // una recomendacion: es un requisito, y el silicio no avisa de nada.
    grxfsiz_  = 0x00000200u;
    dieptxf0_ = 0x02000200u;
    for (auto& t : dieptxf_) t = 0x02000400u;
    gccfg_ = 0;
    hptxfsiz_ = (0x200u << 16) | 0x600u;
    hcfg_ = 0; hfir_ = 60000; hfnum_ = 0x00003FFFu; haintmsk_ = 0; hprt_ = 0;
    dcfg_ = caps.hs ? 0x02200000u : 0x02200003u;   // DSPD = FS en núcleo FS
    dcfg_ &= mask_dcfg() | 0x02200000u;
    dctl_ = 0; dsts_ = 0x00000002u; diepmsk_ = doepmsk_ = 0;
    daintmsk_ = 0; diepempmsk_ = 0; dvbusdis_ = 0x000017D7u; dvbuspulse_ = 0x000005B8u;
    pcgcctl_ = 0;
    for (auto& e : ep_) e = Endpoint{};
    for (auto& c : ch_) c = Canal{};
    // EP0 arranca activo y con 64 bytes: es el único que existe antes de que
    // el firmware programe nada [IR, §12.15.4].
    if (!ep_.empty()) { ep_[0].diepctl = 0; ep_[0].doepctl = 0; }
    for (auto& f : txf_) f = Fifo{};
    rxf_ = Fifo{};
    rx_sts_.clear();
    std::fill(ram_.begin(), ram_.end(), 0u);
    host_ = false; pullup_ = false; solape_ = false; vel_ = caps.hs ? 0u : 1u;
    linea_ = UsbLinea::Desconectado;
    n_sof_ = n_in_ = n_out_ = n_dma_wr_ = n_dma_rd_ = 0;
    o_irq_ = o_wkup_ = o_ep1o_ = o_ep1i_ = false;
    reparte_fifos();
}

// ===========================================================================
// La RAM de FIFOs: la partición que nadie comprueba
// ===========================================================================
inline void OtgBase::reparte_fifos() {
    // GRXFSIZ da el tamaño de la FIFO de recepción, que SIEMPRE empieza en 0.
    rxf_.inicio = 0;
    rxf_.tam    = grxfsiz_ & 0xFFFFu;
    // DIEPTXF0 (que en modo anfitrión es HNPTXFSIZ) y DIEPTXFn traen
    // dirección de comienzo en los 16 bits bajos y profundidad en los altos.
    auto pon = [&](Fifo& f, uint32_t r) {
        f.inicio = r & 0xFFFFu;
        f.tam    = (r >> 16) & 0xFFFFu;
        if (f.rd >= f.tam || f.wr >= f.tam || f.n > f.tam) { f.rd = f.wr = f.n = 0; }
    };
    if (!txf_.empty()) pon(txf_[0], dieptxf0_);
    for (unsigned i = 1; i < txf_.size(); ++i) pon(txf_[i], dieptxf_[i - 1]);

    // Y AQUÍ ES DONDE SE PIERDEN LAS TARDES. El núcleo no comprueba nada: si
    // dos particiones se solapan, o si una se sale de la RAM, escribir en una
    // corrompe la otra y el síntoma aparece tres capas más arriba. El modelo
    // lo detecta y lo deja a la vista.
    solape_ = false;
    auto choca = [](uint32_t a0, uint32_t an, uint32_t b0, uint32_t bn) {
        return an && bn && a0 < b0 + bn && b0 < a0 + an;
    };
    std::vector<Fifo*> todas{&rxf_};
    for (auto& f : txf_) todas.push_back(&f);
    for (size_t i = 0; i < todas.size(); ++i) {
        if (todas[i]->tam &&
            uint32_t(todas[i]->inicio) + todas[i]->tam > ram_.size())
            solape_ = true;
        for (size_t j = i + 1; j < todas.size(); ++j)
            if (choca(todas[i]->inicio, todas[i]->tam,
                      todas[j]->inicio, todas[j]->tam))
                solape_ = true;
    }
}

// ===========================================================================
// La electricidad
// ===========================================================================
inline void OtgBase::conduce_par(UsbLinea l) {
    if (!dm_) return;
    const float V = 3.3f, R = 45.0f;              // impedancia del transceptor
    switch (l) {
        case UsbLinea::SE0: dp_->set_drive(id_dp_, 0.0f, R);
                            dm_->set_drive(id_dm_, 0.0f, R); break;
        case UsbLinea::J:   // FS: J = D+ alto. LS: al revés.
            if (vel_ == 2) { dp_->set_drive(id_dp_, 0.0f, R); dm_->set_drive(id_dm_, V, R); }
            else           { dp_->set_drive(id_dp_, V, R);    dm_->set_drive(id_dm_, 0.0f, R); }
            break;
        case UsbLinea::K:
            if (vel_ == 2) { dp_->set_drive(id_dp_, V, R);    dm_->set_drive(id_dm_, 0.0f, R); }
            else           { dp_->set_drive(id_dp_, 0.0f, R); dm_->set_drive(id_dm_, V, R); }
            break;
        case UsbLinea::SE1: dp_->set_drive(id_dp_, V, R); dm_->set_drive(id_dm_, V, R); break;
        default:            dp_->set_hiz(id_dp_); dm_->set_hiz(id_dm_); break;
    }
}

inline void OtgBase::actualiza_pines() {
    if (!dm_) return;
    if (!phy_encendido()) { soltar_par(); return; }

    if (host_ && caps.host) {
        // ANFITRIÓN: 15 kohm a masa en los dos hilos. Es lo único que hace
        // falta para "ver" a un dispositivo: el que se enchufe traerá su
        // 1,5 kohm a 3,3 V y ganará el divisor.
        const bool pwr = (hprt_ & HPRT_PPWR) != 0;
        dp_->set_drive(id_pu_,  0.0f, pwr ? 15.0e3f : R_HIZ);
        dm_->set_drive(id_pum_, 0.0f, pwr ? 15.0e3f : R_HIZ);
        if (hprt_ & HPRT_PRST)       conduce_par(UsbLinea::SE0);  // reset de bus
        else if (hprt_ & HPRT_PRES)  conduce_par(UsbLinea::K);    // reanudación
        else if (hprt_ & HPRT_PCSTS) conduce_par(UsbLinea::J);    // reposo
        else                         soltar_par_datos();
    } else {
        // DISPOSITIVO: el 1,5 kohm de D+ es LO QUE DICE "estoy aquí". DCTL.SDIS
        // lo quita, y por eso una pila USB puede reenumerar sin tocar el cable.
        pullup_ = !(dctl_ & DCTL_SDIS) && vbus_mv_ > 3000u;
        dp_->set_drive(id_pu_, pullup_ ? 3.3f : 0.0f, pullup_ ? 1.5e3f : R_HIZ);
        dm_->set_hiz(id_pum_);
        if (dctl_ & DCTL_RWUSIG) conduce_par(UsbLinea::K);        // despertar
        else { dp_->set_hiz(id_dp_); dm_->set_hiz(id_dm_); }
    }
}

inline void OtgBase::linea_proc() {
    if (!dm_) return;
    const float vp = dp_->voltage(), vm = dm_->voltage();
    const bool hp = vp > 1.6f, hm = vm > 1.6f;
    const UsbLinea antes = linea_;
    if (!hp && !hm)      linea_ = UsbLinea::SE0;
    else if (hp && hm)   linea_ = UsbLinea::SE1;
    else if (hp)         linea_ = (vel_ == 2) ? UsbLinea::K : UsbLinea::J;
    else                 linea_ = (vel_ == 2) ? UsbLinea::J : UsbLinea::K;

    // Un par que NADIE sujeta no dice nada: su tension es la ultima que hubo,
    // no una medida. Con el puerto alimentado eso no puede pasar -los dos
    // 15 kohm lo sujetan-, y si pasa es que aun no se han aplicado.
    const bool suelto = dp_->floating() && dm_->floating();
    if (host_ && caps.host && (hprt_ & HPRT_PPWR) && !suelto) {
        // DETECCIÓN DE CONEXIÓN Y DE VELOCIDAD, que es puro divisor resistivo:
        // el hilo que suba es el que lleva el 1,5 kohm del dispositivo.
        const bool hay = hp || hm;
        const bool tenia = (hprt_ & HPRT_PCSTS) != 0;
        if (hay && !tenia && !(hprt_ & HPRT_PRST)) {
            hprt_ |= HPRT_PCSTS | HPRT_PCDET;
            vel_ = hp ? 1u : 2u;                       // D+ = FS, D- = LS
            hprt_ = (hprt_ & ~HPRT_PSPD) | (uint32_t(vel_) << 17);
            set_int(INT_HPRTINT);
        } else if (!hay && tenia) {
            hprt_ &= ~(HPRT_PCSTS | HPRT_PENA);
            hprt_ |= HPRT_PCDET | HPRT_PENCHNG;
            set_int(INT_HPRTINT | INT_DISCINT);
        }
    }
    if (!host_ && caps.device && antes != linea_) {
        // EL RESET DE BUS NO ES UN REGISTRO: SON DOS HILOS A CERO. Un
        // dispositivo se reinicia cuando ve SE0 durante mas de 2,5 us, y esa
        // es la unica senal que tiene. Aqui se mide de verdad: se apunta
        // cuando empieza el SE0 y se comprueba cuando termina.
        if (linea_ == UsbLinea::SE0) t_se0_ = sc_core::sc_time_stamp();
        else if (antes == UsbLinea::SE0 && pullup_ &&
                 sc_core::sc_time_stamp() - t_se0_ >
                     sc_core::sc_time(2500, sc_core::SC_NS)) {
            bus_reset_visto();
        }
        // Y la K larga es el anfitrion diciendo "despierta".
        if (linea_ == UsbLinea::K && (dsts_ & DSTS_SUSPSTS)) {
            dsts_ &= ~DSTS_SUSPSTS;
            o_wkup_ = true;
            set_int(INT_WKUINT);
            publica();
        }
    }
    phy_ev_.notify(sc_core::SC_ZERO_TIME);
}

inline void OtgBase::vbus_proc() {
    if (!vbus_) return;
    vbus_mv_ = uint32_t(vbus_->voltage() * 1000.0f);
    const bool valido = vbus_mv_ > 4400u;
    const uint32_t antes = gotgctl_;
    if (caps.otg) {
        // Los comparadores de sesión solo cuentan si el firmware los ha
        // encendido en GCCFG. Con NOVBUSSENS el núcleo se lo cree todo.
        const bool forz = (gccfg_ & CCFG_NOVBUSSENS) != 0;
        if ((gccfg_ & CCFG_VBUSBSEN) || forz)
            gotgctl_ = (gotgctl_ & ~OTGCTL_BSVLD) | ((valido || forz) ? OTGCTL_BSVLD : 0u);
        if ((gccfg_ & CCFG_VBUSASEN) || forz)
            gotgctl_ = (gotgctl_ & ~OTGCTL_ASVLD) | ((valido || forz) ? OTGCTL_ASVLD : 0u);
        if (antes != gotgctl_) { gotgint_ |= 1u << 8; set_int(INT_OTGINT); }
    }
    phy_ev_.notify(sc_core::SC_ZERO_TIME);
}

inline void OtgBase::id_proc() {
    if (!id_net_ || !caps.otg) return;
    // ID a masa = cable A = anfitrión. Al aire (pull-up interno) = dispositivo.
    const bool b_dev = id_net_->voltage() > 1.6f;
    const bool antes = host_;
    if (b_dev) { gotgctl_ |= OTGCTL_CIDSTS;  host_ = false; }
    else       { gotgctl_ &= ~OTGCTL_CIDSTS; host_ = caps.host; }
    // Los bits FHMOD/FDMOD de GUSBCFG fuerzan el rol por encima del pin: es lo
    // que usa toda placa que no lleva conector OTG de cinco hilos.
    if (gusbcfg_ & USB_FHMOD) host_ = caps.host;
    if (gusbcfg_ & USB_FDMOD) host_ = false;
    gintsts_ = (gintsts_ & ~INT_CMOD) | (host_ ? INT_CMOD : 0u);
    if (antes != host_) { set_int(INT_CIDSCHG); }
    phy_ev_.notify(sc_core::SC_ZERO_TIME);
}

// ===========================================================================
// Interrupciones
// ===========================================================================
inline void OtgBase::actualiza_irq() {
    // DAINT es un resumen: un bit por endpoint, y la agregación en IEPINT y
    // OEPINT de GINTSTS. Tres niveles de máscara antes del NVIC.
    uint32_t daint = 0;
    for (unsigned i = 0; i < ep_.size(); ++i) {
        if (ep_[i].diepint & (diepmsk_ | (((diepempmsk_ >> i) & 1u) ? EPI_TXFE : 0u)))
            daint |= 1u << i;
        if (ep_[i].doepint & doepmsk_) daint |= 1u << (16 + i);
    }
    if (daint & daintmsk_ & 0xFFFFu)      gintsts_ |= INT_IEPINT;
    else                                  gintsts_ &= ~INT_IEPINT;
    if (daint & daintmsk_ & 0xFFFF0000u)  gintsts_ |= INT_OEPINT;
    else                                  gintsts_ &= ~INT_OEPINT;
    uint32_t haint = 0;
    for (unsigned i = 0; i < ch_.size() && i < caps.canales; ++i)
        if (ch_[i].hcint & ch_[i].hcintmsk) haint |= 1u << i;
    if (haint & haintmsk_) gintsts_ |= INT_HCINT; else gintsts_ &= ~INT_HCINT;

    const bool act = (gahbcfg_ & AHB_GINTMSK) && (gintsts_ & gintmsk_) != 0;
    // Las dos líneas dedicadas del HS: el endpoint 1 tiene IRQ propia para que
    // un flujo isócrono no dependa de la latencia del manejador global.
    const bool e1i = caps.ep1_irq && ep_.size() > 1 &&
                     (ep_[1].diepint & diepmsk_) != 0;
    const bool e1o = caps.ep1_irq && ep_.size() > 1 &&
                     (ep_[1].doepint & doepmsk_) != 0;
    if (act != o_irq_ || e1i != o_ep1i_ || e1o != o_ep1o_) {
        o_irq_ = act; o_ep1i_ = e1i; o_ep1o_ = e1o;
        publica();
    }
}

// ===========================================================================
// El DMA interno (solo HS): el núcleo va él mismo a por la memoria
// ===========================================================================
inline bool OtgBase::dma_write(uint32_t dir, const uint8_t* d, unsigned n) {
    tlm::tlm_generic_payload gp;
    AhbExt ext; ext.master = BusMaster::OTG_HS_DMA; ext.privileged = true;
    gp.set_command(tlm::TLM_WRITE_COMMAND);
    gp.set_address(dir);
    gp.set_data_ptr(const_cast<unsigned char*>(d));
    gp.set_data_length(n);
    gp.set_streaming_width(n);
    gp.set_byte_enable_ptr(nullptr);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    gp.set_extension(&ext);
    sc_core::sc_time t = sc_core::SC_ZERO_TIME;
    dma_m->b_transport(gp, t);
    gp.clear_extension(&ext);
    if (t > sc_core::SC_ZERO_TIME) sc_core::wait(t);
    ++n_dma_wr_;
    return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
}
inline bool OtgBase::dma_read(uint32_t dir, uint8_t* d, unsigned n) {
    tlm::tlm_generic_payload gp;
    AhbExt ext; ext.master = BusMaster::OTG_HS_DMA; ext.privileged = true;
    gp.set_command(tlm::TLM_READ_COMMAND);
    gp.set_address(dir);
    gp.set_data_ptr(d);
    gp.set_data_length(n);
    gp.set_streaming_width(n);
    gp.set_byte_enable_ptr(nullptr);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    gp.set_extension(&ext);
    sc_core::sc_time t = sc_core::SC_ZERO_TIME;
    dma_m->b_transport(gp, t);
    gp.clear_extension(&ext);
    if (t > sc_core::SC_ZERO_TIME) sc_core::wait(t);
    ++n_dma_rd_;
    return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
}

// ===========================================================================
// LECTURA DE REGISTROS
//
// La regla que decide qué se lee es siempre la misma: si el rasgo no está, el
// registro no existe y vale cero -igual que una dirección reservada del
// silicio-. No hay excepciones "por comodidad".
// ===========================================================================
inline uint32_t OtgBase::reg_read(uint32_t off) {
    // --- Ventanas de FIFO: leer de aquí SACA una palabra de la de recepción --
    if (off >= R_FIFO0 && off < R_FIFO0 * (caps.endpoints + 2u)) {
        if (!rxf_.n) return 0;
        return fifo_pop(rxf_);
    }
    // --- Registros por canal de anfitrión (todos iguales entre sí) ----------
    if (caps.host && off >= R_HC0 && off < R_HC0 + 0x20u * caps.canales) {
        const unsigned c = (off - R_HC0) / 0x20u;
        switch ((off - R_HC0) % 0x20u) {
            case 0x00: return ch_[c].hcchar;
            case 0x04: return caps.split ? ch_[c].hcsplt : 0u;
            case 0x08: return ch_[c].hcint;
            case 0x0C: return ch_[c].hcintmsk;
            case 0x10: return ch_[c].hctsiz;
            case 0x14: return caps.dma_interno ? ch_[c].hcdma : 0u;
            default:   return 0;
        }
    }
    // --- Registros por endpoint (EP0 NO es uno más) -------------------------
    if (caps.device && off >= R_DIEP0 && off < R_DIEP0 + 0x20u * caps.endpoints) {
        const unsigned e = (off - R_DIEP0) / 0x20u;
        switch ((off - R_DIEP0) % 0x20u) {
            case 0x00: return ep_[e].diepctl;
            case 0x08: return ep_[e].diepint |
                              (txf_[e].n < txf_[e].tam ? EPI_TXFE : 0u);
            case 0x10: return ep_[e].dieptsiz;
            case 0x14: return caps.dma_interno ? ep_[e].diepdma : 0u;
            // DTXFSTS: las palabras LIBRES que quedan en la FIFO de este
            // endpoint. Es lo que mira un manejador antes de empujar datos.
            case 0x18: return txf_[e].tam - txf_[e].n;
            default:   return 0;
        }
    }
    if (caps.device && off >= R_DOEP0 && off < R_DOEP0 + 0x20u * caps.endpoints) {
        const unsigned e = (off - R_DOEP0) / 0x20u;
        switch ((off - R_DOEP0) % 0x20u) {
            case 0x00: return ep_[e].doepctl;
            case 0x08: return ep_[e].doepint;
            case 0x10: return ep_[e].doeptsiz;
            case 0x14: return caps.dma_interno ? ep_[e].doepdma : 0u;
            default:   return 0;
        }
    }
    if (off >= R_DIEPTXF1 && off < R_DIEPTXF1 + 4u * 8u) {
        const unsigned i = (off - R_DIEPTXF1) / 4u;
        // Solo existen las FIFO de transmisión de los endpoints que hay.
        return (i + 1 < caps.endpoints) ? dieptxf_[i] : 0u;
    }
    switch (off) {
        case R_GOTGCTL: return caps.otg ? gotgctl_ : 0u;
        case R_GOTGINT: return caps.otg ? gotgint_ : 0u;
        case R_GAHBCFG: return gahbcfg_;
        case R_GUSBCFG: return gusbcfg_;
        case R_GRSTCTL: return grstctl_;
        case R_GINTSTS: return gintsts_;
        case R_GINTMSK: return gintmsk_;
        // GRXSTSR MIRA la cabeza de la cola de estado; GRXSTSP la SACA. Es la
        // única pareja de registros del chip que se diferencia solo en eso.
        case R_GRXSTSR: return rx_sts_.empty() ? 0u : rx_sts_.front();
        case R_GRXSTSP: {
            if (rx_sts_.empty()) return 0;
            const uint32_t v = rx_sts_.front();
            rx_sts_.pop_front();
            if (rx_sts_.empty()) { gintsts_ &= ~INT_RXFLVL; actualiza_irq(); }
            return v;
        }
        case R_GRXFSIZ:  return grxfsiz_;
        // 0x028 es DIEPTXF0 en modo dispositivo y HNPTXFSIZ en modo anfitrión:
        // el MISMO registro con dos significados según el rol.
        case R_DIEPTXF0: return dieptxf0_;
        case R_HNPTXSTS: return 0x00080200u;
        case R_GCCFG:    return gccfg_;
        case R_CID:      return caps.cid;
        case R_HPTXFSIZ: return caps.host ? hptxfsiz_ : 0u;
        case R_HCFG:     return caps.host ? hcfg_ : 0u;
        case R_HFIR:     return caps.host ? hfir_ : 0u;
        case R_HFNUM:    return caps.host ? hfnum_ : 0u;
        case R_HPTXSTS:  return caps.host ? 0x00080100u : 0u;
        case R_HAINT: {
            if (!caps.host) return 0;
            uint32_t h = 0;
            for (unsigned i = 0; i < caps.canales; ++i)
                if (ch_[i].hcint & ch_[i].hcintmsk) h |= 1u << i;
            return h;
        }
        case R_HAINTMSK: return caps.host ? haintmsk_ : 0u;
        case R_HPRT:     return caps.host ? hprt_ : 0u;
        case R_DCFG:     return caps.device ? dcfg_ : 0u;
        case R_DCTL:     return caps.device ? dctl_ : 0u;
        case R_DSTS:     return caps.device ? dsts_ : 0u;
        case R_DIEPMSK:  return caps.device ? diepmsk_ : 0u;
        case R_DOEPMSK:  return caps.device ? doepmsk_ : 0u;
        case R_DAINT: {
            if (!caps.device) return 0;
            uint32_t d = 0;
            for (unsigned i = 0; i < ep_.size(); ++i) {
                if (ep_[i].diepint) d |= 1u << i;
                if (ep_[i].doepint) d |= 1u << (16 + i);
            }
            return d;
        }
        case R_DAINTMSK:   return caps.device ? daintmsk_ : 0u;
        case R_DVBUSDIS:   return (caps.device && caps.otg) ? dvbusdis_ : 0u;
        case R_DVBUSPULSE: return (caps.device && caps.otg) ? dvbuspulse_ : 0u;
        case R_DIEPEMPMSK: return caps.device ? diepempmsk_ : 0u;
        case R_PCGCCTL:    return pcgcctl_;
        default: return 0;
    }
}

// ===========================================================================
// ESCRITURA DE REGISTROS
// ===========================================================================
inline void OtgBase::reg_write(uint32_t off, uint32_t v, uint32_t /*be*/) {
    // --- Ventanas de FIFO: escribir aquí EMPUJA en la FIFO del endpoint n ---
    if (off >= R_FIFO0 && off < R_FIFO0 * (caps.endpoints + 2u)) {
        const unsigned n = off / R_FIFO0 - 1u;
        if (n < txf_.size()) {
            fifo_push(txf_[n], v);
            motor_ev_.notify(sc_core::SC_ZERO_TIME);
        }
        return;
    }
    if (caps.host && off >= R_HC0 && off < R_HC0 + 0x20u * caps.canales) {
        const unsigned c = (off - R_HC0) / 0x20u;
        switch ((off - R_HC0) % 0x20u) {
            case 0x00: {
                const bool antes = (ch_[c].hcchar & HCC_CHENA) != 0;
                ch_[c].hcchar = v & ~(HCC_CHDIS);
                if (v & HCC_CHDIS) { ch_[c].hcchar &= ~HCC_CHENA;
                                     ch_[c].hcint |= HCI_CHH; actualiza_irq(); }
                else if (!antes && (v & HCC_CHENA)) motor_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x04: if (caps.split) ch_[c].hcsplt = v; return;
            case 0x08: ch_[c].hcint &= ~v; actualiza_irq(); return;  // rc_w1
            case 0x0C: ch_[c].hcintmsk = v & 0x7FFu; actualiza_irq(); return;
            case 0x10: ch_[c].hctsiz = v & 0x7FFFFFFFu; return;
            case 0x14: if (caps.dma_interno) ch_[c].hcdma = v; return;
            default: return;
        }
    }
    if (caps.device && off >= R_DIEP0 && off < R_DIEP0 + 0x20u * caps.endpoints) {
        const unsigned e = (off - R_DIEP0) / 0x20u;
        Endpoint& E = ep_[e];
        switch ((off - R_DIEP0) % 0x20u) {
            case 0x00: {
                const uint32_t m = mask_epctl(e, true);
                uint32_t nv = (E.diepctl & ~m) | (v & m);
                // CNAK/SNAK y SD0PID/SD1PID son ÓRDENES, no bits de estado: se
                // ejecutan y no se guardan.
                nv &= ~(EPC_CNAK | EPC_SNAK | EPC_SD0PID | EPC_SD1PID | EPC_EPDIS);
                if (v & EPC_CNAK) nv &= ~EPC_NAKSTS;
                if (v & EPC_SNAK) nv |= EPC_NAKSTS;
                if (v & EPC_SD0PID) nv &= ~EPC_DPID;
                if (v & EPC_SD1PID) nv |= EPC_DPID;
                if ((v & EPC_EPDIS) && e != 0) {
                    nv &= ~EPC_EPENA; E.diepint |= EPI_EPDISD;
                }
                E.diepctl = nv;
                if (v & EPC_EPENA) {
                    // Arranca la transferencia: PKTCNT paquetes de MPSIZ.
                    E.in_pkt   = (E.dieptsiz >> 19) & (e == 0 ? 3u : 0x3FFu);
                    E.in_bytes = E.dieptsiz & (e == 0 ? 0x7Fu : 0x7FFFFu);
                    motor_ev_.notify(sc_core::SC_ZERO_TIME);
                }
                actualiza_irq();
                return;
            }
            case 0x08: E.diepint &= ~v; actualiza_irq(); return;
            case 0x10: E.dieptsiz = v & mask_eptsiz(e, true); return;
            case 0x14: if (caps.dma_interno) E.diepdma = v; return;
            default: return;
        }
    }
    if (caps.device && off >= R_DOEP0 && off < R_DOEP0 + 0x20u * caps.endpoints) {
        const unsigned e = (off - R_DOEP0) / 0x20u;
        Endpoint& E = ep_[e];
        switch ((off - R_DOEP0) % 0x20u) {
            case 0x00: {
                const uint32_t m = mask_epctl(e, false);
                uint32_t nv = (E.doepctl & ~m) | (v & m);
                nv &= ~(EPC_CNAK | EPC_SNAK | EPC_SD0PID | EPC_SD1PID | EPC_EPDIS);
                if (v & EPC_CNAK) nv &= ~EPC_NAKSTS;
                if (v & EPC_SNAK) nv |= EPC_NAKSTS;
                if ((v & EPC_EPDIS) && e != 0) {
                    nv &= ~EPC_EPENA; E.doepint |= EPI_EPDISD;
                }
                E.doepctl = nv;
                if (v & EPC_EPENA) {
                    E.out_pkt   = (E.doeptsiz >> 19) & (e == 0 ? 1u : 0x3FFu);
                    if (!E.out_pkt) E.out_pkt = 1;
                    E.out_bytes = E.doeptsiz & (e == 0 ? 0x7Fu : 0x7FFFFu);
                }
                actualiza_irq();
                return;
            }
            case 0x08: E.doepint &= ~v; actualiza_irq(); return;
            case 0x10: E.doeptsiz = v & mask_eptsiz(e, false); return;
            case 0x14: if (caps.dma_interno) E.doepdma = v; return;
            default: return;
        }
    }
    if (off >= R_DIEPTXF1 && off < R_DIEPTXF1 + 4u * 8u) {
        const unsigned i = (off - R_DIEPTXF1) / 4u;
        if (i + 1 < caps.endpoints) { dieptxf_[i] = v; reparte_fifos(); }
        return;
    }
    switch (off) {
        case R_GOTGCTL:
            if (!caps.otg) return;
            gotgctl_ = (gotgctl_ & ~0x0F03u) | (v & 0x0F03u);
            return;
        case R_GOTGINT: if (caps.otg) { gotgint_ &= ~v; actualiza_irq(); } return;
        case R_GAHBCFG: gahbcfg_ = v & mask_gahbcfg(); actualiza_irq(); return;
        case R_GUSBCFG:
            gusbcfg_ = (v & mask_gusbcfg()) | gusbcfg_fijo();
            id_proc();
            return;
        case R_GRSTCTL: {
            // Los reset del núcleo son AUTOBORRABLES: el firmware escribe el
            // bit y espera a que se apague. Un modelo que lo deje puesto cuelga
            // a cualquier pila USB en el arranque.
            if (v & RST_CSRST) {
                const uint32_t cfg = gahbcfg_, usb = gusbcfg_, cc = gccfg_;
                reset_regs();
                gahbcfg_ = cfg; gusbcfg_ = usb; gccfg_ = cc;
            }
            if (v & RST_RXFFLSH) { fifo_vacia(rxf_); rx_sts_.clear();
                                   gintsts_ &= ~INT_RXFLVL; }
            if (v & RST_TXFFLSH) {
                const unsigned n = (v >> 6) & 0x1Fu;
                if (n == 0x10u) for (auto& f : txf_) fifo_vacia(f);
                else if (n < txf_.size()) fifo_vacia(txf_[n]);
            }
            grstctl_ = RST_AHBIDL;          // todo hecho, y el AHB en reposo
            actualiza_irq();
            return;
        }
        case R_GINTSTS: {
            // Casi todo GINTSTS es rc_w1, pero CMOD, IEPINT, OEPINT, RXFLVL y
            // los resúmenes de puerto y canal son SOLO LECTURA: se borran
            // atendiendo su causa, no escribiendo aquí.
            const uint32_t ro = INT_CMOD | INT_RXFLVL | INT_IEPINT | INT_OEPINT |
                                INT_HPRTINT | INT_HCINT | INT_OTGINT |
                                INT_NPTXFE | INT_PTXFE;
            gintsts_ &= ~(v & ~ro);
            if (v & INT_WKUINT) { o_wkup_ = false; publica(); }
            actualiza_irq();
            return;
        }
        case R_GINTMSK: gintmsk_ = v & ~INT_CMOD; actualiza_irq(); return;
        case R_GRXFSIZ: grxfsiz_ = v & 0xFFFFu; reparte_fifos(); return;
        case R_DIEPTXF0: dieptxf0_ = v; reparte_fifos(); return;
        case R_GCCFG:   gccfg_ = v & mask_gccfg(); vbus_proc();
                        actualiza_pines(); return;
        case R_HPTXFSIZ: if (caps.host) hptxfsiz_ = v; return;
        case R_HCFG:    if (caps.host) hcfg_ = v & 0x7u; return;
        case R_HFIR:    if (caps.host) hfir_ = v & 0xFFFFu; return;
        case R_HAINTMSK: if (caps.host) { haintmsk_ = v & 0xFFFFu; actualiza_irq(); } return;
        case R_HPRT: {
            if (!caps.host) return;
            // OJO: PENA se BORRA escribiendo uno. Un read-modify-write que
            // devuelva el registro entero apaga el puerto que acaba de
            // habilitarse. Es EL error clásico del OTG en modo anfitrión, y
            // aquí se reproduce tal cual.
            const uint32_t m = mask_hprt();
            const uint32_t antes = hprt_;
            hprt_ = (hprt_ & ~(m & ~HPRT_RC_W1)) | (v & m & ~HPRT_RC_W1);
            hprt_ &= ~(v & HPRT_RC_W1);
            if ((v & HPRT_PRST) && !(antes & HPRT_PRST)) {
                // Reset de bus: SE0 en el cable. El dispositivo lo verá.
                phy_ev_.notify(sc_core::SC_ZERO_TIME);
            }
            if (!(v & HPRT_PRST) && (antes & HPRT_PRST)) {
                // Al soltar el reset, el puerto queda habilitado.
                hprt_ |= HPRT_PENA | HPRT_PENCHNG;
                if (dispo_) dispo_->sof(0);
                set_int(INT_HPRTINT);
            }
            actualiza_pines();          // los 15 kohm, ya: no en el delta siguiente
            actualiza_irq();
            return;
        }
        case R_DCFG:
            if (!caps.device) return;
            dcfg_ = v & mask_dcfg();
            // DSPD son dos bits en todos los nucleos, pero sin PHY de ALTA
            // velocidad los codigos 00 y 01 no existen: el bit 1 se queda a
            // uno pase lo que pase. No es una mascara, es un valor forzado.
            if (!caps.hs) dcfg_ |= 2u;
            return;
        case R_DCTL: {
            if (!caps.device) return;
            dctl_ = v & 0x0FFFu & ~(DCTL_GINSTS | DCTL_GONSTS |
                                    DCTL_SGINAK | DCTL_CGINAK |
                                    DCTL_SGONAK | DCTL_CGONAK);
            if (v & DCTL_SGINAK) dctl_ |= DCTL_GINSTS;
            if (v & DCTL_CGINAK) dctl_ &= ~DCTL_GINSTS;
            if (v & DCTL_SGONAK) dctl_ |= DCTL_GONSTS;
            if (v & DCTL_CGONAK) dctl_ &= ~DCTL_GONSTS;
            actualiza_pines();                       // SDIS mueve el pull-up
            return;
        }
        case R_DIEPMSK:  if (caps.device) { diepmsk_ = v & 0xBFu; actualiza_irq(); } return;
        case R_DOEPMSK:  if (caps.device) { doepmsk_ = v & 0x7Fu; actualiza_irq(); } return;
        case R_DAINTMSK: if (caps.device) { daintmsk_ = v; actualiza_irq(); } return;
        case R_DIEPEMPMSK: if (caps.device) { diepempmsk_ = v & 0xFFFFu; actualiza_irq(); } return;
        case R_DVBUSDIS:   if (caps.device && caps.otg) dvbusdis_ = v & 0xFFFFu; return;
        case R_DVBUSPULSE: if (caps.device && caps.otg) dvbuspulse_ = v & 0xFFFu; return;
        case R_PCGCCTL:    pcgcctl_ = v & 0x1Fu; return;
        default: return;
    }
}

// ===========================================================================
// MODO DISPOSITIVO: lo que llega por el cable
//
// El aparejo externo que hace de anfitrión llama aquí con un testigo. La
// respuesta -ACK, NAK o STALL- sale ENTERA de los registros del endpoint, que
// es exactamente como se comporta el silicio: el firmware no contesta a los
// paquetes, contesta a las interrupciones que dejan los paquetes.
// ===========================================================================
inline uint8_t OtgBase::transaccion(uint8_t pid, uint8_t addr, uint8_t ep,
                                    const std::vector<uint8_t>& salida,
                                    std::vector<uint8_t>& entrada) {
    if (!caps.device || host_ || !phy_encendido() || !pullup_) return PID_NADIE;
    if (ep >= caps.endpoints) return PID_NADIE;
    // La dirección del dispositivo vive en DCFG.DAD, y hasta que el anfitrión
    // no la asigna vale cero: por eso toda enumeración empieza hablando al 0.
    const uint8_t dad = uint8_t((dcfg_ >> 4) & 0x7Fu);
    if (addr != dad) return PID_NADIE;
    Endpoint& E = ep_[ep];
    t_actividad_ = sc_core::sc_time_stamp();

    if (pid == PID_SETUP) {
        // Un SETUP se acepta SIEMPRE, aunque el endpoint esté en NAK o en
        // STALL: es la única forma que tiene el anfitrión de recuperar un
        // dispositivo atascado [IR, §12.15.1].
        E.doepctl &= ~(EPC_STALL | EPC_NAKSTS);
        const unsigned n = unsigned(salida.size());
        rx_sts_.push_back(uint32_t(ep) | (uint32_t(n) << 4) |
                          (0u << 15) | (PKTSTS_SETUP_DATA << 17));
        for (unsigned i = 0; i < n; i += 4) {
            uint32_t w = 0;
            for (unsigned k = 0; k < 4 && i + k < n; ++k)
                w |= uint32_t(salida[i + k]) << (8 * k);
            fifo_push(rxf_, w);
        }
        rx_sts_.push_back(uint32_t(ep) | (PKTSTS_SETUP_COMP << 17));
        E.doepint |= EPI_STUP;
        // Un SETUP deja el bit de conmutación de datos preparado para DATA1,
        // que es lo que toca en la fase de datos de un control.
        E.diepctl |= EPC_DPID;
        ++n_out_;
        set_int(INT_RXFLVL);
        actualiza_irq();
        return PID_ACK;
    }
    if (pid == PID_OUT) {
        if (E.doepctl & EPC_STALL) return PID_STALL;
        if (!(E.doepctl & EPC_EPENA) || (E.doepctl & EPC_NAKSTS) ||
            (dctl_ & DCTL_GONSTS)) return PID_NAK;
        const unsigned n = unsigned(salida.size());
        if (caps.dma_interno && (gahbcfg_ & AHB_DMAEN)) {
            // CON DMA NO HAY FIFO QUE LEER: el núcleo escribe él mismo en la
            // memoria, en la dirección que le dejaron en DOEPDMA. Es LA
            // diferencia práctica del HS, y por la que un manejador de HS con
            // DMA no se parece en nada a uno de FS.
            if (n) dma_write(E.doepdma, salida.data(), n);
            E.doepdma += n;
        } else {
            rx_sts_.push_back(uint32_t(ep) | (uint32_t(n) << 4) |
                              (((E.doepctl & EPC_DPID) ? 1u : 0u) << 15) |
                              (PKTSTS_OUT_DATA << 17));
            for (unsigned i = 0; i < n; i += 4) {
                uint32_t w = 0;
                for (unsigned k = 0; k < 4 && i + k < n; ++k)
                    w |= uint32_t(salida[i + k]) << (8 * k);
                fifo_push(rxf_, w);
            }
            set_int(INT_RXFLVL);
        }
        ++n_out_;
        E.out_bytes = (E.out_bytes > n) ? E.out_bytes - n : 0u;
        if (E.out_pkt) --E.out_pkt;
        const unsigned mps = mps_de(ep, E.doepctl);
        if (!E.out_pkt || n < mps || !E.out_bytes) {
            // Fin de transferencia: por cuenta de paquetes o por paquete corto.
            if (!(caps.dma_interno && (gahbcfg_ & AHB_DMAEN)))
                rx_sts_.push_back(uint32_t(ep) | (PKTSTS_OUT_COMP << 17));
            E.doepctl &= ~EPC_EPENA;
            E.doepint |= EPI_XFRC;
        }
        E.doepctl ^= EPC_DPID;
        actualiza_irq();
        return PID_ACK;
    }
    if (pid == PID_IN) {
        if (E.diepctl & EPC_STALL) return PID_STALL;
        if (!(E.diepctl & EPC_EPENA) || (E.diepctl & EPC_NAKSTS)) return PID_NAK;
        const unsigned mps = mps_de(ep, E.diepctl);
        unsigned n = E.in_bytes < mps ? E.in_bytes : mps;
        entrada.clear();
        if (caps.dma_interno && (gahbcfg_ & AHB_DMAEN)) {
            entrada.resize(n);
            if (n) dma_read(E.diepdma, entrada.data(), n);
            E.diepdma += n;
        } else {
            const unsigned pal = (n + 3) / 4;
            if (txf_[ep].n < pal) return PID_NAK;      // el firmware no llegó
            for (unsigned i = 0; i < pal; ++i) {
                const uint32_t w = fifo_pop(txf_[ep]);
                for (unsigned k = 0; k < 4 && entrada.size() < n; ++k)
                    entrada.push_back(uint8_t(w >> (8 * k)));
            }
        }
        ++n_in_;
        E.in_bytes = (E.in_bytes > n) ? E.in_bytes - n : 0u;
        if (E.in_pkt) --E.in_pkt;
        if (!E.in_pkt || !E.in_bytes) {
            E.diepctl &= ~EPC_EPENA;
            E.diepint |= EPI_XFRC;
        }
        E.diepctl ^= EPC_DPID;
        actualiza_irq();
        return PID_ACK;
    }
    return PID_NADIE;
}

// El anfitrión manda un SOF cada milisegundo (o cada 125 us en High Speed).
// Para el dispositivo es el latido que le dice que sigue enchufado: si deja de
// llegar durante 3 ms, hay que suspenderse [IR, §12.15.1].
inline void OtgBase::sof(uint16_t trama) {
    if (!caps.device) return;
    ++n_sof_;
    dsts_ = (dsts_ & ~0x003FFF00u) | (uint32_t(trama & 0x3FFFu) << 8);
    t_actividad_ = sc_core::sc_time_stamp();
    if (dsts_ & DSTS_SUSPSTS) { dsts_ &= ~DSTS_SUSPSTS; }
    set_int(INT_SOF);
}

// El reset de bus (SE0 largo) lo ve el dispositivo como una orden: vuelve a la
// dirección cero, vacía las FIFO y avisa por dos interrupciones seguidas.
inline void OtgBase::bus_reset_visto() {
    if (!caps.device || host_) return;
    dcfg_ &= ~(0x7Fu << 4);                  // dirección 0 otra vez
    for (auto& E : ep_) {
        E.diepctl &= ~(EPC_EPENA | EPC_STALL);
        E.doepctl &= ~(EPC_EPENA | EPC_STALL);
        E.diepint = E.doepint = 0;
    }
    fifo_vacia(rxf_); rx_sts_.clear();
    for (auto& f : txf_) fifo_vacia(f);
    dsts_ &= ~DSTS_SUSPSTS;
    // ENUMSPD: la velocidad que ha quedado negociada. En un núcleo FS siempre
    // es Full Speed (11); un núcleo HS con PHY ULPI puede quedar en 00.
    const uint32_t espd = (caps.hs && caps.ulpi) ? 0u : 3u;
    dsts_ = (dsts_ & ~DSTS_ENUMSPD) | (espd << 1);
    set_int(INT_USBRST);
    set_int(INT_ENUMDNE);
}

// ===========================================================================
// MODO ANFITRIÓN: un canal, una transacción
// ===========================================================================
inline void OtgBase::canal_transaccion(unsigned c) {
    Canal& C = ch_[c];
    if (!(C.hcchar & HCC_CHENA)) return;
    if (!(hprt_ & HPRT_PENA) || !dispo_) {
        C.hcchar &= ~HCC_CHENA;
        C.hcint |= HCI_TXERR | HCI_CHH;
        actualiza_irq();
        return;
    }
    const unsigned mps = unsigned(C.hcchar & 0x7FFu);
    const unsigned epn = unsigned((C.hcchar >> 11) & 0xFu);
    const bool     in  = (C.hcchar & HCC_EPDIR) != 0;
    const unsigned dad = unsigned((C.hcchar >> 22) & 0x7Fu);
    const unsigned tipo = unsigned((C.hcchar >> 18) & 3u);
    unsigned pkt   = (C.hctsiz >> 19) & 0x3FFu;
    unsigned bytes = C.hctsiz & 0x7FFFFu;
    const unsigned dpid = (C.hctsiz >> 29) & 3u;
    // DPID = 3 en una transferencia de control significa SETUP, no un DATAx:
    // es la codificación que usa el núcleo para distinguirlas.
    const uint8_t token = in ? PID_IN
                             : ((tipo == 0 && dpid == 3) ? PID_SETUP : PID_OUT);

    std::vector<uint8_t> salida, entrada;
    if (!in) {
        const unsigned n = bytes < mps ? bytes : mps;
        if (caps.dma_interno && (gahbcfg_ & AHB_DMAEN)) {
            salida.resize(n);
            if (n) dma_read(C.hcdma, salida.data(), n);
            C.hcdma += n;
        } else {
            const unsigned pal = (n + 3) / 4;
            if (txf_[0].n < pal) { motor_ev_.notify(sc_core::SC_ZERO_TIME); return; }
            for (unsigned i = 0; i < pal; ++i) {
                const uint32_t w = fifo_pop(txf_[0]);
                for (unsigned k = 0; k < 4 && salida.size() < n; ++k)
                    salida.push_back(uint8_t(w >> (8 * k)));
            }
        }
    }
    const uint8_t r = dispo_->transaccion(token, uint8_t(dad), uint8_t(epn),
                                          salida, entrada);
    if (r == PID_NADIE)      { C.hcint |= HCI_TXERR; }
    else if (r == PID_STALL) { C.hcint |= HCI_STALL; }
    else if (r == PID_NAK)   { C.hcint |= HCI_NAK; }
    else {
        C.hcint |= HCI_ACK;
        if (in) {
            const unsigned n = unsigned(entrada.size());
            ++n_in_;
            if (caps.dma_interno && (gahbcfg_ & AHB_DMAEN)) {
                if (n) dma_write(C.hcdma, entrada.data(), n);
                C.hcdma += n;
            } else {
                rx_sts_.push_back(uint32_t(c) | (uint32_t(n) << 4) |
                                  (PKTSTS_IN_DATA << 17));
                for (unsigned i = 0; i < n; i += 4) {
                    uint32_t w = 0;
                    for (unsigned k = 0; k < 4 && i + k < n; ++k)
                        w |= uint32_t(entrada[i + k]) << (8 * k);
                    fifo_push(rxf_, w);
                }
                set_int(INT_RXFLVL);
            }
            bytes = (bytes > n) ? bytes - n : 0u;
            if (pkt) --pkt;
            if (!pkt || n < mps || !bytes) {
                C.hcchar &= ~HCC_CHENA;
                C.hcint |= HCI_XFRC | HCI_CHH;
            }
        } else {
            const unsigned n = unsigned(salida.size());
            ++n_out_;
            bytes = (bytes > n) ? bytes - n : 0u;
            if (pkt) --pkt;
            if (!pkt || !bytes) {
                C.hcchar &= ~HCC_CHENA;
                C.hcint |= HCI_XFRC | HCI_CHH;
            }
        }
    }
    if (r == PID_NAK || r == PID_STALL || r == PID_NADIE) {
        C.hcchar &= ~HCC_CHENA;
        C.hcint |= HCI_CHH;
    }
    C.hctsiz = (C.hctsiz & ~0x1FFFFFFFu) | (bytes & 0x7FFFFu) |
               ((pkt & 0x3FFu) << 19);
    actualiza_irq();
}

// ===========================================================================
// El motor: tramas, suspensión y los canales que hay que atender
// ===========================================================================
inline void OtgBase::motor_proc() {
    // Periodo de trama: 1 ms en Full Speed, 125 us en High Speed. Es el latido
    // del bus y de él cuelgan las transferencias periódicas.
    for (;;) {
        const sc_core::sc_time paso =
            (caps.hs && caps.ulpi && vel_ == 0)
                ? sc_core::sc_time(125, sc_core::SC_US)
                : sc_core::sc_time(1, sc_core::SC_MS);
        sc_core::wait(paso, motor_ev_);

        if (!rst_n.read() || !clock_enabled()) continue;
        // Sin los 48 MHz de PLL48CK no hay USB: el transceptor no tiene de
        // dónde sacar el reloj de bit. Es la avería número uno de una placa
        // nueva -y el registro no dice nada-.
        if (clk48_hz.read() < 40.0e6 && !(caps.hs && caps.ulpi)) continue;
        if (pcgcctl_ & 1u) continue;                    // reloj del PHY parado

        if (host_ && caps.host) {
            // El SOF va con el RELOJ, no con cada vez que alguien despierta al
            // motor por escribir un registro: si no, la cuenta de tramas se
            // dispararia con la actividad del firmware.
            if ((hprt_ & HPRT_PENA) &&
                sc_core::sc_time_stamp() - t_sof_ >= paso) {
                t_sof_ = sc_core::sc_time_stamp();
                hfnum_ = (hfnum_ & ~0xFFFFu) | ((hfnum_ + 1u) & 0x3FFFu);
                ++n_sof_;
                set_int(INT_SOF);
                if (dispo_) dispo_->sof(uint16_t(hfnum_ & 0x3FFFu));
            }
            for (unsigned c = 0; c < caps.canales; ++c)
                if (ch_[c].hcchar & HCC_CHENA) canal_transaccion(c);
        } else if (caps.device) {
            // Suspensión: 3 ms sin nada en el cable. El dispositivo tiene que
            // bajar a menos de 2,5 mA, y para eso primero tiene que enterarse.
            if (pullup_ && !(dsts_ & DSTS_SUSPSTS) &&
                sc_core::sc_time_stamp() - t_actividad_ >
                    sc_core::sc_time(3, sc_core::SC_MS)) {
                dsts_ |= DSTS_SUSPSTS;
                set_int(INT_USBSUSP);
            }
        }
    }
}

// ===========================================================================
// Selección del tipo: compilación
// ===========================================================================
template <const OtgCaps& C>
class OtgT : public OtgBase {
public:
    OtgT(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : OtgBase(nm, base, size, C) {}
    static constexpr const OtgCaps& rasgos = C;
};

// Las dos instancias del F407VG, con su base y su tamaño de bloque.
class OtgFs : public OtgT<CAPS_OTG_FS> {
public:
    explicit OtgFs(sc_core::sc_module_name nm)
        : OtgT<CAPS_OTG_FS>(nm, addr::OTG_FS_B, 0x40000) {}
};
class OtgHs : public OtgT<CAPS_OTG_HS> {
public:
    explicit OtgHs(sc_core::sc_module_name nm)
        : OtgT<CAPS_OTG_HS>(nm, addr::OTG_HS_B, 0x40000) {}
};

} // namespace stm32
#endif // STM32_PERIPH_OTG_H
