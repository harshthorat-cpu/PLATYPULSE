#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define IR_RECEIVER_GPIO_NUM    4

static const char *TAG = "IR_DETECT";

void ir_receiver_init(void)
{
    ESP_LOGI(TAG, "initialize the signal ");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IR_RECEIVER_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "ir receive will be going to start  %d", IR_RECEIVER_GPIO_NUM);
}

void app_main(void)
{
    ir_receiver_init();

    while (1) {
      
        int ir_status = gpio_get_level(IR_RECEIVER_GPIO_NUM);

        if (ir_status == 0) {
            ESP_LOGI(TAG, "IR Signal DETECTED!");
        } else {
            ESP_LOGI(TAG, "No IR Signal");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}