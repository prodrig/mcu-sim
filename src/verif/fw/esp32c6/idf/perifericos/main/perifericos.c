/* perifericos.c — E9.4 de doc/analisis_esp32_riscv.md: los periféricos que el
 * ESP32-C6 tiene en común con el C3, y el segundo TWAI, con los
 * controladores de ESP-IDF tal cual (y así la GDMA, el PCR y la matriz).
 * Cada prueba escribe una línea «Pn ...»; el banco de testc6 (D9) la lee y
 * mide los pines. El cableado del banco:
 *   GPIO2  LEDC canal 0                 GPIO3  RMT
 *   GPIO4/5  TWAI0 TX/RX, GPIO6/7  TWAI1 TX/RX: un bus CAN (Y cableada)
 *   GPIO18-21  SPI2 CS, SCLK, MOSI, MISO (MOSI -> MISO en el banco)
 *   GPIO22/23  I2S BCLK y WS, GPIO10 -> GPIO11 (DOUT -> DIN)
 *   GPIO15/1  I2C SCL y SDA, sin nadie: NACK
 *   GPIO0  ADC1 canal 0, a 1,2 V desde el banco
 * Este fichero es parte de mcu-sim (AGPL-3.0-or-later). */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/twai.h"
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha1.h"
#include "esp_err.h"
#include "soc/soc.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"

static void p_ledc(void)
{
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_13_BIT,
                             .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {.gpio_num = 2, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                               .timer_sel = LEDC_TIMER_0, .duty = 2048, .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    printf("P1 LEDC 5000 Hz 25%%\n");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 6144, 40));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE));
    printf("P2 LEDC fundido hasta %lu\n", (unsigned long)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

static void p_rmt(void)
{
    rmt_channel_handle_t ch = NULL;
    rmt_tx_channel_config_t cfg = {.gpio_num = 3, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 1000000,
                                   .mem_block_symbols = 48, .trans_queue_depth = 2};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &ch));
    rmt_encoder_handle_t enc = NULL;
    rmt_copy_encoder_config_t ec = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&ec, &enc));
    ESP_ERROR_CHECK(rmt_enable(ch));
    static const rmt_symbol_word_t s[4] = {
        {.level0 = 1, .duration0 = 10, .level1 = 0, .duration1 = 20},
        {.level0 = 1, .duration0 = 30, .level1 = 0, .duration1 = 40},
        {.level0 = 1, .duration0 = 50, .level1 = 0, .duration1 = 60},
        {.level0 = 1, .duration0 = 70, .level1 = 0, .duration1 = 80},
    };
    rmt_transmit_config_t tc = {.loop_count = 0};
    ESP_ERROR_CHECK(rmt_transmit(ch, enc, s, sizeof s, &tc));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(ch, 1000));
    printf("P3 RMT 4 pulsos\n");
}

static void p_twai(void)
{
    twai_handle_t h[2];
    for (int i = 0; i < 2; ++i) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(i, 4 + 2 * i, 5 + 2 * i, TWAI_MODE_NORMAL);
        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        ESP_ERROR_CHECK(twai_driver_install_v2(&g, &t, &f, &h[i]));
        ESP_ERROR_CHECK(twai_start_v2(h[i]));
    }
    for (int i = 0; i < 2; ++i) {
        twai_message_t m = {.identifier = i ? 0x456 : 0x123, .data_length_code = 8};
        for (int k = 0; k < 8; ++k) m.data[k] = (uint8_t)(0x10 * (i + 1) + k);
        ESP_ERROR_CHECK(twai_transmit_v2(h[i], &m, pdMS_TO_TICKS(100)));
        twai_message_t r;
        esp_err_t e = twai_receive_v2(h[1 - i], &r, pdMS_TO_TICKS(100));
        printf("P4 TWAI%d -> TWAI%d: %s id 0x%lX dlc %d %02X..%02X\n", i, 1 - i, e == ESP_OK ? "OK" : esp_err_to_name(e),
               (unsigned long)r.identifier, r.data_length_code, r.data[0], r.data[7]);
    }
    for (int i = 0; i < 2; ++i) { twai_stop_v2(h[i]); twai_driver_uninstall_v2(h[i]); }
    printf("P4 TWAI parado\n");
}

static void p_spi(void)
{
    spi_bus_config_t b = {.mosi_io_num = 20, .miso_io_num = 21, .sclk_io_num = 19, .quadwp_io_num = -1,
                          .quadhd_io_num = -1, .max_transfer_sz = 256};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &b, SPI_DMA_CH_AUTO));
    printf("P5 SPI2 con GDMA\n");
    spi_device_interface_config_t d = {.clock_speed_hz = 1000000, .mode = 0, .spics_io_num = 18, .queue_size = 1};
    spi_device_handle_t dev;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &d, &dev));
    static uint8_t tx[64] __attribute__((aligned(4))), rx[64] __attribute__((aligned(4)));
    for (int i = 0; i < 64; ++i) tx[i] = (uint8_t)(i * 7 + 3);
    spi_transaction_t t = {.length = 64 * 8, .tx_buffer = tx, .rx_buffer = rx};
    ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
    printf("P5 SPI %s 64 bytes por la GDMA\n", memcmp(tx, rx, 64) == 0 ? "OK" : "MAL");
}

static void p_i2s(void)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, &rx));
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = 22, .ws = 23, .dout = 10, .din = 11},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &sc));
    static int16_t m[256], l[1024];
    for (int i = 0; i < 256; ++i) m[i] = (int16_t)(0x1000 + 37 * i);
    size_t n = 0;
    ESP_ERROR_CHECK(i2s_channel_preload_data(tx, m, sizeof m, &n));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    size_t leidos = 0;
    i2s_channel_read(rx, l, sizeof l, &leidos, 1000);
    int ok = 0;
    for (size_t k = 0; k + 64 <= leidos / 2 && !ok; ++k)
        if (memcmp(&l[k], m, 64 * 2) == 0) ok = 1;
    printf("P6 I2S %s (%u bytes leidos)\n", ok ? "OK" : "MAL", (unsigned)leidos);
    i2s_channel_disable(tx);
    i2s_channel_disable(rx);
}

static void p_i2c(void)
{
    i2c_master_bus_config_t bc = {.i2c_port = 0, .scl_io_num = 15, .sda_io_num = 1,
                                  .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                  .flags.enable_internal_pullup = true};
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
    esp_err_t e = i2c_master_probe(bus, 0x50, 50);
    printf("P7 I2C 0x50: %s\n", esp_err_to_name(e));
}

static void p_adc(void)
{
    adc_oneshot_unit_handle_t u;
    adc_oneshot_unit_init_cfg_t ic = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ic, &u));
    adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(u, ADC_CHANNEL_0, &cc));
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(u, ADC_CHANNEL_0, &raw));
    printf("P8 ADC1 canal 0: %d\n", raw);
    temperature_sensor_handle_t ts;
    temperature_sensor_config_t tc = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&tc, &ts));
    ESP_ERROR_CHECK(temperature_sensor_enable(ts));
    float c = 0;
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(ts, &c));
    printf("P9 temperatura %.0f C\n", c);
}

static int igual(const uint8_t *a, const char *hex, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        if (a[i] != (uint8_t)v) return 0;
    }
    return 1;
}

/* Los vectores de NIST SP 800-38A (AES), y SHA-1, SHA-256 y una
 * exponenciación modular calculados aparte (Python: hashlib, pow) */
static void p_cripto(void)
{
    static const uint8_t k128[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
    static const uint8_t k256[32] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81, 0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7, 0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4};
    static const uint8_t pt[64] = {0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96, 0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A, 0xAE, 0x2D, 0x8A, 0x57, 0x1E, 0x03, 0xAC, 0x9C, 0x9E, 0xB7, 0x6F, 0xAC, 0x45, 0xAF, 0x8E, 0x51, 0x30, 0xC8, 0x1C, 0x46, 0xA3, 0x5C, 0xE4, 0x11, 0xE5, 0xFB, 0xC1, 0x19, 0x1A, 0x0A, 0x52, 0xEF, 0xF6, 0x9F, 0x24, 0x45, 0xDF, 0x4F, 0x9B, 0x17, 0xAD, 0x2B, 0x41, 0x7B, 0xE6, 0x6C, 0x37, 0x10};
    uint8_t ct[64], iv[16], nc[16], sb[16];
    size_t off = 0;
    mbedtls_aes_context a;
    mbedtls_aes_init(&a);
    mbedtls_aes_setkey_enc(&a, k128, 128);
    mbedtls_aes_crypt_ecb(&a, MBEDTLS_AES_ENCRYPT, pt, ct);
    int ok = igual(ct, "3ad77bb40d7a3660a89ecaf32466ef97", 16);
    for (int i = 0; i < 16; ++i) iv[i] = (uint8_t)i;
    mbedtls_aes_crypt_cbc(&a, MBEDTLS_AES_ENCRYPT, 64, iv, pt, ct);
    ok &= igual(ct, "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7", 64);
    for (int i = 0; i < 16; ++i) nc[i] = (uint8_t)(0xF0 + i);
    mbedtls_aes_crypt_ctr(&a, 64, &off, nc, sb, pt, ct);
    ok &= igual(ct, "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee", 64);
    mbedtls_aes_setkey_dec(&a, k128, 128);
    for (int i = 0; i < 16; ++i) iv[i] = (uint8_t)i;
    uint8_t vuelta[64];
    static const uint8_t cbc[64] = {0x76, 0x49, 0xAB, 0xAC, 0x81, 0x19, 0xB2, 0x46, 0xCE, 0xE9, 0x8E, 0x9B, 0x12, 0xE9, 0x19, 0x7D, 0x50, 0x86, 0xCB, 0x9B, 0x50, 0x72, 0x19, 0xEE, 0x95, 0xDB, 0x11, 0x3A, 0x91, 0x76, 0x78, 0xB2, 0x73, 0xBE, 0xD6, 0xB8, 0xE3, 0xC1, 0x74, 0x3B, 0x71, 0x16, 0xE6, 0x9E, 0x22, 0x22, 0x95, 0x16, 0x3F, 0xF1, 0xCA, 0xA1, 0x68, 0x1F, 0xAC, 0x09, 0x12, 0x0E, 0xCA, 0x30, 0x75, 0x86, 0xE1, 0xA7};
    mbedtls_aes_crypt_cbc(&a, MBEDTLS_AES_DECRYPT, 64, iv, cbc, vuelta);
    ok &= memcmp(vuelta, pt, 64) == 0;
    mbedtls_aes_setkey_enc(&a, k256, 256);
    mbedtls_aes_crypt_ecb(&a, MBEDTLS_AES_ENCRYPT, pt, ct);
    ok &= igual(ct, "f3eed1bdb5d2a03c064b5a7e3db181f8", 16);
    mbedtls_aes_free(&a);
    printf("P10 AES (ECB, CBC, CTR; 128 y 256) %s\n", ok ? "OK" : "MAL");

    static uint8_t m[300];
    for (int i = 0; i < 300; ++i) m[i] = (uint8_t)(i * 13 + 7);
    uint8_t h[32];
    mbedtls_sha1(m, sizeof m, h);
    printf("P11 SHA-1 %s\n", igual(h, "ef2bbf166a8fd9e3f0994c7d465043acf84a82a4", 20) ? "OK" : "MAL");
    mbedtls_sha256(m, sizeof m, h, 0);
    printf("P12 SHA-256 %s\n", igual(h, "4e4838828c50ef8bf20e2f5adc26dbbe25e191bea396f183abfc1794b0f54957", 32) ? "OK" : "MAL");

    mbedtls_mpi n, b, e, r;
    mbedtls_mpi_init(&n); mbedtls_mpi_init(&b); mbedtls_mpi_init(&e); mbedtls_mpi_init(&r);
    mbedtls_mpi_read_string(&n, 16, "7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFED");
    mbedtls_mpi_read_string(&b, 16, "123456789ABCDEF0123456789ABCDEF");
    mbedtls_mpi_lset(&e, 65537);
    mbedtls_mpi_exp_mod(&r, &b, &e, &n, NULL);
    char t[80];
    size_t tl = 0;
    mbedtls_mpi_write_string(&r, 16, t, sizeof t, &tl);
    printf("P13 RSA (exp_mod) %s\n", strcmp(t, "1549E4F575FCB33D92AB1B7F1838AA0445C22B0100CEE68513D4DDE1F824AFA1") == 0 ? "OK" : "MAL");
    mbedtls_mpi_free(&n); mbedtls_mpi_free(&b); mbedtls_mpi_free(&e); mbedtls_mpi_free(&r);
}

/* Los registros DATE de cada bloque: que cada uno está donde dice el mapa */
static void p_fechas(void)
{
    static const struct { const char *n; uint32_t a; } f[] = {
        {"RMT", 0x600060CC}, {"I2C", 0x600040F8}, {"UHCI", 0x60005080}, {"LEDC", 0x600071FC},
        {"I2S", 0x6000C080}, {"SARADC", 0x6000E3FC}, {"USJ", 0x6000F080}, {"GDMA", 0x60080068},
        {"SPI2", 0x600810F0}, {"AES", 0x600880B4}, {"SHA", 0x6008902C}, {"RSA", 0x6008A830},
        {"DS", 0x6008CE20}, {"HMAC", 0x6008D1FC}, {"XTS", 0x6000335C},
    };
    for (size_t i = 0; i < sizeof f / sizeof f[0]; ++i) printf("P14 %s 0x%08lX\n", f[i].n, (unsigned long)REG_READ(f[i].a));
}

void app_main(void)
{
    printf("perifericos: empieza\n");
    p_ledc();
    p_rmt();
    p_twai();
    p_spi();
    p_i2s();
    p_i2c();
    p_adc();
    p_cripto();
    p_fechas();
    printf("perifericos: FIN\n");
}
