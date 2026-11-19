#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

/* BTstack Classic (BR/EDR) configuration for Raspberry Pi Pico 2 W / Pico W */
#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif

#ifndef ENABLE_LOG_INFO
#define ENABLE_LOG_INFO
#endif

#ifndef ENABLE_LOG_ERROR
#define ENABLE_LOG_ERROR
#endif

#ifndef ENABLE_PRINTF_HEXDUMP
#define ENABLE_PRINTF_HEXDUMP
#endif

#ifndef HAVE_EMBEDDED_TIME_MS
#define HAVE_EMBEDDED_TIME_MS
#endif

#ifndef HCI_RESET_RESEND_TIMEOUT_MS
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
#endif

/* HCI & L2CAP buffer sizes */
#define HCI_OUTGOING_PRE_BUFFER_SIZE        4
#define HCI_ACL_PAYLOAD_SIZE                (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT        4

#define MAX_NR_HCI_CONNECTIONS              2
#define MAX_NR_L2CAP_SERVICES               4
#define MAX_NR_L2CAP_CHANNELS               4
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
#define NVM_NUM_LINK_KEYS                   4
#define MAX_NR_WHITELIST_ENTRIES            4
#define MAX_NR_SM_LOOKUP_ENTRIES            3
#define MAX_NR_SERVICE_RECORD_ITEMS         4

/* Unused Classic/LE profiles disabled to conserve SRAM */
#define MAX_NR_RFCOMM_MULTIPLEXERS          0
#define MAX_NR_RFCOMM_SERVICES              0
#define MAX_NR_RFCOMM_CHANNELS              0
#define MAX_NR_HFP_CONNECTIONS              0
#define MAX_NR_AVDTP_STREAM_ENDPOINTS       0
#define MAX_NR_AVDTP_CONNECTIONS            0
#define MAX_NR_AVRCP_CONNECTIONS            0
#define MAX_NR_BNEP_SERVICES                0
#define MAX_NR_BNEP_CHANNELS                0
#define MAX_NR_GATT_CLIENTS                 0
#define MAX_NR_LE_DEVICE_DB_ENTRIES         0

#endif /* BTSTACK_CONFIG_H */
