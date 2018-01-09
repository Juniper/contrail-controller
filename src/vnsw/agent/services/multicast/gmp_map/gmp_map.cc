/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "task_map.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mcast_common.h"

#include "task_map.h"
#include "task_thread_api.h"
#include "patricia_api.h"

#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_router.h"
#include "igmp_private.h"
#include "igmp_protocol.h"
#include "gmp_externs.h"

#include "gmp_intf.h"
#include "gmp_map.h"
#ifdef __cplusplus
}
#endif

uint32_t igmp_sg_add_count = 0;
uint32_t igmp_sg_del_count = 0;
uint32_t igmp_host_update_count = 0;

static gmpr_instance_context gmp_inst_ctx = {
    .rctx_oif_map_cb = gmp_oif_map_cb,
    .rctx_policy_cb = gmp_policy_cb,
    .rctx_ssm_check_cb = gmp_ssm_check_cb
};

static gmpr_client_context igmp_client_context = {
    .rctx_notif_cb = igmp_notification_ready,
    .rctx_host_notif_cb = igmp_host_notification_ready,
    .rctx_querier_cb = mgm_querier_change,
    .rctx_delta_notifications = TRUE,
};

mgm_global_data *gmp_init(mc_af mcast_af, task *tp, void *gmp_sm)
{
    gmp_proto proto;
    mgm_global_data *gd;

    gd = (mgm_global_data *)malloc(sizeof(mgm_global_data));
    if (!gd) {
        return NULL;
    }

    bzero(gd, sizeof(mgm_global_data));
    if (mcast_af == MCAST_AF_IPV4) {
        proto = GMP_PROTO_IGMP;
    } else {
        return NULL;
    }

    gd->gmp_sm = gmp_sm;
    gd->tp = tp;
    gd->mgm_gd_af = mcast_af;

    gmp_register_io(GMP_ROLE_ROUTER, proto, gmp_xmit_ready);
    gmp_register_io(GMP_ROLE_HOST, proto, gmp_xmit_ready);
    gmp_register_peek_function(GMP_ROLE_ROUTER, gmp_static_peek,
                                   gmp_static_peek);

    gd->mgm_gmpr_instance = gmpr_create_instance(proto, gd, &gmp_inst_ctx);
    gd->mgm_gmpr_client = gmpr_register(gd->mgm_gmpr_instance, gd,
                            &igmp_client_context);
    return gd;
}

void gmp_deinit(mgm_global_data *gd)
{
    if (!gd) {
        return;
    }

    gmpr_detach(gd->mgm_gmpr_client);
    gmpr_destroy_instance(gd->mgm_gmpr_instance);

    gd->mgm_gmpr_client = NULL;
    gd->mgm_gmpr_instance = NULL;

    free(gd);

    return;
}

void gmp_set_intf_params(mgm_global_data *gd, gmp_intf *gif)
{
    gmpr_intf_params params;
    gmp_intf_handle *handle;

    params.gmpr_ifparm_version = IGMP_VERSION_3;
    params.gmpr_ifparm_fast_leave = FALSE;
    params.gmpr_ifparm_passive_receive = FALSE;
    params.gmpr_ifparm_suppress_gen_query = FALSE;
    params.gmpr_ifparm_suppress_gs_query = FALSE;
    params.gmpr_ifparm_chan_limit = 0;
    params.gmpr_ifparm_chan_threshold = 100;
    params.gmpr_ifparm_log_interval = 0;
    params.gmpr_ifparm_robustness = IGMP_ROBUST_COUNT;
    params.gmpr_ifparm_qivl = IGMP_QUERY_INTERVAL;
    params.gmpr_ifparm_qrivl = IGMP_QUERY_RESPONSE_INTERVAL;
    params.gmpr_ifparm_lmqi = IGMP_QUERY_LASTMEMBER_INTERVAL;

    handle = gmp_gif_to_handle(gif);
    gmpr_set_intf_params(gd->mgm_gmpr_instance, handle, &params);
    return;
}

gmp_intf *gmp_attach_intf(mgm_global_data *gd, void *mif_state)
{
    gmp_intf *gif;

    if (!gd) {
        return 0;
    }

    gif = (gmp_intf *)malloc(sizeof(gmp_intf));
    if (!gif) {
        return 0;
    }

    gif->vm_interface = mif_state;
    if (gd->mgm_gd_af == MCAST_AF_IPV4) {
        gif->gmpif_proto = GMP_PROTO_IGMP;
    } else {
        return 0;
    }

    gif->gmpif_handle.gmpifh_host = FALSE;
    bzero(&gif->gmpif_handle.gmpifh_xmit_thread, sizeof(thread));
    gmpr_attach_intf(gd->mgm_gmpr_instance, &gif->gmpif_handle);
    gmp_set_intf_params(gd, gif);

    return gif;
}

void gmp_detach_intf(mgm_global_data *gd, gmp_intf *gif)
{
    if (!gd) {
        return;
    }

    gmpr_detach_intf(gd->mgm_gmpr_instance, &gif->gmpif_handle);

    free(gif);

    return;
}

boolean gmp_update_intf_state(mgm_global_data *gd, gmp_intf *gif,
                                const u_int8_t *intf_addr)
{
    gmpr_update_intf_state(gd->mgm_gmpr_instance, gmp_gif_to_handle(gif),
                                (const u_int8_t *)&intf_addr);
    return TRUE;
}

boolean gmp_process_pkt(mgm_global_data *gd, gmp_intf *gif,
                        void *rcv_pkt, u_int32_t packet_len,
                        const u_int8_t *src_addr, const u_int8_t *dst_addr)
{
    boolean parse_ok = FALSE;

    if (gd->mgm_gd_af == MCAST_AF_IPV4) {
        parse_ok = igmp_process_pkt(rcv_pkt, src_addr, dst_addr, packet_len,
                        gmp_gif_to_handle(gif), false, NULL, 0);
    }

    return parse_ok;
}

boolean gmp_oif_map_cb(void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr, u_int8_t *source_addr,
                gmp_intf_handle **output_handle)
{
    *output_handle = handle;
    return TRUE;
}

boolean gmp_policy_cb(void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr, u_int8_t *source_addr,
                boolean static_group)
{
    return TRUE;
}

boolean gmp_ssm_check_cb(void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr)
{
    return TRUE;
}

typedef enum {
    MC_GROUP_TYPE_NONE = 0,             /* Don't care, not an IGMP/MLD group */
    MC_GROUP_TYPE_STATIC,               /* IGMP/MLD configured static groups */
    MC_GROUP_TYPE_DYNAMIC,              /* IGMP/MLD learned groups */
    MC_GROUP_TYPE_LOCAL                 /* Box join */
} mc_group_type_t;

#define IGMP_MAX_NOTIF_PER_PASS         10
#define IGMP_MAX_HOST_NOTIF_PER_PASS    50

#define MGM_GROUP_ADDED             1
#define MGM_GROUP_REMOVED           2
#define MGM_GROUP_SRC_REMOVED       3

void mc_group_notify(gmp_intf *gif, u_int8_t group_action, u_int32_t s,
                        u_int32_t g, mc_group_type_t group_type)
{
    if (s) {
        if (group_action == MGM_GROUP_ADDED) {
            igmp_sg_add_count++;
        } else if (group_action == MGM_GROUP_REMOVED) {
            igmp_sg_del_count++;
        }
    }
}

void mc_cache_resync_notify(gmp_intf *gif, u_int32_t s, u_int32_t g)
{
}

void igmp_notification_ready(void *context)
{
    gmpr_client_notification *notification;
    gmp_intf *gif;
    gmp_intf_handle *handle;
    u_int32_t g = 0;
    u_int32_t s = 0;
    mc_group_type_t group_type;
    mgm_global_data *gd;
    int notif_count = IGMP_MAX_NOTIF_PER_PASS;

    gd = (mgm_global_data *)context;

    /*
     * Pull notifications until we run out, hit our max count, or the 
     * protocol is busy.
     *
     * NOTE: We might go over the max notifications per pass,
     * ie. notif_count might be -ve, because we need to make sure that
     * the notifications for the (s,g) of the same group are being
     * processed in the same batch.  For details, please see PR
     * 509013.
     */
    notification = NULL;
    while (notif_count-- > 0 || !gmpr_notification_last_sg(notification)) {
        notification =
            gmpr_get_notification(gd->mgm_gmpr_client, notification);
        if (!notification)
            break;

        /*
         * Process received notification...
         */
        handle = notification->notif_intf_id;
        gif = gmp_handle_to_gif(handle);

        group_type = MC_GROUP_TYPE_DYNAMIC;

        g = *(u_int32_t *)&notification->notif_group_addr.gmp_addr;
        s = *(u_int32_t *)&notification->notif_source_addr.gmp_addr;

        switch (notification->notif_type) {
            case GMPR_NOTIF_ALLOW_SOURCE:
                if (notification->notif_filter_mode ==
                        GMP_FILTER_MODE_INCLUDE) {
                    mc_group_notify(gif, MGM_GROUP_ADDED, s, g, group_type);
                } else {
                    mc_cache_resync_notify(gif, s, g);
                }
                break;

            case GMPR_NOTIF_BLOCK_SOURCE:
                if (notification->notif_filter_mode ==
                        GMP_FILTER_MODE_INCLUDE) {
                    mc_group_notify(gif, MGM_GROUP_REMOVED, s, g, group_type);
                } else {
                    mc_cache_resync_notify(gif, s, g);
                }
                break;

            case GMPR_NOTIF_GROUP_DELETE:
                mc_group_notify(gif, MGM_GROUP_SRC_REMOVED,
                            s, g, group_type);
                mc_group_notify(gif, MGM_GROUP_REMOVED, s, g, group_type);

                break;

            case GMPR_NOTIF_GROUP_ADD_EXCL:
                /* 
                 * Check if PIM requested a refresh.
                 * If so we don't send a group and source REMOVE
                 * otherwise PIM will clear its state 
                 */
                if (gd->refresh_required == FALSE) {
                    mc_group_notify(gif, MGM_GROUP_SRC_REMOVED, s, g,
                            group_type);
                    mc_group_notify(gif, MGM_GROUP_ADDED, s, g, group_type);
                }

                break;

            case GMPR_NOTIF_GROUP_ADD_INCL:
                /* 
                 * Check if PIM requested a refresh.
                 * If so we don't send a group and source REMOVE
                 * otherwise PIM will clear its state 
                 */
                if (gd->refresh_required == FALSE) {
                    mc_group_notify(gif, MGM_GROUP_SRC_REMOVED, s, g,
                            group_type);
                    mc_group_notify(gif, MGM_GROUP_REMOVED, s, g, group_type);
                }

                break;

            case GMPR_NOTIF_REFRESH_END:
                gd->refresh_required = FALSE;
                break;

            default:
                /* We should never get here */
                assert(0);
        }
    }

    /*
     * If there's a notification pointer, it means that we bailed out due 
     * to hitting the notification count limit or someone is too busy to 
     * process the updates.
     *
     * Either way, we explicitly toss the notification block and then fall
     * out, which causes the job to be requeued.
     *
     * On the other hand, if the notification pointer is NULL, it means that
     * we drained the queue, in which case we should kill the job.
     */
     if (notification) {
         gmpr_return_notification(notification);
     }
     return;
}

void mc_host_update(gmp_intf *intf, u_int32_t host, u_int32_t source,
                        u_int32_t group, boolean join_leave)
{
    igmp_host_update_count++;
}

void igmp_host_notification_ready(void_t context)
{
    gmpr_client_host_notification *gmpr_notif = NULL;
    gmp_intf *gif;
    gmp_intf_handle *handle;
    mgm_global_data *gd;
    u_int32_t g = 0;
    u_int32_t s = 0;
    u_int32_t h = 0;
    int notif_count = IGMP_MAX_HOST_NOTIF_PER_PASS;

    gd = (mgm_global_data *)context;

    /*
     *  Do this a limited number of times.
     */
    while (notif_count--) {
        /*
         *  Stop if there are no more notifications.
         */
        gmpr_notif =
            gmpr_get_host_notification(gd->mgm_gmpr_client, gmpr_notif);
        if (!gmpr_notif) {
            break;
        }

        /*
         *  Skip NULL MIFs (local groups).
         */
        handle = gmpr_notif->host_notif_intf_id;
        gif = gmp_handle_to_gif(handle);
        if (!gif) {
            continue;
        }

        /*
         *  Get needed addresses.
         */
        g = *(u_int32_t *)&gmpr_notif->host_notif_group_addr.gmp_addr;
        s = *(u_int32_t *)&gmpr_notif->host_notif_source_addr.gmp_addr;
        h = *(u_int32_t *)&gmpr_notif->host_notif_host_addr.gmp_addr;

        /*
         *  Update mapped OIF module.
         */
        switch (gmpr_notif->host_notif_type) {
            case GMPR_NOTIF_HOST_UNKNOWN:
                break;

            case GMPR_NOTIF_HOST_JOIN:
                /*
                 *  This is a join.
                 */
                mc_host_update(gif, h, s, g, TRUE);
            break;

            case GMPR_NOTIF_HOST_LEAVE:
            case GMPR_NOTIF_HOST_TIMEOUT:
            case GMPR_NOTIF_HOST_IFDOWN:                
            /*
             *  This is a leave.
             */
                mc_host_update(gif, h, s, g, FALSE);
            break;

            default:
                assert(0);
            break;
        }
    }

    /*
     * If there's a notification pointer, it means that we bailed out
     * due to hitting the notification count limit.  In that case we
     * explicitly toss the notification block and then fall out, which
     * causes the job to be requeued.
     *
     * On the other hand, if the notification pointer is NULL, it means that
     * we drained the queue, in which case we should kill the job.
     */
    if (gmpr_notif) {
        gmpr_return_host_notification(gmpr_notif);
    }
    return;
}

void mgm_querier_change(void_t cli_context UNUSED, gmp_intf_handle *handle,
                boolean querier, u_int8_t *querier_addr)
{
    return;
}

void gmpx_trace(void *context, const char *parms, ...)
{
    return;
}

void gmpx_post_event(void *context, gmpx_event_type ev,
                const void *parms, ...)
{
    return;
}

void gmp_xmit_ready(gmp_role role, gmp_proto proto, gmpx_intf_id intf_id)
{
    return;
}

void gmp_static_peek(gmp_intf_handle *handle, gmp_proto proto,
                 gmp_packet *rcv_packet)
{
    return;
}

