#ifndef PN532_CTRL_H
#define PN532_CTRL_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NFC_STATE_IDLE,       // not running
    NFC_STATE_WAITING,    // armed, waiting for a card
    NFC_STATE_GOT_UID     // last_uid holds a fresh UID
} nfc_state_t;

/* Call once at boot: brings up the PN532 (SAM config, power mode, fw check). */
esp_err_t pn532_ctrl_init(void);

/* Starts continuous scanning in a background task. */
void pn532_ctrl_start(void);

/* Stops the background task and returns to IDLE. */
void pn532_ctrl_stop(void);

nfc_state_t pn532_ctrl_get_state(void);

/* Copies the last-read UID (as an uppercase hex string) into out_buf.
 * out_buf must be at least 16 bytes (7 bytes * 2 hex chars + '\0'). */
void pn532_ctrl_get_uid_str(char *out_buf, size_t out_buf_len);

#endif // PN532_CTRL_H
