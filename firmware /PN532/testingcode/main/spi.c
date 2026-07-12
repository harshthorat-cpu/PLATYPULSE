/**
 * @file spi.c
 * @brief Implementation of the SPI bus service.
 */

/*================================= Header Inclusions =================================*/
#include "spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ansi_macro.h"
/*================================= Header Inclusions =================================*/

#define SPI_MISO_PIN GPIO_NUM_12
#define SPI_MOSI_PIN GPIO_NUM_13
#define SPI_CLK_PIN GPIO_NUM_14
#define SPI_CS_RFID_PIN GPIO_NUM_26

/*================================= Definitions and Constants =================================*/
static const char *TAG = BLUE "SPI" RESET;
/*================================= Definitions and Constants =================================*/


/*================================= Static Variables =================================*/
static SemaphoreHandle_t spi_mutex = NULL;
static spi_device_handle_t s_spi_device_handles[SPI_DEVICE_COUNT];
/*================================= Static Variables =================================*/


/*================================= Public Function Implementation =================================*/
esp_err_t spi_bus_init(void) {
    if (spi_mutex != NULL) {
        ESP_LOGW(TAG, "SPI bus already initialized.");
        return ESP_OK;
    }

    spi_mutex = xSemaphoreCreateRecursiveMutex();
    if (spi_mutex == NULL) {
        ESP_LOGE(TAG, RED "Failed to create spi_mutex!" RESET);
        return ESP_FAIL;
    }

    gpio_reset_pin(SPI_CS_RFID_PIN);
    gpio_set_direction(SPI_CS_RFID_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SPI_CS_RFID_PIN, 1);

    spi_bus_config_t buscfg = {
        .miso_io_num = SPI_MISO_PIN,
        .mosi_io_num = SPI_MOSI_PIN,
        .sclk_io_num = SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t rfid_devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 3,
        .flags = SPI_DEVICE_BIT_LSBFIRST,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &rfid_devcfg, &s_spi_device_handles[SPI_DEVICE_RFID]));

    ESP_LOGI(TAG, GREEN "SPI bus successfully initialized." RESET);
    return ESP_OK;
}

bool spi_transmit(spi_device_id_t device_id, const uint8_t *tx_data, uint8_t *rx_data, size_t len_bits) {
    if (device_id >= SPI_DEVICE_COUNT || spi_mutex == NULL) return false;
    if (len_bits == 0 || (len_bits % 8 != 0)) return false;

    // Determine the CS pin based on the device ID
    uint8_t cs_pin;
    switch (device_id) {
        case SPI_DEVICE_RFID: 
            cs_pin = SPI_CS_RFID_PIN; 
            break;
        default: return false;
    }

    // 1. Acquire exclusive control of the SPI bus
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE(TAG, RED "Timeout waiting for SPI bus mutex!" RESET);
        return false;
    }

    // 2. Activate Chip Select
    gpio_set_level(cs_pin, 0);

    // 3. Execute data transmission
    spi_transaction_t transaction = {
        .length = len_bits,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };
    esp_err_t ret = spi_device_transmit(s_spi_device_handles[device_id], &transaction);
    
    // 4. Deactivate Chip Select
    gpio_set_level(cs_pin, 1);

    // 5. Release control of the SPI bus
    xSemaphoreGive(spi_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, RED "SPI transmission failure (spi_device_transmit) for device %d" RESET, device_id);
    }

    return (ret == ESP_OK);
}

void spi_rfid_wakeup(){
    gpio_set_level(SPI_CS_RFID_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));  //  >=10ms more stable 
    gpio_set_level(SPI_CS_RFID_PIN, 1);

    vTaskDelay(pdMS_TO_TICKS(5));
}
/*================================= Public Function Implementation =================================*/
