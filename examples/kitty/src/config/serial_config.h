/*
 * Configuration for serial subsystems in Kitty system
 *
 *  Copyright 2024 UNSW
 *  SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef kitty_serial_config_h
#define kitty_serial_config_h
#pragma once
/* Number of clients that can be connected to the serial server. */
#define SERIAL_NUM_CLIENTS 3

/* Support full duplex. */
#define SERIAL_TX_ONLY 0

/* Default baud rate of the uart device */
#define UART_DEFAULT_BAUD 115200

/* One read/write client */
#define SERIAL_CLI0_NAME "micropython"
#define SERIAL_VIRT_RX_NAME "serial_virt_rx"
#define SERIAL_VIRT_TX_NAME "serial_virt_tx"
#define SERIAL_QUEUE_SIZE                          0x1000
#define SERIAL_DATA_REGION_SIZE                    0x2000

#define SERIAL_TX_DATA_REGION_SIZE_DRIV            SERIAL_DATA_REGION_SIZE
#define SERIAL_TX_DATA_REGION_SIZE_CLI0            SERIAL_DATA_REGION_SIZE
#define SERIAL_RX_DATA_REGION_SIZE_DRIV            SERIAL_DATA_REGION_SIZE
#define SERIAL_RX_DATA_REGION_SIZE_CLI0            SERIAL_DATA_REGION_SIZE
#define SERIAL_MAX_TX_DATA_SIZE MAX(SERIAL_TX_DATA_REGION_SIZE_DRIV, \
                                    SERIAL_TX_DATA_REGION_SIZE_CLI0)
#define SERIAL_MAX_RX_DATA_SIZE MAX(SERIAL_RX_DATA_REGION_SIZE_DRIV, \
                                    SERIAL_RX_DATA_REGION_SIZE_CLI0)
#define SERIAL_MAX_DATA_SIZE MAX(SERIAL_MAX_TX_DATA_SIZE, \
                                 SERIAL_MAX_RX_DATA_SIZE)

/* String to be printed to start console input */
#define SERIAL_CONSOLE_BEGIN_STRING "Begin input\n"
#define SERIAL_CONSOLE_BEGIN_STRING_LEN 13

_Static_assert(SERIAL_MAX_DATA_SIZE < UINT32_MAX,
               "Data regions must be smaller than UINT32"
               " max to use queue data structure correctly.");



static inline void serial_cli_queue_init_sys(const char *pd_name,
                                             serial_queue_handle_t *rx_queue_handle,
                                             serial_queue_t *rx_queue,
                                             char *rx_data,
                                             serial_queue_handle_t *tx_queue_handle,
                                             serial_queue_t *tx_queue,
                                             char *tx_data)
{
    serial_queue_init(rx_queue_handle, rx_queue,
                      SERIAL_DATA_REGION_SIZE, rx_data);
    serial_queue_init(tx_queue_handle, tx_queue,
                      SERIAL_DATA_REGION_SIZE, tx_data);
}

static inline void serial_virt_queue_init_sys(char *pd_name,
                                              serial_queue_handle_t *cli_queue_handle,
                                              uintptr_t cli_queue,
                                              uintptr_t cli_data)
{
        serial_queue_init(cli_queue_handle, (serial_queue_t *)cli_queue,
                          SERIAL_DATA_REGION_SIZE, (char *)cli_data);
        serial_queue_init(&cli_queue_handle[1],
                          (serial_queue_t *)(cli_queue + SERIAL_QUEUE_SIZE),
                          SERIAL_DATA_REGION_SIZE,
                          (char *)(cli_data + SERIAL_DATA_REGION_SIZE));

}
/*
 * UNUSED but needed for compilation
 */
#define SERIAL_SWITCH_CHAR '\0'
#define SERIAL_TERMINATE_NUM (4) /* control-D */

#endif /* kitty_serial_config_h */