/**
 * @file spi.c
 * @brief Implementation of the shared SPI bus service declared in spi.h.
 *
 * @details Wraps the ESP-IDF SPI master driver, adding one spi_device handle
 *          per logical device (ADC, RFID/PN532, ATM90) and a bus mutex so
 *          spi_transmit() is safe to call from multiple tasks.
 *
 *          !!! ADJUST THE PIN / CS / SPEED DEFINES BELOW TO MATCH YOUR
 *          !!! ACTUAL WIRING. The values here are placeholders picked to
 *          !!! compile and link cleanly -- they are NOT guaranteed to match
 *          !!! your hardware.
 */
#include "spi.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPI";

/* ---- Bus pins (shared with the TFT display bus set up in test.c) ----
 * SPI2_HOST/MOSI 23/SCLK 18 must stay identical to TFT_HOST/TFT_MOSI/TFT_SCLK
 * in test.c, since both display and RFID devices sit on the same physical
 * bus. Whichever spi_bus_initialize() call runs first "wins" on bus-level
 * settings like MISO -- test.c's display_hw_init() runs first (see app_main),
 * so TFT_MISO there has been set to this same pin (19) to make it usable. */
#define SPI_HOST_USED     SPI2_HOST
#define PIN_NUM_MOSI      23
#define PIN_NUM_MISO      19
#define PIN_NUM_CLK       18

/* ---- Per-device chip-select pins ----
 * NOTE: GPIO 5 is used as TFT_CS by the display in test.c (shared SPI2
 * bus) -- do not reuse it here or spi_bus_add_device() ends up with two
 * devices on the same CS line, which corrupts transactions on the bus
 * (including the display's). Pick any free GPIO instead; 13 used here
 * as a placeholder for ADC CS -- rewire/verify against your actual wiring. */
/* Moved off GPIO32 -- that pin is now the PN532 IRQ line (see pn532.c
 * RFID_IRQ_PIN), matching actual hardware wiring. GPIO13 is unused
 * elsewhere in this project; rewire/verify against your actual ADC
 * chip select if that's not where it's physically connected. */
#define PIN_CS_ADC        13
#define PIN_CS_RFID       15
#define PIN_CS_ATM90      27

/* ---- Per-device clock speeds ---- */
#define SPI_CLK_HZ_ADC    1000000
#define SPI_CLK_HZ_RFID   1000000
#define SPI_CLK_HZ_ATM90  1000000

static spi_device_handle_t s_dev_handles[SPI_DEVICE_COUNT];
static SemaphoreHandle_t s_bus_mutex = NULL;
static bool s_bus_ready = false;

static esp_err_t add_device(spi_device_id_t id, int cs_pin, int clock_hz)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1,
    };
    return spi_bus_add_device(SPI_HOST_USED, &devcfg, &s_dev_handles[id]);
}

esp_err_t spi_bus_init(void)
{
    if (s_bus_ready) {
        return ESP_OK;
    }

    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutex();
        if (s_bus_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create bus mutex");
            return ESP_FAIL;
        }
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };

    esp_err_t err = spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", err);
        return err;
    }

    err = add_device(SPI_DEVICE_ADC, PIN_CS_ADC, SPI_CLK_HZ_ADC);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ADC device: %d", err);
        return err;
    }

    err = add_device(SPI_DEVICE_RFID, PIN_CS_RFID, SPI_CLK_HZ_RFID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RFID device: %d", err);
        return err;
    }

    err = add_device(SPI_DEVICE_ATM90, PIN_CS_ATM90, SPI_CLK_HZ_ATM90);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ATM90 device: %d", err);
        return err;
    }

    s_bus_ready = true;
    ESP_LOGI(TAG, "SPI bus ready (MOSI=%d MISO=%d CLK=%d)", PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK);
    return ESP_OK;
}

bool spi_transmit(spi_device_id_t device_id, const uint8_t *tx_data, uint8_t *rx_data, size_t len_bits)
{
    if (!s_bus_ready || device_id >= SPI_DEVICE_COUNT) {
        return false;
    }
    if (len_bits == 0) {
        return false;
    }

    bool ok = false;
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = len_bits;
        t.tx_buffer = tx_data;
        t.rx_buffer = rx_data;

        esp_err_t err = spi_device_transmit(s_dev_handles[device_id], &t);
        ok = (err == ESP_OK);
        if (!ok) {
            ESP_LOGE(TAG, "spi_device_transmit failed: %d", err);
        }
        xSemaphoreGive(s_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Bus mutex timeout");
    }
    return ok;
}

void spi_rfid_wakeup(void)
{
    /* PN532 in SPI mode wakes on a CS low pulse. A dummy single-byte
     * transaction to the RFID device toggles CS low then high. */
    if (!s_bus_ready) {
        return;
    }
    uint8_t dummy = 0x00;
    spi_transmit(SPI_DEVICE_RFID, &dummy, NULL, 8);
}