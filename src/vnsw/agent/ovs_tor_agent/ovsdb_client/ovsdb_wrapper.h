/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_WRAPPER_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_WRAPPER_H_

#include <stdlib.h>

struct ovsdb_wrapper_port_vlan_binding {
    int64_t vlan;
    struct ovsdb_idl_row *ls;
};

struct ovsdb_wrapper_port_vlan_stats {
    int64_t vlan;
    struct ovsdb_idl_row *stats;
};

/* Wrapper for C APIs */
struct ovsdb_idl * ovsdb_wrapper_idl_create();
void ovsdb_wrapper_idl_destroy(struct ovsdb_idl *idl);
const struct vteprec_global *ovsdb_wrapper_vteprec_global_first(struct ovsdb_idl *);
int ovsdb_wrapper_row_type(struct ovsdb_idl_row *row);
bool ovsdb_wrapper_msg_echo_req(struct jsonrpc_msg *msg);
bool ovsdb_wrapper_msg_echo_reply(struct jsonrpc_msg *msg);
struct json * ovsdb_wrapper_jsonrpc_clone_id(struct jsonrpc_msg *msg);
struct jsonrpc_msg* ovsdb_wrapper_jsonrpc_create_reply(struct jsonrpc_msg *msg);
struct jsonrpc_msg* ovsdb_wrapper_jsonrpc_create_echo_request();

void ovsdb_wrapper_idl_set_callback(struct ovsdb_idl *idl, void *idl_base,
        void (*cb)(void*, int, struct ovsdb_idl_row *),
        void (*ack_cb)(void*, struct ovsdb_idl_txn *));
struct jsonrpc_msg *ovsdb_wrapper_idl_encode_monitor_request(struct ovsdb_idl *);
bool ovsdb_wrapper_idl_msg_is_monitor_response(struct json *, struct jsonrpc_msg *);
void ovsdb_wrapper_idl_msg_process(struct ovsdb_idl *, struct jsonrpc_msg *msg);
struct json *ovsdb_wrapper_jsonrpc_msg_to_json(struct jsonrpc_msg *);
char *ovsdb_wrapper_json_to_string(const struct json *, int);
void ovsdb_wrapper_json_destroy(struct json *);
struct json_parser *ovsdb_wrapper_json_parser_create(int);
size_t ovsdb_wrapper_json_parser_feed(struct json_parser *, const char *, size_t);
bool ovsdb_wrapper_json_parser_is_done(const struct json_parser *);
struct json *ovsdb_wrapper_json_parser_finish(struct json_parser *);
char *ovsdb_wrapper_jsonrpc_msg_from_json(struct json *, struct jsonrpc_msg **);
void ovsdb_wrapper_jsonrpc_msg_destroy(struct jsonrpc_msg *msg);

struct ovsdb_idl_txn *ovsdb_wrapper_idl_txn_create(struct ovsdb_idl *idl);
void ovsdb_wrapper_idl_txn_destroy(struct ovsdb_idl_txn *txn);
bool ovsdb_wrapper_is_txn_success(struct ovsdb_idl_txn *txn);
const char *ovsdb_wrapper_txn_get_error(struct ovsdb_idl_txn *txn);
struct jsonrpc_msg *ovsdb_wrapper_idl_txn_encode(struct ovsdb_idl_txn *txn);

/* Physical Switch */
char *ovsdb_wrapper_physical_switch_name(struct ovsdb_idl_row *row);
const char *ovsdb_wrapper_physical_switch_tunnel_ip(struct ovsdb_idl_row *row);
size_t ovsdb_wrapper_physical_switch_ports_count(struct ovsdb_idl_row *row);
void ovsdb_wrapper_physical_switch_ports(struct ovsdb_idl_row *row,
                                         struct ovsdb_idl_row **ports,
                                         size_t n);

/* Logical Switch */
char *ovsdb_wrapper_logical_switch_name(struct ovsdb_idl_row *row);
int64_t ovsdb_wrapper_logical_switch_tunnel_key(struct ovsdb_idl_row *row);
struct ovsdb_idl_row *ovsdb_wrapper_add_logical_switch(struct ovsdb_idl_txn *,
        struct ovsdb_idl_row *, const char *, int64_t);
void ovsdb_wrapper_delete_logical_switch(struct ovsdb_idl_row *);

/* Physical Port */
char *ovsdb_wrapper_physical_port_name(struct ovsdb_idl_row *row);
size_t ovsdb_wrapper_physical_port_vlan_binding_count(struct ovsdb_idl_row *row);
void ovsdb_wrapper_physical_port_vlan_binding(struct ovsdb_idl_row *row,
        struct ovsdb_wrapper_port_vlan_binding*);
size_t ovsdb_wrapper_physical_port_vlan_stats_count(struct ovsdb_idl_row *row);
void ovsdb_wrapper_physical_port_vlan_stats(struct ovsdb_idl_row *row,
        struct ovsdb_wrapper_port_vlan_stats*);
void ovsdb_wrapper_update_physical_port(struct ovsdb_idl_txn *, struct ovsdb_idl_row *,
        struct ovsdb_wrapper_port_vlan_binding*, size_t binding_count);

/* Physical Locator */
char *ovsdb_wrapper_physical_locator_dst_ip(struct ovsdb_idl_row *row);
void ovsdb_wrapper_add_physical_locator(struct ovsdb_idl_txn *,
        struct ovsdb_idl_row *, const char *);
void ovsdb_wrapper_delete_physical_locator(struct ovsdb_idl_row *);

/* Physical Locator Set*/
size_t ovsdb_wrapper_physical_locator_set_locator_count(struct ovsdb_idl_row *row);
struct ovsdb_idl_row ** ovsdb_wrapper_physical_locator_set_locators(struct ovsdb_idl_row *row);

/* unicast mac local */
char *ovsdb_wrapper_ucast_mac_local_mac(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_local_ip(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_local_logical_switch(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_local_dst_ip(struct ovsdb_idl_row *row);
void ovsdb_wrapper_delete_ucast_mac_local(struct ovsdb_idl_row *row);

/* unicast mac remote */
void ovsdb_wrapper_add_ucast_mac_remote(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row, const char *mac, struct ovsdb_idl_row *ls,
        struct ovsdb_idl_row *pl, const char *dest_ip);
void ovsdb_wrapper_delete_ucast_mac_remote(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_remote_mac(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_remote_ip(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_remote_logical_switch(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_ucast_mac_remote_dst_ip(struct ovsdb_idl_row *row);

/* multicast mac local */
void ovsdb_wrapper_delete_mcast_mac_local(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_mcast_mac_local_mac(struct ovsdb_idl_row *row);
char *
ovsdb_wrapper_mcast_mac_local_logical_switch(struct ovsdb_idl_row *row);
struct ovsdb_idl_row *
ovsdb_wrapper_mcast_mac_local_physical_locator_set(struct ovsdb_idl_row *row);

/* multicast mac remote */
void ovsdb_wrapper_add_mcast_mac_remote(struct ovsdb_idl_txn *txn,
    struct ovsdb_idl_row *row, const char *mac, struct ovsdb_idl_row *ls,
    struct ovsdb_idl_row *pl, const char *dst_ip);
void ovsdb_wrapper_delete_mcast_mac_remote(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_mcast_mac_remote_mac(struct ovsdb_idl_row *row);
char *
ovsdb_wrapper_mcast_mac_remote_logical_switch(struct ovsdb_idl_row *row);
char *ovsdb_wrapper_mcast_mac_remote_dst_ip(struct ovsdb_idl_row *row);

/* logical binding stats */
void ovsdb_wrapper_get_logical_binding_stats(struct ovsdb_idl_row *row,
        int64_t *in_pkts, int64_t *in_bytes,
        int64_t *out_pkts, int64_t *out_bytes);

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_WRAPPER_H_

