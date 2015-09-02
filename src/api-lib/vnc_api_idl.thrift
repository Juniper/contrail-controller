exception InvalidOp {
    1: i32 what,
    2: string why,
}

/* Common types */
typedef string vpc_id_t
typedef string addr_space_id_t
typedef string vn_id_t
typedef string sg_id_t
typedef string policy_id_t
typedef string route_table_id_t

enum ip_ver_t {
    IPV4 = 4,
    IPV6 = 6
}

typedef string ip_addr_t
typedef string ip6_addr_t

enum app_type_t {
    HTTP = 1,
    TCP_ALL,
    TCP_CUSTOM,
    UDP_ALL,
    UDP_CUSTOM,
    PING,
    ICMP_ALL,
    ICMP_CUSTOM,
}

enum action_t {
    ALLOW = 1,
    DENY
}

struct port_param_s {
    /** start = end = 0 => all ports */
    1: optional i32 tp_sport_start;
    2: optional i32 tp_sport_end;
    3: optional i32 tp_dport_start;
    4: optional i32 tp_dport_end;
}
typedef port_param_s tcp_param_t
typedef port_param_s udp_param_t

struct icmp_param_s {
}
typedef icmp_param_s icmp_param_t

struct application_s {
    1: required app_type_t app_type
    2: optional tcp_param_t tcp_param
    3: optional udp_param_t udp_param
    4: optional icmp_param_t icmp_param
}
typedef application_s application_t

/* Address Space types */
struct addr_space_s {
    1: optional ip_addr_t aspc_ip4_start,
    2: optional list<byte> aspc_ip6_start,
    3: optional i32 aspc_prefix_len,
}
typedef addr_space_s addr_space_t

/* Virtual Network types */

struct subnet_s {
    1: required ip_ver_t sn_ip_ver
    2: optional ip_addr_t sn_ip_net
    3: optional ip6_addr_t sn_ip6_net
    4: optional i32 sn_prefix_len
}
typedef subnet_s subnet_t

struct ipam_s {
    1: optional list<ip_addr_t> ipam_dns_servers
}
typedef ipam_s ipam_t

struct vn_s {
    1: required string vn_name;
    2: required vpc_id_t vn_vpc_id;
    3: optional list<subnet_t> vn_subnets;
    4: optional list<ipam_t> vn_ipams;
}
typedef vn_s vn_t

/* Security Group types */

enum direction_t {
    INBOUND = 1,
    OUTBOUND,
    BOTH
}

enum sgr_peer_type_t {
    VN = 1,
    SUBNET,
    SECURITY_GROUP
}

struct sgr_peer_s {
    1: required sgr_peer_type_t sp_type
    2: optional vn_id_t sp_vn_id
    3: optional subnet_t sp_subnet
}
typedef sgr_peer_s sgr_peer_t

typedef action_t sgr_action_t
 
struct sg_rule_s {
    1: optional direction_t sge_dir
    2: optional sgr_peer_t  sge_peer
    3: optional application_t sge_app
    4: optional sgr_action_t sge_action
}
typedef sg_rule_s sg_rule_t

struct security_group_s {
    1: required string sg_name
    2: required vpc_id_t sg_vpc_id
    3: optional list<sg_rule_t> sg_rules
}
typedef security_group_s security_group_t

/* Policy types */
enum pe_endpt_type_t {
    PEP_VN = 1,
    PEP_SUBNET
}
struct pe_endpt_s {
    1: required pe_endpt_type_t pep_type
    2: optional vn_id_t pep_vn_id
    3: optional subnet_t pep_subnet
}
typedef pe_endpt_s pe_endpt_t

typedef action_t pe_action_t

struct policy_entry_s {
    1: optional i32 pe_rule_num;
    2: optional pe_endpt_t pe_from;
    3: optional pe_endpt_t pe_to;
    4: optional application_t pe_app;
    5: optional pe_action_t pe_action;
}
typedef policy_entry_s policy_entry_t

struct policy_s {
    1: required string p_name
    2: required vpc_id_t p_vpc_id
    3: optional list<policy_entry_t> p_entries;
}
typedef policy_s policy_t


/* Route types */
enum re_dest_type_t {
    RE_DEST_SUBNET = 1
    RE_DEST_VN
}

struct route_entry_dest_s {
    1: optional re_dest_type_t red_type
    2: optional subnet_t red_subnet
    3: optional vn_id_t red_vn_id;
}
typedef route_entry_dest_s route_entry_dest_t

enum re_target_type_t {
    RE_TARGET_INET_GW = 1
    RE_TARGET_VPN_GW
}

struct route_entry_target_s {
    1: optional re_target_type_t ret_type
    2: optional ip_addr_t ret_ipaddr
    3: optional ip6_addr_t ret_ip6addr
}
typedef route_entry_target_s route_entry_target_t

struct route_entry_s {
    1: optional route_entry_dest_t re_dest
    2: optional route_entry_target_t re_tgt
}
typedef route_entry_s route_entry_t

struct route_table_s {
    1: required string rtbl_name
    2: optional list<route_entry_t> rtbl_entries;
}
typedef route_table_s route_table_t

/* Service types */

/* VPC types */
struct vpc_s {
    1: required string vpc_name
    2: optional list<vn_t> vpc_vnets;
    3: optional list<security_group_t> vpc_security_groups;
    4: optional list<policy_t> vpc_policies;
    5: optional list<route_table_t> vpc_route_tables;
}
typedef vpc_s vpc_t

service vnc_api_service {
    /* VPC */
    vpc_id_t vpc_create(1: string vpc_name) throws (1:InvalidOp exc),
    void vpc_delete(1: vpc_id_t id) throws (1: InvalidOp exc),
    vpc_t vpc_read(1: optional string vpc_id,
                   2: optional string vpc_name) throws (1: InvalidOp exc),
    list<vpc_t> vpc_list() throws (1: InvalidOp exc),

    /* Address Space */
    addr_space_id_t addr_space_create(1: addr_space_t aspc) 
                                      throws (1: InvalidOp exc)
    void addr_space_delete(1: addr_space_t aspc),

    /* Virtual Network */
    vn_id_t vn_create(1: vpc_id_t vpc_id, 2: string vn_name)
                      throws (1:InvalidOp exc)
    void vn_delete(1: vn_id_t id) throws (1: InvalidOp exc),
    void subnet_create(1: vn_id_t vn_id, 2: subnet_t subnet)
                      throws (1:InvalidOp exc)
    void subnets_set(1: vn_id_t vn_id, 2: list<subnet_t> subnet)
                      throws (1:InvalidOp exc)
    list<subnet_t> subnet_list(1: vn_id_t vn_id) throws (1:InvalidOp exc)
    void subnet_delete(1: vn_id_t vn_id, 2: subnet_t subnet)
                      throws (1:InvalidOp exc)
    void subnet_delete_all(1: vn_id_t vn_id) throws (1:InvalidOp exc)

    /* Security Group */
    sg_id_t sg_create(1: vpc_id_t vpc_id, 2: string sg_name)
                      throws (1:InvalidOp exc)
     
}
