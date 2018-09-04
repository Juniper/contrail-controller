/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_gmp_map_h
#define vnsw_agent_gmp_map_h

typedef enum {
    MCAST_AF_IPV4,
    MCAST_AF_FIRST = MCAST_AF_IPV4,
    MCAST_AF_IPV6,
    MCAST_AF_MAX
} mc_af;

typedef struct _mgm_global_data {
    void *gmp_sm;
    task *tp;
    mc_af mgm_gd_af;            /* Address family */

    boolean refresh_required;
    void *mgm_gmpr_instance;        /* GMP instance ID */
    void *mgm_gmpr_client;      /* GMP client ID */
} mgm_global_data;

#define IGMP_MAX_NOTIF_PER_PASS         10
#define IGMP_MAX_HOST_NOTIF_PER_PASS    50

#define MGM_GROUP_ADDED             1
#define MGM_GROUP_REMOVED           2
#define MGM_GROUP_SRC_REMOVED       3

extern mgm_global_data *gmp_init(mc_af mcast_af, task *tp, void *gmp_sm);
extern void gmp_deinit(mc_af mcast_af);
extern gmp_intf *gmp_attach_intf(mgm_global_data *gd, void *mif_state);
extern void gmp_detach_intf(mgm_global_data *gd, gmp_intf *gif);
extern boolean gmp_update_intf_state(mgm_global_data *gd, gmp_intf *gif,
                                const gmp_addr_string *intf_addr);
extern boolean gmp_update_intf_querying(mgm_global_data *gd, gmp_intf *gif,
                                boolean query);
extern boolean gmp_process_pkt(mgm_global_data *gd, gmp_intf *gif,
                        void *rcv_pkt, uint32_t packet_len,
                        const gmp_addr_string *src_addr,
                        const gmp_addr_string *dst_addr);

extern boolean gmp_oif_map_cb (void *inst_context UNUSED, gmp_intf_handle *handle,
                uint8_t *group_addr, uint8_t *source_addr,
                gmp_intf_handle **output_handle);
extern boolean gmp_policy_cb (void *inst_context, gmp_intf_handle *handle,
                uint8_t *group_addr, uint8_t *source_addr,
                boolean static_group);
extern void igmp_notification_ready (void *context);
extern void igmp_host_notification_ready (void *context);
extern void mgm_querier_change(void *cli_context UNUSED, gmp_intf_handle *handle,
                boolean querier, uint8_t *querier_addr);
extern boolean gmp_ssm_check_cb (void *inst_context UNUSED, gmp_intf_handle *handle,
                uint8_t *group_addr);
extern void gmp_xmit_ready(gmp_role role, gmp_proto proto, gmpx_intf_id intf_id);
extern void gmp_static_peek(gmp_intf_handle *handle, gmp_proto proto,
                 gmp_packet *rcv_packet);

extern boolean gmp_policy_check(mgm_global_data *gd, gmp_intf *intf,
                            gmp_addr_string source, gmp_addr_string group);
extern void gmp_group_notify(mgm_global_data *gd, gmp_intf *gif,
                            int group_action, gmp_addr_string source,
                            gmp_addr_string group);
extern void gmp_cache_resync_notify(mgm_global_data *gd, gmp_intf *gif,
                            gmp_addr_string source, gmp_addr_string group);
extern void gmp_host_update(mgm_global_data *gd, gmp_intf *intf, boolean join,
                            gmp_addr_string host, gmp_addr_string source,
                            gmp_addr_string group);
extern void gmp_notification_ready(mgm_global_data *gd);
extern boolean gmp_notification_handler(mgm_global_data *gd);
extern uint8_t *gmp_get_send_buffer(mgm_global_data *gd, gmp_intf *intf);
extern void gmp_free_send_buffer(mgm_global_data *gd, gmp_intf *intf,
                            uint8_t *buffer);
extern void gmp_send_one_packet(mgm_global_data *gd, gmp_intf *intf,
                            uint8_t *pkt, uint32_t pkt_len,
                            gmp_addr_string dest);

#endif /* vnsw_agent_gmp_map_h */
