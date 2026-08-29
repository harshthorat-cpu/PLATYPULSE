/**
 * @file spi.h
 * @brief API for the SPI bus service.
 * @details Provides granular control over the bus mutex and device selection
 *          (CS), in addition to data transfer.
 */

#ifndef SPI_H
#define SPI_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SPI_DEVICE_ADC,
    SPI_DEVICE_RFID,
    SPI_DEVICE_ATM90,
    SPI_DEVICE_COUNT // Used to know the number of devices
} spi_device_id_t;



/**
 * @brief Initializes the SPI bus and adds known devices.
 * @return ESP_OK on success.
 */
esp_err_t spi_bus_init(void);

/**
 * @brief Executes a complete SPI transaction atomically and thread-safely.
 * @details This is the only function required for SPI communication. It internally manages
 *          bus lock and Chip Select (CS) control.
 * 
 * @param device_id The ID of the target device for the transaction.
 * @param tx_data Pointer to the data to be sent. Can be NULL if only receiving.
 * @param rx_data Pointer to the receive buffer. Can be NULL if only sending.
 * @param len_bits The length of the transaction in bits (must be a multiple of 8).
 * @return true if the transaction was successful, false otherwise.
 */
bool spi_transmit(spi_device_id_t device_id, const uint8_t *tx_data, uint8_t *rx_data, size_t len_bits);

/**
 * @brief Function to 
"wake up" the RFID device via SPI.
 * @details Sends a pulse on the RFID's CS to take it out of sleep mode.
 */
void spi_rfid_wakeup();

#endif 
