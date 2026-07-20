#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define IR_PIN 26 

void app_main(void)
{
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Ensures pin stays HIGH when idle
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    

    while (1) {
        // TSOP outputs LOW (0) when it detects a 38kHz IR light burst
        if (gpio_get_level(IR_PIN) == 0) {
            printf("ir signal mil gaya \n");
            
            // Short delay to avoid spamming the console while the button is held
            vTaskDelay(pdMS_TO_TICKS(200)); 
        }

        // Poll every 10ms to stay responsive
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}