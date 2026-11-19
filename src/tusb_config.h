#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * COMMON CONFIGURATION
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#ifdef PICO_RP2350
#define CFG_TUSB_MCU OPT_MCU_RP2350
#else
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif
#endif

#ifndef CFG_TUSB_OS
#ifdef PICO_ON_DEVICE
#define CFG_TUSB_OS OPT_OS_PICO
#else
#define CFG_TUSB_OS OPT_OS_NONE
#endif
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

/*--------------------------------------------------------------------
 * DEVICE CONFIGURATION
 *--------------------------------------------------------------------*/
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE      64
#endif

/* Enable USB HID class with 2 interfaces:
 *   0: Composite (Absolute Mouse + Keyboard + Consumer Control)
 *   1: Vendor-defined WebHID Configurator */
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 2
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* HID buffer size (>= 65 bytes so Report ID 4 + 64-byte Feature Report fits) */
#define CFG_TUD_HID_EP_BUFSIZE      128

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
