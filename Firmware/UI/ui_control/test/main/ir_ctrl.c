/**
 * @file ir_ctrl.c
 * @brief Record-then-replay IR controller, adapted from store_and_emulate.c
 *        into a task the UI can start/stop and query.
 */
#include "ir_ctrl.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"

static const char *TAG = "IR_CTRL";

/* Hardware wiring: IR RX is on GPIO25. This no longer clashes with
 * anything else -- the PN532 IRQ lives on GPIO32 (see pn532.c), not 25. */
#define IR_RX_GPIO_NUM   25
#define IR_TX_GPIO_NUM   14
#define RMT_RESOLUTION_HZ 1000000
#define MAX_RECV_SYMBOLS  512

static rmt_channel_handle_t s_rx_chan = NULL;
static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static QueueHandle_t s_rx_queue = NULL;

static rmt_symbol_word_t s_stored_symbols[MAX_RECV_SYMBOLS];
static int s_stored_symbol_count = 0;

static TaskHandle_t s_task_handle = NULL;
static volatile bool s_run_flag = false;
static volatile ir_state_t s_state = IR_STATE_IDLE;
static bool s_init_ok = false;

static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel,
                                            const rmt_rx_done_event_data_t *edata,
                                            void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(s_rx_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

esp_err_t ir_ctrl_init(void)
{
    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    }

    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 256,
        .gpio_num = IR_RX_GPIO_NUM,
    };
    esp_err_t err = rmt_new_rx_channel(&rx_chan_config, &s_rx_chan);
    if (err != ESP_OK) return err;

    rmt_rx_event_callbacks_t rx_cbs = { .on_recv_done = rmt_rx_done_callback };
    err = rmt_rx_register_event_callbacks(s_rx_chan, &rx_cbs, NULL);
    if (err != ESP_OK) return err;
    err = rmt_enable(s_rx_chan);
    if (err != ESP_OK) return err;

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 256,
        .gpio_num = IR_TX_GPIO_NUM,
        .trans_queue_depth = 4,
    };
    err = rmt_new_tx_channel(&tx_chan_config, &s_tx_chan);
    if (err != ESP_OK) return err;

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33f,
        .flags.always_on = false,
        .flags.polarity_active_low = false,
    };
    err = rmt_apply_carrier(s_tx_chan, &carrier_config);
    if (err != ESP_OK) return err;
    err = rmt_enable(s_tx_chan);
    if (err != ESP_OK) return err;

    rmt_copy_encoder_config_t copy_encoder_config = {0};
    err = rmt_new_copy_encoder(&copy_encoder_config, &s_copy_encoder);
    if (err != ESP_OK) return err;

    s_init_ok = true;
    ESP_LOGI(TAG, "IR channels ready (RX=%d, TX=%d)", IR_RX_GPIO_NUM, IR_TX_GPIO_NUM);
    return ESP_OK;
}

static void ir_task(void *pv)
{
    static rmt_symbol_word_t temp_symbols[MAX_RECV_SYMBOLS];

    s_stored_symbol_count = 0;
    s_state = IR_STATE_RECORDING;
    ESP_LOGW(TAG, "RECORD MODE: point remote at TSOP and press a button...");

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 40000000,
    };

    while (s_run_flag && s_stored_symbol_count == 0) {
        esp_err_t err = rmt_receive(s_rx_chan, temp_symbols, sizeof(temp_symbols), &receive_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_receive failed: %d", err);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        rmt_rx_done_event_data_t rx_data;
        if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_stored_symbol_count = rx_data.num_symbols;
            memcpy(s_stored_symbols, temp_symbols, s_stored_symbol_count * sizeof(rmt_symbol_word_t));
            ESP_LOGI(TAG, "Signal saved! Captured %d symbols.", s_stored_symbol_count);
        }
    }

    if (!s_run_flag) {
        s_state = IR_STATE_IDLE;
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_state = IR_STATE_EMITTING;
    rmt_transmit_config_t tx_config = { .loop_count = 0 };

    while (s_run_flag) {
        ESP_LOGI(TAG, "EMITTING stored signal...");
        esp_err_t err = rmt_transmit(s_tx_chan, s_copy_encoder, s_stored_symbols,
                                      s_stored_symbol_count * sizeof(rmt_symbol_word_t), &tx_config);
        if (err == ESP_OK) {
            rmt_tx_wait_all_done(s_tx_chan, pdMS_TO_TICKS(2000));
        }
        for (int i = 0; i < 30 && s_run_flag; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    s_state = IR_STATE_IDLE;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void ir_ctrl_start(void)
{
    if (!s_init_ok) {
        ESP_LOGE(TAG, "Refusing to start: ir_ctrl_init() never succeeded.");
        return;
    }
    if (s_task_handle != NULL) {
        return;
    }
    s_run_flag = true;
    xTaskCreate(ir_task, "ir_ctrl_task", 8192, NULL, 5, &s_task_handle);
}

void ir_ctrl_stop(void)
{
    s_run_flag = false;
}

ir_state_t ir_ctrl_get_state(void)
{
    return s_state;
}