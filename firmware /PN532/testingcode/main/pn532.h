/**
 * @file pn532.h
 * @brief Component driver for the PN532 RFID/NFC reader.
 *
 * @details This module provides a high-level, blocking, and thread-safe API
 *          to interact with the PN532 chip. It encapsulates all the complexity
 *          of the SPI protocol, interrupt management (IRQ), and task
 *          synchronization (semaphore), allowing the application layer to
 *          use it in a simple and sequential manner.
 */

#ifndef PN532_H
#define PN532_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*================================= Public Functions =================================*/

/**
 * @brief Initializes the PN532 driver.
 * @details This function should be called only once during system initialization.
 *          It initializes the internal semaphore, configures the GPIO and ISR for the
 *          interrupt pin (IRQ), and establishes initial communication with the chip.
 * 
 * @return ESP_OK if the complete initialization was successful.
 * @return ESP_FAIL in case of failure in semaphore creation or ISR configuration.
 */
esp_err_t pn532_init(void);

/**
 * @brief Configures the SAM (Secure Access Module) of the PN532 for normal operation with IRQ.
 * @details Blocking function that configures the module for the default reading mode.
 *          Uses the internal IRQ mechanism to wait for responses.
 * 
 * @return ESP_OK if the configuration was successful.
 * @return ESP_FAIL in case of communication error or timeout.
 */
esp_err_t pn532_sam_config(void);

/**
 * @brief Configures the PN532 to the default power consumption mode.
 * @details Blocking function that communicates with the chip to adjust the power mode.
 *          Uses the internal IRQ mechanism to wait for responses.
 * 
 * @param version_out Pointer to a uint32_t variable where the version will be stored.
 * @return ESP_OK if the configuration was successful.
 * @return ESP_FAIL in case of communication error or timeout.
 */
esp_err_t pn532_set_power_mode(void);

/**
 * @brief Gets the firmware version of the PN532 chip.
 * @details Blocking function that communicates with the chip to read the firmware version.
 *          Uses the internal IRQ mechanism to wait for responses.
 * 
 * @param version_out Pointer to a uint32_t variable where the version will be stored.
 * @return ESP_OK if the reading was successful.
 * @return ESP_FAIL in case of communication error or timeout.
 */
esp_err_t pn532_get_firmware_version(void);

/**
 * @brief Executes a software reset on the PN532, reinitializing the module without cutting power.
 *
 * This function sends the Soft Reset command to the PN532 and waits for the minimum
 * time required for the chip to return to its initial state, allowing reconfiguration
 * and recovery after communication failures.
 *
 * @return ESP_OK: Reset performed successfully   
 * @return ESP_FAIL: Failed to send the reset command
 */
esp_err_t pn532_soft_reset(void);

/**
 * @brief Blocking function that arms the reader and waits for card detection.
 * @details This is the main wait function of the driver. It sends the command to
 *          detect a target and blocks the calling task efficiently (without
 *          CPU consumption) until the PN532 signals the presence of a card via IRQ.
 * 
 * @return ESP_OK if a card was detected (IRQ signal received).
 * @return ESP_FAIL if the command to arm the reader failed.
 */
esp_err_t pn532_wait_for_card(void);

/**
 * @brief Attempts to read the UID of a passive target that has just been detected.
 * @details This function should be called immediately after pn532_wait_for_card()
 *          returns successfully. It performs the reading of the data that the PN532
 *          has already prepared in its internal buffer.
 * 
 * @param uid Pointer to the buffer that will store the UID (must have at least 7 bytes).
 * @param uidLength Pointer to the variable that will store the UID size.
 * @return ESP_OK if a card was read successfully.
 * @return ESP_ERR_NOT_FOUND if the PN532 response does not indicate the presence of a card.
 * @return ESP_FAIL for other communication errors.
 */
esp_err_t pn532_read_detected_passive_target(uint8_t *uid, uint8_t *uidLength);

/**
 * @brief Manually triggers the interrupt service routine (ISR) via firmware.
 *
 * This function forces the execution of the associated ISR, allowing testing or
 * simulating interrupt events without depending on the actual hardware signal.
 */
void trigger_isr();



/*================================= Public Functions =================================*/

#endif // PN532_H
