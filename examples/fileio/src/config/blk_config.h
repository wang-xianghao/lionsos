/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sddf/util/string.h>
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>

#define BLK_NUM_CLIENTS 1

#define BLK_NAME_CLI0                      "fat"

#define BLK_QUEUE_CAPACITY_CLI_FAT              16
#define BLK_QUEUE_CAPACITY_CLI0                 BLK_QUEUE_CAPACITY_CLI_FAT
#define BLK_QUEUE_CAPACITY_DRIV                 1024

#define BLK_REGION_SIZE                     0x200000
#define BLK_CONFIG_REGION_SIZE_CLI0         BLK_REGION_SIZE

#define BLK_DATA_REGION_SIZE_CLI0           BLK_REGION_SIZE
#define BLK_DATA_REGION_SIZE_DRIV           BLK_REGION_SIZE

#define BLK_QUEUE_REGION_SIZE_CLI0          BLK_REGION_SIZE
#define BLK_QUEUE_REGION_SIZE_DRIV          BLK_REGION_SIZE

_Static_assert(BLK_DATA_REGION_SIZE_CLI0 >= BLK_TRANSFER_SIZE && BLK_DATA_REGION_SIZE_CLI0 % BLK_TRANSFER_SIZE == 0,
               "Client0 data region size must be a multiple of the transfer size");
_Static_assert(BLK_DATA_REGION_SIZE_DRIV >= BLK_TRANSFER_SIZE && BLK_DATA_REGION_SIZE_DRIV % BLK_TRANSFER_SIZE == 0,
               "Driver data region size must be a multiple of the transfer size");

/* Mapping from client index to disk partition that the client will have access to. */
static const int blk_partition_mapping[BLK_NUM_CLIENTS] = { 0 };

static inline blk_storage_info_t *blk_virt_cli_storage_info(blk_storage_info_t *info, unsigned int id)
{
    switch (id) {
    case 0:
        return info;
    default:
        return NULL;
    }
}

static inline uintptr_t blk_virt_cli_data_region(uintptr_t data, unsigned int id)
{
    switch (id) {
    case 0:
        return data;
    default:
        return 0;
    }
}

static inline uint64_t blk_virt_cli_data_region_size(unsigned int id)
{
    switch (id) {
    case 0:
        return BLK_DATA_REGION_SIZE_CLI0;
    default:
        return 0;
    }
}

static inline blk_req_queue_t *blk_virt_cli_req_queue(blk_req_queue_t *req, unsigned int id)
{
    switch (id) {
    case 0:
        return req;
    default:
        return NULL;
    }
}

static inline blk_resp_queue_t *blk_virt_cli_resp_queue(blk_resp_queue_t *resp, unsigned int id)
{
    switch (id) {
    case 0:
        return resp;
    default:
        return NULL;
    }
}

static inline uint32_t blk_virt_cli_queue_capacity(unsigned int id)
{
    switch (id) {
    case 0:
        return BLK_QUEUE_CAPACITY_CLI0;
    default:
        return 0;
    }
}

static inline uint32_t blk_cli_queue_capacity(char *pd_name)
{
    if (!sddf_strcmp(pd_name, BLK_NAME_CLI0)) {
        return BLK_QUEUE_CAPACITY_CLI0;
    } else {
        return 0;
    }
}
