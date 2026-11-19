#include "ir_leds.h"
#include "config.h"

#ifdef PICO_ON_DEVICE
#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"
#endif

static bool s_ir_leds_enabled = false;

void ir_leds_init(void) {
#ifdef PICO_ON_DEVICE
    gpio_init(IR_LED_GPIO_PIN);
    gpio_set_dir(IR_LED_GPIO_PIN, GPIO_OUT);
    gpio_put(IR_LED_GPIO_PIN, 0);
#endif
    s_ir_leds_enabled = false;
}

void ir_leds_set_enabled(bool enabled) {
    s_ir_leds_enabled = enabled;
#ifdef PICO_ON_DEVICE
    gpio_put(IR_LED_GPIO_PIN, enabled ? 1 : 0);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, enabled ? 1 : 0);
#endif
}

bool ir_leds_is_enabled(void) {
    return s_ir_leds_enabled;
}
