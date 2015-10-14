/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#define OPEN_CONTRAIL_CLIENT
#include <util.h>
#include <json.h>
#include <jsonrpc.h>
#include <vtep-idl.h>
#include <ovsdb-idl.h>

#include <ovsdb_wrapper.h>

#define CONTRAIL_LOGICAL_SWITCH_NAME "Contrail-"
#define CONTRAIL_LS_NAME_LENGTH 9

void ovsdb_idl_set_callback(struct ovsdb_idl *idl, void *idl_base,
        idl_callback cb, txn_ack_callback ack_cb);
struct jsonrpc_msg * ovsdb_idl_encode_monitor_request(struct ovsdb_idl *idl);
void ovsdb_idl_msg_process(struct ovsdb_idl *idl, struct jsonrpc_msg *msg);
struct jsonrpc_msg * ovsdb_idl_txn_encode(struct ovsdb_idl_txn *txn);

struct ovsdb_idl *
ovsdb_wrapper_idl_create()
{
    vteprec_init();
    return ovsdb_idl_create(NULL, &vteprec_idl_class, true, false);
}

void
ovsdb_wrapper_idl_destroy(struct ovsdb_idl *idl)
{
    ovsdb_idl_destroy(idl);
}

const struct vteprec_global *
ovsdb_wrapper_vteprec_global_first(struct ovsdb_idl *idl)
{
    return vteprec_global_first(idl);
}

int
ovsdb_wrapper_row_type(struct ovsdb_idl_row *row) {
    if (row->table->class->columns == vteprec_physical_switch_columns) {
        return 0;
    } else if (row->table->class->columns == vteprec_logical_switch_columns) {
        return 1;
    } else if (row->table->class->columns == vteprec_physical_port_columns) {
        return 2;
    } else if (row->table->class->columns == vteprec_physical_locator_columns) {
        return 3;
    } else if (row->table->class->columns == vteprec_ucast_macs_local_columns) {
        return 4;
    } else if (row->table->class->columns == vteprec_ucast_macs_remote_columns) {
        return 5;
    } else if (row->table->class->columns == vteprec_physical_locator_set_columns) {
        return 6;
    } else if (row->table->class->columns == vteprec_mcast_macs_local_columns) {
        return 7;
    } else if (row->table->class->columns == vteprec_mcast_macs_remote_columns) {
        return 8;
    }
    return 100;
}

bool
ovsdb_wrapper_msg_echo_req(struct jsonrpc_msg *msg) {
    return (msg->type == JSONRPC_REQUEST && !strcmp(msg->method, "echo"));
}

bool
ovsdb_wrapper_msg_echo_reply(struct jsonrpc_msg *msg) {
    return (msg->type == JSONRPC_REPLY && msg->id &&
            msg->id->type == JSON_STRING && !strcmp(msg->id->u.string, "echo"));
}

struct json *
ovsdb_wrapper_jsonrpc_clone_id(struct jsonrpc_msg *msg) {
    if (msg->id == NULL) {
        return NULL;
    }

    return json_clone(msg->id);
}

struct jsonrpc_msg *
ovsdb_wrapper_jsonrpc_create_reply(struct jsonrpc_msg *msg) {
    return jsonrpc_create_reply(json_clone(msg->params), msg->id);
}

// Creates OVSDB echo request.
struct jsonrpc_msg *
ovsdb_wrapper_jsonrpc_create_echo_request() {
    struct jsonrpc_msg *request =
        jsonrpc_create_request("echo", json_array_create_empty(), NULL);
    // set the id to be echo request
    request->id = json_string_create("echo");
    return request;
}

void
ovsdb_wrapper_idl_set_callback(struct ovsdb_idl *idl, void *idl_base,
        idl_callback cb, txn_ack_callback ack_cb)
{
    ovsdb_idl_set_callback(idl, idl_base, cb, ack_cb);
}

struct jsonrpc_msg *
ovsdb_wrapper_idl_encode_monitor_request(struct ovsdb_idl *idl)
{
    return ovsdb_idl_encode_monitor_request(idl);
}

bool
ovsdb_wrapper_idl_msg_is_monitor_response(struct json *monitor_request_id,
                                          struct jsonrpc_msg *msg)
{
    if (msg->type == JSONRPC_REPLY &&
        monitor_request_id != NULL &&
        json_equal(monitor_request_id, msg->id)) {
        return true;
    }

    return false;
}

void
ovsdb_wrapper_idl_msg_process(struct ovsdb_idl *idl, struct jsonrpc_msg *msg)
{
    ovsdb_idl_msg_process(idl, msg);
}

struct json *
ovsdb_wrapper_jsonrpc_msg_to_json(struct jsonrpc_msg *msg)
{
    return jsonrpc_msg_to_json(msg);
}

char *
ovsdb_wrapper_json_to_string(const struct json *msg, int flag)
{
    return json_to_string(msg, flag);
}

void
ovsdb_wrapper_json_destroy(struct json *msg)
{
    json_destroy(msg);
}

struct json_parser *
ovsdb_wrapper_json_parser_create(int flag)
{
    return json_parser_create(flag);
}

size_t
ovsdb_wrapper_json_parser_feed(struct json_parser *parser, const char *msg,
        size_t len)
{
    return json_parser_feed(parser, msg, len);
}

bool
ovsdb_wrapper_json_parser_is_done(const struct json_parser *parser)
{
    return json_parser_is_done(parser);
}

struct json *
ovsdb_wrapper_json_parser_finish(struct json_parser *parser)
{
    return json_parser_finish(parser);
}

char *
ovsdb_wrapper_jsonrpc_msg_from_json(struct json *msg, struct jsonrpc_msg **rpc)
{
    return jsonrpc_msg_from_json(msg, rpc);
}

void
ovsdb_wrapper_jsonrpc_msg_destroy(struct jsonrpc_msg *msg)
{
    jsonrpc_msg_destroy(msg);
}

struct ovsdb_idl_txn *
ovsdb_wrapper_idl_txn_create(struct ovsdb_idl *idl)
{
    return ovsdb_idl_txn_create(idl);
}

void
ovsdb_wrapper_idl_txn_destroy(struct ovsdb_idl_txn *txn)
{
    ovsdb_idl_txn_destroy(txn);
}

bool
ovsdb_wrapper_is_txn_success(struct ovsdb_idl_txn *txn)
{
    return ovsdb_idl_is_txn_success(txn);
}

const char *
ovsdb_wrapper_txn_get_error(struct ovsdb_idl_txn *txn)
{
    return ovsdb_idl_txn_get_error(txn);
}

struct jsonrpc_msg *
ovsdb_wrapper_idl_txn_encode(struct ovsdb_idl_txn *txn)
{
    return ovsdb_idl_txn_encode(txn);
}

/* physical switch */
char *
ovsdb_wrapper_physical_switch_name(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_switch *ps =
        row ? CONTAINER_OF(row, struct vteprec_physical_switch, header_) : NULL;
    return ps->name;
}

const char *
ovsdb_wrapper_physical_switch_tunnel_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_switch *ps =
        row ? CONTAINER_OF(row, struct vteprec_physical_switch, header_) : NULL;
    if (ps->n_tunnel_ips == 0) {
        return "0.0.0.0";
    }
    /* return the first tunnel ip. */
    return ps->tunnel_ips[0];
}

size_t
ovsdb_wrapper_physical_switch_ports_count(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_switch *ps =
        row ? CONTAINER_OF(row, struct vteprec_physical_switch, header_) : NULL;
    if (ps == NULL) {
        return 0;
    }
    return ps->n_ports;
}

void
ovsdb_wrapper_physical_switch_ports(struct ovsdb_idl_row *row,
                                    struct ovsdb_idl_row **ports, size_t n)
{
    struct vteprec_physical_switch *ps =
        row ? CONTAINER_OF(row, struct vteprec_physical_switch, header_) : NULL;
    if (ps == NULL) {
        return;
    }
    size_t count = (n > ps->n_ports) ? ps->n_ports : n;
    size_t i = 0;
    while (i < count) {
        ports[i] = &(ps->ports[i]->header_);
        i++;
    }
}

/* logical switch */
char *
ovsdb_wrapper_logical_switch_name(struct ovsdb_idl_row *row)
{
    struct vteprec_logical_switch *ls =
        row ? CONTAINER_OF(row, struct vteprec_logical_switch, header_) : NULL;
    if (strncmp(ls->name, CONTRAIL_LOGICAL_SWITCH_NAME,
                CONTRAIL_LS_NAME_LENGTH) == 0) {
        /* Skip "Contrail-" from logical-switch name */
        return &(ls->name[CONTRAIL_LS_NAME_LENGTH]);
    }
    /* return the complete name for non-contrail logical-switch */
    return ls->name;
}

int64_t
ovsdb_wrapper_logical_switch_tunnel_key(struct ovsdb_idl_row *row)
{
    struct vteprec_logical_switch *ls =
        row ? CONTAINER_OF(row, struct vteprec_logical_switch, header_) : NULL;
    if (ls->n_tunnel_key == 0) {
        return 0;
    }
    return ls->tunnel_key[0];
}

struct ovsdb_idl_row *
ovsdb_wrapper_add_logical_switch(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row, const char *name, int64_t vxlan)
{
    struct vteprec_logical_switch *ls =
        row ? CONTAINER_OF(row, struct vteprec_logical_switch, header_) : NULL;
    if (ls == NULL)
        ls = vteprec_logical_switch_insert(txn);
    /* prepend "Contrail-" in the logical-switch name */
    char contrail_name[256];
    strcpy(contrail_name, CONTRAIL_LOGICAL_SWITCH_NAME);
    strncat(contrail_name, name, 256 - CONTRAIL_LS_NAME_LENGTH);
    vteprec_logical_switch_set_name(ls, contrail_name);
    vteprec_logical_switch_set_tunnel_key(ls, &vxlan, 1);
    return &(ls->header_);
}

void
ovsdb_wrapper_delete_logical_switch(struct ovsdb_idl_row *row)
{
    struct vteprec_logical_switch *ls =
        CONTAINER_OF(row, struct vteprec_logical_switch, header_);
    vteprec_logical_switch_delete(ls);
}

/* physical port */
char *
ovsdb_wrapper_physical_port_name(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_port *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_port, header_) : NULL;
    return p->name;
}

size_t
ovsdb_wrapper_physical_port_vlan_binding_count(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_port *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_port, header_) : NULL;
    return p->n_vlan_bindings;
}

void
ovsdb_wrapper_physical_port_vlan_binding(struct ovsdb_idl_row *row,
        struct ovsdb_wrapper_port_vlan_binding *binding)
{
    struct vteprec_physical_port *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_port, header_) : NULL;
    size_t count = p->n_vlan_bindings;
    size_t i = 0;
    while (i < count) {
        binding[i].vlan = p->key_vlan_bindings[i];
        binding[i].ls = ((struct ovsdb_idl_row *) ((char *)(p->value_vlan_bindings[i]) + offsetof(struct vteprec_logical_switch, header_)));
        i++;
    }
}

size_t
ovsdb_wrapper_physical_port_vlan_stats_count(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_port *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_port, header_) : NULL;
    return p->n_vlan_stats;
}

void
ovsdb_wrapper_physical_port_vlan_stats(struct ovsdb_idl_row *row,
        struct ovsdb_wrapper_port_vlan_stats *stats)
{
    struct vteprec_physical_port *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_port, header_) : NULL;
    size_t count = p->n_vlan_stats;
    size_t i = 0;
    while (i < count) {
        stats[i].vlan = p->key_vlan_stats[i];
        stats[i].stats = ((struct ovsdb_idl_row *) ((char *)(p->value_vlan_stats[i]) + offsetof(struct vteprec_logical_binding_stats, header_)));
        i++;
    }
}

void
ovsdb_wrapper_update_physical_port(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row,
        struct ovsdb_wrapper_port_vlan_binding *bindings, size_t binding_count)
{
    struct vteprec_physical_port *p =
        CONTAINER_OF(row, struct vteprec_physical_port, header_);
    size_t i = 0;
    int64_t binding_keys[binding_count];
    struct vteprec_logical_switch *binding_values[binding_count];
    while (i < binding_count) {
        binding_keys[i] = bindings[i].vlan;
        binding_values[i] = CONTAINER_OF(bindings[i].ls, struct vteprec_logical_switch, header_);
        i++;
    }
    vteprec_physical_port_set_vlan_bindings(p, binding_keys, binding_values,
            binding_count);
}

/* physical locator */
char *
ovsdb_wrapper_physical_locator_dst_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_locator *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_locator, header_) : NULL;
    return p->dst_ip;
}

void
ovsdb_wrapper_add_physical_locator(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row, const char *dip)
{
    struct vteprec_physical_locator *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_locator, header_) : NULL;
    if (p == NULL)
        p = vteprec_physical_locator_insert(txn);
    vteprec_physical_locator_set_dst_ip(p, dip);
    vteprec_physical_locator_set_encapsulation_type(p, "vxlan_over_ipv4");
}

void
ovsdb_wrapper_delete_physical_locator(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_locator *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_locator, header_) : NULL;
    vteprec_physical_locator_delete(p);
}

/* Physical Locator Set*/
size_t
ovsdb_wrapper_physical_locator_set_locator_count(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_locator_set *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_locator_set,
                           header_) : NULL;
    if (p == NULL) {
        return 0;
    }

    return p->n_locators;
}

struct ovsdb_idl_row **
ovsdb_wrapper_physical_locator_set_locators(struct ovsdb_idl_row *row)
{
    struct vteprec_physical_locator_set *p =
        row ? CONTAINER_OF(row, struct vteprec_physical_locator_set,
                           header_) : NULL;
    if (p == NULL) {
        return NULL;
    }

    return (struct ovsdb_idl_row **)p->locators;
}

/* unicast mac local */
char *
ovsdb_wrapper_ucast_mac_local_mac(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_local *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_local, header_) : NULL;
    return mac->MAC;
}

char *
ovsdb_wrapper_ucast_mac_local_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_local *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_local, header_) : NULL;
    return mac->ipaddr;
}

char *
ovsdb_wrapper_ucast_mac_local_logical_switch(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_local *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_local, header_) : NULL;
    if (mac->logical_switch) {
        return ovsdb_wrapper_logical_switch_name(&mac->logical_switch->header_);
    }
    return NULL;
}

char *
ovsdb_wrapper_ucast_mac_local_dst_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_local *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_local, header_) : NULL;
    if (mac->locator) {
        return mac->locator->dst_ip;
    }
    return NULL;
}

void
ovsdb_wrapper_delete_ucast_mac_local(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_local *ucast =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_local, header_) : NULL;
    vteprec_ucast_macs_local_delete(ucast);
}

/* unicast mac remote */
void
ovsdb_wrapper_add_ucast_mac_remote(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row, const char *mac, struct ovsdb_idl_row *ls,
        struct ovsdb_idl_row *pl, const char *dest_ip)
{
    struct vteprec_ucast_macs_remote *ucast =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    if (ucast == NULL)
        ucast = vteprec_ucast_macs_remote_insert(txn);
    vteprec_ucast_macs_remote_set_MAC(ucast, mac);
    struct vteprec_logical_switch *l_switch =
        ls? CONTAINER_OF(ls, struct vteprec_logical_switch, header_) : NULL;
    vteprec_ucast_macs_remote_set_logical_switch(ucast, l_switch);
    struct vteprec_physical_locator *p =
        pl ? CONTAINER_OF(pl, struct vteprec_physical_locator, header_) : NULL;
    if (p == NULL) {
        p = vteprec_physical_locator_insert(txn);
        vteprec_physical_locator_set_dst_ip(p, dest_ip);
        vteprec_physical_locator_set_encapsulation_type(p, "vxlan_over_ipv4");
    }
    vteprec_ucast_macs_remote_set_locator(ucast, p);
}

void
ovsdb_wrapper_delete_ucast_mac_remote(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_remote *ucast =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    vteprec_ucast_macs_remote_delete(ucast);
}

char *
ovsdb_wrapper_ucast_mac_remote_mac(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_remote *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    return mac->MAC;
}

char *
ovsdb_wrapper_ucast_mac_remote_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_remote *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    return mac->ipaddr;
}

char *
ovsdb_wrapper_ucast_mac_remote_logical_switch(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_remote *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    if (mac->logical_switch) {
        return ovsdb_wrapper_logical_switch_name(&mac->logical_switch->header_);
    }
    return NULL;
}

char *
ovsdb_wrapper_ucast_mac_remote_dst_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_ucast_macs_remote *mac =
        row ? CONTAINER_OF(row, struct vteprec_ucast_macs_remote, header_) : NULL;
    if (mac->locator) {
        return mac->locator->dst_ip;
    }
    return NULL;
}

/* multicast mac local */
void
ovsdb_wrapper_delete_mcast_mac_local(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_local *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_local, header_) : NULL;
    struct vteprec_physical_locator_set *l_set = mcast->locator_set;

    if (l_set == NULL) {
        // mcast entry and locator set always exists together in case locator
        // set is missing donot encode delete for mcast entry as well, on next
        // try when physical locator is also available it will successfully
        // delete it
        return;
    }

    vteprec_physical_locator_set_delete(l_set);
    vteprec_mcast_macs_local_delete(mcast);
}

char *
ovsdb_wrapper_mcast_mac_local_mac(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_local *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_local, header_) : NULL;
    return mcast->MAC;
}

char *
ovsdb_wrapper_mcast_mac_local_logical_switch(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_local *mac =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_local, header_) : NULL;
    if (mac->logical_switch)
        return ovsdb_wrapper_logical_switch_name(&mac->logical_switch->header_);
    return NULL;
}

struct ovsdb_idl_row *
ovsdb_wrapper_mcast_mac_local_physical_locator_set(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_local *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_local, header_) : NULL;
    return &(mcast->locator_set->header_);
}

/* multicast mac remote */
void
ovsdb_wrapper_add_mcast_mac_remote(struct ovsdb_idl_txn *txn,
        struct ovsdb_idl_row *row, const char *mac, struct ovsdb_idl_row *ls,
        struct ovsdb_idl_row *pl, const char *dst_ip)
{
    struct vteprec_mcast_macs_remote *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_remote, header_) : NULL;
    struct vteprec_physical_locator_set *l_set =
        vteprec_physical_locator_set_insert(txn);
    if (mcast == NULL) {
        mcast = vteprec_mcast_macs_remote_insert(txn);
    }
    vteprec_mcast_macs_remote_set_MAC(mcast, mac);
    vteprec_mcast_macs_remote_set_locator_set(mcast, l_set);
    struct vteprec_logical_switch *l_switch =
        ls ? CONTAINER_OF(ls, struct vteprec_logical_switch, header_) : NULL;
    vteprec_mcast_macs_remote_set_logical_switch(mcast, l_switch);
    struct vteprec_physical_locator *p =
        pl ? CONTAINER_OF(pl, struct vteprec_physical_locator, header_) : NULL;
    if (p == NULL) {
        p = vteprec_physical_locator_insert(txn);
        vteprec_physical_locator_set_dst_ip(p, dst_ip);
        vteprec_physical_locator_set_encapsulation_type(p, "vxlan_over_ipv4");
    }
    vteprec_physical_locator_set_set_locators(l_set, &p, 1);
}

void
ovsdb_wrapper_delete_mcast_mac_remote(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_remote *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_remote, header_) : NULL;
    struct vteprec_physical_locator_set *l_set = mcast->locator_set;

    if (l_set == NULL) {
        // mcast entry and locator set always exists together in case locator
        // set is missing donot encode delete for mcast entry as well, on next
        // try when physical locator is also available it will successfully
        // delete it
        return;
    }

    vteprec_physical_locator_set_delete(l_set);
    vteprec_mcast_macs_remote_delete(mcast);
}

char *
ovsdb_wrapper_mcast_mac_remote_mac(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_remote *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_remote, header_) : NULL;
    return mcast->MAC;
}

char *
ovsdb_wrapper_mcast_mac_remote_logical_switch(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_remote *mac =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_remote, header_) : NULL;
    if (mac->logical_switch)
        return ovsdb_wrapper_logical_switch_name(&mac->logical_switch->header_);
    return NULL;
}

char *
ovsdb_wrapper_mcast_mac_remote_dst_ip(struct ovsdb_idl_row *row)
{
    struct vteprec_mcast_macs_remote *mcast =
        row ? CONTAINER_OF(row, struct vteprec_mcast_macs_remote, header_) : NULL;

    if (mcast == NULL || mcast->locator_set == NULL)
        return "";

    size_t n_locators = mcast->locator_set->n_locators;
    if (n_locators == 0)
        return "";

    // Pick up first locator
    return mcast->locator_set->locators[0]->dst_ip;
}

/* logical binding stats */
void
ovsdb_wrapper_get_logical_binding_stats(struct ovsdb_idl_row *row,
        int64_t *in_pkts, int64_t *in_bytes,
        int64_t *out_pkts, int64_t *out_bytes)
{
    struct vteprec_logical_binding_stats *stats =
        row ? CONTAINER_OF(row, struct vteprec_logical_binding_stats, header_) : NULL;
    if (row == NULL)
        return;
    *out_pkts = stats->packets_from_local;
    *out_bytes = stats->bytes_from_local;
    *in_pkts = stats->packets_to_local;
    *in_bytes = stats->bytes_to_local;
}

