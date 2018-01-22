/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "task_map.h"

#ifdef __cplusplus
extern "C" {
#endif
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

extern void gmp_set_def_ipv4_ivl_params(uint32_t robust_count, uint32_t qivl,
                        uint32_t qrivl, uint32_t lmqi);
extern void gmp_set_def_intf_params(mc_af mcast_af);
#ifdef __cplusplus
}
#endif

mgm_global_data mgm_global[MCAST_AF_MAX];
gmpr_intf_params def_gmpr_intf_params[MCAST_AF_MAX];

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

void gmp_set_def_ipv4_ivl_params(uint32_t robust_count, uint32_t qivl,
                        uint32_t qrivl, uint32_t lmqi)
{
    gmpr_intf_params *params = NULL;

    params = &def_gmpr_intf_params[MCAST_AF_IPV4];
    params->gmpr_ifparm_robustness = robust_count;
    params->gmpr_ifparm_qivl = qivl;
    params->gmpr_ifparm_qrivl = qrivl;
    params->gmpr_ifparm_lmqi = lmqi;

    return;
}

void gmp_set_def_intf_params(mc_af mcast_af)
{
    gmpr_intf_params *params = NULL;

    if (mcast_af == MCAST_AF_IPV4) {

        params = &def_gmpr_intf_params[mcast_af];
        params->gmpr_ifparm_version = IGMP_VERSION_3;
        gmp_set_def_ipv4_ivl_params(IGMP_ROBUST_COUNT, IGMP_QUERY_INTERVAL,
                            IGMP_QUERY_RESPONSE_INTERVAL,
                            IGMP_QUERY_LASTMEMBER_INTERVAL);
    } else {
        return;
    }

    params->gmpr_ifparm_fast_leave = FALSE;
    params->gmpr_ifparm_passive_receive = FALSE;
    params->gmpr_ifparm_suppress_gen_query = FALSE;
    params->gmpr_ifparm_suppress_gs_query = FALSE;
    params->gmpr_ifparm_chan_limit = 0;
    params->gmpr_ifparm_chan_threshold = 100;
    params->gmpr_ifparm_log_interval = 0;

    return;
}

mgm_global_data *gmp_init(mc_af mcast_af, task *tp, void *gmp_sm)
{
    gmp_proto proto;
    mgm_global_data *gd;

    if (mcast_af == MCAST_AF_IPV4) {
        proto = GMP_PROTO_IGMP;
    } else {
        return NULL;
    }

    gd = &mgm_global[mcast_af];
    gd->gmp_sm = gmp_sm;
    gd->tp = tp;
    gd->mgm_gd_af = mcast_af;

    gmp_register_io(GMP_ROLE_ROUTER, proto, gmp_xmit_ready);
    gmp_register_peek_function(GMP_ROLE_ROUTER, gmp_static_peek,
                                   gmp_static_peek);

    gd->mgm_gmpr_instance = gmpr_create_instance(proto, gd, &gmp_inst_ctx);
    gd->mgm_gmpr_client = gmpr_register(gd->mgm_gmpr_instance, gd,
                            &igmp_client_context);
    gmp_set_def_intf_params(mcast_af);

    return gd;
}

void gmp_deinit(mc_af mcast_af)
{
    mgm_global_data *gd;

    gd = &mgm_global[mcast_af];
    gmpr_detach(gd->mgm_gmpr_client);
    gmpr_destroy_instance(gd->mgm_gmpr_instance);

    gd->mgm_gmpr_client = NULL;
    gd->mgm_gmpr_instance = NULL;

    return;
}

void gmp_set_intf_params(mgm_global_data *gd, gmp_intf *gif)
{
    gmpr_intf_params *params;
    gmp_intf_handle *handle;

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
        return;
    }

    params = &def_gmpr_intf_params[gd->mgm_gd_af];
    handle = gmp_gif_to_handle(gif);
    gmpr_set_intf_params(gd->mgm_gmpr_instance, handle, params);
    return;
}

gmp_intf *gmp_attach_intf(mgm_global_data *gd, void *mif_state)
{
    gmp_intf *gif = NULL;

    if (!gd) {
        return NULL;
    }

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
        return NULL;
    }

    gif = (gmp_intf *)malloc(sizeof(gmp_intf));
    if (!gif) {
        return NULL;
    }

    gif->vm_interface = mif_state;
    gif->gmpif_proto = GMP_PROTO_IGMP;
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
                                const gmp_addr_string *intf_addr)
{
    gmpr_update_intf_state(gd->mgm_gmpr_instance, gmp_gif_to_handle(gif),
                                (const u_int8_t *)intf_addr);
    return TRUE;
}

boolean gmp_process_pkt(mgm_global_data *gd, gmp_intf *gif,
                        void *rcv_pkt, u_int32_t packet_len,
                        const gmp_addr_string *src_addr,
                        const gmp_addr_string *dst_addr)
{
    boolean parse_ok = FALSE;

    if (gd->mgm_gd_af == MCAST_AF_IPV4) {
        parse_ok = igmp_process_pkt(rcv_pkt, src_addr->gmp_v4_addr,
                        dst_addr->gmp_v4_addr, packet_len,
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

void igmp_notification_ready(void *context)
{
    gmpr_client_notification *notification;
    gmp_intf *gif;
    gmp_intf_handle *handle;
    gmp_addr_string g;
    gmp_addr_string s;
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

        memcpy(&g, notification->notif_group_addr.gmp_addr, IPV4_ADDR_LEN);
        memcpy(&s, notification->notif_source_addr.gmp_addr, IPV4_ADDR_LEN);

        switch (notification->notif_type) {
            case GMPR_NOTIF_ALLOW_SOURCE:
                if (notification->notif_filter_mode ==
                        GMP_FILTER_MODE_INCLUDE) {
                    gmp_group_notify(gd, gif, MGM_GROUP_ADDED, s, g);
                } else {
                    gmp_cache_resync_notify(gd, gif, s, g);
                }
                break;

            case GMPR_NOTIF_BLOCK_SOURCE:
                if (notification->notif_filter_mode ==
                        GMP_FILTER_MODE_INCLUDE) {
                    gmp_group_notify(gd, gif, MGM_GROUP_REMOVED, s, g);
                } else {
                    gmp_cache_resync_notify(gd, gif, s, g);
                }
                break;

            case GMPR_NOTIF_GROUP_DELETE:
                gmp_group_notify(gd, gif, MGM_GROUP_SRC_REMOVED, s, g);
                gmp_group_notify(gd, gif, MGM_GROUP_REMOVED, s, g);
                break;

            case GMPR_NOTIF_GROUP_ADD_EXCL:
                /*
                 * Check if PIM requested a refresh.
                 * If so we don't send a group and source REMOVE
                 * otherwise PIM will clear its state
                 */
                if (gd->refresh_required == FALSE) {
                    gmp_group_notify(gd, gif, MGM_GROUP_SRC_REMOVED, s, g);
                }
                gmp_group_notify(gd, gif, MGM_GROUP_ADDED, s, g);
                break;

            case GMPR_NOTIF_GROUP_ADD_INCL:
                /*
                 * Check if PIM requested a refresh.
                 * If so we don't send a group and source REMOVE
                 * otherwise PIM will clear its state
                 */
                if (gd->refresh_required == FALSE) {
                    gmp_group_notify(gd, gif, MGM_GROUP_SRC_REMOVED, s, g);
                    gmp_group_notify(gd, gif, MGM_GROUP_REMOVED, s, g);
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

void igmp_host_notification_ready(void_t context)
{
    gmpr_client_host_notification *gmpr_notif = NULL;
    gmp_intf *gif;
    gmp_intf_handle *handle;
    mgm_global_data *gd;
    gmp_addr_string g;
    gmp_addr_string s;
    gmp_addr_string h;
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
        memcpy(&g, gmpr_notif->host_notif_group_addr.gmp_addr, IPV4_ADDR_LEN);
        memcpy(&s, gmpr_notif->host_notif_source_addr.gmp_addr, IPV4_ADDR_LEN);
        memcpy(&h, gmpr_notif->host_notif_host_addr.gmp_addr, IPV4_ADDR_LEN);

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
                gmp_host_update(gd, gif, h, s, g, TRUE);
                break;

            case GMPR_NOTIF_HOST_LEAVE:
            case GMPR_NOTIF_HOST_TIMEOUT:
            case GMPR_NOTIF_HOST_IFDOWN:
                /*
                 *  This is a leave.
                 */
                gmp_host_update(gd, gif, h, s, g, FALSE);
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

