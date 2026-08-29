#ifndef IR_CTRL_H
#define IR_CTRL_H

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    IR_STATE_IDLE,       // not running
    IR_STATE_RECORDING,  // "point remote and press a button"
    IR_STATE_EMITTING    // signal captured, looping playback
} ir_state_t;

/* Call once at boot (sets up RMT RX/TX channels + GPIOs). */
esp_err_t ir_ctrl_init(void);

/* Starts the record->emit loop in a background task.
 * Safe to call again after ir_ctrl_stop(). */
void ir_ctrl_start(void);

/* Stops the background task and returns to IDLE. */
void ir_ctrl_stop(void);

/* Current state, safe to poll from the UI thread. */
ir_state_t ir_ctrl_get_state(void);

#endif // IR_CTRL_H
