/* $Id: gmpr_group.c 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmpr_group.c - IGMP/MLD Router-Side group handling
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmpr_trace.h"


static void gmpr_log_event(gmpr_intf *intf , gmpr_limit_state_t state)
{
    if(!intf) {
	return;
    }

    boolean logged = FALSE; 
    switch(state) {
	case GMPR_ABOVE_LIMIT: 
            gmpr_post_event(intf->rintf_instance, GMP_GROUP_LIMIT_EXCEED ,
			    intf->rintf_id,
			    intf->rintf_channel_limit,
			    intf->rintf_channel_count );
            logged = TRUE;			     
            break; 
        case GMPR_ABOVE_THRESHOLD_BELOW_LIMIT :
	    gmpr_post_event(intf->rintf_instance, GMP_GROUP_THRESHOLD_EXCEED ,
	                    intf->rintf_id,
	                    intf->rintf_channel_threshold * intf->rintf_channel_limit/100 ,
			    intf->rintf_channel_count );
            logged = TRUE;
            break; 
        case GMPR_BELOW_THRESHOLD :
	    gmpr_post_event(intf->rintf_instance, GMP_GROUP_LIMIT_BELOW,
	                    intf->rintf_id,
	                    intf->rintf_channel_threshold * intf->rintf_channel_limit/100 ,
			    intf->rintf_channel_count );
            logged = TRUE;
            break;
        default :
	    break;
    }
    
    if(logged && intf->rintf_log_interval) {
       time(&(intf->last_log_time));
    }
}

/* returns 1 if limit is exceeded  when interface is null, 0 otherwise */
boolean gmpr_check_grp_limit(gmpr_intf *intf, boolean incr)
{         
    
    if(!intf)
       return 1;
    gmpr_limit_state_t cur_state = GMPR_BELOW_THRESHOLD;
    
    uint32_t actual_ch_count = intf->rintf_channel_count; 
    if(incr) {
       actual_ch_count = intf->rintf_channel_count + 1;
    }
    
    if((intf->rintf_channel_limit) &&
       (intf->rintf_channel_count >= intf->rintf_channel_limit)) {
         cur_state = GMPR_ABOVE_LIMIT;
         if(!intf->rintf_log_interval) {
	     intf->rintf_limit_state = cur_state;
             gmpr_post_event(intf->rintf_instance, GMP_GROUP_LIMIT_EXCEED , 
			 intf->rintf_id,
	  	         intf->rintf_channel_limit,
 		         intf->rintf_channel_count );
         }
    }else if((intf->rintf_channel_limit) &&
	     (intf->rintf_channel_threshold != 100 ) &&
	     (actual_ch_count > ((intf->rintf_channel_threshold *
					    intf->rintf_channel_limit)/100) )) {
	cur_state = GMPR_ABOVE_THRESHOLD_BELOW_LIMIT;
	    if(!intf->rintf_log_interval) {
	       intf->rintf_limit_state = cur_state;
	       gmpr_post_event(intf->rintf_instance, GMP_GROUP_THRESHOLD_EXCEED ,
	          intf->rintf_id,
	          intf->rintf_channel_threshold * intf->rintf_channel_limit/100 ,
	          actual_ch_count );
            }
    }else if (intf->rintf_channel_limit) {
	 cur_state = GMPR_BELOW_THRESHOLD;
	 
	 if(!intf->rintf_log_interval &&
	    (intf->rintf_limit_state != cur_state)) {
	     intf->rintf_limit_state = cur_state;
	     gmpr_post_event(intf->rintf_instance, GMP_GROUP_LIMIT_BELOW ,
		         intf->rintf_id,
	                 intf->rintf_channel_threshold * intf->rintf_channel_limit/100 ,
			 intf->rintf_channel_count );
         }
    }
    
    if(intf->rintf_log_interval) {
	/* If state different than previous state, a event to be logged has happened
	   for the first time. Any first time state changes are to be logged */
        
	if(intf->rintf_limit_state != cur_state) {
	    gmpr_log_event(intf, cur_state);
	    intf->rintf_limit_state = cur_state;
        } else {
            /* 
	     * state is same as prev state, this is a repeat event
	     * log messages if time elasped since since last log is
	     * is greater than log_interval. BELOW_THRESHOLD repeat
	     * events are not logged
             */
	        
	    if(cur_state) {
	      time_t cur_time;
	      time(&cur_time);
                   
	      /* There should be a last log time since this is a repeat event.
	       * The last event should have been logged and hence last log
	       * time should have been set to non zero value
	       */
                   
  	      double time_elasped = difftime(cur_time, intf->last_log_time);
	      if (time_elasped > intf->rintf_log_interval) {
	         gmpr_log_event(intf, cur_state);
              }
            } 
	}
    }

    return ( cur_state == GMPR_ABOVE_LIMIT ? TRUE: FALSE);
}

/*
 * gmpr_next_intf_group
 *
 * Returns the next input group on an interface, given the previous one.
 * If the previous pointer is NULL, returns the first input group on the
 * interface.
 *
 * Returns a pointer to the group, or NULL if there are no more groups.
 */
gmpr_group *
gmpr_next_intf_group (gmpr_intf *intf, gmpr_group *prev_group)
{
    gmpr_group *next_group;
    gmpx_patnode *node;

    /* Look up the node. */

    if (prev_group) {
	node = gmpx_patricia_get_next(intf->rintf_group_root,
				      &prev_group->rgroup_intf_patnode);
    } else {
	node = gmpx_patricia_lookup_least(intf->rintf_group_root);
    }

    /* Convert to a group pointer.  The pointer may be NULL. */

    next_group = gmpr_intf_patnode_to_group(node);

    return next_group;
}


/*
 * gmpr_next_oif_group
 *
 * Returns the next output group on an interface, given the previous one.
 * If the previous pointer is NULL, returns the first output group on the
 * interface.
 *
 * Returns a pointer to the group, or NULL if there are no more groups.
 */
gmpr_ogroup *
gmpr_next_oif_group (gmpr_intf *oif, gmpr_ogroup *prev_group)
{
    gmpr_ogroup *next_group;
    gmpx_patnode *node;

    /* Look up the node. */

    if (prev_group) {
	node = gmpx_patricia_get_next(oif->rintf_oif_group_root,
				      &prev_group->rogroup_intf_patnode);
    } else {
	node = gmpx_patricia_lookup_least(oif->rintf_oif_group_root);
    }

    /* Convert to a group pointer.  The pointer may be NULL. */

    next_group = gmpr_oif_patnode_to_group(node);

    return next_group;
}


/*
 * gmpr_delink_oif_group
 *
 * Delink an input group from its OIF group, if any.
 */
static void
gmpr_delink_oif_group (gmpr_group *group)
{
    if (group->rgroup_oif_group) {
	thread_remove(&group->rgroup_oif_thread);
	group->rgroup_oif_group = NULL;
    }
}


/*
 * gmpr_link_oif_group
 *
 * Link an input group to an OIF group.
 */
static void
gmpr_link_oif_group (gmpr_group *group, gmpr_ogroup *ogroup)
{
    /* Better not be linked yet. */

    gmpx_assert(!group->rgroup_oif_group);

    /* Add it to the thread and set the pointer. */

    thread_circular_add_bottom(&ogroup->rogroup_oif_head,
			       &group->rgroup_oif_thread);
    group->rgroup_oif_group = ogroup;
}


/*
 * gmpr_delink_oif_source
 *
 * Delink an input source from its OIF source, if any.
 *
 * Returns a pointer to the previously linked output group source, or NULL.
 */
static gmpr_ogroup_addr_entry *
gmpr_delink_oif_source (gmpr_group_addr_entry *group_addr)
{
    gmpr_ogroup_addr_entry *ogroup_addr;

    ogroup_addr = group_addr->rgroup_addr_oif_addr;
    if (ogroup_addr) {
	thread_remove(&group_addr->rgroup_addr_oif_thread);
	group_addr->rgroup_addr_oif_addr = NULL;

	/*
	 * If this was the last entry on the OIF source, flag it for
	 * notification.
	 */
	if (thread_circular_thread_empty(&ogroup_addr->rogroup_addr_oif_head))
	    ogroup_addr->rogroup_notify = TRUE;
    }

    return ogroup_addr;
}


/*
 * gmpr_link_oif_source
 *
 * Link an input source to an OIF source.
 */
static void
gmpr_link_oif_source (gmpr_group_addr_entry *group_addr,
		      gmpr_ogroup_addr_entry *ogroup_addr)
{
    boolean was_empty;

    /* Better not be linked yet. */

    gmpx_assert(!group_addr->rgroup_addr_oif_addr);

    /* Add it to the thread and set the pointer. */

    was_empty =
	thread_circular_thread_empty(&ogroup_addr->rogroup_addr_oif_head);
    thread_circular_add_bottom(&ogroup_addr->rogroup_addr_oif_head,
			       &group_addr->rgroup_addr_oif_thread);
    group_addr->rgroup_addr_oif_addr = ogroup_addr;

    /*
     * If we just added the first component source, flag the output source
     * for notification.
     */
    if (was_empty)
	ogroup_addr->rogroup_notify = TRUE;
}


/*
 * gmpr_alloc_ogroup_addr_entry
 *
 * Allocate an address entry for insertion into the group running-timer or
 * stopped-timer list.
 *
 * Returns a pointer to the embedded address list entry, or NULL if
 * out of memory.
 */
static gmp_addr_list_entry *
gmpr_alloc_ogroup_addr_entry (void *context)
{
    gmpr_ogroup_addr_entry *group_addr_entry;
    gmpr_ogroup *group;

    group = context;

    /* Allocate a block. */

    group_addr_entry = gmpx_malloc_block(gmpr_ogroup_addr_entry_tag);
    if (!group_addr_entry)
	return NULL;

    /* Initialize it. */

    group_addr_entry->rogroup_addr_group = group;
    gmpr_set_notification_type(group_addr_entry->rogroup_addr_client_thread,
			       GMPR_NOTIFY_SOURCE);
    thread_new_circular_thread(&group_addr_entry->rogroup_addr_oif_head);

    return &group_addr_entry->rogroup_addr_entry;
}


/*
 * gmpr_free_ogroup_addr_entry
 *
 * Callback to free an output group address entry.
 *
 * Any associated input group entries are delinked;  it is up to that side of
 * the code to ensure they get cleaned up.
 */
static void
gmpr_free_ogroup_addr_entry (gmp_addr_list_entry *addr_entry)
{
    gmpr_ogroup_addr_entry *ogroup_addr_entry;
    gmpr_group_addr_entry *group_addr_entry;
    thread *thread_ptr;

    ogroup_addr_entry = gmpr_addr_entry_to_ogroup_entry(addr_entry);

    /* Ensure that the entry isn't on any notification threads. */

    gmpr_flush_notifications(ogroup_addr_entry->rogroup_addr_client_thread,
			     TRUE);

    /* Delink any input group address entries. */

    thread_ptr = NULL;
    while (TRUE) {
	thread_ptr = thread_circular_top(
				 &ogroup_addr_entry->rogroup_addr_oif_head);
	group_addr_entry =
	    gmpr_oif_thread_to_group_addr_entry(thread_ptr);
	if (!group_addr_entry)
	    break;
	gmpr_delink_oif_source(group_addr_entry);
    }

    /* Free the block. */

    gmpx_free_block(gmpr_ogroup_addr_entry_tag, ogroup_addr_entry);
}


/*
 * gmpr_alloc_group_addr_entry
 *
 * Allocate an address entry for insertion into the input group
 * running-timer or stopped-timer list.
 *
 * Returns a pointer to the embedded address list entry, or NULL if
 * out of memory or the channel limit was reached.
 */
static gmp_addr_list_entry *
gmpr_alloc_group_addr_entry (void *context)
{
    gmpr_group_addr_entry *group_addr_entry;
    gmpr_group *group;
    gmpr_intf *intf;
    gmpr_instance *instance;

    group = context;
    intf = group->rgroup_intf;
    instance = intf->rintf_instance;

    /*
     * If this was not the first source on the group, check the
     * channel limit.  We don't count the first entry because the bare
     * group still counts as a channel.
     */
    if (!gmpr_all_group_lists_empty(group)) {
	if (intf->rintf_channel_limit) {
	    if( gmpr_check_grp_limit(intf, TRUE)) {
	        intf->rintf_chan_limit_drops++;
	        return NULL;
            }
	}
        intf->rintf_channel_count++;
    }

    /* Allocate a block. */

    group_addr_entry = gmpx_malloc_block(gmpr_group_addr_entry_tag);
    if (!group_addr_entry)
	return NULL;

    /* Initialize it. */

    group_addr_entry->rgroup_addr_group = group;
    thread_new_circular_thread(&group_addr_entry->rgroup_host_addr_head);

    /* Allocate the source timer. */

    group_addr_entry->rgroup_addr_timer = 
	gmpx_create_timer(instance->rinst_context, "GMP router source timer",
			  gmpr_source_timer_expiry, group_addr_entry);

    return &group_addr_entry->rgroup_addr_entry;
}


/*
 * gmpr_free_group_addr_entry
 *
 * Callback to free an input group address entry.
 *
 * Frees the timer as well as the entry.
 *
 * Any associated host group entries are delinked;  it is up to that side of
 * the code to ensure they get cleaned up.
 */
static void
gmpr_free_group_addr_entry (gmp_addr_list_entry *addr_entry)
{
    gmpr_group_addr_entry *group_addr_entry;
    gmpr_host_group_addr *host_group_addr_entry;
    gmpr_group *group;
    gmpr_intf *intf;
    thread *thread_ptr;

    group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);

    /* Delink any host address entries. */

    thread_ptr = NULL;
    while (TRUE) {
	thread_ptr = thread_circular_dequeue_top(
				 &group_addr_entry->rgroup_host_addr_head);
	host_group_addr_entry =
	    gmpr_thread_to_host_group_addr_entry(thread_ptr);
	if (!host_group_addr_entry)
	    break;

	host_group_addr_entry->rhga_source = NULL;
    }

    /* Delink from the output address entry, if any. */

    gmpr_update_source_oif(group_addr_entry, OIF_DELETE);

    /*
     * If this was not the last source on the group, drop the channel
     * count.  We don't count the last entry because the bare group
     * still counts as a channel.
     */
    group = group_addr_entry->rgroup_addr_group;
    intf = group->rgroup_intf;

    if (!gmpr_all_group_lists_empty(group)) {
	gmpx_assert(intf->rintf_channel_count > 0);
	intf->rintf_channel_count--;
	gmpr_check_grp_limit(intf, FALSE);
    }

    /* Destroy the timer and free the block. */

    gmpx_destroy_timer(group_addr_entry->rgroup_addr_timer);
    gmpx_free_block(gmpr_group_addr_entry_tag, group_addr_entry);
}


/*
 * gmpr_set_notification_type
 *
 * Initialize all notifications in an array with the notification type.
 */
void
gmpr_set_notification_type (gmpr_notify_block *notify_block,
			    gmpr_notification_type notify_type)
{
    ordinal_t client_ord;
    for (client_ord = 0; client_ord < GMPX_MAX_RTR_CLIENTS; client_ord++) {
	notify_block->gmpr_notify_type = notify_type;
	notify_block++;
    }
}


/*
 * gmpr_ogroup_lookup
 *
 * Look up an output group entry given the group address and the
 * output interface.
 *
 * Returns a pointer to the group entry, or NULL if it's not there.
 */
gmpr_ogroup *
gmpr_ogroup_lookup (gmpr_intf *intf, const uint8_t *group_addr)
{
    gmpr_ogroup *group;
    patnode *node;

    /* Bail if there's no interface. */

    if (!intf)
	return NULL;

    /* Look up the entry. */

    node = gmpx_patricia_lookup(intf->rintf_oif_group_root, group_addr);
    group = gmpr_oif_patnode_to_group(node);

    return group;
}


/*
 * gmpr_group_lookup
 *
 * Look up an input group entry given the group address and the interface.
 *
 * Returns a pointer to the group entry, or NULL if it's not there.
 */
gmpr_group *
gmpr_group_lookup (gmpr_intf *intf, const uint8_t *group_addr)
{
    gmpr_group *group;
    patnode *node;

    /* Look up the entry. */

    node = gmpx_patricia_lookup(intf->rintf_group_root, group_addr);
    group = gmpr_intf_patnode_to_group(node);

    return group;
}


/*
 * gmpr_group_notifications_active
 *
 * Returns TRUE if there are any active notifications on this group, or
 * FALSE if not.
 */
static boolean
gmpr_group_notifications_active (gmpr_ogroup *group)
{
    return gmpr_notifications_active(group->rogroup_client_thread);
}


/*
 * gmpr_group_version
 *
 * Returns the GMP version to be used for this group.  The version is
 * the lowest of that based on the versioning timers and the interface
 * version.
 *
 * If no group is passed, the interface version is returned.
 */
gmp_version
gmpr_group_version (gmpr_intf *intf, gmpr_group *group)
{
    gmp_version version;

    /* Figure it out based on timers if a group is present. */

    version = GMP_VERSION_SOURCES;
    if (group) {
	if (gmpx_timer_running(group->rgroup_basic_host_present)) {
	    version = GMP_VERSION_BASIC;
	} else if (gmpx_timer_running(group->rgroup_leaves_host_present)) {
	    version = GMP_VERSION_LEAVES;
	}
    }

    /* Now cap it based on the configured interface version. */

    if (intf->rintf_ver < version)
	version = intf->rintf_ver;

    return version;
}


/*
 * gmpr_evaluate_group_version
 *
 * Evaluate the version compatibility mode for a group.  We choose it based
 * on the status of the versioning timers.
 */
void
gmpr_evaluate_group_version (gmpr_group *group)
{
    gmpr_intf *intf;

    intf = group->rgroup_intf;

    /* Let the common code do the heavy lifting. */

    group->rgroup_compatibility_mode = gmpr_group_version(intf, group);
}


/*
 * gmpr_group_version_timer_expiry
 *
 * A version timer (host present) has expired.  We reevaluate the group
 * version.
 */
static void
gmpr_group_version_timer_expiry (gmpx_timer *timer, void *context)
{
    gmpr_group *group;

    group = context;

    /* Stop the timer. */

    gmpx_stop_timer(timer);

    /* Reevaluate the group version. */

    gmpr_evaluate_group_version(group);
}


/*
 * gmpr_ogroup_create
 *
 * Create a new output group entry, given the group and interface, and
 * link it in.
 *
 * Returns a pointer to the group entry, or NULL if no memory or the
 * output interface is NULL.
 */
static gmpr_ogroup *
gmpr_ogroup_create (gmpr_intf *intf, gmp_addr_string *group_addr)
{
    gmpr_ogroup *ogroup;
    gmpr_instance *instance;

    /* Bail if no output interface. */

    if (!intf)
	return NULL;

    instance = intf->rintf_instance;

    /* Allocate the block. */

    ogroup = gmpx_malloc_block(gmpr_ogroup_tag);
    if (!ogroup)
	return NULL;			/* No memory */

    /* Initialize it. */

    ogroup->rogroup_intf = intf;
    memmove(ogroup->rogroup_addr.gmp_addr, group_addr->gmp_addr, instance->rinst_addrlen);
    ogroup->rogroup_filter_mode = GMP_FILTER_MODE_INCLUDE;
    thread_new_circular_thread(&ogroup->rogroup_oif_head);

    gmp_addr_list_init(&ogroup->rogroup_src_addr_deleted,
		       &instance->rinst_addr_cat, gmpr_alloc_ogroup_addr_entry,
		       gmpr_free_ogroup_addr_entry, ogroup);
    gmp_addr_list_init(&ogroup->rogroup_incl_src_addr,
		       &instance->rinst_addr_cat, gmpr_alloc_ogroup_addr_entry,
		       gmpr_free_ogroup_addr_entry, ogroup);
    gmp_addr_list_init(&ogroup->rogroup_excl_src_addr,
		       &instance->rinst_addr_cat, gmpr_alloc_ogroup_addr_entry,
		       gmpr_free_ogroup_addr_entry, ogroup);

    gmpr_set_notification_type(ogroup->rogroup_client_thread,
			       GMPR_NOTIFY_GROUP);

    /* Link it in. */

    gmpx_assert(gmpx_patricia_add(intf->rintf_oif_group_root,
				  &ogroup->rogroup_intf_patnode));
    gmpr_link_global_group(ogroup);
    intf->rintf_oif_group_count++;

    /* Notify the clients. */

    gmpr_group_notify_clients(ogroup);

    gmpr_trace(instance, GMPR_TRACE_GROUP, "Created OIF group %a, intf %i",
	       ogroup->rogroup_addr.gmp_addr, intf->rintf_id);

    return ogroup;
}


/*
 * gmpr_group_create
 *
 * Create a new group entry, given the group and interface, and link it in.
 *
 * Returns a pointer to the group entry, or NULL if no memory or we've hit
 * the channel limit.
 */
gmpr_group *
gmpr_group_create (gmpr_intf *intf, const gmp_addr_string *group_addr)
{
    gmpr_group *group;
    gmpr_instance *instance;

    instance = intf->rintf_instance;

    /* Bail if we've hit the channel limit. */
    
    if (intf->rintf_channel_limit) {
        if( gmpr_check_grp_limit(intf, TRUE)) {
	    intf->rintf_chan_limit_drops++;
	    return NULL;
        }
    }

    /* Allocate the block. */

    group = gmpx_malloc_block(gmpr_group_tag);
    if (!group)
	return NULL;			/* No memory */

    /* Initialize it. */

    group->rgroup_intf = intf;
    intf->rintf_channel_count++;
    memmove(group->rgroup_addr.gmp_addr, group_addr->gmp_addr, instance->rinst_addrlen);
    group->rgroup_filter_mode = GMP_FILTER_MODE_INCLUDE;
    group->rgroup_compatibility_mode = GMP_VERSION_SOURCES;
    thread_new_circular_thread(&group->rgroup_host_group_head);

    gmp_addr_list_init(&group->rgroup_src_addr_running,
		       &instance->rinst_addr_cat, gmpr_alloc_group_addr_entry,
		       gmpr_free_group_addr_entry, group);
    gmp_addr_list_init(&group->rgroup_src_addr_stopped,
		       &instance->rinst_addr_cat, gmpr_alloc_group_addr_entry,
		       gmpr_free_group_addr_entry, group);
    gmp_addr_list_init(&group->rgroup_query_lo_timers,
		       &instance->rinst_addr_cat,
		       gmp_alloc_generic_addr_list_entry,
		       gmp_free_generic_addr_list_entry, NULL);
    gmp_addr_list_init(&group->rgroup_query_hi_timers,
		       &instance->rinst_addr_cat,
		       gmp_alloc_generic_addr_list_entry,
		       gmp_free_generic_addr_list_entry, NULL);

    group->rgroup_group_timer =
	gmpx_create_timer(instance->rinst_context, "GMP router group timer",
			  gmpr_group_timer_expiry, group);
    group->rgroup_query_timer =
	gmpx_create_timer(instance->rinst_context, "GMP router group query",
			  gmpr_group_query_timer_expiry, group);
    group->rgroup_gss_query_timer =
	gmpx_create_timer(instance->rinst_context, "GMP router GSS query",
			  gmpr_gss_query_timer_expiry, group);
    group->rgroup_basic_host_present =
	gmpx_create_timer(instance->rinst_context,
			  "GMP router group basic host",
			  gmpr_group_version_timer_expiry, group);
    group->rgroup_leaves_host_present =
	gmpx_create_timer(instance->rinst_context,
			  "GMP router group leave host",
			  gmpr_group_version_timer_expiry, group);

    /* Set the group compatibility mode. */

    gmpr_evaluate_group_version(group);

    /* Link it in. */

    gmpx_assert(gmpx_patricia_add(intf->rintf_group_root,
				  &group->rgroup_intf_patnode));
    intf->rintf_group_count++;

    gmpr_trace(instance, GMPR_TRACE_GROUP, "Created group %a, intf %i",
	       group->rgroup_addr.gmp_addr, intf->rintf_id);

    return group;
}


/*
 * gmpr_destroy_ogroup
 *
 * Destroy an output group entry.  Delinks it and frees memory.
 */
static void
gmpr_destroy_ogroup (gmpr_ogroup *ogroup)
{
    gmpr_group *group;
    gmpr_intf *intf;
    thread *thread_ptr;
    gmpr_instance *instance;

    intf = ogroup->rogroup_intf;
    instance = intf->rintf_instance;

    gmpr_trace(instance, GMPR_TRACE_GROUP, "Deleted output group %a, intf %i",
	       ogroup->rogroup_addr.gmp_addr, intf->rintf_id);

    /*
     * Pull the group off of any client notification thread.  Normally this
     * should be empty, but if things are shutting down there may be
     * dangling notifications.
     */
    gmpr_flush_notifications(ogroup->rogroup_client_thread, TRUE);

    /* Walk the input group thread and delink them all. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&ogroup->rogroup_oif_head);
	group = gmpr_oif_thread_to_group(thread_ptr);
	if (!group)
	    break;
	gmpr_delink_oif_group(group);
    }

    /*
     * Flush the address lists.  They should normally be empty, but may
     * not be if we're shutting down unceremoniously.
     */
    gmp_addr_list_clean(&ogroup->rogroup_incl_src_addr);
    gmp_addr_list_clean(&ogroup->rogroup_excl_src_addr);
    gmp_addr_list_clean(&ogroup->rogroup_src_addr_deleted);

    /* Remove ourselves from the interface tree. */

    gmpx_assert(gmpx_patricia_delete(intf->rintf_oif_group_root,
				     &ogroup->rogroup_intf_patnode));
    gmpx_assert(intf->rintf_oif_group_count > 0);
    intf->rintf_oif_group_count--;

    /* Delink from the global tree. */

    gmpr_delink_global_group(ogroup);

    /* Free the block. */

    gmpx_free_block(gmpr_ogroup_tag, ogroup);
}


/*
 * gmpr_destroy_group
 *
 * Destroy a group entry.  Delinks it and frees memory.
 */
static void
gmpr_destroy_group (gmpr_group *group)
{
    gmpr_host_group *host_group;
    gmpr_intf *intf;
    thread *thread_ptr;
    gmpr_instance *instance;

    intf = group->rgroup_intf;
    instance = intf->rintf_instance;

    gmpr_trace(instance, GMPR_TRACE_GROUP, "Deleted group %a, intf %i",
	       group->rgroup_addr.gmp_addr, intf->rintf_id);

    /* Walk the host group thread and delink them all. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&group->rgroup_host_group_head);
	host_group = gmpr_thread_to_host_group(thread_ptr);
	if (!host_group)
	    break;
	host_group->rhgroup_group = NULL;
	thread_remove(thread_ptr);
    }

    /* Delink from any output group. */

    gmpr_update_group_oif(group, OIF_DELETE);

    /*
     * Flush the address lists.  They should normally be empty, but may
     * not be if we're shutting down unceremoniously.
     */
    gmp_addr_list_clean(&group->rgroup_src_addr_running);
    gmp_addr_list_clean(&group->rgroup_src_addr_stopped);
    gmp_addr_list_clean(&group->rgroup_query_lo_timers);
    gmp_addr_list_clean(&group->rgroup_query_hi_timers);

    /* Destroy the timers. */

    gmpx_destroy_timer(group->rgroup_query_timer);
    gmpx_destroy_timer(group->rgroup_group_timer);
    gmpx_destroy_timer(group->rgroup_gss_query_timer);
    gmpx_destroy_timer(group->rgroup_basic_host_present);
    gmpx_destroy_timer(group->rgroup_leaves_host_present);

    /* Delink from the interface transmit thread. */

    thread_remove(&group->rgroup_xmit_thread);

    /* Remove ourselves from the interface tree. */

    gmpx_assert(gmpx_patricia_delete(intf->rintf_group_root,
				     &group->rgroup_intf_patnode));
    gmpx_assert(intf->rintf_group_count > 0);
    intf->rintf_group_count--;
    gmpx_assert(intf->rintf_channel_count > 0);
    intf->rintf_channel_count--;
    gmpr_check_grp_limit(intf, FALSE); 
     
    /* Free the block. */

    gmpx_free_block(gmpr_group_tag, group);
}


/*
 * gmpr_destroy_intf_groups
 *
 * Unceremoniously destroy all groups on an interface.
 */
void
gmpr_destroy_intf_groups (gmpr_intf *intf)
{
    gmpr_group *group;
    gmpr_ogroup *ogroup;

    /* Walk all output groups on the interface. */

    while (TRUE) {
	ogroup = gmpr_next_oif_group(intf, NULL);
	if (!ogroup)
	    break;

	/* Destroy the group. */

	gmpr_destroy_ogroup(ogroup);
    }

    /* Walk all input groups on the interface. */

    while (TRUE) {
	group = gmpr_next_intf_group(intf, NULL);
	if (!group)
	    break;

	/* Destroy the group. */

	gmpr_destroy_group(group);
    }
}


/*
 * gmpr_enqueue_group_xmit
 *
 * Enqueue a group entry for transmission of a Report message on an
 * interface.
 *
 * All timers and linkages are expected to be set.
 */
void
gmpr_enqueue_group_xmit (gmpr_group *group)
{
    /* Enqueue it if it's not already on the queue. */

    if (!thread_node_on_thread(&group->rgroup_xmit_thread)) {
	thread_circular_add_bottom(&group->rgroup_intf->rintf_xmit_head,
				   &group->rgroup_xmit_thread);
    }
}


/*
 * gmpr_first_group_xmit
 *
 * Get the first group entry on an interface transmit list.
 *
 * Returns a pointer to the group, or NULL if the list is empty.
 */
gmpr_group *
gmpr_first_group_xmit (gmpr_intf *intf)
{
    thread *thread_ptr;
    gmpr_group *group;

    thread_ptr = thread_circular_top(&intf->rintf_xmit_head);
    group = gmpr_xmit_thread_to_group(thread_ptr);

    return group;
}


/*
 * gmpr_dequeue_group_xmit
 *
 * Dequeue a group entry from an interface.
 */
void
gmpr_dequeue_group_xmit (gmpr_group *group)
{
    thread_remove(&group->rgroup_xmit_thread);
}


/*
 * gmpr_attempt_ogroup_free
 *
 * Attempt to free the output group entry.  We will do so if there is
 * no client interest in the group and there's nothing more to send
 * for the group.
 *
 * returns TRUE if gmpr_ogroup was freed.  Otherwise FALSE.
 */
boolean
gmpr_attempt_ogroup_free (gmpr_ogroup *ogroup)
{
    /*
     * Bail if we're in Exclude mode.  Groups are only deleted when
     * they're empty, and they're only empty when the group is
     * in state Include {}.
     */
    if (ogroup->rogroup_filter_mode == GMP_FILTER_MODE_EXCLUDE)
	return FALSE;

    /* Bail if there are any pending client notifications. */

    if (gmpr_group_notifications_active(ogroup))
	return FALSE;

    /* Bail if there are any input groups bound to this group. */

    if (!thread_circular_thread_empty(&ogroup->rogroup_oif_head))
	return FALSE;

    /* Bail if there's anything on any of the lists. */

    if (!gmp_addr_list_empty(&ogroup->rogroup_incl_src_addr))
	return FALSE;
    if (!gmp_addr_list_empty(&ogroup->rogroup_excl_src_addr))
	return FALSE;
    if (!gmp_addr_list_empty(&ogroup->rogroup_src_addr_deleted))
	return FALSE;
    if (!thread_circular_thread_empty(&ogroup->rogroup_oif_head))
	return FALSE;

    /* Looks safe.  Destroy the group. */
    
    gmpr_destroy_ogroup(ogroup);
    return TRUE;
}


/*
 * gmpr_attempt_group_free
 *
 * Attempt to free the group entry.  We will do so if there is no client
 * interest in the group and there's nothing more to send for the group.
 */
void
gmpr_attempt_group_free (gmpr_group *group)
{
    /*
     * Bail if we're in Exclude mode.  Groups are only deleted when
     * they're empty, and they're only empty when the group is
     * in state Include {}.
     */
    if (group->rgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE)
	return;

    /* Bail if this group is bound to an output group. */

    if (group->rgroup_oif_group)
	return;

    /* Bail if there's anything on any of the lists. */

    if (!gmp_addr_list_empty(&group->rgroup_src_addr_running))
	return;
    if (!gmp_addr_list_empty(&group->rgroup_src_addr_stopped))
	return;
    if (!gmp_addr_list_empty(&group->rgroup_query_lo_timers))
	return;
    if (!gmp_addr_list_empty(&group->rgroup_query_hi_timers))
	return;

    /* Bail if the group timer is running. */

    if (gmpx_timer_running(group->rgroup_group_timer))
	return;

    /* Looks safe.  Destroy the group. */

    gmpr_destroy_group(group);
}


/*
 * gmpr_lookup_global_group
 *
 * Get the global group entry corresponding to the provided group entry.
 *
 * Returns a pointer to the global group entry, or NULL if it's not there.
 */
gmpr_global_group *
gmpr_lookup_global_group (gmpr_instance *instance, uint8_t *group_addr)
{
    gmpr_global_group *global_group;
    gmpx_patnode *node;

    /* Look up the group in the tree. */

    node = gmpx_patricia_lookup(instance->rinst_global_state_root, group_addr);
    global_group = gmpr_patnode_to_global_group(node);

    return global_group;
}


/*
 * gmpr_get_global_group
 *
 * Get the global group entry corresponding to the provided group entry.
 * Create one if it's not there.
 *
 * Returns a pointer to the global group entry, or NULL if out of memory.
 */
static gmpr_global_group *
gmpr_get_global_group (gmpr_ogroup *group)
{
    gmpr_instance *instance;
    gmpr_global_group *global_group;

    instance = group->rogroup_intf->rintf_instance;

    /* Look up the group in the tree. */

    global_group = gmpr_lookup_global_group(instance,
					    group->rogroup_addr.gmp_addr);
    if (!global_group) {

        /* Entry isn't there.  Create one. */

        global_group = gmpx_malloc_block(gmpr_global_group_tag);
        if (!global_group)
            return NULL;		/* Out of memory */

        /* Initialize. */

        thread_new_circular_thread(&global_group->global_group_head);
        memmove(global_group->global_group_addr.gmp_addr,
            group->rogroup_addr.gmp_addr,
            instance->rinst_addrlen);

        /* Stick it into the tree. */

        gmpx_assert(gmpx_patricia_add(instance->rinst_global_state_root,
                          &global_group->global_group_node));
    }

    return global_group;
}


/*
 * gmpr_link_global_group
 *
 * Link a new group to the global group tree.
 *
 * Returns a pointer to the global group, or NULL if out of memory.
 */
gmpr_global_group *
gmpr_link_global_group (gmpr_ogroup *group)
{
    // gmpr_instance *instance;
    gmpr_global_group *global_group;

    // instance = group->rogroup_intf->rintf_instance;

    /* Get the global group entry.  One is created if necessary. */

    global_group = gmpr_get_global_group(group);
    if (!global_group)
	return NULL;

    /* Got it.  Thread the new group to it. */

    thread_circular_add_bottom(&global_group->global_group_head,
			       &group->rogroup_global_thread);

    return global_group;
}


/*
 * gmpr_attempt_delete_global_group
 *
 * Attempt to delete a global group entry.  We do so if there are no
 * groups or sources attached to the entry.
 */
static void
gmpr_attempt_delete_global_group(gmpr_instance *instance,
				 gmpr_global_group *global_group)
{
    /* See if there are any groups. */

    if (thread_circular_thread_empty(&global_group->global_group_head)) {

	/* No groups.  Delink from the instance tree. */

	gmpx_assert(gmpx_patricia_delete(instance->rinst_global_state_root,
				 &global_group->global_group_node));

	/* Free the block. */

	gmpx_free_block(gmpr_global_group_tag, global_group);
    }
}


/*
 * gmpr_delink_global_group
 *
 * Delink a group from the global tree.  Frees the global group entry if
 * all groups are gone.
 */
void
gmpr_delink_global_group (gmpr_ogroup *group)
{
    gmpr_instance *instance;
    gmpr_global_group *global_group;

    instance = group->rogroup_intf->rintf_instance;

    /* Bail if not threaded. */

    if (!thread_node_on_thread(&group->rogroup_global_thread))
	return;

    /* Delink from the thread. */

    thread_remove(&group->rogroup_global_thread);

    /* Look up the group. */

    global_group = gmpr_lookup_global_group(instance,
					    group->rogroup_addr.gmp_addr);
    gmpx_assert(global_group);

    /* Delete the global group if it's empty. */

    gmpr_attempt_delete_global_group(instance, global_group);
}


/*
 * gmpr_group_forwards_all_sources
 *
 * Returns TRUE if the group forwards all sources (it's an Exclude{}), or
 * FALSE if not.
 */
boolean
gmpr_group_forwards_all_sources (gmpr_ogroup *group)
{
    gmp_addr_vect result_vector;
    boolean result;

    /* If not Exclude mode, we don't forward everything. */

    if (group->rogroup_filter_mode != GMP_FILTER_MODE_EXCLUDE)
	return FALSE;

    /* If the exclude list is empty, we forward all sources. */

    if (gmp_addr_list_empty(&group->rogroup_excl_src_addr))
	return TRUE;

    /*
     * The exclude list isn't empty.  Subtract the include list from
     * the exclude list.  If the result is empty, the group forwards
     * all sources.  Otherwise, it doesn't.
     */
    gmp_init_addr_vector(&result_vector, NULL);
    gmp_addr_vect_minus(&group->rogroup_excl_src_addr.addr_vect,
			&group->rogroup_incl_src_addr.addr_vect,
			&result_vector, NULL, NULL, 0);
    result = gmp_addr_vect_empty(&result_vector);
    gmp_addr_vect_clean(&result_vector);
    return result;
}


/*
 * gmpr_group_forwards_source
 *
 * Returns TRUE if the source is being forwarded as part of the group, or
 * FALSE if not.
 */
boolean
gmpr_group_forwards_source (gmpr_ogroup *group, const uint8_t *source_addr)
{
    gmpr_instance *instance;
    gmp_addr_cat_entry *cat_entry;
    boolean active;
    boolean result;

    instance = group->rogroup_intf->rintf_instance;

    /* Look up the catalog entry.  It may or may not be there. */

    cat_entry = gmp_lookup_addr_cat_entry(&instance->rinst_addr_cat,
					  source_addr);

    /*
     * If the catalog entry is there, see if the source is active.  If the
     * catalog entry isn't there, the source isn't active.
     */
    if (cat_entry) {
	active = gmpr_source_ord_is_active(group, cat_entry->adcat_ent_ord);
    } else {
	active = FALSE;
    }

    /* Check out the filter mode. */

    if (group->rogroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {

	/* Include mode.  We're forwarding if the source is active. */

	result = active;

    } else {

	/* Exclude mode.  We're forwarding if the source is *not* active. */

	result = !active;
    }

    return result;
}


/*
 * gmpr_wipe_source_timer
 *
 * Callback from a vector walk to prematurely time out a source timer.
 */
static boolean
gmpr_wipe_source_timer (void *context, bv_bitnum_t bitnum,
			boolean new_value GMPX_UNUSED,
			boolean old_value GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr_entry;

    group = context;

    /* Look up the entry. */

    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
				       bitnum);
    gmpx_assert(addr_entry);
    group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);

    /* Whack the timer. */

    gmpx_start_timer(group_addr_entry->rgroup_addr_timer, 0, 0);

    return FALSE;
}


/*
 * gmpr_timeout_group
 *
 * Prematurely time out a group.
 */
void
gmpr_timeout_group (gmpr_group *group)
{
    /* If the group is in Exclude mode, just whack the group timer. */

    if (group->rgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE) {
	gmpx_start_timer(group->rgroup_group_timer, 0, 0);

    } else {

	/*
	 * Include mode.  We need to walk all of the sources and
	 * whack their individual timers.
	 */
	gmp_addr_vect_walk(&group->rgroup_src_addr_running.addr_vect,
			   gmpr_wipe_source_timer, group);
    }
}


/*
 * gmpr_evaluate_oif_group
 *
 * State has possibly changed on an OIF group.  Send any necessary
 * notifications.
 */
static void
gmpr_evaluate_oif_group (gmpr_ogroup *ogroup)
{
    gmp_filter_mode new_mode;

    /* Tolerate NULL pointers. */

    if (!ogroup)
	return;

    /*
     * See whether the filter mode has changed.  The filter mode is
     * Include if the Exclude list is empty and there are no input
     * groups linked to the group, or is Exclude otherwise.
     */
    if (gmp_addr_list_empty(&ogroup->rogroup_excl_src_addr) &&
	thread_circular_thread_empty(&ogroup->rogroup_oif_head)) {
	new_mode = GMP_FILTER_MODE_INCLUDE;
    } else {
	new_mode = GMP_FILTER_MODE_EXCLUDE;
    }
    if (new_mode != ogroup->rogroup_filter_mode) {

	/* The mode changed.  Switch it and do the notifications. */

	ogroup->rogroup_filter_mode = new_mode;
	gmpr_mode_change_notify_clients(ogroup);

    } else {

	/*
	 * The mode didn't change.  See if the group has become inactive
	 * (we may have just deleted the final source) and notify
	 * the clients if so.
	 */
	if (!gmpr_ogroup_is_active(ogroup)) {
	    gmpr_group_notify_clients(ogroup);
	}
    }

    /* Try to free the entry, just in case. */

    gmpr_attempt_ogroup_free(ogroup);
}


/*
 * gmpr_evaluate_oif_source
 *
 * State has possibly changed on an OIF source.  Do The Right Thing, which
 * includes possibly updating the OIF group and/or sending notifications.
 */
static void
gmpr_evaluate_oif_source (gmpr_ogroup_addr_entry *ogroup_addr)
{
    gmpr_ogroup *ogroup;
    gmpr_ogroup_addr_entry *other_ogroup_addr;
    gmp_addr_list *addr_list;
    gmp_addr_list_entry *addr_entry;

    /* Tolerate NULL pointers. */

    if (!ogroup_addr)
	return;

    /* See if this source has any components left. */

    ogroup = ogroup_addr->rogroup_addr_group;
    if (thread_circular_thread_empty(&ogroup_addr->rogroup_addr_oif_head)) {

	/*
	 * No components left.  Check to see if the same address is
	 * present on the other list (include/exclude).
	 */
	if (gmpr_group_addr_mode(ogroup_addr) == GMP_FILTER_MODE_INCLUDE) {
	    addr_list =	gmpr_ogroup_source_list_by_mode(ogroup,
						GMP_FILTER_MODE_EXCLUDE);
	} else {
	    addr_list =	gmpr_ogroup_source_list_by_mode(ogroup,
						GMP_FILTER_MODE_INCLUDE);
	}
	addr_entry = gmp_lookup_addr_entry(addr_list,
			   ogroup_addr->rogroup_addr_entry.addr_ent_ord);
	other_ogroup_addr = gmpr_addr_entry_to_ogroup_entry(addr_entry);

	if (other_ogroup_addr) {

	    /*
	     * The same address exists on the other list.  Simply destroy
	     * this one, and post a notification for the other one.
	     */
	    gmp_delete_addr_list_entry(&ogroup_addr->rogroup_addr_entry);
	    ogroup_addr = other_ogroup_addr;
	    ogroup_addr->rogroup_notify = TRUE;

	} else {

	    /*
	     * This source doesn't exist on the other list.  Move the
	     * source to the delete list.
	     */
	    gmp_move_addr_list_entry(&ogroup->rogroup_src_addr_deleted,
				     &ogroup_addr->rogroup_addr_entry);
	}
    }
    gmpr_source_notify_clients(ogroup_addr, NOTIFY_CONDITIONAL);

    /*
     * Now evaluate the group.  The change in source may have caused a
     * mode change.
     */
    gmpr_evaluate_oif_group(ogroup);
}


/*
 * gmpr_link_oif_group_intf
 *
 * Link an input group to an OIF group by interface.  If the OIF group
 * doesn't exist, create it.
 *
 * Returns a pointer to the OIF group, or NULL if out of memory, or if the
 * OIF pointer is NULL.
 */
static gmpr_ogroup *
gmpr_link_oif_group_intf (gmpr_group *group, gmpr_intf *oif)
{
    gmpr_ogroup *ogroup;

    /* Bail if no interface. */

    if (!oif)
	return NULL;

    /* Look up the output group. */

    ogroup = gmpr_ogroup_lookup(oif, group->rgroup_addr.gmp_addr);
    if (!ogroup) {

	/* The output group doesn't exist.  Create it. */

	ogroup = gmpr_ogroup_create(oif, &group->rgroup_addr);
	if (!ogroup)
	    return NULL;		/* Out of memory */
    }

    /* Link the input and output groups together. */

    gmpr_link_oif_group(group, ogroup);

    return ogroup;
}


/*
 * gmpr_lookup_oif_source
 *
 * Look up an OIF source given the input source and OIF.
 *
 * Returns a pointer to the OIF source, or NULL if it doesn't exist.
 * Also returns a pointer to the OIF group if that exists.
 */
static gmpr_ogroup_addr_entry *
gmpr_lookup_oif_source (gmpr_intf *oif, gmpr_group_addr_entry *group_addr,
			gmpr_ogroup **ogroup)
{
    gmpr_ogroup_addr_entry *ogroup_addr_entry;
    gmpr_group *group;
    gmp_addr_list *addr_list;
    gmp_addr_list_entry *addr_entry;

    /* If there's no OIF, there's no output group. */

    if (!oif) {
	*ogroup = NULL;
	return NULL;
    }

    /* Look up the output group. */

    group = group_addr->rgroup_addr_group;
    *ogroup = gmpr_ogroup_lookup(oif, group->rgroup_addr.gmp_addr);
    if (!*ogroup)
	return NULL;			/* Ain't here. */

    /* Look up the output source. */

    addr_list = gmpr_ogroup_source_list_by_mode(*ogroup,
						group->rgroup_filter_mode);
    addr_entry = gmp_lookup_addr_entry(addr_list,
			       group_addr->rgroup_addr_entry.addr_ent_ord);
    ogroup_addr_entry = gmpr_addr_entry_to_ogroup_entry(addr_entry);

    return ogroup_addr_entry;
}


/*
 * gmpr_link_oif_source_intf
 *
 * Link an input source to an OIF source by interface.  If the OIF
 * group and/or source don't exist, create them.
 *
 * Returns a pointer to the new OIF source, or NULL if out of memory or the
 * OIF pointer was NULL.
 *
 * Note that we will harvest a source from the deleted list if there, in order
 * to avoid sending redundant notifications, and to ensure that the same
 * source is never on both an active (include/exclude) list and the deleted
 * list.
 */
static gmpr_ogroup_addr_entry *
gmpr_link_oif_source_intf (gmpr_group_addr_entry *group_addr, gmpr_intf *oif)
{
    gmpr_ogroup_addr_entry *ogroup_addr;
    gmpr_ogroup *ogroup;
    gmpr_group *group;
    gmp_addr_list *addr_list;
    gmp_addr_list_entry *addr_entry;
    ordinal_t addr_ord;

    addr_ord = group_addr->rgroup_addr_entry.addr_ent_ord;

    /* Bail if no output interface. */

    if (!oif)
	return NULL;

    /* Look up the output group and source. */

    group = group_addr->rgroup_addr_group;
    ogroup_addr = gmpr_lookup_oif_source(oif, group_addr, &ogroup);
    if (!ogroup) {

	/* The output group doesn't exist.  Create it. */

	ogroup = gmpr_ogroup_create(oif, &group->rgroup_addr);
	if (!ogroup)
	    return NULL;		/* Out of memory */
    }

    /* If the output source doesn't exist, get one. */

    if (!ogroup_addr) {

	/* Figure out which list it's going to. */

	addr_list = gmpr_ogroup_source_list_by_mode(ogroup,
						    group->rgroup_filter_mode);
	/* See if we can get it from the deleted list. */

	addr_entry = gmp_lookup_addr_entry(&ogroup->rogroup_src_addr_deleted,
					   addr_ord);
	if (addr_entry) {

	    /* On the deleted list.  Move it to the active list. */

	    gmp_move_addr_list_entry(addr_list, addr_entry);

	} else {

	    /* Not there.  Create one. */

	    addr_entry = gmp_create_addr_list_entry(addr_list, addr_ord);
	}

	ogroup_addr = gmpr_addr_entry_to_ogroup_entry(addr_entry);
	if (!ogroup_addr)
	    return NULL;		/* Out of memory */
    }

    /*
     * Link the input and output sources together.  This will trigger
     * a notification.
     */
    gmpr_link_oif_source(group_addr, ogroup_addr);

    return ogroup_addr;
}


/*
 * gmpr_get_current_source_oif
 *
 * Returns the current OIF for a source, or NULL if it has none.
 *
 * Tolerates NULL pointers.
 */
static gmpr_intf *
gmpr_get_current_source_oif (gmpr_group_addr_entry *group_addr)
{
    gmpr_ogroup_addr_entry *ogroup_addr;
    gmpr_intf *oif;
    gmpr_ogroup *ogroup;

    if (!group_addr)
	return NULL;

    /* Pick up the current output source entry, if any. */

    ogroup_addr = group_addr->rgroup_addr_oif_addr;
    if (ogroup_addr) {
	ogroup = ogroup_addr->rogroup_addr_group;
	oif = ogroup->rogroup_intf;
    } else {
	oif = NULL;
    }

    return oif;
}


/*
 * gmpr_get_current_group_oif
 *
 * Returns the current OIF for a group, or NULL if it has none.
 *
 * Tolerates NULL pointers.
 */
static gmpr_intf *
gmpr_get_current_group_oif (gmpr_group *group)
{
    gmpr_intf *oif;
    gmpr_ogroup *ogroup;

    if (!group)
	return NULL;

    /* Grab the current output group, if any, and get the interface. */

    ogroup = group->rgroup_oif_group;
    if (ogroup) {
	oif = ogroup->rogroup_intf;
    } else {
	oif = NULL;
    }

    return oif;
}


/*
 * gmpr_get_mapped_oif
 *
 * Get the mapped OIF for a (*,G) or (S,G).  Returns a pointer to the OIF
 * (which may be the same as the input interface) or NULL if there is
 * no output interface.
 */
static gmpr_intf *
gmpr_get_mapped_oif (gmpr_intf *intf, uint8_t *group, uint8_t *source)
{
    gmpr_intf *oif;
    gmpx_intf_id oif_id;
    gmpr_instance *instance;
    gmpr_instance_context *ctx;
    boolean oif_ok;

    instance = intf->rintf_instance;
    ctx = &instance->rinst_cb_context;
    if (ctx->rctx_oif_map_cb) {
	oif_ok = (*ctx->rctx_oif_map_cb)(instance->rinst_context,
					 intf->rintf_id, group, source,
					 &oif_id);
	if (oif_ok) {
	    oif = gmpr_intf_lookup(instance, oif_id); /* May be NULL */
	} else {
	    oif = NULL;
	}
    } else {
	oif = intf;
    }

    return oif;
}


/*
 * gmpr_map_group_oif
 *
 * Do the work to (re)map a group to an output interface.
 *
 * If the output interface is changing, we clean up the old interface.
 */
static void
gmpr_map_group_oif (gmpr_group *group)
{
    gmpr_intf *oif;
    gmpr_intf *old_oif;
    gmpr_ogroup *ogroup;

    /*
     * Validity checks.  We have to be active, in Exclude mode, and have
     * no sources.
     */
    gmpx_assert(gmpr_group_is_active(group));
    gmpx_assert(group->rgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE);
    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));

    /* Get the current and new mapped output interfaces. */

    ogroup = group->rgroup_oif_group;
    old_oif = gmpr_get_current_group_oif(group);
    oif = gmpr_get_mapped_oif(group->rgroup_intf, group->rgroup_addr.gmp_addr,
			      NULL);

    /*
     * If the output interface has changed, delink from the old one
     * and link to the new one.  This handles the NULL cases properly.
     */
    if (oif != old_oif) {
	gmpr_delink_oif_group(group);
	gmpr_evaluate_oif_group(ogroup);
	ogroup = gmpr_link_oif_group_intf(group, oif);
    }

    /* Evaluate the current OIF group. */

    gmpr_evaluate_oif_group(ogroup);
}


/*
 * gmpr_update_group_oif
 *
 * Update the OIF information for an input group.
 */
void
gmpr_update_group_oif (gmpr_group *group, oif_update_type update_type)
{
    gmpr_ogroup *ogroup;
    // gmpr_intf *intf;
    // gmpr_instance *instance;

    // intf = group->rgroup_intf;
    // instance = intf->rintf_instance;
    ogroup = group->rgroup_oif_group;

    /* Switch based on the operation (Update or Delete). */

    switch (update_type) {
      case OIF_UPDATE:

	/*
	 * See if the group has any sources.  If so, it should not be
	 * linked to an OIF (since the sources are individually linked.)
	 * This can happen if a group was originally (*,G) but switched
	 * to having sources.
	 */
	if (!gmp_addr_list_empty(&group->rgroup_src_addr_stopped) ||
	    !gmp_addr_list_empty(&group->rgroup_src_addr_running)) {

	    /* Sources present.  Delink us. */

	    gmpr_delink_oif_group(group);
	    gmpr_evaluate_oif_group(ogroup);

	} else {

	    /* No sources present.  Remap the group. */

	    gmpr_map_group_oif(group);
	}
	break;

      case OIF_DELETE:

	/* Delink from the output group and then reevaluate it. */

	gmpr_delink_oif_group(group);
	gmpr_evaluate_oif_group(ogroup);
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmpr_map_source_oif
 *
 * (Re)map the output interface for a source.
 *
 * If the output interface is changing, we clean up the old one.
 */
static void
gmpr_map_source_oif (gmpr_group_addr_entry *group_addr)
{
    gmpr_group *group;
    gmpr_ogroup *ogroup;
    gmpr_ogroup_addr_entry *old_ogroup_addr;
    gmpr_ogroup_addr_entry *ogroup_addr;
    gmpr_intf *intf;
    gmpr_intf *oif;
    gmpr_intf *old_oif;
    gmpr_instance *instance;
    gmp_addr_cat_entry *cat_entry;

    group = group_addr->rgroup_addr_group;
    intf = group->rgroup_intf;
    instance = intf->rintf_instance;
    old_ogroup_addr = group_addr->rgroup_addr_oif_addr;
    old_oif = gmpr_get_current_source_oif(group_addr);

    /*
     * Look up the output interface.  We may be given an intf_id
     * that we don't know about, or the policy may tell us to
     * block the group.  In either of those cases we will end
     * up with a NULL OIF pointer, and the right thing will happen.
     */
    cat_entry = gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat,
			    group_addr->rgroup_addr_entry.addr_ent_ord);
    gmpx_assert(cat_entry);
    oif = gmpr_get_mapped_oif(intf, group->rgroup_addr.gmp_addr,
			      cat_entry->adcat_ent_addr.gmp_addr);

    /*
     * If the output interface has changed, delink from the old
     * one and link to the new one.  Note that either of the oif pointers
     * may be NULL.
     */
    if (oif != old_oif) {
	gmpr_delink_oif_source(group_addr);
	gmpr_evaluate_oif_source(old_ogroup_addr);
	ogroup_addr = gmpr_link_oif_source_intf(group_addr, oif);

    } else {

	/* Output interface unchanged.  Check the output source. */

	ogroup_addr = gmpr_lookup_oif_source(oif, group_addr, &ogroup);

	/*
	 * If the output source has changed, delink from the old one
	 * and link to the new one.  Note that the call to
	 * gmpr_link_oif_source_intf() will find the same output group
	 * address if it exists, or create one if it doesn't.  Note also that
	 * either the new or old output group address may be NULL.
	 */
	if (ogroup_addr != old_ogroup_addr) {
	    gmpr_delink_oif_source(group_addr);
	    gmpr_evaluate_oif_source(old_ogroup_addr);
	    ogroup_addr = gmpr_link_oif_source_intf(group_addr, oif);
	}
    }

    /* Evaluate the source. */

    gmpr_evaluate_oif_source(ogroup_addr);
}


/*
 * gmpr_update_source_oif
 *
 * Update the OIF information for an input source.
 */
void
gmpr_update_source_oif (gmpr_group_addr_entry *group_addr,
			oif_update_type update_type)
{
    gmpr_ogroup_addr_entry *old_ogroup_addr;

    /* Switch based on the operation (Update or Delete). */

    switch (update_type) {
      case OIF_UPDATE:

	/* Remap the output interface. */

	gmpr_map_source_oif(group_addr);
	break;

      case OIF_DELETE:

	/* Deleting the source.  Delink it and reevaluate the output source. */

	old_ogroup_addr = gmpr_delink_oif_source(group_addr);
	gmpr_evaluate_oif_source(old_ogroup_addr);
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmpr_update_all_group_source_oif
 *
 * Update the OIF information for all sources that are part of a group.
 */
static void
gmpr_update_all_group_source_oif (gmpr_group *group,
				  oif_update_type update_type)
{
    gmpr_group_addr_entry *group_addr;
    gmp_addr_list *addr_list;
    gmp_addr_list_entry *addr_entry;

    /* Walk all of the sources on the group. */

    addr_entry = NULL;
    addr_list = gmpr_group_source_list(group);
    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	group_addr = gmpr_addr_entry_to_group_entry(addr_entry);

	/* Bail if all done. */

	if (!group_addr)
	    break;

	/* Got an entry.  Update it. */

	gmpr_update_source_oif(group_addr, update_type);
    }
}


/*
 * gmpr_update_oif_mode_change
 *
 * Update the OIF info for an input group when the group changes
 * filter modes (from Include to Exclude, or vice versa.)
 *
 * This routine is also called when a (*,G) group is created or destroyed,
 * since a mode change takes place for both of those.
 *
 * The OIF info is updated as necessary.
 *
 * If appropriate, an attempt is made to delete the input group as well.
 */
void
gmpr_update_oif_mode_change (gmpr_group *group)
{
    /* See if the group is active (Exclude mode, or sources.) */

    if (gmpr_group_is_active(group)) {

	/*
	 * The group is active.  Update the OIF for the group and all of the
	 * sources.
	 */
	gmpr_update_group_oif(group, OIF_UPDATE);
	gmpr_update_all_group_source_oif(group, OIF_UPDATE);

    } else {

	/*
	 * Group is no longer active.  Delink the OIF and try to free
	 * the group.
	 */
	gmpr_update_group_oif(group, OIF_DELETE);
	gmpr_attempt_group_free(group);
    }
}


/*
 * gmpr_notify_oif_map_change_internal
 *
 * An OIF map has changed.  We need to reevaluate all of the groups and
 * sources on the interface.
 */
void
gmpr_notify_oif_map_change_internal (gmpr_intf *intf)
{
    gmpr_group *group;
    gmpr_group_addr_entry *group_addr;
    gmp_addr_list *source_list;
    gmp_addr_list_entry *addr_entry;

    /* Walk all of the groups on the interface. */

    group = NULL;
    while (TRUE) {

	/* Get the next group.  Bail if we've run out. */

	group = gmpr_next_intf_group(intf, group);
	if (!group)
	    break;

	/* Process only active groups. */

	if (gmpr_group_is_active(group)) {

	    /* Get the active source list. */

	    source_list = gmpr_group_source_list(group);

	    /* If there are sources on the list, process them. */

	    if (!gmp_addr_list_empty(source_list)) {

		addr_entry = NULL;
		while (TRUE) {
		    addr_entry =
			gmp_addr_list_next_entry(source_list, addr_entry);
		    group_addr = gmpr_addr_entry_to_group_entry(addr_entry);

		    /* Bail if out of sources. */

		    if (!group_addr)
			break;

		    /* Remap the source. */

		    gmpr_map_source_oif(group_addr);
		}

	    } else {

		/* It's a (*,G) entry.  Remap it. */

		gmpr_map_group_oif(group);
	    }

	    /* CAC wants host notifications for an oif change */

	    gmpr_host_notify_oif_map_change(group);
	}
    }

    /* Alert the clients to any changes. */

    gmpr_alert_clients(intf->rintf_instance);
}


/*
 * gmpr_update_intf_output_groups
 *
 * Update the output groups on an interface when the interface goes up
 * and down.
 *
 * We send delete notifications when they go down, and add
 * notifications when they come up (the notification code teases this
 * out from the interface status.)
 *
 * We only care about groups with a different output interface than
 * input interface, since the groups with the same interface will be
 * ripped out anyhow (since the input interface will be going down.)
 */
void
gmpr_update_intf_output_groups (gmpr_intf *intf)
{
    gmpr_ogroup *ogroup;
    gmpr_group *group;
    thread *thread_ptr;

    /* Walk all output groups on the interface. */

    ogroup = NULL;
    while (TRUE) {
	ogroup = gmpr_next_oif_group(intf, ogroup);
	if (!ogroup)
	    break;

	/*
	 * Got an output group.  Process it if any input interface doesn't
	 * match the output interface.
	 */
	thread_ptr = NULL;
	while (TRUE) {

	    /* Grab the next input group. */

	    thread_ptr = thread_circular_thread_next(&ogroup->rogroup_oif_head,
						     thread_ptr);
	    group = gmpr_oif_thread_to_group(thread_ptr);

	    /* Bail if we're done. */

	    if (!group)
		break;

	    /* Process it if the input interface doesn't match the oif. */

	    if (group->rgroup_intf != ogroup->rogroup_intf) {

		/*
		 * Interface mismatch.  If we're going down, flush all source
		 * notifications for the group and enqueue the group (which
		 * will turn into a group_delete notification.)  If we're
		 * coming up, enqueue the group and all of the sources.
		 */
		if (ogroup->rogroup_intf->rintf_up) {

		    /* Coming up. */

		    gmpr_group_notify_clients(ogroup);
		    gmpr_enqueue_all_source_notifications(ogroup, NULL);

		} else {

		    /* Going down. */

		    gmpr_flush_notifications_group(ogroup);
		    gmpr_group_notify_clients(ogroup);
		}

		/* No need to look at any further input groups. */

		break;
	    }
	}
    }
}


/*
 * gmpr_flush_intf_input_groups
 *
 * Flush the input groups from an interface when it goes down.
 */
void
gmpr_flush_intf_input_groups (gmpr_intf *intf)
{
    gmpr_group *group;

    /* Walk all input groups on the interface. */

    group = NULL;
    while (TRUE) {
	group = gmpr_next_intf_group(intf, group);
	if (!group)
	    break;

	/* Got a group.  Force an early timeout. */

	gmpr_timeout_group(group);
    }
}
