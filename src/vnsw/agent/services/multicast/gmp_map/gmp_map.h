/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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

extern mgm_global_data *gmp_init(mc_af mcast_af, task *tp, void *gmp_sm);
extern void gmp_deinit(mgm_global_data *gd);
extern gmp_intf *gmp_attach_intf(mgm_global_data *gd, void *mif_state);
extern void gmp_detach_intf(mgm_global_data *gd, gmp_intf *gif);
extern boolean gmp_update_intf_state(mgm_global_data *gd, gmp_intf *gif,
                                const u_int8_t *intf_addr);
extern boolean gmp_process_pkt(mgm_global_data *gd, gmp_intf *gif,
                        void *rcv_pkt, u_int32_t packet_len,
                        const u_int8_t *src_addr, const u_int8_t *dst_addr);

extern boolean gmp_oif_map_cb (void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr, u_int8_t *source_addr,
                gmp_intf_handle **output_handle);
extern boolean gmp_policy_cb (void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr, u_int8_t *source_addr,
                boolean static_group);
extern void igmp_notification_ready (void_t context);
extern void igmp_host_notification_ready (void_t context);
extern void mgm_querier_change(void_t cli_context UNUSED, gmp_intf_handle *handle,
                boolean querier, u_int8_t *querier_addr);
extern boolean gmp_ssm_check_cb (void_t inst_context UNUSED, gmp_intf_handle *handle,
                u_int8_t *group_addr);
extern void gmp_xmit_ready(gmp_role role, gmp_proto proto, gmpx_intf_id intf_id);
extern void gmp_static_peek(gmp_intf_handle *handle, gmp_proto proto,
                 gmp_packet *rcv_packet);

#endif /* vnsw_agent_gmp_map_h */
