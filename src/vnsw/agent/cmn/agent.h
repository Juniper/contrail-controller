/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_hpp
#define vnsw_agent_hpp

#include <netinet/ether.h>

class AgentParam;
class AgentInit;
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
class Interface;
class OperDB;
typedef boost::intrusive_ptr<Interface> InterfaceRef;
typedef boost::intrusive_ptr<const Interface> InterfaceConstRef;
class VmEntry;
typedef boost::intrusive_ptr<VmEntry> VmEntryRef;
typedef boost::intrusive_ptr<const VmEntry> VmEntryConstRef;
class VnEntry;
typedef boost::intrusive_ptr<VnEntry> VnEntryRef;
typedef boost::intrusive_ptr<const VnEntry> VnEntryConstRef;
class SgEntry;
typedef boost::intrusive_ptr<SgEntry> SgEntryRef;
typedef boost::intrusive_ptr<const SgEntry> SgEntryConstRef;
class VrfEntry;
typedef boost::intrusive_ptr<VrfEntry> VrfEntryRef;
class MplsLabel;
typedef boost::intrusive_ptr<MplsLabel> MplsLabelRef;
class MirrorEntry;
typedef boost::intrusive_ptr<MirrorEntry> MirrorEntryRef;
class VxLanId;
typedef boost::intrusive_ptr<VxLanId> VxLanIdRef;
class Inet4UnicastRouteEntry;
class Inet4MulticastRouteEntry;
class Layer2RouteEntry;
class Route;
typedef boost::intrusive_ptr<Route> RouteRef;
class NextHop;
typedef boost::intrusive_ptr<NextHop> NextHopRef;
typedef boost::intrusive_ptr<const NextHop> NextHopConstRef;
class AddrBase;
typedef boost::intrusive_ptr<AddrBase> AddrRef;
class AclDBEntry;
typedef boost::intrusive_ptr<AclDBEntry> AclDBEntryRef;
typedef boost::intrusive_ptr<const AclDBEntry> AclDBEntryConstRef;

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
class AddrTable;
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
    const std::string &GetHostName();
    const std::string &GetProgramName() {return prog_name_;};
    static const std::string &DefaultConfigFile() {return config_file_;}
    static const std::string &DefaultLogFile() {return log_file_;}
    static const std::string &NullString() {return null_str_;};
    static const uint8_t *vrrp_mac() {return vrrp_mac_;}
    static const std::string &BcastMac() {return bcast_mac_;};
    InterfaceTable *GetInterfaceTable() {return intf_table_;};
    MirrorCfgTable *GetMirrorCfgTable() {return mirror_cfg_table_;};
    IntfMirrorCfgTable *GetIntfMirrorCfgTable() {return intf_mirror_cfg_table_;};
    NextHopTable *GetNextHopTable() {return nh_table_;};
    NextHopTable *nexthop_table() {return nh_table_;};
    Inet4UnicastAgentRouteTable *GetDefaultInet4UnicastRouteTable() {
        return uc_rt_table_;
    };
    Inet4MulticastAgentRouteTable *GetDefaultInet4MulticastRouteTable() {
        return mc_rt_table_;
    };
    Layer2AgentRouteTable *GetLayer2AgentRouteTable() {return l2_rt_table_;};
    VrfTable *vrf_table() const { return vrf_table_;}
    VrfTable *GetVrfTable() { return vrf_table_;};
    VmTable *GetVmTable() { return vm_table_;};
    VnTable *GetVnTable() { return vn_table_;};
    SgTable *GetSgTable() { return sg_table_;};
    AddrTable *GetAddressTable() { return addr_table_;};
    MplsTable *GetMplsTable() { return mpls_table_;};
    AclTable *GetAclTable() { return acl_table_;};
    MirrorTable *GetMirrorTable() { return mirror_table_;};
    CfgIntTable *GetIntfCfgTable() {return intf_cfg_table_;};
    DomainConfig *GetDomainConfigTable() {return domain_config_table_;};
    VrfAssignTable *GetVrfAssignTable() {return vrf_assign_table_;};
    VxLanTable *GetVxLanTable() { return vxlan_table_;};
    int GetSandeshPort() { return sandesh_port_;};

    EventManager *GetEventManager() {return event_mgr_;};
    DB *GetDB() {return db_;};

    uint16_t GetMirrorPort() {return mirror_src_udp_port_;};
    Ip4Address GetRouterId() {return router_id_; };
    void SetRouterId(const Ip4Address &addr) {
        router_id_ = addr;
        SetRouterIdConfigured(true);
    };

    uint32_t GetPrefixLen() {return prefix_len_;};
    void SetPrefixLen(uint32_t plen) {prefix_len_ = plen;};

    bool GetRouterIdConfigured() { return router_id_configured_; }
    LifetimeManager *GetLifetimeManager() { return lifetime_manager_;};

    Ip4Address GetGatewayId() {return gateway_id_; };
    void SetGatewayId(const Ip4Address &addr) {gateway_id_ = addr;};

    const std::string &GetIpFabricItfName() {
        return ip_fabric_intf_name_;
    };

    const std::string &GetXmppCfgServer() {return xs_cfg_addr_; };
    const int8_t &GetXmppCfgServerIdx() {return xs_idx_; };
    void SetXmppCfgServer(const std::string &addr, uint8_t xs_idx) {
        xs_cfg_addr_ = addr;
        xs_idx_ = xs_idx;
    };
    void ResetXmppCfgServer() {
        xs_cfg_addr_.clear();
        xs_idx_ = -1;
    }
    const std::string &GetXmppServer(uint8_t idx) {return xs_addr_[idx]; };
    const uint32_t GetXmppPort(uint8_t idx) {return xs_port_[idx]; };
    void SetXmppServer(const std::string &addr, uint8_t idx) {
        xs_addr_[idx] = addr;
    };

    void SetXmppPort(uint32_t port, uint8_t idx) {
        xs_port_[idx] = port;
    };

    const uint64_t GetAgentXmppChannelSetupTime(uint8_t idx) {return xs_stime_[idx];}
    void SetAgentXmppChannelSetupTime(uint64_t time, uint8_t idx) {xs_stime_[idx] = time;}
 
    const int8_t &GetXmppDnsCfgServerIdx() {return xs_dns_idx_; };
    void SetXmppDnsCfgServer(uint8_t xs_idx) { xs_dns_idx_ = xs_idx; };
    const std::string &GetDnsXmppServer(uint8_t idx) {
        return xs_dns_addr_[idx]; 
    }
    void SetDnsXmppServer(const std::string &addr, uint8_t idx) {
        xs_dns_addr_[idx] = addr;
    }

    const uint32_t GetDnsXmppPort(uint8_t idx) {
        return xs_dns_port_[idx]; 
    }
    void SetDnsXmppPort(uint32_t port, uint8_t idx) {
        xs_dns_port_[idx] = port;
    }

    /* Discovery Server, port, service-instances */
    const std::string &GetDiscoveryServer() {
        return dss_addr_; 
    }

    const uint32_t GetDiscoveryServerPort() {
        return dss_port_; 
    }

    const int GetDiscoveryXmppServerInstances() {
        return dss_xs_instances_; 
    }
   
    const std::string &GetAgentMcastLabelRange(uint8_t idx) { 
        return label_range_[idx]; 
    };

    void SetAgentMcastLabelRange(uint8_t idx) {
        std::stringstream str;
        str << (MULTICAST_LABEL_RANGE_START + 
                (idx * MULTICAST_LABEL_BLOCK_SIZE)) << "-"
            << (MULTICAST_LABEL_RANGE_START + 
                ((idx + 1) * MULTICAST_LABEL_BLOCK_SIZE) - 1); 
        label_range_[idx] = str.str();
    };
    void ResetAgentMcastLabelRange(uint8_t idx) {
        label_range_[idx].clear();
    }

    AgentXmppChannel* GetControlNodeMulticastBuilder() {
        return cn_mcast_builder_;
    };
    void set_cn_mcast_builder(AgentXmppChannel *peer);

    const std::string &GetFabricVnName() {return fabric_vn_name_;};
    const std::string &GetDefaultVrf() {return fabric_vrf_name_;};
    const std::string &GetLinkLocalVnName() {return link_local_vn_name_;}
    const std::string &GetLinkLocalVrfName() {return link_local_vrf_name_;}

    void set_fabric_vrf_name(const std::string &name) {
        fabric_vrf_name_ = name;
    }

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

    const std::string &GetHostInterfaceName();

    const Interface *vhost_interface() const {
        return vhost_interface_;
    }
    void set_vhost_interface(const Interface *interface) {
        vhost_interface_ = interface;
    }

    AgentXmppChannel *GetAgentXmppChannel(uint8_t idx) { 
        return agent_xmpp_channel_[idx];
    };
    AgentIfMapXmppChannel *GetAgentIfMapXmppChannel(uint8_t idx) { 
        return ifmap_channel_[idx];
    };
    XmppClient *GetAgentXmppClient(uint8_t idx) {
        return xmpp_client_[idx];
    };
    XmppInit *GetAgentXmppInit(uint8_t idx) {
        return xmpp_init_[idx];
    };
    AgentDnsXmppChannel *GetAgentDnsXmppChannel(uint8_t idx) { 
        return dns_xmpp_channel_[idx];
    };
    XmppClient *GetAgentDnsXmppClient(uint8_t idx) {
        return dns_xmpp_client_[idx];
    };
    XmppInit *GetAgentDnsXmppInit(uint8_t idx) {
        return dns_xmpp_init_[idx];
    };
    DiscoveryServiceClient *GetDiscoveryServiceClient() {
        return ds_client_; 
    };
    uint16_t GetMetadataServerPort() {
        return metadata_server_port_;
    }
    IFMapAgentParser *GetIfMapAgentParser() {return ifmap_parser_;};
    IFMapAgentStaleCleaner *GetIfMapAgentStaleCleaner() {return agent_stale_cleaner_;};
    
    ArpProto *GetArpProto() { return arp_proto_; }
    DhcpProto *GetDhcpProto() { return dhcp_proto_; }
    DnsProto *GetDnsProto() { return dns_proto_; }
    IcmpProto *GetIcmpProto() { return icmp_proto_; }
    FlowProto *GetFlowProto() { return flow_proto_; }

    const Peer *local_peer() const {return local_peer_.get();}
    const Peer *local_vm_peer() const {return local_vm_peer_.get();}
    const Peer *link_local_peer() const {return linklocal_peer_.get();}
    const Peer *ecmp_peer() const {return ecmp_peer_.get();}
    const Peer *vgw_peer() const {return vgw_peer_.get();}

    bool debug() { return debug_; }
    void set_debug(bool debug) { debug_ = debug; }
    VxLanNetworkIdentifierMode vxlan_network_identifier_mode() const {
        return vxlan_network_identifier_mode_;
    }
    bool headless_agent_mode() const {return headless_agent_mode_;}

    void SetInterfaceTable(InterfaceTable *table) {
         intf_table_ = table;
    };

    void SetNextHopTable(NextHopTable *table) {
        nh_table_ = table;
    };

    void SetDefaultInet4UnicastRouteTable(Inet4UnicastAgentRouteTable *
                                                 table) {
        uc_rt_table_ = table;
    };

    void SetDefaultInet4UnicastRouteTable(RouteTable * table) {
        uc_rt_table_ = (Inet4UnicastAgentRouteTable *)table;
    };

    void SetDefaultInet4MulticastRouteTable(Inet4MulticastAgentRouteTable *
                                                   table) {
        mc_rt_table_ = table;
    };

    void SetDefaultInet4MulticastRouteTable(RouteTable *table) {
        mc_rt_table_ = (Inet4MulticastAgentRouteTable *)table;
    };

    void SetDefaultLayer2RouteTable(Layer2AgentRouteTable *table) {
        l2_rt_table_ = table;
    };

    void SetDefaultLayer2RouteTable(RouteTable *table) {
        l2_rt_table_ = (Layer2AgentRouteTable *)table;
    };

    void SetVrfTable(VrfTable *table) {
        vrf_table_ = table;
    };

    void SetVmTable(VmTable *table) {
        vm_table_ = table;
    };

    void SetVnTable(VnTable *table) {
        vn_table_ = table;
    };

    void SetSgTable(SgTable *table) {
        sg_table_ = table;
    }

    void SetAddressTable(AddrTable *table) { 
        addr_table_ = table;
    };

    void SetMplsTable(MplsTable *table) { 
        mpls_table_ = table;
    };
    
    void SetAclTable(AclTable *table) { 
        acl_table_ = table;
    };
    
    void SetIntfCfgTable(CfgIntTable *table) {
        intf_cfg_table_ = table;
    };

    void SetMirrorCfgTable(MirrorCfgTable *table) {
        mirror_cfg_table_ = table;
    }

    void SetIntfMirrorCfgTable(IntfMirrorCfgTable *table) {
        intf_mirror_cfg_table_ = table;
    }

    void SetMirrorTable(MirrorTable *table) {
        mirror_table_ = table;
    };

    void SetDomainConfigTable(DomainConfig *table) {
        domain_config_table_ = table;
    };

    void SetVrfAssignTable(VrfAssignTable *table) {
        vrf_assign_table_ = table;
    };

    void SetVxLanTable(VxLanTable *table) { 
        vxlan_table_ = table;
    };
    
    void SetMirrorPort(uint16_t mirr_port) {
        mirror_src_udp_port_ = mirr_port;
    }

    void SetAgentXmppChannel(AgentXmppChannel *channel, uint8_t idx) {
        agent_xmpp_channel_[idx] = channel;
    };

    void SetAgentIfMapXmppChannel(AgentIfMapXmppChannel *channel, 
                                         uint8_t idx) {
        ifmap_channel_[idx] = channel;
    };

    void SetAgentXmppClient(XmppClient *client, uint8_t idx) {
        xmpp_client_[idx] = client;
    };

    void SetAgentXmppInit(XmppInit *init, uint8_t idx) {
        xmpp_init_[idx] = init;
    };

    void SetAgentDnsXmppChannel(AgentDnsXmppChannel *chnl, uint8_t idx) { 
        dns_xmpp_channel_[idx] = chnl;
    };

    void SetAgentDnsXmppClient(XmppClient *client, uint8_t idx) {
        dns_xmpp_client_[idx] = client;
    };

    void SetAgentDnsXmppInit(XmppInit *xmpp, uint8_t idx) {
        dns_xmpp_init_[idx] = xmpp;
    };

    void SetDiscoveryServiceClient(DiscoveryServiceClient *client) {
        ds_client_ = client;
    };

    void SetMetadataServerPort(uint16_t port) {
        metadata_server_port_ = port;
    };

    void SetAgentStaleCleaner(IFMapAgentStaleCleaner *cl) {
        agent_stale_cleaner_ = cl;
    };

    void SetIfMapAgentParser(IFMapAgentParser *parser) {
        ifmap_parser_ = parser;
    };

    void SetArpProto(ArpProto *proto) { arp_proto_ = proto; }
    void SetDhcpProto(DhcpProto *proto) { dhcp_proto_ = proto; }
    void SetDnsProto(DnsProto *proto) { dns_proto_ = proto; }
    void SetIcmpProto(IcmpProto *proto) { icmp_proto_ = proto; }
    void SetFlowProto(FlowProto *proto) { flow_proto_ = proto; }

    void SetRouterIdConfigured(bool value) {
        router_id_configured_ = value;
    }

    void SetEventManager(EventManager *evm) {
        event_mgr_ = evm;
    }

    void set_vxlan_network_identifier_mode(VxLanNetworkIdentifierMode mode) {
        vxlan_network_identifier_mode_ = mode;
    }

    void set_headless_agent_mode(bool mode) {headless_agent_mode_ = mode;}
    std::string GetUuidStr(boost::uuids::uuid uuid_val) {
        std::ostringstream str;
        str << uuid_val;
        return str.str();
    }
    void GlobalVrouterConfig(IFMapNode *node);

    void set_ksync_sync_mode(bool sync_mode) {
        ksync_sync_mode_ = sync_mode;
    }

    bool ksync_sync_mode() const {
        return ksync_sync_mode_;
    }

    bool test_mode() const { return test_mode_; }
    void set_test_mode(bool test_mode) { test_mode_ = test_mode; }

    uint32_t flow_table_size() const { return flow_table_size_; }
    void set_flow_table_size(uint32_t count) { flow_table_size_ = count; }

    bool isXenMode();

    static Agent *GetInstance() {return singleton_;}

    void Shutdown() {
    }

    DiagTable *diag_table() const;
    void set_diag_table(DiagTable *table);

    void CreateLifetimeManager();
    void ShutdownLifetimeManager();
    void SetAgentTaskPolicy();

    void InitCollector();
    void CreateDBTables();
    void CreateDBClients();
    void CreateVrf();
    void CreateNextHops();
    void CreateInterfaces();
    void InitPeers();
    void InitModules();
    void InitDone();

    void Init(AgentParam *param, AgentInit *init);
    AgentParam *params() const { return params_; }
    AgentInit *init() const { return init_; }

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

    void CopyConfig(AgentParam *params, AgentInit *init);
private:

    AgentParam *params_;
    AgentInit *init_;
    std::auto_ptr<AgentConfig> cfg_;
    std::auto_ptr<AgentStats> stats_;
    std::auto_ptr<KSync> ksync_;
    std::auto_ptr<AgentUve> uve_;
    std::auto_ptr<PktModule> pkt_;
    std::auto_ptr<ServicesModule> services_;
    std::auto_ptr<VirtualGateway> vgw_;
    std::auto_ptr<OperDB> oper_db_;
    std::auto_ptr<DiagTable> diag_table_;
    std::auto_ptr<VNController> controller_;

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
    AddrTable *addr_table_;
    MplsTable *mpls_table_;
    AclTable *acl_table_;
    MirrorTable *mirror_table_;
    VrfAssignTable *vrf_assign_table_;
    VxLanTable *vxlan_table_;

    // Mirror config table
    MirrorCfgTable *mirror_cfg_table_;
    // Interface Mirror config table
    IntfMirrorCfgTable *intf_mirror_cfg_table_;
    
    // Config DB Table handles
    CfgIntTable *intf_cfg_table_;

    // DomainConfig handle
    DomainConfig *domain_config_table_;

    Ip4Address router_id_;
    uint32_t prefix_len_;
    Ip4Address gateway_id_;
    std::string xs_cfg_addr_;
    int8_t xs_idx_;
    std::string xs_addr_[MAX_XMPP_SERVERS];
    uint32_t xs_port_[MAX_XMPP_SERVERS];
    uint64_t xs_stime_[MAX_XMPP_SERVERS];
    int8_t xs_dns_idx_;
    std::string xs_dns_addr_[MAX_XMPP_SERVERS];
    uint32_t xs_dns_port_[MAX_XMPP_SERVERS];
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
    bool debug_;
    bool test_mode_;

    // Flow information
    uint32_t flow_table_size_;

    // Constants
    static const std::string config_file_;
    static const std::string log_file_;
    static const std::string null_str_;
    static std::string fabric_vrf_name_;
    static const std::string fabric_vn_name_;
    static const std::string link_local_vrf_name_;
    static const std::string link_local_vn_name_;
    static const uint8_t vrrp_mac_[ETHER_ADDR_LEN];
    static const std::string bcast_mac_;
};

#endif // vnsw_agent_hpp
