#include "wiimote_bt.h"
#include "config.h"
#include "flash_storage.h"
#include "ir_leds.h"

#include <stdio.h>
#include <string.h>

#ifdef PICO_ON_DEVICE
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#endif

#define WIIMOTE_REPORT_QUEUE_SIZE 16U

static wiimote_report_t s_report_queue[WIIMOTE_REPORT_QUEUE_SIZE];
static volatile uint8_t s_queue_head = 0;
static volatile uint8_t s_queue_tail = 0;

bool wiimote_bt_pop_report(wiimote_report_t *out) {
    if (!out || s_queue_head == s_queue_tail) {
        return false;
    }
    *out = s_report_queue[s_queue_tail];
    s_queue_tail = (uint8_t)((s_queue_tail + 1U) % WIIMOTE_REPORT_QUEUE_SIZE);
    return true;
}

bool wiimote_parse_report_0x33(const uint8_t *packet, uint16_t len, wiimote_report_t *out) {
    if (!packet || !out || len < 19U) {
        return false;
    }
    if (packet[0] != 0xA1U || packet[1] != 0x33U) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Core buttons: mask out accelerometer LSB bits (0x60 in bytes 2 & 3) */
    out->buttons = ((uint16_t)(packet[2] & 0x1FU)) |
                   (((uint16_t)(packet[3] & 0x9FU)) << 8);

    /* 10-bit accelerometers centered around 512 (0x200) */
    uint16_t raw_ax = ((uint16_t)packet[4] << 2) | ((uint16_t)(packet[2] >> 5) & 0x03U);
    uint16_t raw_ay = ((uint16_t)packet[5] << 2) | ((uint16_t)(packet[3] >> 5) & 0x02U);
    uint16_t raw_az = ((uint16_t)packet[6] << 2) | ((uint16_t)(packet[3] >> 4) & 0x02U);

    out->accel_x = (int16_t)raw_ax - 512;
    out->accel_y = (int16_t)raw_ay - 512;
    out->accel_z = (int16_t)raw_az - 512;

    /* 12-byte Extended IR payload: 4 slots x 3 bytes */
    uint8_t visible = 0;
    for (int i = 0; i < 4; ++i) {
        const uint8_t b0 = packet[7 + i * 3];
        const uint8_t b1 = packet[8 + i * 3];
        const uint8_t b2 = packet[9 + i * 3];

        if (b0 == 0xFFU && b1 == 0xFFU) {
            out->ir[i].valid = false;
            continue;
        }

        uint16_t x = (uint16_t)b0 | (((uint16_t)(b2 & 0x30U)) << 4);
        uint16_t y = (uint16_t)b1 | (((uint16_t)(b2 & 0xC0U)) << 2);
        uint8_t size = (uint8_t)(b2 & 0x0FU);

        out->ir[i].x = x;
        out->ir[i].y = y;
        out->ir[i].size = size;
        out->ir[i].valid = (x < WIIMOTE_IR_WIDTH && y < WIIMOTE_IR_HEIGHT);
        if (out->ir[i].valid) {
            visible++;
        }
    }
    out->visible_ir_count = visible;

#ifdef PICO_ON_DEVICE
    out->timestamp_ms = to_ms_since_boot(get_absolute_time());
#else
    out->timestamp_ms = 0;
#endif

    return true;
}

#ifdef PICO_ON_DEVICE

static void queue_push_report(const wiimote_report_t *r) {
    uint8_t next_head = (uint8_t)((s_queue_head + 1U) % WIIMOTE_REPORT_QUEUE_SIZE);
    if (next_head == s_queue_tail) {
        /* Drop oldest report to keep latest pointer data low-latency */
        s_queue_tail = (uint8_t)((s_queue_tail + 1U) % WIIMOTE_REPORT_QUEUE_SIZE);
    }
    s_report_queue[s_queue_head] = *r;
    s_queue_head = next_head;
}

#ifndef PSM_HID_CONTROL
#define PSM_HID_CONTROL             0x0011U
#endif
#ifndef PSM_HID_INTERRUPT
#define PSM_HID_INTERRUPT           0x0013U
#endif
#define WIIMOTE_COD_PERIPHERAL      0x002504U
#define WIIMOTE_INQUIRY_DURATION    5U      /* 5 * 1.28s = 6.4s */
#define WIIMOTE_INIT_STEP_DELAY_MS  50U

typedef enum {
    BT_STATE_IDLE = 0,
    BT_STATE_INQUIRY,
    BT_STATE_NAME_REQUEST,
    BT_STATE_CONNECTING_CTRL,
    BT_STATE_CONNECTING_INTR,
    BT_STATE_INIT_WIIMOTE,
    BT_STATE_CONNECTED
} bt_conn_state_t;

typedef enum {
    INIT_STEP_LED1 = 0,
    INIT_STEP_IR_CLK,
    INIT_STEP_IR_LOGIC,
    INIT_STEP_REG_30_1,
    INIT_STEP_SENS_BLOCK1,
    INIT_STEP_SENS_BLOCK2,
    INIT_STEP_IR_MODE_EXT,
    INIT_STEP_REG_30_2,
    INIT_STEP_REPORT_0X33,
    INIT_STEP_DONE
} wiimote_init_step_t;

static btstack_packet_callback_registration_t s_hci_event_callback_reg;
static bt_conn_state_t s_bt_state = BT_STATE_IDLE;
static wiimote_init_step_t s_init_step = INIT_STEP_DONE;

static bd_addr_t s_target_addr;
static bool      s_target_is_tr = false;
static bool      s_pin_use_host_addr = false;

static uint16_t  s_cid_control = 0;
static uint16_t  s_cid_interrupt = 0;
static bool      s_init_tx_inflight = false;
static uint32_t  s_last_init_tx_ms = 0;

static bool      s_led_update_pending = false;
static uint8_t   s_pending_led_mask = WIIMOTE_LED_1;
static bool      s_pending_rumble = false;

/* PixArt Sensitivity Profiles (WiiBrew verified) */
#if WIIMOTE_IR_SENSITIVITY_LEVEL == 1
static const uint8_t IR_SENS_BLOCK1[9] = {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xAA, 0x00, 0x64};
static const uint8_t IR_SENS_BLOCK2[2] = {0x63, 0x03};
#elif WIIMOTE_IR_SENSITIVITY_LEVEL == 3
static const uint8_t IR_SENS_BLOCK1[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x0C};
static const uint8_t IR_SENS_BLOCK2[2] = {0x00, 0x00};
#elif WIIMOTE_IR_SENSITIVITY_LEVEL == 4
static const uint8_t IR_SENS_BLOCK1[9] = {0x07, 0x00, 0x00, 0x71, 0x01, 0x00, 0x72, 0x00, 0x20};
static const uint8_t IR_SENS_BLOCK2[2] = {0x1F, 0x03};
#else
/* Level 2 (Default): inio High Sensitivity for 920-940nm LEDs at 2.5m+ */
static const uint8_t IR_SENS_BLOCK1[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x41};
static const uint8_t IR_SENS_BLOCK2[2] = {0x40, 0x00};
#endif

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool is_wiimote_name(const char *name) {
    if (!name) {
        return false;
    }
    return strncmp(name, "Nintendo RVL-CNT-01", 19) == 0;
}

static void start_inquiry(void) {
    if (s_bt_state == BT_STATE_IDLE || s_bt_state == BT_STATE_INQUIRY) {
        s_bt_state = BT_STATE_INQUIRY;
        gap_inquiry_start(WIIMOTE_INQUIRY_DURATION);
    }
}

static void handle_disconnect_cleanup(void) {
    s_cid_control = 0;
    s_cid_interrupt = 0;
    s_init_step = INIT_STEP_DONE;
    s_init_tx_inflight = false;
    s_led_update_pending = false;
    s_bt_state = BT_STATE_IDLE;
    ir_leds_set_enabled(false);
    start_inquiry();
}

static void send_reg_write_0x16(uint16_t cid, uint32_t reg_addr,
                                const uint8_t *data, uint8_t data_len) {
    uint8_t pkt[23];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0xA2U;
    pkt[1] = 0x16U;
    pkt[2] = (uint8_t)((reg_addr >> 24) & 0xFFU);
    pkt[3] = (uint8_t)((reg_addr >> 16) & 0xFFU);
    pkt[4] = (uint8_t)((reg_addr >> 8) & 0xFFU);
    pkt[5] = (uint8_t)(reg_addr & 0xFFU);
    pkt[6] = data_len;
    if (data_len > 16U) {
        data_len = 16U;
    }
    memcpy(&pkt[7], data, data_len);
    l2cap_send(cid, pkt, sizeof(pkt));
}

static void send_next_init_step(void) {
    if (s_cid_interrupt == 0) {
        return;
    }

    uint8_t val = 0;
    switch (s_init_step) {
        case INIT_STEP_LED1: {
            uint8_t pkt[3] = {0xA2U, 0x11U, WIIMOTE_LED_1};
            l2cap_send(s_cid_interrupt, pkt, sizeof(pkt));
            break;
        }
        case INIT_STEP_IR_CLK: {
            uint8_t pkt[3] = {0xA2U, 0x13U, 0x04U};
            l2cap_send(s_cid_interrupt, pkt, sizeof(pkt));
            break;
        }
        case INIT_STEP_IR_LOGIC: {
            uint8_t pkt[3] = {0xA2U, 0x1AU, 0x04U};
            l2cap_send(s_cid_interrupt, pkt, sizeof(pkt));
            break;
        }
        case INIT_STEP_REG_30_1:
            val = 0x01U;
            send_reg_write_0x16(s_cid_interrupt, 0x04B00030UL, &val, 1U);
            break;
        case INIT_STEP_SENS_BLOCK1:
            send_reg_write_0x16(s_cid_interrupt, 0x04B00000UL,
                                IR_SENS_BLOCK1, sizeof(IR_SENS_BLOCK1));
            break;
        case INIT_STEP_SENS_BLOCK2:
            send_reg_write_0x16(s_cid_interrupt, 0x04B0001AUL,
                                IR_SENS_BLOCK2, sizeof(IR_SENS_BLOCK2));
            break;
        case INIT_STEP_IR_MODE_EXT:
            val = 0x03U; /* Extended IR mode: 4 points, X/Y + size */
            send_reg_write_0x16(s_cid_interrupt, 0x04B00033UL, &val, 1U);
            break;
        case INIT_STEP_REG_30_2:
            val = 0x08U; /* Enable IR camera output */
            send_reg_write_0x16(s_cid_interrupt, 0x04B00030UL, &val, 1U);
            break;
        case INIT_STEP_REPORT_0X33: {
            /* Continuous (0x04) 100 Hz reporting in mode 0x33 */
            uint8_t pkt[4] = {0xA2U, 0x12U, 0x04U, 0x33U};
            l2cap_send(s_cid_interrupt, pkt, sizeof(pkt));
            break;
        }
        case INIT_STEP_DONE:
        default:
            return;
    }

    s_last_init_tx_ms = now_ms();
    s_init_tx_inflight = true;
    s_init_step = (wiimote_init_step_t)(s_init_step + 1);
    if (s_init_step == INIT_STEP_DONE) {
        s_bt_state = BT_STATE_CONNECTED;
        s_init_tx_inflight = false;
        printf("Wiimote IR camera and 0x33 reporting initialized.\n");
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    bd_addr_t event_addr;

    if (packet_type == L2CAP_DATA_PACKET) {
        if (channel == s_cid_interrupt && size >= 2U && packet[0] == 0xA1U) {
            if (packet[1] == 0x33U) {
                wiimote_report_t rep;
                if (wiimote_parse_report_0x33(packet, size, &rep)) {
                    queue_push_report(&rep);
                }
            } else if (packet[1] == 0x22U) {
                /* Write ACK received: allow next init command immediately */
                s_init_tx_inflight = false;
                if (s_bt_state == BT_STATE_INIT_WIIMOTE && s_init_step < INIT_STEP_DONE) {
                    l2cap_request_can_send_now_event(s_cid_interrupt);
                }
            }
        }
        return;
    }

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    uint8_t event = hci_event_packet_get_type(packet);
    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BTstack Classic working. Listening & scanning for Wiimote...\n");
                start_inquiry();
            }
            break;

        case GAP_EVENT_INQUIRY_RESULT: {
            if (s_bt_state != BT_STATE_INQUIRY) {
                break;
            }
            gap_event_inquiry_result_get_bd_addr(packet, event_addr);
            uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
            bool has_name = gap_event_inquiry_result_get_name_available(packet) != 0;
            bool match = false;

            if (has_name) {
                const char *name = (const char *)gap_event_inquiry_result_get_name(packet);
                if (is_wiimote_name(name)) {
                    s_target_is_tr = (strstr(name, "-TR") != NULL);
                    match = true;
                }
            } else if (cod == WIIMOTE_COD_PERIPHERAL) {
                memcpy(s_target_addr, event_addr, sizeof(bd_addr_t));
                uint8_t psr = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
                uint16_t clk = gap_event_inquiry_result_get_clock_offset(packet);
                s_bt_state = BT_STATE_NAME_REQUEST;
                gap_inquiry_stop();
                gap_remote_name_request(s_target_addr, psr, clk);
                break;
            }

            if (match) {
                memcpy(s_target_addr, event_addr, sizeof(bd_addr_t));
                gap_inquiry_stop();
                s_bt_state = BT_STATE_CONNECTING_CTRL;
                gap_set_security_level(s_target_is_tr ? LEVEL_2 : LEVEL_0);
                l2cap_create_channel(packet_handler, s_target_addr,
                                     PSM_HID_CONTROL, 64U, &s_cid_control);
            }
            break;
        }

        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
            if (s_bt_state == BT_STATE_NAME_REQUEST) {
                uint8_t status = hci_event_remote_name_request_complete_get_status(packet);
                const char *name = hci_event_remote_name_request_complete_get_remote_name(packet);
                if (status == 0 && is_wiimote_name(name)) {
                    s_target_is_tr = (strstr(name, "-TR") != NULL);
                    s_bt_state = BT_STATE_CONNECTING_CTRL;
                    gap_set_security_level(s_target_is_tr ? LEVEL_2 : LEVEL_0);
                    l2cap_create_channel(packet_handler, s_target_addr,
                                         PSM_HID_CONTROL, 64U, &s_cid_control);
                } else {
                    s_bt_state = BT_STATE_IDLE;
                    start_inquiry();
                }
            }
            break;

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (s_bt_state == BT_STATE_INQUIRY) {
                s_bt_state = BT_STATE_IDLE;
                start_inquiry();
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
            hci_event_pin_code_request_get_bd_addr(packet, event_addr);
            uint8_t pin[6];
            if (s_pin_use_host_addr) {
                bd_addr_t local_addr;
                gap_local_bd_addr(local_addr);
                for (int i = 0; i < 6; ++i) {
                    pin[i] = local_addr[5 - i];
                }
            } else {
                for (int i = 0; i < 6; ++i) {
                    pin[i] = event_addr[5 - i];
                }
            }
            gap_pin_code_response_binary(event_addr, pin, 6U);
            break;
        }

        case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT: {
            uint8_t status = hci_event_authentication_complete_get_status(packet);
            if (status != 0) {
                /* Toggle between 1+2 (remote BD_ADDR) and SYNC (host BD_ADDR) */
                s_pin_use_host_addr = !s_pin_use_host_addr;
            }
            break;
        }

        case L2CAP_EVENT_INCOMING_CONNECTION: {
            uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
            uint16_t local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
            l2cap_event_incoming_connection_get_address(packet, event_addr);

            if (psm == PSM_HID_CONTROL) {
                gap_inquiry_stop();
                memcpy(s_target_addr, event_addr, sizeof(bd_addr_t));
                s_cid_control = local_cid;
                s_bt_state = BT_STATE_CONNECTING_INTR;
                l2cap_accept_connection(local_cid);
            } else if (psm == PSM_HID_INTERRUPT) {
                s_cid_interrupt = local_cid;
                l2cap_accept_connection(local_cid);
            } else {
                l2cap_decline_connection(local_cid);
            }
            break;
        }

        case L2CAP_EVENT_CHANNEL_OPENED: {
            uint8_t status = l2cap_event_channel_opened_get_status(packet);
            uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
            uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);
            l2cap_event_channel_opened_get_address(packet, event_addr);

            if (status != 0) {
                if (status == 0x05U || status == 0x06U || status == 0x16U) {
                    s_pin_use_host_addr = !s_pin_use_host_addr;
                }
                handle_disconnect_cleanup();
                break;
            }

            if (psm == PSM_HID_CONTROL) {
                s_cid_control = local_cid;
                if (s_bt_state == BT_STATE_CONNECTING_CTRL) {
                    s_bt_state = BT_STATE_CONNECTING_INTR;
                    l2cap_create_channel(packet_handler, event_addr,
                                         PSM_HID_INTERRUPT, 64U, &s_cid_interrupt);
                }
            } else if (psm == PSM_HID_INTERRUPT) {
                s_cid_interrupt = local_cid;
                memcpy(s_target_addr, event_addr, sizeof(bd_addr_t));
                flash_storage_save_paired_addr(event_addr);
                ir_leds_set_enabled(true);
                s_bt_state = BT_STATE_INIT_WIIMOTE;
                s_init_step = INIT_STEP_LED1;
                s_init_tx_inflight = false;
                l2cap_request_can_send_now_event(s_cid_interrupt);
            }
            break;
        }

        case L2CAP_EVENT_CAN_SEND_NOW: {
            uint16_t cid = l2cap_event_can_send_now_get_local_cid(packet);
            if (cid != s_cid_interrupt) {
                break;
            }
            if (s_bt_state == BT_STATE_INIT_WIIMOTE && s_init_step < INIT_STEP_DONE) {
                send_next_init_step();
            } else if (s_bt_state == BT_STATE_CONNECTED && s_led_update_pending) {
                uint8_t pkt[3] = {
                    0xA2U,
                    0x11U,
                    (uint8_t)((s_pending_led_mask & 0xF0U) | (s_pending_rumble ? 0x01U : 0x00U))
                };
                s_led_update_pending = false;
                l2cap_send(s_cid_interrupt, pkt, sizeof(pkt));
            }
            break;
        }

        case L2CAP_EVENT_CHANNEL_CLOSED: {
            uint16_t local_cid = l2cap_event_channel_closed_get_local_cid(packet);
            if (local_cid == s_cid_interrupt || local_cid == s_cid_control) {
                handle_disconnect_cleanup();
            }
            break;
        }

        default:
            break;
    }
}

void wiimote_bt_init(void) {
    l2cap_init();
    l2cap_register_service(packet_handler, PSM_HID_CONTROL, 64U, LEVEL_0);
    l2cap_register_service(packet_handler, PSM_HID_INTERRUPT, 64U, LEVEL_0);

    gap_set_local_name("Pico-Wiimote-Mouse");
    gap_connectable_control(1);
    gap_discoverable_control(0);
    gap_ssp_set_enable(0);

    s_hci_event_callback_reg.callback = &packet_handler;
    hci_add_event_handler(&s_hci_event_callback_reg);

    hci_power_control(HCI_POWER_ON);
}

void wiimote_bt_task(void) {
    cyw43_arch_poll();

    if (s_bt_state == BT_STATE_INIT_WIIMOTE &&
        s_init_step < INIT_STEP_DONE &&
        s_cid_interrupt != 0) {
        if (!s_init_tx_inflight ||
            (now_ms() - s_last_init_tx_ms) >= WIIMOTE_INIT_STEP_DELAY_MS) {
            s_init_tx_inflight = true;
            s_last_init_tx_ms = now_ms();
            l2cap_request_can_send_now_event(s_cid_interrupt);
        }
    }
}

bool wiimote_bt_is_connected(void) {
    return (s_bt_state == BT_STATE_CONNECTED || s_bt_state == BT_STATE_INIT_WIIMOTE);
}

void wiimote_bt_set_leds(uint8_t led_mask, bool rumble) {
    s_pending_led_mask = led_mask;
    s_pending_rumble = rumble;
    s_led_update_pending = true;
    if (s_bt_state == BT_STATE_CONNECTED && s_cid_interrupt != 0) {
        l2cap_request_can_send_now_event(s_cid_interrupt);
    }
}

#else /* Host stub for unit testing */

static bool s_host_bt_connected = false;

void wiimote_bt_init(void) {
    s_host_bt_connected = false;
}

void wiimote_bt_task(void) {
}

bool wiimote_bt_is_connected(void) {
    return s_host_bt_connected;
}

void wiimote_bt_set_leds(uint8_t led_mask, bool rumble) {
    (void)led_mask;
    (void)rumble;
}

#endif /* PICO_ON_DEVICE */
