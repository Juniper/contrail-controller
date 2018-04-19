/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef gmpx_environment_h
#define gmpx_environment_h

#include "bvx_environment.h"

/* Pick up the patricia stuff directly. */

#define gmpx_patnode patnode
#define gmpx_patroot patroot
#define GMPX_PATNODE_TO_STRUCT PATNODE_TO_STRUCT
#define gmpx_patricia_lookup_least patricia_lookup_least
#define gmpx_patricia_lookup_geq patricia_lookup_geq
#define gmpx_patroot_init(keylen, offset) \
    patricia_root_init(NULL, FALSE, (keylen), (offset))
#define GMPX_PATRICIA_OFFSET STRUCT_OFFSET
#define gmpx_patricia_add patricia_add
#define gmpx_patroot_destroy patricia_root_delete
#define gmpx_patricia_get_next patricia_get_next
#define gmpx_patricia_lookup patricia_lookup
#define gmpx_patricia_delete patricia_delete

/* Interface index. */

typedef struct gmp_intf_handle_ *gmpx_intf_id;
#define GMPX_MANY_INTFS 50        /* Intf count before changing
                     * general query strategy
                     */

/* Memory stuff. */

#define gmpx_block_tag block_t
#define gmpx_malloc_block task_block_alloc
#define gmpx_free_block task_block_free
#define gmpx_malloc_block_create task_block_init

/* Checksum */

#define gmpx_calculate_cksum inet_cksum

/* Assert. */

#define gmpx_assert assert

#define GMPX_UNUSED UNUSED

#define GMPX_MAX_RTR_CLIENTS 2        /* Should be enough */

/*
 * Packet attribute.  For now it's just a boolean, with TRUE meaning that
 * the packet represents statically defined groups.
 */
typedef boolean gmpx_packet_attr;    /* Packet attribute */

/* Tracing */

extern void gmpx_trace(void *context, const char *parms, ...);

typedef enum {
    GMP_VERSION_MISMATCH = 0,
    GMP_GROUP_LIMIT_EXCEED,
    GMP_GROUP_THRESHOLD_EXCEED,
    GMP_GROUP_LIMIT_BELOW
}gmpx_event_type;

/* Event */
extern void gmpx_post_event(void *context, gmpx_event_type ev,
                const void *parms, ...);

/*
 * Timer entry
 *
 * This consists of a timer embedded in an outer structure so that we can
 * adapt rpd's timers to GMP's expectations.
 */
typedef struct gmpx_timer_ gmpx_timer;
typedef void (*gmpx_timer_callback)(gmpx_timer *timer, void *context);

struct gmpx_timer_ {
    task_timer *gmpxt_timer;        /* rpd's timer */
    void *gmpxt_context;        /* timer context */
    gmpx_timer_callback gmpxt_callback;    /* Callback address */
    gmp_timer_group gmpxt_group;    /* Timer group */
    gmp_proto gmpxt_proto;        /* Associated protocol */
};


/* gmpx_environment.c */

extern void gmpx_start_timer(gmpx_timer *timer, uint32_t ivl,
                 uint32_t jitter_pct);
extern gmpx_timer *gmpx_create_timer(void *inst_context, const char *name,
                     gmpx_timer_callback callback,
                     void *timer_context);
extern gmpx_timer *gmpx_create_grouped_timer(gmp_timer_group group,
                         void *inst_context,
                         const char *name,
                         gmpx_timer_callback callback,
                         void *timer_context);
extern void gmpx_destroy_timer(gmpx_timer *timer);
extern uint32_t gmpx_timer_time_remaining(gmpx_timer *timer);
extern boolean gmpx_timer_running(gmpx_timer *timer);
extern void gmpx_stop_timer(gmpx_timer *timer);
extern void gmpx_smear_timer_group(gmp_proto proto, gmp_timer_group group);

#endif /* gmpx_environment_h */
