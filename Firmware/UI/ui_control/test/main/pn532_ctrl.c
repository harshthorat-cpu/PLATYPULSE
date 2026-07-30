/**
 * @file pn532_ctrl.c
 * @brief Continuous NFC/RFID scan controller built on top of pn532.c / spi.c.
 *        Adapted from the _rfid_task loop in main.c into a task the UI
 *        can start/stop and poll for the latest UID.
 */
#include "pn532_ctrl.h"
#include "pn532.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "PN532_CTRL";
#define RECOVERY_DELAY_MS 1000

static TaskHandle_t s_task_handle = NULL;
static volatile bool s_run_flag = false;
static volatile nfc_state_t s_state = NFC_STATE_IDLE;

static SemaphoreHandle_t s_uid_mutex = NULL;
static char s_last_uid[16] = "";
static bool s_spi_ready = false;
static bool s_init_ok = false;

esp_err_t pn532_ctrl_init(void)
{
    if (s_uid_mutex == NULL) {
        s_uid_mutex = xSemaphoreCreateMutex();
    }

    if (!s_spi_ready) {
        if (spi_bus_init() != ESP_OK) {
            return ESP_FAIL;
        }
        s_spi_ready = true;
    }

    if (pn532_init() != ESP_OK) return ESP_FAIL;
    if (pn532_sam_config() != ESP_OK) return ESP_FAIL;
    if (pn532_set_power_mode() != ESP_OK) return ESP_FAIL;
    if (pn532_get_firmware_version() != ESP_OK) return ESP_FAIL;

    s_init_ok = true;
    ESP_LOGI(TAG, "PN532 driver ready.");
    return ESP_OK;
}

static void nfc_task(void *pv)
{
    while (s_run_flag) {
        s_state = NFC_STATE_WAITING;

        if (pn532_wait_for_card() == ESP_OK) {
            uint8_t uid[7];
            uint8_t uidLength = 0;

            if (pn532_read_detected_passive_target(uid, &uidLength) == ESP_OK) {
                char uid_str[16];
                for (int i = 0; i < uidLength; i++) {
                    sprintf(&uid_str[i * 2], "%02X", uid[i]);
                }
                uid_str[uidLength * 2] = '\0';

                ESP_LOGI(TAG, "Card detected. UID: %s", uid_str);

                if (xSemaphoreTake(s_uid_mutex, portMAX_DELAY) == pdTRUE) {
                    strncpy(s_last_uid, uid_str, sizeof(s_last_uid) - 1);
                    s_last_uid[sizeof(s_last_uid) - 1] = '\0';
                    xSemaphoreGive(s_uid_mutex);
                }
                s_state = NFC_STATE_GOT_UID;
            } else {
                ESP_LOGW(TAG, "Interrupt received, but card read failed.");
            }

            vTaskDelay(pdMS_TO_TICKS(500));

        } else {
            ESP_LOGE(TAG, "Failed to arm the reader. Trying to recover...");
            vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));

            pn532_init();
            pn532_soft_reset();
            pn532_sam_config();
            trigger_isr();
        }
    }

    s_state = NFC_STATE_IDLE;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void pn532_ctrl_start(void)
{
    if (!s_init_ok) {
        ESP_LOGE(TAG, "Refusing to start: pn532_ctrl_init() never succeeded.");
        return;
    }
    if (s_task_handle != NULL) {
        return;
    }
    s_run_flag = true;
    xTaskCreatePinnedToCore(nfc_task, "pn532_ctrl_task", 8192, NULL, 5, &s_task_handle, tskNO_AFFINITY);
}

void pn532_ctrl_stop(void)
{
    s_run_flag = false;
    // The task is normally blocked inside pn532_wait_for_card() (a semaphore
    // take with portMAX_DELAY). Nudge it so it can notice s_run_flag and exit.
    trigger_isr();
}

nfc_state_t pn532_ctrl_get_state(void)
{
    return s_state;
}

void pn532_ctrl_get_uid_str(char *out_buf, size_t out_buf_len)
{
    if (out_buf == NULL || out_buf_len == 0) return;
    out_buf[0] = '\0';
    if (s_uid_mutex == NULL) return;

    if (xSemaphoreTake(s_uid_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(out_buf, s_last_uid, out_buf_len - 1);
        out_buf[out_buf_len - 1] = '\0';
        xSemaphoreGive(s_uid_mutex);
    }
}