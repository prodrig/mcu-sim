/* motor.c — E11 de doc/analisis_esp32_riscv.md: el MCPWM, el PCNT y el ETM
 * del ESP32-C6, con los controladores de ESP-IDF tal cual. Cada paso escribe
 * una línea «Mn ...»; el banco de testc6 (D16) la lee y mide los pines:
 *   GPIO0  MCPWM operador 0, generador A: 10 kHz, 30 % con 5 µs de tiempo
 *          muerto en la subida
 *   GPIO2  el generador B: el complementario de A, con 5 µs en su subida
 *   GPIO4  el fallo 0 (activo alto, del banco): frena las dos salidas a 0
 *   GPIO5  el ETM: cada subida de GPIO0 la invierte (5 kHz, sin la CPU)
 *   el PCNT cuenta las subidas de GPIO0 durante 100 ms
 * Este fichero es parte de mcu-sim (AGPL-3.0-or-later). */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/mcpwm_prelude.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/gpio_etm.h"
#include "esp_etm.h"
#include "esp_err.h"

static volatile int frenos;

static bool al_frenar(mcpwm_oper_handle_t oper, const mcpwm_brake_event_data_t *d, void *ctx)
{
    frenos++;
    return false;
}

void app_main(void)
{
    // --- M1: el MCPWM ---
    mcpwm_timer_handle_t timer;
    mcpwm_timer_config_t tc = {
        .group_id = 0, .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT, .resolution_hz = 1000000,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP, .period_ticks = 100,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&tc, &timer));
    mcpwm_oper_handle_t oper;
    mcpwm_operator_config_t oc = {.group_id = 0};
    ESP_ERROR_CHECK(mcpwm_new_operator(&oc, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));
    mcpwm_cmpr_handle_t cmp;
    mcpwm_comparator_config_t cc = {.flags.update_cmp_on_tez = true};
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &cc, &cmp));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmp, 30));
    mcpwm_gen_handle_t gena, genb;
    mcpwm_generator_config_t gc = {.gen_gpio_num = 0};
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gc, &gena));
    gc.gen_gpio_num = 2;
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gc, &genb));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gena,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gena,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmp, MCPWM_GEN_ACTION_LOW)));
    mcpwm_dead_time_config_t dt = {.posedge_delay_ticks = 5};
    ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gena, gena, &dt));
    mcpwm_dead_time_config_t dtb = {.negedge_delay_ticks = 5, .flags.invert_output = true};
    ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(gena, genb, &dtb));
    // El fallo en GPIO4: frena del todo (OST), las dos salidas a 0
    mcpwm_fault_handle_t fallo;
    mcpwm_gpio_fault_config_t fc = {.group_id = 0, .gpio_num = 4, .flags.active_level = 1, .flags.pull_down = true};
    ESP_ERROR_CHECK(mcpwm_new_gpio_fault(&fc, &fallo));
    mcpwm_brake_config_t bc = {.fault = fallo, .brake_mode = MCPWM_OPER_BRAKE_MODE_OST};
    ESP_ERROR_CHECK(mcpwm_operator_set_brake_on_fault(oper, &bc));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_brake_event(gena,
        MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_OPER_BRAKE_MODE_OST, MCPWM_GEN_ACTION_LOW)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_brake_event(genb,
        MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_OPER_BRAKE_MODE_OST, MCPWM_GEN_ACTION_LOW)));
    mcpwm_operator_event_callbacks_t cbs = {.on_brake_ost = al_frenar};
    ESP_ERROR_CHECK(mcpwm_operator_register_event_callbacks(oper, &cbs, NULL));
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    printf("M1 mcpwm en marcha\n");

    // --- M2: el PCNT cuenta las subidas de GPIO0 ---
    pcnt_unit_handle_t unidad;
    pcnt_unit_config_t uc = {.high_limit = 30000, .low_limit = -1};
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, &unidad));
    pcnt_channel_handle_t canal;
    pcnt_chan_config_t chc = {.edge_gpio_num = 0, .level_gpio_num = -1};
    ESP_ERROR_CHECK(pcnt_new_channel(unidad, &chc, &canal));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(canal, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_unit_enable(unidad));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unidad));
    ESP_ERROR_CHECK(pcnt_unit_start(unidad));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(pcnt_unit_stop(unidad));
    int n = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(unidad, &n));
    printf("M2 pcnt %d\n", n);

    // --- M3: el ETM, cada subida de GPIO0 invierte GPIO5 ---
    gpio_config_t g5 = {.pin_bit_mask = 1ULL << 5, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&g5));
    esp_etm_event_handle_t suceso;
    gpio_etm_event_config_t ec = {.edge = GPIO_ETM_EVENT_EDGE_POS};
    ESP_ERROR_CHECK(gpio_new_etm_event(&ec, &suceso));
    ESP_ERROR_CHECK(gpio_etm_event_bind_gpio(suceso, 0));
    esp_etm_task_handle_t tarea;
    gpio_etm_task_config_t tk = {.action = GPIO_ETM_TASK_ACTION_TOG};
    ESP_ERROR_CHECK(gpio_new_etm_task(&tk, &tarea));
    ESP_ERROR_CHECK(gpio_etm_task_add_gpio(tarea, 5));
    esp_etm_channel_handle_t ch;
    esp_etm_channel_config_t ecc = {};
    ESP_ERROR_CHECK(esp_etm_new_channel(&ecc, &ch));
    ESP_ERROR_CHECK(esp_etm_channel_connect(ch, suceso, tarea));
    ESP_ERROR_CHECK(esp_etm_channel_enable(ch));
    printf("M3 etm en marcha\n");

    // --- M4: el banco sube GPIO4 y el operador frena ---
    while (!frenos) vTaskDelay(1);
    printf("M4 freno %d\n", frenos);
    while (gpio_get_level(4)) vTaskDelay(1);
    ESP_ERROR_CHECK(mcpwm_operator_recover_from_fault(oper, fallo));
    printf("M5 vuelve\n");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
