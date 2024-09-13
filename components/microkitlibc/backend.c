#include <microkit.h>

#include <sddf/serial/queue.h>
#include <serial_config.h>
#include <sddf/util/printf.h>

extern int app_main(void);

/*
 * Shared regions for Serial communication
 */
char *serial_rx_data;
char *serial_tx_data;
serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;
serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

void init(void) {
    microkit_dbg_puts("Microkit C Library initializes...");

    serial_cli_queue_init_sys(microkit_name, &serial_rx_queue_handle,
                            serial_rx_queue, serial_rx_data,
                            &serial_tx_queue_handle, serial_tx_queue,
                            serial_tx_data);

    int rc = app_main();

    sddf_dprintf("Return code %d\n", rc);
}

void notified(microkit_channel ch) {

    sddf_dprintf("Channel %d\n", ch);
}