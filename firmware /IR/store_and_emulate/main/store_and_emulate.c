#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"

static const char *TAG = "IR_STORE_EMIT";

#define IR_RX_GPIO_NUM  25
#define IR_TX_GPIO_NUM  14
#define RMT_RESOLUTION_HZ 1000000 
#define MAX_RECV_SYMBOLS  512     

static QueueHandle_t rx_queue;

// Memory to permanently store the recorded signal
static rmt_symbol_word_t stored_symbols[MAX_RECV_SYMBOLS];
static int stored_symbol_count = 0;

static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(rx_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing IR System...");
    rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

    // 1. Setup RX Channel
    rmt_channel_handle_t rx_chan = NULL;
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 256,
        .gpio_num = IR_RX_GPIO_NUM,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
    
    rmt_rx_event_callbacks_t rx_cbs = { .on_recv_done = rmt_rx_done_callback };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &rx_cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    // 2. Setup TX Channel
    rmt_channel_handle_t tx_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 256,
        .gpio_num = IR_TX_GPIO_NUM,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
        .flags.always_on = false,
        .flags.polarity_active_low = false ,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_config));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));

    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));

    // Temporary buffer for the first read
    rmt_symbol_word_t temp_symbols[MAX_RECV_SYMBOLS];

    while (1) {
        if (stored_symbol_count == 0) {
            // ==========================================
            // PHASE 1: RECORD MODE
            // ==========================================
            rmt_receive_config_t receive_config = {
                .signal_range_min_ns = 1250,      
                .signal_range_max_ns = 40000000,  
            };
            ESP_ERROR_CHECK(rmt_receive(rx_chan, temp_symbols, sizeof(temp_symbols), &receive_config));

            ESP_LOGW(TAG, "RECORD MODE: Point remote at TSOP and press a button...");

            rmt_rx_done_event_data_t rx_data;
            if (xQueueReceive(rx_queue, &rx_data, portMAX_DELAY)) {
                // Copy the temporary data into our permanent storage array
                stored_symbol_count = rx_data.num_symbols;
                memcpy(stored_symbols, temp_symbols, stored_symbol_count * sizeof(rmt_symbol_word_t));
                
                ESP_LOGI(TAG, "Signal saved! Captured %d symbols.", stored_symbol_count);
                
                // Disable the RX channel so we don't accidentally record over it
                rmt_disable(rx_chan); 
            }
        } 
        else {
            // ==========================================
            // PHASE 2: EMULATE MODE
            // ==========================================
            ESP_LOGI(TAG, "EMITTING stored signal...");
            
            rmt_transmit_config_t tx_config = {
                .loop_count = 0, 
            };
            
            ESP_ERROR_CHECK(rmt_transmit(tx_chan, copy_encoder, stored_symbols, stored_symbol_count * sizeof(rmt_symbol_word_t), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_chan, -1));
            
            // Wait 3 seconds before emitting again
            vTaskDelay(pdMS_TO_TICKS(3000)); 
        }
    }
}