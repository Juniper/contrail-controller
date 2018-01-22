/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef gmpx_basic_types_h
#define gmpx_basic_types_h

#include <stdint.h>
#include <assert.h>

#include "task_block_api.h"
#include "task_thread_api.h"
#include "task_timer_api.h"
#include "patricia_api.h"

#define STRUCT_OFFSET(structure, pat_node, member)                      \
    (offsetof(structure, member) - offsetof(structure, pat_node))

#define MEMBER_TO_STRUCT(function, structure, type, member)             \
    static inline structure *(function)(type *address) {                \
        if (address) {                                                  \
            return (structure *)((char *)address - offsetof(structure, member));    \
        }                                                               \
        return NULL;                                                    \
    }

#ifndef UNUSED
#define UNUSED
#endif

static inline u_short
get_short (const void *ptr)
{
    const u_char *cp = (const u_char *)ptr;
    u_short sh;

    sh = *cp++;
    sh = (sh << 8) | *cp;
    return(sh);
}

static inline void *
put_short (void *ptr, u_short value)
{
    u_char *cp = (u_char *)ptr;

    *cp++ = value >> 8;
    *cp++ = value & 0xff;
    return(cp);
}

static inline u_int16_t inet_cksum(void *packet, size_t length)
{
    u_int32_t sum = 0;
    u_int16_t *ptr = (u_int16_t *)packet;

    while (length > 1) {
        sum += *ptr++;
        length -= 2;
        if (sum & 0x80000000)
            sum = (sum & 0xFFFF) + (sum >> 16);
    }

    if (length > 0)
        sum += *(u_int8_t *)ptr;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return ~sum;
}

#endif /* gmpx_basic_types_h */
