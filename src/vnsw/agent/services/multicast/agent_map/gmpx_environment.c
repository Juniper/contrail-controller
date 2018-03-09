/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmp_intf.h"
#include "gmpx_environment.h"
#include "gmp_map.h"

static inline gmp_proto
mc_af_to_gmp_proto (const mc_af mcaf)
{
    switch (mcaf) { 
        case MCAST_AF_IPV4:
            return GMP_PROTO_IGMP;
        case MCAST_AF_IPV6:
            return GMP_PROTO_MLD;
        default:
            assert(FALSE);
    }
    return GMP_PROTO_IGMP;  /* Statement added to avoid compiler warnings. */
}

/* Statics... */

static boolean initialized = FALSE;
static block_t gmpx_timer_block;
static task_timer_root *gmpx_timer_roots[GMP_NUM_TIMER_GROUPS][GMP_NUM_PROTOS];


/*
 * gmpx_smear_timer_group
 *
 * Smear the timers for a protocol and group.
 */
void
gmpx_smear_timer_group (gmp_proto proto, gmp_timer_group group)
{
    /* Smear the timers. */

    task_timer_smear_auto_parent_timers(gmpx_timer_roots[group][proto]);
}


/*
 * gmpx_destroy_timer
 *
 * Get rid of a timer.
 */
void
gmpx_destroy_timer (gmpx_timer *timer)
{
    if (!timer)
	return;

    /* Free the rpd timer. */

    task_timer_delete(timer->gmpxt_timer);

    /* Free the block. */

    task_block_free(gmpx_timer_block, timer);
}


/*
 * gmpx_start_timer
 *
 * Start a timer.
 */
void
gmpx_start_timer (gmpx_timer *timer, uint32_t ivl, uint32_t jitter_pct)
{
    utime_t long_ivl;

    /* Convert the timer interval to a utime_t. */

    long_ivl.ut_sec = ivl / MSECS_PER_SEC;
    long_ivl.ut_usec = (ivl % MSECS_PER_SEC) * USECS_PER_MSEC;

    /* Start the timer. */

    task_timer_uset_alt_root_auto_parent_oneshot(
		 gmpx_timer_roots[timer->gmpxt_group][timer->gmpxt_proto],
		 timer->gmpxt_timer, &long_ivl, jitter_pct);
}


/*
 * gmpx_timer_expiry
 *
 * Handle a timer expiry.
 */
static void
gmpx_timer_expiry (task_timer *rpd_timer, time_t tm UNUSED)
{
    gmpx_timer *timer;

    /* Stop the timer and then call the caller's callback. */

    task_timer_reset(rpd_timer);
    timer = task_timer_data(rpd_timer);

    if (timer->gmpxt_callback)
	(*timer->gmpxt_callback)(timer, timer->gmpxt_context);
}


/*
 * gmpx_create_grouped_timer
 *
 * Create a timer as part of a group.
 *
 * Returns a pointer to a timer, or NULL if out of memory.
 */
gmpx_timer *
gmpx_create_grouped_timer (gmp_timer_group group, void *inst_context,
			   const char *name, gmpx_timer_callback callback,
			   void *timer_context)
{
    gmpx_timer *timer;
    mc_af af;
    mgm_global_data *gd;
    gmp_timer_group group_num;
    gmp_proto proto;

    gd = inst_context;
    af = gd->mgm_gd_af;

    /* If we haven't initialized, do so now. */

    if (!initialized) {
	gmpx_timer_block = task_block_init(sizeof(gmpx_timer), "GMP timer");
	for (proto = 0; proto < GMP_NUM_PROTOS; proto++) {
	    for (group_num = 0; group_num < GMP_NUM_TIMER_GROUPS;
		 group_num++) {
		gmpx_timer_roots[group_num][proto] =
		    task_timer_get_auto_parent_root();
	    }
	}
	initialized = TRUE;
    }

    /* Allocate the timer block. */

    timer = task_block_alloc(gmpx_timer_block);
    if (timer) {

	/* Got one.  Initialize the block. */

	timer->gmpxt_timer = 
	    task_timer_create_idle_leaf(gd->tp, name, 0, NULL,
					gmpx_timer_expiry, timer);
	timer->gmpxt_context = timer_context;
	timer->gmpxt_callback = callback;
	timer->gmpxt_group = group;
	timer->gmpxt_proto = mc_af_to_gmp_proto(af);
    }

    return timer;
}

    
/*
 * gmpx_create_timer
 *
 * Create a timer.
 *
 * Returns a pointer to a timer, or NULL if out of memory.
 */
gmpx_timer *
gmpx_create_timer (void *inst_context, const char *name,
		   gmpx_timer_callback callback, void *timer_context)
{
    return gmpx_create_grouped_timer(GMP_TIMER_GROUP_DEFAULT, inst_context,
				     name, callback, timer_context);
}


/*
 * gmpx_timer_time_remaining
 *
 * Returns the time remaining before expiration of a timer, in millseconds.
 */
uint32_t
gmpx_timer_time_remaining (gmpx_timer *timer)
{
    utime_t time_left_usec;
    uint32_t time_left_msec;

    if (!timer)
	return 0;

    /* Get the remaining time. */

    task_timer_utime_left(timer->gmpxt_timer, &time_left_usec);
    time_left_msec = ((time_left_usec.ut_sec * MSECS_PER_SEC) +
		      (time_left_usec.ut_usec / USECS_PER_MSEC));

    return time_left_msec;
}


/*
 * gmpx_timer_running
 *
 * Returns TRUE if the timer is running, or FALSE if it is not.
 */
boolean
gmpx_timer_running (gmpx_timer *timer)
{
    if (!timer)
	return FALSE;

    return task_timer_running(timer->gmpxt_timer);
}


/*
 * gmpx_stop_timer
 *
 * Stop a timer.
 */
void
gmpx_stop_timer (gmpx_timer *timer)
{
    if (!timer)
	return;

    task_timer_reset(timer->gmpxt_timer);
}

