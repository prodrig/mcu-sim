// =============================================================================
// flash_if.h — Memoria Flash (1 MB) + acelerador ART + registros FLASH_*
//
// Tres caras de bus:
//   icode : lecturas de instrucción (vía ART: prefetch + caché I) [IR, §5.2.3]
//   dcode : lecturas/escrituras de dato (caché D; programación con secuencias
//           de FLASH_KEYR y estados de espera FLASH_ACR.LATENCY) [IR, §5.5-5.9]
//   regs  : registros FLASH_* en AHB1 (0x4002 3C00) [IR, §5.5-5.8]
//
// Contenido: Flash principal 1 MB (12 sectores [IR, §5.2.1]), System Memory
// (bootloader, 30 KB), zona OTP (528 B) y option bytes (16 B) [IR, §15.1].
//
// Alias de 0x0000 0000: lo resuelve el router del núcleo con MEM_MODE
// (SYSCFG_MEMRMP/BOOT) [IR, §12.21.2]. La cara icode/dcode mantiene además el
// espejo por defecto 0x0000 0000-0x000F FFFF -> Flash principal, que es el modo
// de arranque tras reset con BOOT0=0 [IR, §2.3].
// =============================================================================
#ifndef STM32_MEM_FLASH_IF_H
#define STM32_MEM_FLASH_IF_H

#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "../common/periph_base.h"
#include "mem_caps.h"

namespace stm32 {

class FlashIf : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<FlashIf> icode{"icode"};
    tlm_utils::simple_target_socket<FlashIf> dcode{"dcode"};
    tlm_utils::simple_target_socket<FlashIf> regs{"regs"};
    sc_core::sc_in<bool>   hclk{"hclk"};
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};
    sc_core::sc_in<bool>   rst_n{"rst_n"};
    sc_core::sc_out<bool>  irq{"irq"};              // FLASH global (IRQ 4)
    // Nivel del Brown-Out Reset programado en los option bytes (OPTCR[3:2]).
    // Lo consume PowerPads para decidir el umbral de reset [IR, §5.7.1].
    sc_core::sc_out<uint8_t> bor_lev{"bor_lev"};

    // ---- Offsets de registro [IR, §5.8] ------------------------------------
    enum : uint32_t {
        ACR = 0x00, KEYR = 0x04, OPTKEYR = 0x08,
        SR  = 0x0C, CR   = 0x10, OPTCR   = 0x14
    };
    // ---- Llaves de desbloqueo [IR, §5.6.1, §5.6.2] -------------------------
    static constexpr uint32_t KEY1     = 0x45670123, KEY2     = 0xCDEF89AB;
    static constexpr uint32_t OPTKEY1  = 0x08192A3B, OPTKEY2  = 0x4C5D6E7F;

    // ---- Geometría del ART [IR, §5.2.3] ------------------------------------
    static constexpr unsigned ART_LINE     = 16;   // 128 bits
    static constexpr unsigned ART_ICACHE_N = 64;   // 64 líneas de 128 bits
    static constexpr unsigned ART_DCACHE_N = 8;    // 8 líneas de 128 bits

    // ⚠ NO DISPONIBLE EN LAS FUENTES: tiempos de programación y borrado de la
    // Flash. Se exponen como parámetros del modelo para que el banco de pruebas
    // o una fase posterior los ajusten sin tocar el código.
    sc_core::sc_time t_prog {16, sc_core::SC_US};  // por palabra programada
    sc_core::sc_time t_erase{1,  sc_core::SC_MS};  // por sector
    sc_core::sc_time t_mass_erase{16, sc_core::SC_MS};

    // El mapa de ESTA Flash: base, tamano, tabla de sectores, memoria de
    // sistema, OTP y bytes de opcion, mas la curva de estados de espera. Por
    // omision el del F407, de modo que un `FlashIf` construido como siempre es
    // el de siempre, byte a byte. [mem/mem_caps.h]
    const MapaFlash mapa;

    explicit FlashIf(sc_core::sc_module_name nm,
                     MapaFlash m = FLASH_STM32F407VG)
        : sc_core::sc_module(nm), mapa(m),
          mem_(m.size, 0xFF), sysmem_(m.sysmem_size, 0xFF),
          otp_(m.otp_size, 0xFF), optb_(m.opt_size, 0xFF) {
        // Valor de fábrica de los option bytes (OPTCR = 0x0FFF AAED): RDP nivel
        // 0, sin protección de escritura, BOR desactivado [IR, §5.7.1].
        const uint32_t opt_factory = 0x0FFFAAEDu;
        std::memcpy(optb_.data(), &opt_factory, 4);
        icode.register_b_transport(this, &FlashIf::bt_icode);
        icode.register_transport_dbg(this, &FlashIf::dbg_mem);
        dcode.register_b_transport(this, &FlashIf::bt_dcode);
        dcode.register_transport_dbg(this, &FlashIf::dbg_mem);
        regs.register_b_transport(this, &FlashIf::bt_regs);
        regs.register_transport_dbg(this, &FlashIf::dbg_regs);
        reset_regs();
        SC_HAS_PROCESS(FlashIf);
        SC_METHOD(reset_proc);
        sensitive << rst_n;
        dont_initialize();
        // La IRQ la actualizan tanto reset_proc como el acceso a registros
        // (que corre en el proceso del maestro): un solo escritor del puerto.
        SC_METHOD(irq_proc);
        sensitive << irq_ev_;   // sin dont_initialize: publica BOR_LEV en t = 0
    }

    // =======================================================================
    // Carga de imágenes de firmware (usada por el testbench / sc_main)
    // =======================================================================
    // Carga cruda en la Flash principal, con desplazamiento desde 0x0800 0000.
    bool load_binary(const uint8_t* data, size_t n, uint32_t offset = 0) {
        if (uint64_t(offset) + n > mem_.size()) return false;
        std::memcpy(mem_.data() + offset, data, n);
        art_flush();
        return true;
    }
    // Carga desde fichero .bin. Devuelve bytes cargados o -1 si falla.
    long load_binary_file(const char* path, uint32_t offset = 0) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return -1;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0 || uint64_t(offset) + uint64_t(n) > mem_.size()) { std::fclose(f); return -1; }
        const size_t rd = std::fread(mem_.data() + offset, 1, size_t(n), f);
        std::fclose(f);
        art_flush();
        return long(rd);
    }
    // Carga de Intel HEX (formato habitual de salida de las toolchains ARM).
    // Acepta registros 00 (datos), 01 (fin), 02/04 (dirección extendida).
    long load_ihex_file(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return -1;
        char line[600];
        uint32_t hi = 0;
        long total = 0;
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] != ':') continue;
            auto hx = [&](int i) {
                auto d = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int a = d(line[1 + 2 * i]), b = d(line[2 + 2 * i]);
                return (a < 0 || b < 0) ? -1 : (a << 4) | b;
            };
            const int len = hx(0), a1 = hx(1), a0 = hx(2), rt = hx(3);
            if (len < 0 || a1 < 0 || a0 < 0 || rt < 0) { std::fclose(f); return -1; }
            const uint32_t off = uint32_t(a1) << 8 | uint32_t(a0);
            if (rt == 0x01) break;
            if (rt == 0x04 || rt == 0x02) {
                const int h1 = hx(4), h0 = hx(5);
                if (h1 < 0 || h0 < 0) { std::fclose(f); return -1; }
                const uint32_t v = uint32_t(h1) << 8 | uint32_t(h0);
                hi = (rt == 0x04) ? (v << 16) : (v << 4);
                continue;
            }
            if (rt != 0x00) continue;
            for (int i = 0; i < len; ++i) {
                const int b = hx(4 + i);
                if (b < 0) { std::fclose(f); return -1; }
                if (!poke_byte(hi + off + uint32_t(i), uint8_t(b))) { std::fclose(f); return -1; }
                ++total;
            }
        }
        std::fclose(f);
        art_flush();
        return total;
    }

    // Acceso directo para verificación (sin bus ni tiempo)
    bool poke_byte(uint32_t a, uint8_t v) {
        uint8_t* p = locate(a);
        if (!p) return false;
        *p = v;
        return true;
    }
    bool peek_byte(uint32_t a, uint8_t& v) {
        const uint8_t* p = locate(a);
        if (!p) return false;
        v = *p;
        return true;
    }
    uint32_t peek32(uint32_t a) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) {
            uint8_t b = 0xFF; peek_byte(a + i, b); v |= uint32_t(b) << (8 * i);
        }
        return v;
    }

    // Estado observable por el banco de pruebas
    uint32_t reg_acr()   const { return acr_; }
    uint32_t reg_sr()     const { return sr_; }
    uint32_t reg_cr()     const { return cr_; }
    uint32_t reg_optcr()  const { return optcr_; }
    bool     cr_locked()  const { return (cr_ & (1u << 31)) != 0; }
    bool     opt_locked() const { return (optcr_ & 1u) != 0; }
    unsigned latency()    const { return acr_ & 0xFu; }
    uint64_t art_hits()   const { return art_hits_; }
    uint64_t art_misses() const { return art_misses_; }

private:
    // ---------------------------------------------------------------------
    // Arrays de memoria
    // ---------------------------------------------------------------------
    std::vector<uint8_t> mem_, sysmem_, otp_, optb_;

    // Registros [IR, §5.5-5.8]
    uint32_t acr_ = 0, sr_ = 0, cr_ = 0x80000000, optcr_ = 0x0FFFAAED;
    unsigned key_state_ = 0, optkey_state_ = 0;   // progreso de las secuencias
    bool     key_error_ = false;                  // secuencia rota -> hasta reset

    // ART: cachés de líneas de 128 bits (dirección de línea válida + LRU simple)
    struct Line { uint32_t tag = 0xFFFFFFFFu; uint32_t age = 0; bool valid = false; };
    Line     ic_[ART_ICACHE_N], dc_[ART_DCACHE_N];
    uint32_t art_clock_ = 0;
    uint64_t art_hits_ = 0, art_misses_ = 0;

    // -----------------------------------------------------------------------
    // Localización de una dirección en los arrays físicos
    // -----------------------------------------------------------------------
    uint8_t* locate(uint32_t a) {
        if (a < 0x00100000u)                                    // espejo de boot
            return mem_.data() + (a % mem_.size());
        if (a >= mapa.base && a < mapa.base + mapa.size)
            return mem_.data() + (a - mapa.base);
        if (a >= mapa.sysmem_base && a < mapa.sysmem_base + mapa.sysmem_size)
            return sysmem_.data() + (a - mapa.sysmem_base);
        if (a >= mapa.otp_base && a < mapa.otp_base + mapa.otp_size)
            return otp_.data() + (a - mapa.otp_base);
        if (a >= mapa.opt_base && a < mapa.opt_base + mapa.opt_size)
            return optb_.data() + (a - mapa.opt_base);
        return nullptr;
    }

    void reset_regs() {
        acr_ = 0x00000000; sr_ = 0; cr_ = 0x80000000;
        key_state_ = optkey_state_ = 0; key_error_ = false;
        art_flush();
        // Los option bytes son NO VOLÁTILES: tras un reset, OPTCR se recarga
        // desde el bloque programado, no desde una constante. Es lo que hace
        // que un BOR_LEV programado siga vigente en el siguiente arranque
        // [IR, §5.7]. OPTLOCK vuelve a 1 y OPTSTRT a 0.
        std::memcpy(&optcr_, optb_.data(), 4);
        optcr_ = (optcr_ & ~0x2u) | 1u;
        // Los option bytes reflejan OPTCR en su primera palabra; el resto del
        // bloque de 16 B no está detallado en las fuentes.
        // ⚠ NO DISPONIBLE EN LAS FUENTES: distribución byte a byte del bloque
        // de option bytes 0x1FFF C000-0x1FFF C00F.
    }
    void reset_proc() { if (!rst_n.read()) { reset_regs(); o_irq_ = false; irq_ev_.notify(sc_core::SC_ZERO_TIME); } }
    void irq_proc()   {
        irq.write(o_irq_);
        bor_lev.write(uint8_t((optcr_ >> 2) & 3u));
    }
    bool o_irq_ = false;
    sc_core::sc_event irq_ev_;

    void art_flush() {
        for (Line& l : ic_) { l.valid = false; l.tag = 0xFFFFFFFFu; }
        for (Line& l : dc_) { l.valid = false; l.tag = 0xFFFFFFFFu; }
    }

    sc_core::sc_time cycles(unsigned n) const {
        const double f = hclk_hz.read();
        return f > 0.0 ? sc_core::sc_time(double(n) * 1.0e12 / f, sc_core::SC_PS)
                       : sc_core::SC_ZERO_TIME;
    }

    // Consulta/inserta una línea en una caché; devuelve true si era acierto.
    bool art_lookup(Line* set, unsigned n, uint32_t line_tag, bool enabled) {
        if (!enabled) return false;
        for (unsigned i = 0; i < n; ++i)
            if (set[i].valid && set[i].tag == line_tag) {
                set[i].age = ++art_clock_;
                return true;
            }
        // Fallo: sustituir la línea menos recientemente usada
        unsigned victim = 0;
        for (unsigned i = 0; i < n; ++i) {
            if (!set[i].valid) { victim = i; break; }
            if (set[i].age < set[victim].age) victim = i;
        }
        set[victim].valid = true;
        set[victim].tag   = line_tag;
        set[victim].age   = ++art_clock_;
        return false;
    }

    // -----------------------------------------------------------------------
    // Lectura del array (común a icode y dcode)
    // -----------------------------------------------------------------------
    bool read_array(tlm::tlm_generic_payload& gp) {
        const uint64_t a   = gp.get_address();
        const unsigned len = gp.get_data_length();
        uint8_t* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) {
            const uint8_t* p = locate(uint32_t(a + i));
            if (!p) {                              // hueco reservado del Code
                gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return false;
            }
            d[i] = *p;
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    // Coste temporal de una lectura según el ART [IR, §5.2.2, §5.2.3]:
    //   acierto de caché o línea ya prefetchada -> 0 estados de espera
    //   fallo -> LATENCY estados de espera (+1 ciclo de acceso)
    sc_core::sc_time read_cost(uint64_t a, unsigned len, bool instr) {
        const bool icen  = (acr_ >> 9) & 1u;
        const bool dcen  = (acr_ >> 10) & 1u;
        const bool prft  = (acr_ >> 8) & 1u;
        const unsigned ws = latency();
        unsigned miss = 0, total = 0;
        const uint32_t first = uint32_t(a) / ART_LINE;
        const uint32_t last  = uint32_t(a + (len ? len - 1 : 0)) / ART_LINE;
        for (uint32_t l = first; l <= last; ++l) {
            ++total;
            const bool hit = instr ? art_lookup(ic_, ART_ICACHE_N, l, icen)
                                   : art_lookup(dc_, ART_DCACHE_N, l, dcen);
            if (hit) { ++art_hits_; continue; }
            ++art_misses_; ++miss;
            // Prefetch: al fallar se trae también la línea siguiente, de modo
            // que el acceso secuencial posterior es acierto [IR, §5.2.3].
            if (instr && prft) art_lookup(ic_, ART_ICACHE_N, l + 1, true);
        }
        return cycles(total + miss * ws);
    }

    void bt_icode(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        if (gp.is_write()) {                       // el ICode no escribe
            gp.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (!read_array(gp)) return;
        t += read_cost(gp.get_address(), gp.get_data_length(), true);
    }

    void bt_dcode(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        if (gp.is_write()) { program_write(gp, t); return; }
        if (!read_array(gp)) return;
        t += read_cost(gp.get_address(), gp.get_data_length(), false);
    }

    unsigned dbg_mem(tlm::tlm_generic_payload& gp) {
        const uint64_t a = gp.get_address();
        const unsigned len = gp.get_data_length();
        uint8_t* d = gp.get_data_ptr();
        unsigned n = 0;
        for (unsigned i = 0; i < len; ++i) {
            uint8_t* p = locate(uint32_t(a + i));
            if (!p) break;
            if (gp.is_read()) d[i] = *p; else *p = d[i];
            ++n;
        }
        if (!gp.is_read()) art_flush();
        return n;
    }

    // -----------------------------------------------------------------------
    // Programación de la Flash [IR, §5.9]
    // -----------------------------------------------------------------------
    unsigned psize_bytes() const {
        switch ((cr_ >> 8) & 3u) { case 0: return 1; case 1: return 2;
                                   case 2: return 4; default: return 8; }
    }
    bool sector_write_protected(int sector) const {
        if (sector < 0 || sector >= int(mapa.n_sectores)) return true;
        return ((optcr_ >> (16 + sector)) & 1u) == 0;   // nWRP: 0 = protegido
    }

    void set_err(uint32_t bit) { sr_ |= bit; update_irq(); }
    void update_irq() {
        const bool eopie = (cr_ >> 24) & 1u, errie = (cr_ >> 25) & 1u;
        const bool eop   = sr_ & 1u;
        const bool err   = (sr_ & 0x000000F2u) != 0;   // OPERR|WRPERR|PGAERR|PGPERR|PGSERR
        o_irq_ = (eopie && eop) || (errie && err);
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void program_write(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t a   = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        const uint8_t* d   = gp.get_data_ptr();

        if (cr_ & (1u << 31)) {                       // LOCK: acceso bloqueado
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        if (!(cr_ & 1u)) {                            // PG = 0: no programando
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        uint8_t* p = locate(a);
        if (!p) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }

        sr_ |= (1u << 16);                            // BSY
        const unsigned ps = psize_bytes();
        bool ok = true;
        if (len != ps)                { set_err(1u << 6); ok = false; }  // PGPERR
        else if (a % ps)              { set_err(1u << 5); ok = false; }  // PGAERR
        else {
            const int sec = mapa.sector_de(a);
            if (sec >= 0 && sector_write_protected(sec)) {
                set_err(1u << 4); ok = false;                             // WRPERR
            } else {
                for (unsigned i = 0; i < len; ++i)
                    if (p[i] != 0xFF) { set_err(1u << 7); ok = false; break; } // PGSERR
            }
        }
        if (ok) {
            t += t_prog;
            for (unsigned i = 0; i < len; ++i) p[i] &= d[i];   // solo 1 -> 0
            art_flush();
            sr_ |= 1u;                                          // EOP
        } else {
            sr_ |= (1u << 1);                                   // OPERR
        }
        sr_ &= ~(1u << 16);                                     // BSY = 0
        update_irq();
        gp.set_response_status(ok ? tlm::TLM_OK_RESPONSE
                                  : tlm::TLM_GENERIC_ERROR_RESPONSE);
    }

    void do_erase(sc_core::sc_time& t) {
        sr_ |= (1u << 16);                            // BSY
        if (cr_ & (1u << 2)) {                        // MER: borrado total
            t += t_mass_erase;
            std::fill(mem_.begin(), mem_.end(), 0xFF);
            sr_ |= 1u;
        } else if (cr_ & (1u << 1)) {                 // SER: borrado de sector
            const unsigned snb = (cr_ >> 3) & 0xFu;
            if (snb >= mapa.n_sectores) {
                set_err(1u << 1);                     // OPERR (sector inválido)
            } else if (sector_write_protected(int(snb))) {
                set_err(1u << 4);                     // WRPERR
                sr_ |= (1u << 1);
            } else {
                t += t_erase;
                const uint32_t off = mapa.sectores[snb].base - mapa.base;
                std::fill(mem_.begin() + off,
                          mem_.begin() + off + mapa.sectores[snb].size, 0xFF);
                sr_ |= 1u;                            // EOP
            }
        }
        art_flush();
        cr_ &= ~(1u << 16);                           // STRT se limpia con BSY
        sr_ &= ~(1u << 16);
        update_irq();
    }

    // -----------------------------------------------------------------------
    // Registros FLASH_* [IR, §5.5-5.8]
    // -----------------------------------------------------------------------
    void bt_regs(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (a < addr::FLASHIF_B || a + gp.get_data_length() > addr::FLASHIF_B + 0x400) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const uint32_t off = uint32_t(a - addr::FLASHIF_B) & ~3u;
        if (gp.is_read()) {
            const uint32_t v = reg_read(off);
            uint8_t* d = gp.get_data_ptr();
            for (unsigned i = 0; i < gp.get_data_length(); ++i)
                d[i] = uint8_t(v >> (8 * ((uint32_t(a) + i) & 3u)));
        } else {
            uint32_t v = 0;
            const uint8_t* d = gp.get_data_ptr();
            for (unsigned i = 0; i < gp.get_data_length() && i < 4; ++i)
                v |= uint32_t(d[i]) << (8 * ((uint32_t(a) + i) & 3u));
            reg_write(off, v, t);
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    unsigned dbg_regs(tlm::tlm_generic_payload& gp) {
        if (!gp.is_read()) return 0;
        const uint32_t off = uint32_t(gp.get_address() - addr::FLASHIF_B) & ~3u;
        const uint32_t v = reg_read(off);
        std::memcpy(gp.get_data_ptr(), &v, std::min<unsigned>(gp.get_data_length(), 4));
        return std::min<unsigned>(gp.get_data_length(), 4);
    }

    uint32_t reg_read(uint32_t off) const {
        switch (off) {
            case ACR:     return acr_;
            case KEYR:    return 0;              // solo escritura
            case OPTKEYR: return 0;              // solo escritura
            case SR:      return sr_;
            case CR:      return cr_;
            case OPTCR:   return optcr_;
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, sc_core::sc_time& t) {
        switch (off) {
            case ACR: {
                const uint32_t wmask = 0x00001F0Fu;   // DCRST|ICRST|DCEN|ICEN|PRFTEN|LATENCY
                acr_ = (acr_ & ~wmask) | (v & wmask);
                if (v & (1u << 12)) {                 // DCRST (solo con DCEN=0)
                    if (!((acr_ >> 10) & 1u)) for (Line& l : dc_) l.valid = false;
                    acr_ &= ~(1u << 12);
                }
                if (v & (1u << 11)) {                 // ICRST (solo con ICEN=0)
                    if (!((acr_ >> 9) & 1u)) for (Line& l : ic_) l.valid = false;
                    acr_ &= ~(1u << 11);
                }
                // Aviso de latencia insuficiente para la frecuencia actual
                const double f = hclk_hz.read();
                if (f > 0.0 && latency() < mapa.latencia_minima(f))
                    SC_REPORT_WARNING("flash",
                        "FLASH_ACR.LATENCY insuficiente para la HCLK programada [IR, 5.2.2]");
                break;
            }
            case KEYR:
                if (key_error_) break;                // bloqueado hasta reset
                if (key_state_ == 0 && v == KEY1)       key_state_ = 1;
                else if (key_state_ == 1 && v == KEY2) { cr_ &= ~(1u << 31);
                                                         key_state_ = 0; }
                else { key_state_ = 0; key_error_ = true; }  // secuencia rota
                break;
            case OPTKEYR:
                if (optkey_state_ == 0 && v == OPTKEY1)      optkey_state_ = 1;
                else if (optkey_state_ == 1 && v == OPTKEY2) { optcr_ &= ~1u;
                                                               optkey_state_ = 0; }
                else optkey_state_ = 0;
                break;
            case SR: {
                const uint32_t rc_w1 = 0x000000F3u;   // EOP|OPERR|WRPERR|PGAERR|PGPERR|PGSERR
                sr_ &= ~(v & rc_w1);
                update_irq();
                break;
            }
            case CR: {
                if (cr_ & (1u << 31)) {               // LOCK=1: escrituras ignoradas
                    break;
                }
                // ERRIE(25)|EOPIE(24)|STRT(16)|PSIZE(9:8)|SNB(6:3)|MER(2)|SER(1)|PG(0)
                const uint32_t wmask = 0x0301037Fu;
                cr_ = (cr_ & ~wmask) | (v & wmask);
                if (v & (1u << 31)) cr_ |= (1u << 31);           // LOCK es 'rs'
                if (cr_ & (1u << 16)) do_erase(t);               // STRT
                update_irq();
                break;
            }
            case OPTCR: {
                if (optcr_ & 1u) break;               // OPTLOCK=1: ignorado
                // nWRP(27:16)|RDP(15:8)|nRST_STDBY(7)|nRST_STOP(6)|WDG_SW(5)|
                // BOR_LEV(3:2)|OPTSTRT(1). OPTLOCK (bit 0) es 'rs'.
                const uint32_t wmask = 0x0FFFFFEEu;
                optcr_ = (optcr_ & ~wmask) | (v & wmask);
                if (v & 1u) optcr_ |= 1u;             // OPTLOCK es 'rs'
                irq_ev_.notify(sc_core::SC_ZERO_TIME);    // republica BOR_LEV
                if (optcr_ & (1u << 1)) {             // OPTSTRT
                    std::memcpy(optb_.data(), &optcr_, 4);
                    optcr_ &= ~(1u << 1);
                    sr_ |= 1u;                        // EOP
                    update_irq();
                }
                break;
            }
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_MEM_FLASH_IF_H
