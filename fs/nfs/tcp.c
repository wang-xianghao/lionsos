/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <string.h>

#include <sddf/timer/client.h>
#include <sddf/network/shared_ringbuffer.h>
#include <sddf/network/constants.h>
#include <ethernet_config.h>

#include <lwip/dhcp.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/snmp.h>
#include <lwip/stats.h>
#include <lwip/sys.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <netif/etharp.h>

#include "util.h"
#include "tcp.h"

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
} state_t;

typedef struct pbuf_custom_offset {
    struct pbuf_custom custom;
    uintptr_t offset;
} pbuf_custom_offset_t;

typedef struct {
    struct tcp_pcb *sock_tpcb;
    int port;
    int connected;
    int used;

    char rx_buf[SOCKET_BUF_SIZE];
    int rx_head;
    int rx_len;
} socket_t;

state_t state;

LWIP_MEMPOOL_DECLARE(
    RX_POOL,
    RX_RING_SIZE_CLI0 * 2,
    sizeof(pbuf_custom_offset_t),
    "Zero-copy RX pool");

uintptr_t rx_free;
uintptr_t rx_used;
uintptr_t tx_free;
uintptr_t tx_used;
uintptr_t rx_buffer_data_region;
uintptr_t tx_buffer_data_region;

// Should only need 1 at any one time, accounts for any reconnecting that might happen
socket_t sockets[MAX_SOCKETS] = {0};

static bool network_ready;
static bool notify_tx;
static bool notify_rx;

int tcp_ready(void) {
    return network_ready;
}

void tcp_maybe_notify(void) {
    if (notify_rx && require_signal(state.rx_ring.free_ring)) {
        cancel_signal(state.rx_ring.free_ring);
        notify_rx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETHERNET_RX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_RX_CHANNEL) {
            microkit_notify(ETHERNET_RX_CHANNEL);
        }
    }

    if (notify_tx && require_signal(state.tx_ring.used_ring)) {
        cancel_signal(state.tx_ring.used_ring);
        notify_tx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETHERNET_TX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_TX_CHANNEL) {
            microkit_notify(ETHERNET_TX_CHANNEL);
        }
    }
}

uint32_t sys_now(void) {
    return sddf_timer_time_now(TIMER_CHANNEL) / NS_IN_MS;
}

static void netif_status_callback(struct netif *netif) {
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));

        microkit_mr_set(0, ip4_addr_get_u32(netif_ip4_addr(netif)));
        microkit_mr_set(1, (state.mac[0] << 8) | state.mac[1]);
        microkit_mr_set(2, (state.mac[2] << 24) |  (state.mac[3] << 16) | (state.mac[4] << 8) | state.mac[5]);
        microkit_ppcall(ETHERNET_ARP_CHANNEL, microkit_msginfo_new(0, 3));

        network_ready = true;
    }
}

static err_t lwip_eth_send(struct netif *netif, struct pbuf *p) {
    err_t ret = ERR_OK;

    if (p->tot_len > BUFF_SIZE) {
        return ERR_MEM;
    }

    buff_desc_t buffer;
    int err = dequeue_free(&(state.tx_ring), &buffer);
    if (err) {
        return ERR_MEM;
    }
    
    unsigned char *frame = (unsigned char *)(buffer.phys_or_offset + tx_buffer_data_region);
    unsigned int copied = 0;
    for (struct pbuf *curr = p; curr != NULL; curr = curr->next) {
        memcpy(frame + copied, curr->payload, curr->len);
        copied += curr->len;
    }

    buffer.len = copied;
    err = enqueue_used(&state.tx_ring, buffer);
    notify_tx = true;

    return ret;
}

/**
 * Free a pbuf. This also returns the underlying buffer to
 * the appropriate place.
 *
 * @param buf pbuf to free.
 *
 */
static void interface_free_buffer(struct pbuf *buf) {
    SYS_ARCH_DECL_PROTECT(old_level);
    pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    buff_desc_t buffer = {custom_pbuf_offset->offset, 0};
    int err = enqueue_free(&(state.rx_ring), buffer);
    assert(!err);
    notify_rx = true;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf_offset);
    SYS_ARCH_UNPROTECT(old_level);
}

/**
 * Initialise the network interface data structure.
 *
 * @param netif network interface data structuer.
 */
static err_t ethernet_init(struct netif *netif)
{
    if (netif->state == NULL) return ERR_ARG;
    state_t *data = netif->state;

    netif->hwaddr[0] = data->mac[0];
    netif->hwaddr[1] = data->mac[1];
    netif->hwaddr[2] = data->mac[2];
    netif->hwaddr[3] = data->mac[3];
    netif->hwaddr[4] = data->mac[4];
    netif->hwaddr[5] = data->mac[5];
    netif->mtu = ETHER_MTU;
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->output = etharp_output;
    netif->linkoutput = lwip_eth_send;
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;
    return ERR_OK;
}

void tcp_process_rx(void) {
    bool reprocess = true;
    while (reprocess) {
        while (!ring_empty(state.rx_ring.used_ring)) {
            buff_desc_t buffer;
            dequeue_used(&state.rx_ring, &buffer);

            pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
            custom_pbuf_offset->offset = buffer.phys_or_offset;
            custom_pbuf_offset->custom.custom_free_function = interface_free_buffer;

            struct pbuf *p = pbuf_alloced_custom(
                PBUF_RAW,
                buffer.len,
                PBUF_REF,
                &custom_pbuf_offset->custom,
                (void *)(buffer.phys_or_offset + rx_buffer_data_region),
                BUFF_SIZE
            );

            if (state.netif.input(p, &state.netif) != ERR_OK) {
                dlog("netif.input() != ERR_OK");
                pbuf_free(p);
            }
        }

        request_signal(state.rx_ring.used_ring);
        reprocess = false;

        if (!ring_empty(state.rx_ring.used_ring)) {
            cancel_signal(state.rx_ring.used_ring);
            reprocess = true;
        }
    }
}

void tcp_update(void)
{
    sys_check_timeouts();
}

void tcp_init_0(void)
{
    cli_ring_init_sys(microkit_name, &state.rx_ring, rx_free, rx_used, &state.tx_ring, tx_free, tx_used);
    buffers_init((ring_buffer_t *)tx_free, 0, state.tx_ring.free_ring->size);

    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);

    cli_mac_addr_init_sys(microkit_name, state.mac);

    /* Set some dummy IP configuration values to get lwIP bootstrapped  */
    struct ip4_addr netmask, ipaddr, gw, multicast;
    ipaddr_aton("0.0.0.0", &gw);
    ipaddr_aton("0.0.0.0", &ipaddr);
    ipaddr_aton("0.0.0.0", &multicast);
    ipaddr_aton("255.255.255.0", &netmask);

    state.netif.name[0] = 'e';
    state.netif.name[1] = '0';

    if (!netif_add(&(state.netif), &ipaddr, &netmask, &gw, &state,
                   ethernet_init, ethernet_input)) {
        dlog("Netif add returned NULL");
    }
    netif_set_default(&(state.netif));
    netif_set_status_callback(&(state.netif), netif_status_callback);
    netif_set_up(&(state.netif));

    int err = dhcp_start(&(state.netif));
    dlogp(err, "failed to start DHCP negotiation");

    if (notify_rx && require_signal(state.rx_ring.free_ring)) {
        cancel_signal(state.rx_ring.free_ring);
        notify_rx = false;
        if (!have_signal) microkit_notify_delayed(ETHERNET_RX_CHANNEL);
        else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_RX_CHANNEL) microkit_notify(ETHERNET_RX_CHANNEL);
    }

    if (notify_tx && require_signal(state.tx_ring.used_ring)) {
        cancel_signal(state.tx_ring.used_ring);
        notify_tx = false;
        if (!have_signal) microkit_notify_delayed(ETHERNET_TX_CHANNEL);
        else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_TX_CHANNEL) microkit_notify(ETHERNET_TX_CHANNEL);
    }
}

void socket_err_func(void *arg, err_t err)
{
    dlog("error %d with socket", err);
}

static err_t socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    dlogp(err, "error %d", err);

    if (p == NULL) {
        dlog("closing connection...");
        tcp_close(tpcb);
        return ERR_OK;
    }

    socket_t *socket = arg;

    int copied = 0, remaining = p->tot_len;
    while (remaining != 0) {
        int rx_tail = (socket->rx_head + socket->rx_len) % SOCKET_BUF_SIZE;
        int to_copy = MIN(remaining, SOCKET_BUF_SIZE - MAX(socket->rx_len, rx_tail));
        pbuf_copy_partial(p, socket->rx_buf + rx_tail, to_copy, copied);
        socket->rx_len += to_copy;
        copied += to_copy;
        remaining -= to_copy;
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

// Connected function
err_t socket_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    ((socket_t *)arg)->connected = 1;
    tcp_sent(tpcb, socket_sent_callback);
    tcp_recv(tpcb, socket_recv_callback);
    return ERR_OK;
}

int tcp_socket_create(void) {
    int free_index = 0;

    socket_t *socket = NULL;
    for (free_index = 0; free_index < MAX_SOCKETS; free_index++) {
        if (!sockets[free_index].used) {
            socket = &sockets[free_index];
            break;
        }
    }
    if (socket == NULL) {
        dlog("no free sockets");
        return -1;
    }

    socket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (socket->sock_tpcb == NULL) {
        dlog("couldn't create socket");
        return -1;
    }

    socket->used = 1;
    tcp_err(socket->sock_tpcb, socket_err_func);
    tcp_arg(socket->sock_tpcb, socket);

    for (int i = 512; ; i++) {
        if (tcp_bind(socket->sock_tpcb, IP_ADDR_ANY, i) == ERR_OK) {
            return free_index;
        }
    }

    socket->used = 0;
    return -1;
}

int tcp_socket_connect(int index, int port) {
    socket_t *sock = &sockets[index];
    sock->port = port;

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr(NFS_SERVER));

    err_t err = tcp_connect(sock->sock_tpcb, &ipaddr, port, socket_connected);
    if (err != ERR_OK) {
        dlog("error connecting (%d)", err);
        return 1;
    }

    return 0;
}

int tcp_socket_close(int index)
{
    socket_t *sock = &sockets[index];

    if (sock->used) {
        int err = tcp_close(sock->sock_tpcb);
        if (err != ERR_OK) {
            dlog("error closing socket (%d)", err);
            return -1;
        }
    }

    sock->sock_tpcb = NULL;
    sock->port = 0;
    sock->connected = 0;
    sock->used = 0;
    sock->rx_head = 0;
    sock->rx_len = 0;
    return 0;
}

int tcp_socket_dup(int index_old, int index_new)
{
    socket_t *sock_old = &sockets[index_old];

    if (!(0 <= index_new && index_new < MAX_SOCKETS)) {
        return -1;
    }
    socket_t *sock_new = &sockets[index_new];

    tcp_close(sock_new->sock_tpcb);

    if (sock_old->used) {
        sock_new->sock_tpcb = sock_old->sock_tpcb;
        tcp_arg(sock_new->sock_tpcb, sock_new);
        sock_new->used = 1;
        sock_new->port = sock_old->port;
        return index_new;
    }
    return -1;
}

int tcp_socket_write(int index, const char *buf, int len) {
    socket_t *sock = &sockets[index];
    int to_write = MIN(len, tcp_sndbuf(sock->sock_tpcb));
    int err = tcp_write(sock->sock_tpcb, (void *)buf, to_write, 1);
    if (err != ERR_OK) {
        dlog("tcp_write failed (%d)", err);
        return -1;
    }
    err = tcp_output(sock->sock_tpcb);
    if (err != ERR_OK) {
        dlog("tcp_output failed (%d)", err);
        return -1;
    }
    return to_write;
}

int tcp_socket_recv(int index, char *buf, int len) {
    socket_t *sock = &sockets[index];
    int remaining = len;
    while (remaining != 0) {
        int to_copy = MIN(len, MIN(sock->rx_len, SOCKET_BUF_SIZE - sock->rx_head));
        if (to_copy == 0) {
            return len - remaining;
        }
        memcpy(buf, sock->rx_buf + sock->rx_head, to_copy);
        sock->rx_head = (sock->rx_head + to_copy) % SOCKET_BUF_SIZE;
        sock->rx_len -= to_copy;
        remaining -= to_copy;
    }
    return len;
}

int tcp_socket_readable(int index) {
    socket_t *socket = &sockets[index];
    return socket->rx_len;
}

int tcp_socket_writable(int index) {
    return !ring_empty(state.tx_ring.free_ring);
}
