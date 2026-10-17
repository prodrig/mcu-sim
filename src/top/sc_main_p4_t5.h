// =============================================================================
// sc_main_p4_t5.h — Los grupos de testp4 de la fase T5, D4: el BSL del P4
//
// Se incluye desde top/sc_main_p4.cpp, como sc_main_p4_t4.h. El banco hace de
// BSL Scripter: habla el protocolo de TI por el backchannel (bcl, en memoria,
// a 9600 8E1) [SLAU622; segunda fuente: el código de BSL Scripter 3.4,
// UartComm.cpp y Family32bit.cpp]:
//
//   sincronía   el PC manda 0xFF y el BSL contesta 0x00 (mide los baudios)
//   paquete     0x80, longitud (16 bits, LE), datos, CRC-CCITT de los datos
//               (16 bits, LE, semilla 0xFFFF)
//   respuesta   un byte de ACK (0x00, o 0x51-0x56 si el paquete llegó mal) y,
//               según la orden, otro paquete: 0x3B y un código (mensaje), o
//               0x3A y datos
//
//   P68  el BSL B1 con la Flash en blanco: contraseña, borrado masivo,
//        p4_demo por RX_DATA_BLOCK_32, CRC_CHECK_32, TX_DATA_BLOCK_32,
//        TX_BSL_VERSION, una orden desconocida, y REBOOT_RESET: p4_demo arranca
//   P69  una aplicación (p4_bsl) entra en el BSL por su función de entrada
//        (0x0020_2000), y vuelve con LOAD_PC_32
//
// Y aparte, con `TESTP4_SOLO=bsl_ti P4_BSL_TI=<fichero>`, la misma sesión con
// el BSL de TI, que no se versiona.
// =============================================================================
#ifndef STM32_TOP_SC_MAIN_P4_T5_H
#define STM32_TOP_SC_MAIN_P4_T5_H

#include <fstream>
#include <vector>

static const char* const FW_DEMO_BIN_P4 = "verif/fw/msp432p4/p4_demo/p4_demo.bin";
static const char* const FW_BSL_APP = "verif/fw/msp432p4/p4_bsl/p4_bsl.out";

static uint16_t crc_bsl(const std::vector<uint8_t>& d, size_t desde = 0, size_t n = SIZE_MAX) {
    uint16_t crc = 0xFFFF;
    for (size_t i = desde; i < d.size() && i - desde < n; ++i) {
        uint8_t x = uint8_t((crc >> 8) ^ d[i]);
        x ^= x >> 4;
        crc = uint16_t((crc << 8) ^ (uint16_t(x) << 12) ^ (uint16_t(x) << 5) ^ x);
    }
    return crc;
}

inline bool TbP4::b_lee(uint8_t& b, double ms) {
    const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
    while (bcl->recibido().size() <= bpos_) {
        if (sc_time_stamp() >= lim) return false;
        wait(100, SC_US);
    }
    b = uint8_t(bcl->recibido()[bpos_++]);
    return true;
}

// Manda una orden y lee el ACK y, si la orden contesta, su paquete. Devuelve
// el ACK (0x00 bien), o -1 si no llega nada
inline int TbP4::b_paquete(const std::vector<uint8_t>& d, std::vector<uint8_t>* resp, double ms) {
    std::string p;
    p += char(0x80); p += char(d.size() & 0xFF); p += char(d.size() >> 8);
    for (uint8_t c : d) p += char(c);
    const uint16_t crc = crc_bsl(d);
    p += char(crc & 0xFF); p += char(crc >> 8);
    bcl->envia(p);
    uint8_t ack = 0;
    if (!b_lee(ack, ms + 1000.0 * double(p.size()) * 11.0 / 9600.0)) return -1;
    if (ack != 0x00 || !resp) return ack;
    uint8_t h = 0, l0 = 0, l1 = 0;
    if (!b_lee(h, ms) || h != 0x80 || !b_lee(l0, ms) || !b_lee(l1, ms)) return -1;
    const size_t n = size_t(l0) | (size_t(l1) << 8);
    resp->clear();
    for (size_t i = 0; i < n; ++i) {
        uint8_t c;
        if (!b_lee(c, ms)) return -1;
        resp->push_back(c);
    }
    uint8_t c0 = 0, c1 = 0;
    if (!b_lee(c0, ms) || !b_lee(c1, ms)) return -1;
    if (uint16_t(c0 | (c1 << 8)) != crc_bsl(*resp)) return -2;
    return 0;
}

inline bool TbP4::espera_estado(uint32_t e, double ms) {
    const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
    while (sc_time_stamp() < lim) {
        if (buzon(B_ESTADO) == e) return true;
        wait(50, SC_US);
    }
    return buzon(B_ESTADO) == e;
}

// Lo que hace BSL Scripter al abrir el puerto: 0xFF hasta que contesta 0x00
inline bool TbP4::b_sincroniza() {
    for (int i = 0; i < 10; ++i) {
        bcl->envia(std::string(1, char(0xFF)));
        uint8_t b = 0xFF;
        if (b_lee(b, 100) && b == 0x00) return true;
    }
    return false;
}

static std::vector<uint8_t> le_dir(uint8_t orden, uint32_t a) {
    return { orden, uint8_t(a), uint8_t(a >> 8), uint8_t(a >> 16), uint8_t(a >> 24) };
}

// La sesión de BSL Scripter con el ejemplo de TI (script_P4xx_uart.txt):
// contraseña, borrado masivo, la imagen, la versión, leerla, y reset
inline void TbP4::t5_bsl_sesion(const char* que, bool ti) {
    // La Flash en blanco, como un chip nuevo
    mcu.reset_pin(true);
    wait(20, SC_US);
    for (uint32_t a = 0; a < dut.flash.main_size(); ++a) dut.flash.poke_byte(a, 0xFF);
    uint8_t ceros[0x200] = {};
    dut.sram.load(ceros, sizeof ceros, 0);
    const unsigned e0 = dut.entradas_bsl();
    bcl->set_baudios(9600);
    FormatoUart f8e1; f8e1.paridad = Paridad::par;
    bcl->set_formato(f8e1);
    bcl->borra_recibido();
    bpos_ = 0;
    bcl->set_enabled(true);
    mcu.reset_pin(false);
    wait(5, SC_MS);
    char m[200];
    std::snprintf(m, sizeof m, "%s: con la Flash en blanco, el codigo de arranque entra en el BSL", que);
    const uint32_t pc = dut.core.cpu.pc();
    check(dut.entradas_bsl() == e0 + 1 && ((pc >= p4::BSL_B && pc < p4::BSL_B + p4::BSL_SIZE) ||
                                           (pc >= 0x02000000u && pc < 0x02008000u)), m);
    if (std::getenv("P4_BSL_TRAZA")) {
        std::printf("    pc 0x%08X, excepciones %llu, MCLK %.0f Hz, lpm %u\n", dut.core.cpu.pc(),
                    (unsigned long long)dut.core.cpu.exc_count, dut.f_mclk(), dut.modo_lpm());
        uint16_t v[16];
        for (unsigned k = 0; k < 16; ++k) tm.read16(0x40004C00u + 2 * k, v[k]);
        std::printf("    PA:");
        for (unsigned k = 0; k < 16; ++k) std::printf(" %04X", v[k]);
        uint32_t iser1 = 0, ispr1 = 0, stk = 0;
        tm.read32(0xE000E104u, iser1); tm.read32(0xE000E204u, ispr1); tm.read32(0xE000E010u, stk);
        std::printf("\n    ISER1 %08X ISPR1 %08X STCTRL %08X primask %u\n", iser1, ispr1, stk,
                    unsigned(dut.core.cpu.reg.primask));
    }
    check(b_sincroniza(), "0xFF a 9600 8E1: el BSL mide los baudios y contesta 0x00");
    if (std::getenv("P4_BSL_TRAZA"))
        std::printf("    pc 0x%08X, excepciones %llu, recibido %zu\n", dut.core.cpu.pc(),
                    (unsigned long long)dut.core.cpu.exc_count, bcl->recibido().size());
    std::vector<uint8_t> r;
    // Una contraseña mala: los 256 bytes de los vectores no son esos
    std::vector<uint8_t> pw(257, 0x00); pw[0] = 0x21;
    int a = b_paquete(pw, &r);
    check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x05,
          "RX_PASSWORD con una contrasena mala: mensaje 0x05 (PASSWORD_ERROR)");
    a = b_paquete({ 0x15 }, &r);
    check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x04,
          "y MASS_ERASE, con el BSL cerrado: 0x04 (LOCKED)");
    // La buena, con la Flash en blanco: 256 bytes a 0xFF
    pw.assign(257, 0xFF); pw[0] = 0x21;
    a = b_paquete(pw, &r);
    check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x00,
          "RX_PASSWORD con los vectores de la Flash en blanco (0xFF): abre el BSL");
    a = b_paquete({ 0x15 }, &r, 2000);
    check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x00, "MASS_ERASE: 0x00");
    // p4_demo, a bloques de 256 como BSL Scripter
    std::ifstream fi(FW_DEMO_BIN_P4, std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    const sc_time t0 = sc_time_stamp();
    bool bien = img.size() > 1000;
    for (size_t i = 0; i < img.size() && bien; i += 256) {
        std::vector<uint8_t> d = le_dir(0x20, uint32_t(i));
        for (size_t k = i; k < img.size() && k < i + 256; ++k) d.push_back(img[k]);
        while ((d.size() - 5) % 4) d.push_back(0xFF);
        a = b_paquete(d, &r, 500);
        bien = a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x00;
        if (!bien) std::printf("    bloque en 0x%05zX: ack %d, %zu bytes\n", i, a, r.size());
    }
    std::printf("    %zu bytes de p4_demo.bin en %.1f ms simulados\n", img.size(),
                (sc_time_stamp() - t0).to_seconds() * 1e3);
    check(bien, "RX_DATA_BLOCK_32: p4_demo.bin entero, a bloques de 256, todos con 0x00");
    bool igual = true;
    for (size_t i = 0; i < img.size(); ++i)
        if (uint8_t(dut.flash.peek32(uint32_t(i) & ~3u) >> (8 * (i & 3u))) != img[i]) igual = false;
    check(igual, "y la Flash tiene lo descargado, byte a byte");
    std::vector<uint8_t> c = le_dir(0x26, 0);
    c.push_back(uint8_t(img.size())); c.push_back(uint8_t(img.size() >> 8));
    a = b_paquete(c, &r);
    const uint16_t crc = crc_bsl(img);
    check(a == 0 && r.size() == 3 && r[0] == 0x3A && uint16_t(r[1] | (r[2] << 8)) == crc,
          "CRC_CHECK_32 de lo descargado: el mismo CRC que el del PC");
    c = le_dir(0x28, 0x100); c.push_back(32); c.push_back(0);
    a = b_paquete(c, &r);
    bool leido = a == 0 && r.size() == 33 && r[0] == 0x3A;
    for (size_t i = 0; leido && i < 32; ++i) leido = r[1 + i] == img[0x100 + i];
    check(leido, "TX_DATA_BLOCK_32: 32 bytes de 0x100, los mismos");
    a = b_paquete({ 0x19 }, &r);
    std::printf("    TX_BSL_VERSION:");
    for (size_t i = 1; i < r.size(); ++i) std::printf(" %02X", r[i]);
    std::printf("\n");
    check(a == 0 && r.size() == 11 && r[0] == 0x3A &&
          r[1] == uint8_t(dut.flash.peek32(p4::BSL_B + 0x10)) &&
          r[10] == uint8_t(dut.flash.peek32(p4::BSL_B + 0x18) >> 8),
          "TX_BSL_VERSION: los diez bytes de 0x0020_2010");
    if (!ti) {
        a = b_paquete({ 0x7E }, &r);
        check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x07,
              "una orden que no existe: 0x07 (UNKNOWN_COMMAND)");
        // Un paquete con el CRC mal: el ACK ya dice que llego mal
        bcl->envia(std::string("\x80\x01\x00\x19\x00\x00", 6));
        uint8_t ack = 0;
        check(b_lee(ack, 100) && ack == 0x52, "un paquete con el CRC mal: ACK 0x52 (CHECKSUM_INCORRECT)");
    }
    const unsigned rs0 = dut.resets();
    // REBOOT_RESET no espera: el BSL hace el reset en cuanto la recibe, y el
    // ACK puede no llegar a salir (BSL Scripter no lo espera)
    const size_t ya = bcl->recibido().size();
    bcl->envia(std::string("\x80\x01\x00\x25\x37\x95", 6));
    // El BSL de TI tiene su tabla de vectores en 0x2000_0000, encima del
    // buzon: se limpia durante el reset, antes de que arranque p4_demo
    for (int k = 0; k < 50000 && !dut.en_reset(); ++k) wait(1, SC_US);
    check(dut.en_reset(), "REBOOT_RESET: el BSL resetea el chip");
    dut.sram.load(ceros, sizeof ceros, 0);
    std::printf("    tras REBOOT_RESET llegaron %zu bytes\n", bcl->recibido().size() - ya);
    check(espera_estado(100, 50) && dut.resets() > rs0,
          "y el chip se resetea y arranca p4_demo, que parpadea");
    check(dut.entradas_bsl() == e0 + 1, "ya sin pasar por el BSL: la Flash no esta en blanco");
    bcl->set_enabled(false);
}

inline void TbP4::t5_bsl_b1() {
    grupo("P68 El BSL B1 con la Flash en blanco: la sesion de BSL Scripter, por el banco");
    if (std::getenv("TESTP4_HORA"))
        std::printf("    (P68 empieza en %s; sin P21: %s)\n", sc_time_stamp().to_string().c_str(),
                    (sc_time_stamp() - g_t_gdb).to_string().c_str());
    check(dut.origen_bsl() == Msp432P4::OrigenBsl::B1, "el chip sale con el BSL B1, sin --bsl");
    check_eq(dut.huella_bsl(), bsl_b1::FNV, "la imagen es la de bsl_b1.inc");
    t5_bsl_sesion("B1", false);
}

// Una aplicación que entra en el BSL: p4_bsl llama a la función de entrada de
// 0x0020_2000 con 0xFC48FFFF
inline void TbP4::t5_bsl_app() {
    grupo("P69 Una aplicacion entra en el BSL por su funcion de entrada (p4_bsl)");
    bcl->set_baudios(9600);
    FormatoUart f8e1; f8e1.paridad = Paridad::par;
    bcl->set_formato(f8e1);
    bcl->borra_recibido();
    bpos_ = 0;
    bcl->set_enabled(true);
    const unsigned e0 = dut.entradas_bsl();
    arranca(0, FW_BSL_APP);
    check(espera_estado(90, 20), "p4_bsl llega a llamar al BSL");
    wait(2, SC_MS);
    check(dut.core.cpu.pc() >= p4::BSL_B && dut.core.cpu.pc() < p4::BSL_B + p4::BSL_SIZE &&
          dut.entradas_bsl() == e0, "y el nucleo corre en el BSL, sin el codigo de arranque");
    check(b_sincroniza(), "el BSL contesta a la sincronia");
    std::vector<uint8_t> r;
    int a = b_paquete({ 0x19 }, &r);
    check(a == 0 && r.size() == 2 && r[0] == 0x3B && r[1] == 0x04,
          "TX_BSL_VERSION con el BSL cerrado: 0x04 (LOCKED)");
    // Con la contraseña de verdad -los vectores de p4_bsl-, el BSL se abre
    std::vector<uint8_t> pw(1, 0x21);
    for (uint32_t i = 0; i < 256; ++i) pw.push_back(uint8_t(dut.flash.peek32(i & ~3u) >> (8 * (i & 3u))));
    a = b_paquete(pw, &r);
    check(a == 0 && r.size() == 2 && r[1] == 0x00,
          "la contrasena son los 256 primeros bytes de la Flash: los vectores de p4_bsl");
    a = b_paquete({ 0x19 }, &r);
    check(a == 0 && r.size() == 11 && r[0] == 0x3A, "y ya abierto, TX_BSL_VERSION contesta");
    std::vector<uint8_t> c = le_dir(0x27, dut.flash.peek32(4));
    a = b_paquete(c, nullptr);
    check(a == 0, "LOAD_PC_32 al reset de p4_bsl: ACK...");
    check(espera_estado(100, 50) && buzon(B_ARRANQUES) == 2,
          "...y la aplicacion vuelve a correr desde su reset (segundo arranque)");
    bcl->set_enabled(false);
}

// El BSL de TI (B2), de fichero: no va en la suite
inline void TbP4::t5_bsl_ti(const char* ruta) {
    grupo("BSL de TI (B2): la misma sesion");
    const std::string e = dut.pon_bsl_fichero(ruta);
    if (!check(e.empty(), e.empty() ? "el BSL de TI se carga en 0x0020_2000" : e.c_str())) return;
    std::printf("    %s, huella 0x%08X\n", ruta, unsigned(dut.huella_bsl()));
    if (std::getenv("P4_BSL_TRAZA")) dut.core.cpu.fault_trace = 30;
    if (const char* d = std::getenv("P4_BSL_DESDE")) {
        dut.core.cpu.trace_from = std::strtoull(d, nullptr, 0);
        dut.core.cpu.trace_limit = 250;
    }
    t5_bsl_sesion("TI", true);
    dut.pon_bsl_b1();
}

#endif // STM32_TOP_SC_MAIN_P4_T5_H
