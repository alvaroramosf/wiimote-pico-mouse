#ifndef IR_LEDS_H
#define IR_LEDS_H

#include <stdbool.h>

/**
 * @brief Initialize GPIO pin driving the 4-LED IR diamond transistor switch.
 *        Starts in OFF state (0 V on GP15) for automatic power saving.
 */
void ir_leds_init(void);

/**
 * @brief Enable or disable the 4 IR LEDs around the TV.
 *        Called automatically when a Wiimote connects or disconnects.
 *
 * @param enabled true to power on IR LEDs, false to power off.
 */
void ir_leds_set_enabled(bool enabled);

/**
 * @brief Query current IR LED power state.
 *
 * @return true if IR LEDs are currently powered on.
 */
bool ir_leds_is_enabled(void);

#endif /* IR_LEDS_H */
