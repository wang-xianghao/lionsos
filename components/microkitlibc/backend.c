#include <microkit.h>

#include <sddf/serial/queue.h>
#include <serial_config.h>
#include <sddf/util/printf.h>
#include <libmicrokitco.h>

#define MICROPY_STACK_SIZE (0x100000)
static char mp_stack[MICROPY_STACK_SIZE];
static co_control_t co_controller_mem;

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

void t_mc_entrypoint(void) {
    int rc = app_main();
    
    sddf_dprintf("Return code %d\n", rc);
}

void init(void) {
    microkit_dbg_puts("Microkit C Library initializes...");

    serial_cli_queue_init_sys(microkit_name, &serial_rx_queue_handle,
                            serial_rx_queue, serial_rx_data,
                            &serial_tx_queue_handle, serial_tx_queue,
                            serial_tx_data);
                            
    microkit_cothread_init(&co_controller_mem, MICROPY_STACK_SIZE, mp_stack);

    if (microkit_cothread_spawn(t_mc_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        // printf("MP|ERROR: Cannot initialise Microkitlibc cothread\n");
        sddf_dprintf("MP|ERROR: Cannot initialise Microkitlibc cothread\n");
        
        while (true) {}
    }

    // Run the Microkitlibc cothread
    microkit_cothread_yield();
}

void notified(microkit_channel ch) {

    sddf_dprintf("Channel %d\n", ch);
}
