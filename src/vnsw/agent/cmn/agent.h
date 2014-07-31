/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_hpp
#define vnsw_agent_hpp

#if defined(__linux__)
#include <netinet/ether.h>
#elif defined(__FreeBSD__)
#include <net/ethernet.h>
#else
#error "Unsupported platform"
#endif

#include <vector>
#include <stdint.h>
#include <boost/intrusive_ptr.hpp>
#include <cmn/agent_cmn.h>

class Agent;
class AgentParam;
class AgentConfig;
class AgentStats;
class KSync;
class AgentUve;
class PktModule;
class VirtualGateway;
class ServicesModule;
class MulticastHandler;
class DiscoveryAgentClient;
class AgentDBEntry;
class XmppClient;
class OperDB;
class AgentRoute;

class Interface;
typedef boost::intrusive_ptr<Interface> InterfaceRef;
typedef boost::intrusive_ptr<const Interface> InterfaceConstRef;
void intrusive_ptr_release(const Interface* p);
void intrusive_ptr_add_ref(const Interface* p);

class VmEntry;
typedef boost::intrusive_ptr<VmEntry> VmEntryRef;
typedef boost::intrusive_ptr<const VmEntry> VmEntryConstRef;
void intrusive_ptr_release(const VmEntry* p);
void intrusive_ptr_add_ref(const VmEntry* p);

class VnEntry;
typedef boost::intrusive_ptr<VnEntry> VnEntryRef;
typedef boost::intrusive_ptr<const VnEntry> VnEntryConstRef;
void intrusive_ptr_release(const VnEntry* p);
void intrusive_ptr_add_ref(const VnEntry* p);

class SgEntry;
typedef boost::intrusive_ptr<SgEntry> SgEntryRef;
typedef boost::intrusive_ptr<const SgEntry> SgEntryConstRef;
void intrusive_ptr_release(const SgEntry* p);
void intrusive_ptr_add_ref(const SgEntry* p);

class VrfEntry;
typedef boost::intrusive_ptr<VrfEntry> VrfEntryRef;
void intrusive_ptr_release(const VrfEntry* p);
void intrusive_ptr_add_ref(const VrfEntry* p);

class MplsLabel;
typedef boost::intrusive_ptr<MplsLabel> MplsLabelRef;
void intrusive_ptr_release(const MplsLabel* p);
void intrusive_ptr_add_ref(const MplsLabel* p);

class MirrorEntry;
typedef boost::intrusive_ptr<MirrorEntry> MirrorEntryRef;
void intrusive_ptr_release(const MirrorEntry* p);
void intrusive_ptr_add_ref(const MirrorEntry* p);

class VxLanId;
typedef boost::intrusive_ptr<VxLanId> VxLanIdRef;
void intrusive_ptr_release(const VxLanId* p);
void intrusive_ptr_add_ref(const VxLanId* p);

class Inet4UnicastRouteEntry;
class Inet4MulticastRouteEntry;
class Layer2RouteEntry;
class Route;
typedef boost::intrusive_ptr<Route> RouteRef;
void intrusive_ptr_release(const Route* p);
void intrusive_ptr_add_ref(const Route* p);

class NextHop;
typedef boost::intrusive_ptr<NextHop> NextHopRef;
typedef boost::intrusive_ptr<const NextHop> NextHopConstRef;
void intrusive_ptr_release(const NextHop* p);
void intrusive_ptr_add_ref(const NextHop* p);

class AddrBase;
typedef boost::intrusive_ptr<AddrBase> AddrRef;
void intrusive_ptr_release(const AddrBase* p);
void intrusive_ptr_add_ref(const AddrBase* p);

class AclDBEntry;
typedef boost::intrusive_ptr<AclDBEntry> AclDBEntryRef;
typedef boost::intrusive_ptr<const AclDBEntry> AclDBEntryConstRef;
void intrusive_ptr_release(const AclDBEntry* p);
void intrusive_ptr_add_ref(const AclDBEntry* p);

//class SecurityGroup;
typedef std::vector<int> SecurityGroupList;

class AgentDBTable;
class InterfaceTable;
class NextHopTable;
class VmTable;
class VnTable;
class SgTable;
class VrfTable;
class MplsTable;
class RouteTable;
class AgentRouteTable;
class Inet4UnicastAgentRouteTable;
class Inet4MulticastAgentRouteTable;
class Layer2AgentRouteTable;
class CfgIntTable;
class AclTable;
class MirrorTable;
class VrfAssignTable;
class DomainConfig;
class VxLanTable;
class MulticastGroupObject;

class MirrorCfgTable;
class IntfMirrorCfgTable;

class XmppInit;
class AgentXmppChannel;
class AgentIfMapXmppChannel;
class AgentDnsXmppChannel;
class DiscoveryServiceClient;
class EventManager;
class IFMapAgentStaleCleaner;
class CfgListener;

class ArpProto;
class DhcpProto;
class DnsProto;
class IcmpProto;
class FlowProto;

class Peer;
class LifetimeManager;
class DiagTable;
class VNController;
class ConnectionState;
class AgentSignal;
class ServiceInstanceTable;
class Agent;

extern void RouterIdDepInit(Agent *agent);

#define MULTICAST_LABEL_RANGE_START 1024
#define MULTICAST_LABEL_BLOCK_SIZE 2048        

#define MAX_XMPP_SERVERS 2
#define XMPP_SERVER_PORT 5269
#define DISCOVERY_SERVER_PORT 5998
#define METADATA_IP_ADDR ntohl(inet_addr("169.254.169.254"))
#define METADATA_PORT 8775
#define METADATA_NAT_PORT 80

class Agent {
public:
    static const uint32_t kDefaultMaxLinkLocalOpenFds = 2048;
    // max open files in the agent, excluding the linklocal bind ports
    static const uint32_t kMaxOtherOpenFds = 64;
    // default timeout zero means, this timeout is not used
    static const uint32_t kDefaultFlowCacheTimeout = 0;

    enum VxLanNetworkIdentifierMode {
        AUTOMATIC,
        CONFIGURED
    };

    enum RouteTableType {
        INET4_UNICAST = 0,
        INET4_MULTICAST,
        LAYER2,
        ROUTE_TABLE_MAX
    };

    Agent();
    virtual ~Agent();
    void Shutdown() { }

    static Agent *GetInstance() {return singleton_;}
    static const std::string &NullString() {return null_string_;};
    static const uint8_t *vrrp_mac() {return vrrp_mac_;}
    static const std::string &BcastMac() {return bcast_mac_;};
    static const std::string &xmpp_dns_server_prefix() {
        return xmpp_dns_server_connection_name_prefix_;
    }
    static const std::string &xmpp_control_node_prefix() {
        return xmpp_control_node_connection_name_prefix_;
    }

    const std::string &host_name() const {return host_name_; }
    const std::string &program_name() const {return prog_name_;}
    const std::string &config_file() const {return config_file_;}
    const std::string &log_file() const {return log_file_;}

    // DB Table accessor methods
    InterfaceTable *interface_table() const {return intf_table_;}
    void set_interface_table(InterfaceTable *table) {
         intf_table_ = table;
    }

    MirrorCfgTable *mirror_cfg_table() const {return mirror_cfg_table_;}
    void set_mirror_cfg_table(MirrorCfgTable *table) {
        mirror_cfg_table_ = table;
    }

    NextHopTable *nexthop_table() const {return nh_table_;}
    void set_nexthop_table(NextHopTable *table) {
        nh_table_ = table;
    }

    VrfTable *vrf_table() const { return vrf_table_;}
    void set_vrf_table(VrfTable *table) {
        vrf_table_ = table;
    }

    VmTable *vm_table() const { return vm_table_;}
    void set_vm_table(VmTable *table) {
        vm_table_ = table;
    }

    VnTable *vn_table() const { return vn_table_;}
    void set_vn_table(VnTable *table) {
        vn_table_ = table;
    }

    SgTable *sg_table() const { return sg_table_;}
    void set_sg_table(SgTable *table) {
        sg_table_ = table;
    }

    MplsTable *mpls_table() const { return mpls_table_;}
    void set_mpls_table(MplsTable *table) { 
        mpls_table_ = table;
    }
    
    AclTable *acl_table() const { return acl_table_;}
    void set_acl_table(AclTable *table) { 
        acl_table_ = table;
    }
    
    MirrorTable *mirror_table() const { return mirror_table_;}
    void set_mirror_table(MirrorTable *table) {
        mirror_table_ = table;
    }

    VrfAssignTable *vrf_assign_table() const {return vrf_assign_table_;}
    void set_vrf_assign_table(VrfAssignTable *table) {
        vrf_assign_table_ = table;
    }

    VxLanTable *vxlan_table() const { return vxlan_table_;}
    void set_vxlan_table(VxLanTable *table) { 
        vxlan_table_ = table;
    }
    
    CfgIntTable *interface_config_table() const {return intf_cfg_table_;}
    void set_interface_config_table(CfgIntTable *table) {
        intf_cfg_table_ = table;
    }

    DomainConfig *domain_config_table() const;

    IntfMirrorCfgTable *interface_mirror_cfg_table() const {
        return intf_mirror_cfg_table_;
    }
    void set_interface_mirror_cfg_table(IntfMirrorCfgTable *table) {
        intf_mirror_cfg_table_ = table;
    }

    Inet4UnicastAgentRouteTable *fabric_inet4_unicast_table() const {
        return uc_rt_table_;
    }
    void set_fabric_inet4_unicast_table(Inet4UnicastAgentRouteTable *
                                                 table) {
        uc_rt_table_ = table;
    }
    void set_fabric_inet4_unicast_table(RouteTable * table) {
        uc_rt_table_ = (Inet4UnicastAgentRouteTable *)table;
    }

    Inet4MulticastAgentRouteTable *fabric_inet4_multicast_table() const {
        return mc_rt_table_;
    }
    void set_fabric_inet4_multicast_table
        (Inet4MulticastAgentRouteTable *table) {
        mc_rt_table_ = table;
    }
    void set_fabric_inet4_multicast_table(RouteTable *table) {
        mc_rt_table_ = (Inet4MulticastAgentRouteTable *)table;
    }

    Layer2AgentRouteTable *fabric_l2_unicast_table() const {
        return l2_rt_table_;
    }
    void set_fabric_l2_unicast_table(Layer2AgentRouteTable *table) {
        l2_rt_table_ = table;
    }
    void set_fabric_l2_unicast_table(RouteTable *table) {
        l2_rt_table_ = (Layer2AgentRouteTable *)table;
    }

    // VHOST related
    uint32_t vhost_prefix_len() const {return prefix_len_;}
    void set_vhost_prefix_len(uint32_t plen) {prefix_len_ = plen;}

    Ip4Address vhost_default_gateway() const {return gateway_id_;}
    void set_vhost_default_gateway(const Ip4Address &addr) {
        gateway_id_ = addr;
    }

    Ip4Address router_id() const {return router_id_;}
    void set_router_id(const Ip4Address &addr) {
        router_id_ = addr;
        set_router_id_configured(true);
    }
    bool router_id_configured() { return router_id_configured_; }
    void set_router_id_configured(bool value) {
        router_id_configured_ = value;
    }

    AgentSignal *agent_signal() const { return agent_signal_.get(); }

    // TODO: Should they be moved under controller/dns/cfg?

    // Common XMPP Client for control-node and config clients
    const std::string &controller_ifmap_xmpp_server(uint8_t idx) const {
        return xs_addr_[idx];
    }
    void set_controller_ifmap_xmpp_server(const std::string &addr, uint8_t idx) {
        xs_addr_[idx] = addr;
    }
    void reset_controller_ifmap_xmpp_server(uint8_t idx) {
        xs_addr_[idx].clear();
        xs_port_[idx] = 0;
    }

    const uint32_t controller_ifmap_xmpp_port(uint8_t idx) const {
        return xs_port_[idx];
    }
    void set_controller_ifmap_xmpp_port(uint32_t port, uint8_t idx) {
        xs_port_[idx] = port;
    }

    XmppInit *controller_ifmap_xmpp_init(uint8_t idx) const {
        return xmpp_init_[idx];
    }
    void set_controller_ifmap_xmpp_init(XmppInit *init, uint8_t idx) {
        xmpp_init_[idx] = init;
    }

    XmppClient *controller_ifmap_xmpp_client(uint8_t idx) {
        return xmpp_client_[idx];
    }

    void set_controller_ifmap_xmpp_client(XmppClient *client, uint8_t idx) {
        xmpp_client_[idx] = client;
    }

    // Config XMPP server specific
    const int8_t &ifmap_active_xmpp_server_index() const {return xs_idx_;}
    const std::string &ifmap_active_xmpp_server() const {return xs_cfg_addr_;}
    void set_ifmap_active_xmpp_server(const std::string &addr,
                                      uint8_t xs_idx) {
        xs_cfg_addr_ = addr;
        xs_idx_ = xs_idx;
    }
    void reset_ifmap_active_xmpp_server() {
        xs_cfg_addr_.clear();
        xs_idx_ = -1;
    }

    AgentIfMapXmppChannel *ifmap_xmpp_channel(uint8_t idx) const {
        return ifmap_channel_[idx];
    }
    void set_ifmap_xmpp_channel(AgentIfMapXmppChannel *channel, 
                                uint8_t idx) {
        ifmap_channel_[idx] = channel;
    }

    // Controller XMPP server
    const uint64_t controller_xmpp_channel_setup_time(uint8_t idx) const {
        return xs_stime_[idx];
    }
    void set_controller_xmpp_channel_setup_time(uint64_t time, uint8_t idx) {
        xs_stime_[idx] = time;
    }
 
    AgentXmppChannel *controller_xmpp_channel(uint8_t idx) { 
        return agent_xmpp_channel_[idx];
    }

    void set_controller_xmpp_channel(AgentXmppChannel *channel, uint8_t idx) {
        agent_xmpp_channel_[idx] = channel;
    };

    // Service instance
   ServiceInstanceTable *service_instance_table() const {
       return service_instance_table_;
   }

   void set_service_instance_table(ServiceInstanceTable *table) {
       service_instance_table_= table;
   }

    // DNS XMPP Server
    const int8_t &dns_xmpp_server_index() const {return xs_dns_idx_;}
    void set_dns_xmpp_server_index(uint8_t xs_idx) {xs_dns_idx_ = xs_idx;}

    XmppInit *dns_xmpp_init(uint8_t idx) const {
        return dns_xmpp_init_[idx];
    }
    void set_dns_xmpp_init(XmppInit *xmpp, uint8_t idx) {
        dns_xmpp_init_[idx] = xmpp;
    }

    XmppClient *dns_xmpp_client(uint8_t idx) const {
        return dns_xmpp_client_[idx];
    }
    void set_dns_xmpp_client(XmppClient *client, uint8_t idx) {
        dns_xmpp_client_[idx] = client;
    }

    AgentDnsXmppChannel *dns_xmpp_channel(uint8_t idx) const {
        return dns_xmpp_channel_[idx];
    }
    void set_dns_xmpp_channel(AgentDnsXmppChannel *chnl, uint8_t idx) {
        dns_xmpp_channel_[idx] = chnl;
    }

    // DNS Server and port
    const std::string &dns_server(uint8_t idx) const {return dns_addr_[idx];}
    void set_dns_server(const std::string &addr, uint8_t idx) {
        dns_addr_[idx] = addr;
    }
    void reset_dns_server(uint8_t idx) {
        dns_addr_[idx].clear();
        dns_port_[idx] = 0;
    }

    const uint32_t dns_server_port(uint8_t idx) const {return dns_port_[idx];}
    void set_dns_server_port(uint32_t port, uint8_t idx) {
        dns_port_[idx] = port;
    }

    /* Discovery Server, port, service-instances */
    const std::string &discovery_server() const {return dss_addr_;}
    const uint32_t discovery_server_port() {
        return dss_port_; 
    }
    const int discovery_xmpp_server_instances() const {
        return dss_xs_instances_;
    }

    DiscoveryServiceClient *discovery_service_client() {
        return ds_client_; 
    }
    void set_discovery_service_client(DiscoveryServiceClient *client) {
        ds_client_ = client;
    }

    // Multicast related
    const std::string &multicast_label_range(uint8_t idx) { 
        return label_range_[idx]; 
    }
    void SetAgentMcastLabelRange(uint8_t idx) {
        std::stringstream str;
        str << (MULTICAST_LABEL_RANGE_START + 
                (idx * MULTICAST_LABEL_BLOCK_SIZE)) << "-"
            << (MULTICAST_LABEL_RANGE_START + 
                ((idx + 1) * MULTICAST_LABEL_BLOCK_SIZE) - 1); 
        label_range_[idx] = str.str();
    }
    void ResetAgentMcastLabelRange(uint8_t idx) {
        label_range_[idx].clear();
    }

    AgentXmppChannel* mulitcast_builder() {
        return cn_mcast_builder_;
    };
    void set_cn_mcast_builder(AgentXmppChannel *peer);

    // Fabric related
    const std::string &fabric_vn_name() {return fabric_vn_name_;};

    const std::string &fabric_vrf_name() {return fabric_vrf_name_;};
    void set_fabric_vrf_name(const std::string &name) {
        fabric_vrf_name_ = name;
    }

    const std::string &linklocal_vn_name() {return link_local_vn_name_;}
    const std::string &linklocal_vrf_name() {return link_local_vrf_name_;}

    const std::string &vhost_interface_name() const;
    void set_vhost_interface_name(const std::string &name) {
        vhost_interface_name_ = name;
    }

    const std::string &pkt_interface_name() const {
        return pkt_interface_name_; 
    }
    void set_pkt_interface_name(const std::string &name) {
        pkt_interface_name_ = name;
    }

    const std::string &GetHostInterfaceName() const;

    const Interface *vhost_interface() const {
        return vhost_interface_;
    }
    void set_vhost_interface(const Interface *interface) {
        vhost_interface_ = interface;
    }
    ConnectionState* connection_state() const {
        return connection_state_;
    }
    void set_connection_state(ConnectionState* state) {
        connection_state_ = state;
    }
    uint16_t metadata_server_port() const {return metadata_server_port_;}
    void set_metadata_server_port(uint16_t port) {
        metadata_server_port_ = port;
    }

    // Protocol objects
    ArpProto *GetArpProto() { return arp_proto_; }
    void SetArpProto(ArpProto *proto) { arp_proto_ = proto; }

    DhcpProto *GetDhcpProto() { return dhcp_proto_; }
    void SetDhcpProto(DhcpProto *proto) { dhcp_proto_ = proto; }

    DnsProto *GetDnsProto() { return dns_proto_; }
    void SetDnsProto(DnsProto *proto) { dns_proto_ = proto; }

    IcmpProto *GetIcmpProto() { return icmp_proto_; }
    void SetIcmpProto(IcmpProto *proto) { icmp_proto_ = proto; }

    FlowProto *GetFlowProto() { return flow_proto_; }
    void SetFlowProto(FlowProto *proto) { flow_proto_ = proto; }

    // Peer objects
    const Peer *local_peer() const {return local_peer_.get();}
    const Peer *local_vm_peer() const {return local_vm_peer_.get();}
    const Peer *link_local_peer() const {return linklocal_peer_.get();}
    const Peer *ecmp_peer() const {return ecmp_peer_.get();}
    const Peer *vgw_peer() const {return vgw_peer_.get();}

    // Agent Modules
    AgentConfig *cfg() const; 
    void set_cfg(AgentConfig *cfg);

    CfgListener *cfg_listener() const;

    AgentStats *stats() const;
    void set_stats(AgentStats *stats);

    KSync *ksync() const;
    void set_ksync(KSync *ksync);

    AgentUve *uve() const;
    void set_uve(AgentUve *uve);

    PktModule *pkt() const;
    void set_pkt(PktModule *pkt);

    ServicesModule *services() const;
    void set_services(ServicesModule *services);

    DiscoveryAgentClient *discovery_client() const;
    void set_discovery_client(DiscoveryAgentClient *client);

    VirtualGateway *vgw() const;
    void set_vgw(VirtualGateway *vgw);

    OperDB *oper_db() const;
    void set_oper_db(OperDB *oper_db);

    VNController *controller() const;
    void set_controller(VNController *val);

    // Miscellaneous
    EventManager *event_manager() const {return event_mgr_;}
    void set_event_manager(EventManager *evm) {
        event_mgr_ = evm;
    }

    DiagTable *diag_table() const;
    void set_diag_table(DiagTable *table);

    uint16_t mirror_port() const {return mirror_src_udp_port_;}
    void set_mirror_port(uint16_t mirr_port) {
        mirror_src_udp_port_ = mirr_port;
    }

    int sandesh_port() const { return sandesh_port_;}
    DB *db() const {return db_;}
    const std::string &fabric_interface_name() const {
        return ip_fabric_intf_name_;
    }

    bool debug() { return debug_; }
    void set_debug(bool debug) { debug_ = debug; }

    VxLanNetworkIdentifierMode vxlan_network_identifier_mode() const {
        return vxlan_network_identifier_mode_;
    }
    void set_vxlan_network_identifier_mode(VxLanNetworkIdentifierMode mode) {
        vxlan_network_identifier_mode_ = mode;
    }

    bool headless_agent_mode() const {return headless_agent_mode_;}
    void set_headless_agent_mode(bool mode) {headless_agent_mode_ = mode;}

    IFMapAgentParser *ifmap_parser() const {return ifmap_parser_;}
    void set_ifmap_parser(IFMapAgentParser *parser) {
        ifmap_parser_ = parser;
    }

    IFMapAgentStaleCleaner *ifmap_stale_cleaner() const {
        return agent_stale_cleaner_;
    }
    void set_ifmap_stale_cleaner(IFMapAgentStaleCleaner *cl) {
        agent_stale_cleaner_ = cl;
    }

    std::string GetUuidStr(boost::uuids::uuid uuid_val) const;

    bool ksync_sync_mode() const {return ksync_sync_mode_;}
    void set_ksync_sync_mode(bool sync_mode) {
        ksync_sync_mode_ = sync_mode;
    }

    bool test_mode() const { return test_mode_; }
    void set_test_mode(bool test_mode) { test_mode_ = test_mode; }

    uint32_t flow_table_size() const { return flow_table_size_; }
    void set_flow_table_size(uint32_t count) { flow_table_size_ = count; }

    bool init_done() const { return init_done_; }
    void set_init_done(bool done) { init_done_ = done; }

    AgentParam *params() const { return params_; }

    bool isXenMode();
    void SetAgentTaskPolicy();
    void CopyConfig(AgentParam *params);

    void Init(AgentParam *param);
    void InitPeers();
    void InitDone();
    void InitXenLinkLocalIntf();
    void InitCollector();

    LifetimeManager *lifetime_manager() { return lifetime_manager_;}
    void CreateLifetimeManager();
    void ShutdownLifetimeManager();

private:

    AgentParam *params_;
    AgentConfig *cfg_;
    AgentStats *stats_;
    KSync *ksync_;
    AgentUve *uve_;
    PktModule *pkt_;
    ServicesModule *services_;
    VirtualGateway *vgw_;
    OperDB *oper_db_;
    DiagTable *diag_table_;
    VNController *controller_;

    EventManager *event_mgr_;
    AgentXmppChannel *agent_xmpp_channel_[MAX_XMPP_SERVERS];
    AgentIfMapXmppChannel *ifmap_channel_[MAX_XMPP_SERVERS];
    XmppClient *xmpp_client_[MAX_XMPP_SERVERS];
    XmppInit *xmpp_init_[MAX_XMPP_SERVERS];
    AgentDnsXmppChannel *dns_xmpp_channel_[MAX_XMPP_SERVERS];
    XmppClient *dns_xmpp_client_[MAX_XMPP_SERVERS];
    XmppInit *dns_xmpp_init_[MAX_XMPP_SERVERS];
    IFMapAgentStaleCleaner *agent_stale_cleaner_;
    AgentXmppChannel *cn_mcast_builder_;
    DiscoveryServiceClient *ds_client_;
    uint16_t metadata_server_port_;
    std::string host_name_;
    std::string prog_name_;
    int sandesh_port_;

    // DB handles
    DB *db_;
    InterfaceTable *intf_table_;
    NextHopTable *nh_table_;
    Inet4UnicastAgentRouteTable *uc_rt_table_;
    Inet4MulticastAgentRouteTable *mc_rt_table_;
    Layer2AgentRouteTable *l2_rt_table_;
    VrfTable *vrf_table_;
    VmTable *vm_table_;
    VnTable *vn_table_;
    SgTable *sg_table_;
    MplsTable *mpls_table_;
    AclTable *acl_table_;
    MirrorTable *mirror_table_;
    VrfAssignTable *vrf_assign_table_;
    VxLanTable *vxlan_table_;
    ServiceInstanceTable *service_instance_table_;

    // Mirror config table
    MirrorCfgTable *mirror_cfg_table_;
    // Interface Mirror config table
    IntfMirrorCfgTable *intf_mirror_cfg_table_;
    
    // Config DB Table handles
    CfgIntTable *intf_cfg_table_;

    Ip4Address router_id_;
    uint32_t prefix_len_;
    Ip4Address gateway_id_;
    std::string xs_cfg_addr_;
    int8_t xs_idx_;
    std::string xs_addr_[MAX_XMPP_SERVERS];
    uint32_t xs_port_[MAX_XMPP_SERVERS];
    uint64_t xs_stime_[MAX_XMPP_SERVERS];
    int8_t xs_dns_idx_;
    std::string dns_addr_[MAX_XMPP_SERVERS];
    uint32_t dns_port_[MAX_XMPP_SERVERS];
    std::string dss_addr_;
    uint32_t dss_port_;
    int dss_xs_instances_;
    std::string label_range_[MAX_XMPP_SERVERS];
    std::string ip_fabric_intf_name_;
    std::string vhost_interface_name_;
    std::string pkt_interface_name_;
    CfgListener *cfg_listener_;

    ArpProto *arp_proto_;
    DhcpProto *dhcp_proto_;
    DnsProto *dns_proto_;
    IcmpProto *icmp_proto_;
    FlowProto *flow_proto_;

    std::auto_ptr<Peer> local_peer_;
    std::auto_ptr<Peer> local_vm_peer_;
    std::auto_ptr<Peer> linklocal_peer_;
    std::auto_ptr<Peer> ecmp_peer_;
    std::auto_ptr<Peer> vgw_peer_;

    std::auto_ptr<AgentSignal> agent_signal_;

    IFMapAgentParser *ifmap_parser_;
    bool router_id_configured_;

    uint16_t mirror_src_udp_port_;
    LifetimeManager *lifetime_manager_;
    bool ksync_sync_mode_;
    std::string mgmt_ip_;
    static Agent *singleton_;
    VxLanNetworkIdentifierMode vxlan_network_identifier_mode_;
    bool headless_agent_mode_;
    const Interface *vhost_interface_;
    ConnectionState* connection_state_;
    bool debug_;
    bool test_mode_;
    bool init_done_;

    // Flow information
    uint32_t flow_table_size_;

    // Constants
    static const std::string config_file_;
    static const std::string log_file_;
    static const std::string null_string_;
    static std::string fabric_vrf_name_;
    static const std::string fabric_vn_name_;
    static const std::string link_local_vrf_name_;
    static const std::string link_local_vn_name_;
    static const uint8_t vrrp_mac_[ETHER_ADDR_LEN];
    static const std::string bcast_mac_;
    static const std::string xmpp_dns_server_connection_name_prefix_;
    static const std::string xmpp_control_node_connection_name_prefix_;
};

#endif // vnsw_agent_hpp
