/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_cmn_hpp
#define vnsw_agent_cmn_hpp

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <io/event_manager.h>
#include <base/logging.h>
#include <net/address.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <base/task.h>
#include <base/task_trigger.h>
#include <cmn/agent_db.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <cfg/cfg_listener.h>
#include <io/event_manager.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>

#define MULTICAST_LABEL_RANGE_START 1024
#define MULTICAST_LABEL_BLOCK_SIZE 2048        

class AgentDBEntry;
class XmppClient;
class Interface;
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
class Inet4UcRoute;
class Inet4McRoute;
class Route;
typedef boost::intrusive_ptr<Route> RouteRef;
class NextHop;
typedef boost::intrusive_ptr<NextHop> NextHopRef;
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
class Inet4UcRouteTable;
class Inet4McRouteTable;
class AddrTable;
class CfgIntTable;
class AclTable;
class MirrorTable;
class VrfAssignTable;
class DomainConfig;

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

using namespace boost::uuids;

#define MAX_XMPP_SERVERS 2
#define XMPP_SERVER_PORT 5269
#define DISCOVERY_SERVER_PORT 5998
#define METADATA_IP_ADDR ntohl(inet_addr("169.254.169.254"))
#define METADATA_PORT 8775
#define METADATA_NAT_PORT 80

class Agent {
public:
    const std::string &GetHostName();
    const std::string GetBuildInfo();
    void SetHostName(const std::string &name);
    const std::string &GetProgramName() {return prog_name_;};
    void SetProgramName(const char *name) { prog_name_ = name; };
    const std::string &NullString() {return null_str_;};
    InterfaceTable *GetInterfaceTable() {return intf_table_;};
    MirrorCfgTable *GetMirrorCfgTable() {return mirror_cfg_table_;};
    IntfMirrorCfgTable *GetIntfMirrorCfgTable() {return intf_mirror_cfg_table_;};
    NextHopTable *GetNextHopTable() {return nh_table_;};
    Inet4UcRouteTable *GetDefaultInet4UcRouteTable() {return uc_rt_table_;};
    Inet4McRouteTable *GetDefaultInet4McRouteTable() {return mc_rt_table_;};
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
    void SetSandeshPort(int sandesh_port) { sandesh_port_ = sandesh_port; };
    int GetSandeshPort() { return sandesh_port_;};
    std::string GetMgmtIp() { return mgmt_ip_; }
    void SetMgmtIp(std::string ip) { mgmt_ip_ = ip; }

    EventManager *GetEventManager() {return event_mgr_;};
    DB *GetDB() {return db_;};
    const char *GetHostIfname() {return "pkt0";};

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
    void SetIpFabricItfName(const std::string &name) {
        ip_fabric_intf_name_ = name;
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
    void SetDiscoveryServer(const std::string &addr) {
        dss_addr_ = addr;
    }
    const uint32_t GetDiscoveryServerPort() {
        return dss_port_; 
    }
    void SetDiscoveryServerPort(uint32_t port) {
        dss_port_ = port;
    }
    const int GetDiscoveryXmppServerInstances() {
        return dss_xs_instances_; 
    }
    void SetDiscoveryXmppServerInstances(int xs_instances) {
        dss_xs_instances_ = xs_instances;
    }
   
    const std::string &GetAgentMcastLabelRange(uint8_t idx) { 
        return label_range_[idx]; 
    };
    void SetAgentMcastLabelRange(uint8_t idx) {
        std::stringstream str;
        str << (MULTICAST_LABEL_RANGE_START + (idx * MULTICAST_LABEL_BLOCK_SIZE)) << "-"
            << (MULTICAST_LABEL_RANGE_START + ((idx + 1) * MULTICAST_LABEL_BLOCK_SIZE) - 1); 
        label_range_[idx] = str.str();
    };
    void ResetAgentMcastLabelRange(uint8_t idx) {
        label_range_[idx].clear();
    }

    AgentXmppChannel* GetControlNodeMulticastBuilder() {
        return cn_mcast_builder_;
    };
    void SetControlNodeMulticastBuilder(AgentXmppChannel *peer) { 
        cn_mcast_builder_ =  peer;
    };

    const std::string &GetFabricVnName() {return fabric_vn_name_;};
    const std::string &GetDefaultVrf() {return fabric_vrf_name_;};
    const std::string &GetLinkLocalVnName() {return link_local_vn_name_;}
    const std::string &GetLinkLocalVrfName() {return link_local_vrf_name_;}

    const std::string &GetHostInterfaceName();
    void SetHostInterfaceName(const std::string &name);

    const std::string &GetVirtualHostInterfaceName();
    void SetVirtualHostInterfaceName(const std::string &name);

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
    IFMapAgentParser *GetIfMapAgentParser() {return ifmap_parser_;};
    IFMapAgentStaleCleaner *GetIfMapAgentStaleCleaner() {return agent_stale_cleaner_;};
    
    ArpProto *GetArpProto() { return arp_proto_; }
    DhcpProto *GetDhcpProto() { return dhcp_proto_; }
    DnsProto *GetDnsProto() { return dns_proto_; }
    IcmpProto *GetIcmpProto() { return icmp_proto_; }
    FlowProto *GetFlowProto() { return flow_proto_; }

    const Peer *GetLocalPeer() {return local_peer_;};
    const Peer *GetLocalVmPeer() {return local_vm_peer_;};
    const Peer *GetMdataPeer() {return mdata_vm_peer_;};

    void SetInterfaceTable(InterfaceTable *table) {
         intf_table_ = table;
    };

    void SetNextHopTable(NextHopTable *table) {
        nh_table_ = table;
    };

    void SetDefaultInet4UcRouteTable(Inet4UcRouteTable *table) {
        uc_rt_table_ = table;
    };

    void SetDefaultInet4McRouteTable(Inet4McRouteTable *table) {
        mc_rt_table_ = table;
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

    void SetLocalPeer(Peer *peer) {
        local_peer_ = peer;
    };

    void SetLocalVmPeer(Peer *peer) {
        local_vm_peer_ = peer;
    };

    void SetMdataPeer(Peer *peer) {
        mdata_vm_peer_ = peer;
    };

    void SetRouterIdConfigured(bool value) {
        router_id_configured_ = value;
    }

    void SetEventManager(EventManager *evm) {
        event_mgr_ = evm;
    }

    void SetCfgListener(CfgListener *cfg_listener) {
        cfg_listener_ = cfg_listener;
    }

    CfgListener* GetCfgListener() {
        return cfg_listener_;
    }

    std::string GetUuidStr(uuid uuid_val) {
        std::ostringstream str;
        str << uuid_val;
        return str.str();
    }

    bool IsTestMode() {
        return test_mode_;
    }

    void SetTestMode() {
        test_mode_ = true;
    }

    bool isXenMode();

    Agent() :
        event_mgr_(NULL), agent_xmpp_channel_(), ifmap_channel_(), xmpp_client_(), 
        xmpp_init_(), dns_xmpp_channel_(), dns_xmpp_client_(), dns_xmpp_init_(), 
        agent_stale_cleaner_(NULL), cn_mcast_builder_(NULL), ds_client_(NULL), 
        host_name_(""), prog_name_(""), sandesh_port_(0), 
        db_(NULL), intf_table_(NULL), nh_table_(NULL), uc_rt_table_(NULL), 
        mc_rt_table_(NULL), vrf_table_(NULL), vm_table_(NULL), vn_table_(NULL),
        sg_table_(NULL), addr_table_(NULL), mpls_table_(NULL), acl_table_(NULL),
        mirror_table_(NULL), vrf_assign_table_(NULL), mirror_cfg_table_(NULL),
        intf_mirror_cfg_table_(NULL), intf_cfg_table_(NULL), 
        domain_config_table_(NULL), router_id_(0), prefix_len_(0), 
        gateway_id_(0), xs_cfg_addr_(""), xs_idx_(0), xs_addr_(), xs_port_(), 
        xs_stime_(), xs_dns_idx_(0), xs_dns_addr_(), xs_dns_port_(), 
        dss_addr_(""), dss_port_(0), dss_xs_instances_(0), label_range_(), 
        ip_fabric_intf_name_(""), virtual_host_intf_name_(""), 
        cfg_listener_(NULL), arp_proto_(NULL), dhcp_proto_(NULL),
        dns_proto_(NULL), icmp_proto_(NULL), flow_proto_(NULL),
        local_peer_(NULL), local_vm_peer_(NULL),
        mdata_vm_peer_(NULL), ifmap_parser_(NULL), router_id_configured_(false),
        mirror_src_udp_port_(0), lifetime_manager_(NULL), test_mode_(false), 
        mgmt_ip_("") {

        assert(singleton_ == NULL);
        db_ = new DB();
        assert(db_);

        event_mgr_ = new EventManager();
        assert(event_mgr_);

        SetAgentTaskPolicy();
        CreateLifetimeManager();
    }

    ~Agent() {
        delete event_mgr_;
        event_mgr_ = NULL;

        ShutdownLifetimeManager();

        delete db_;
        db_ = NULL;
    }
    static void Init() {
        singleton_ = new Agent();
    }

    static Agent *GetInstance() {return singleton_;}

    void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    void CreateLifetimeManager();
    void ShutdownLifetimeManager();
    void SetAgentTaskPolicy();

private:
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
    std::string host_name_;
    std::string prog_name_;
    int sandesh_port_;


    // DB handles
    DB *db_;
    InterfaceTable *intf_table_;
    NextHopTable *nh_table_;
    Inet4UcRouteTable *uc_rt_table_;
    Inet4McRouteTable *mc_rt_table_;
    VrfTable *vrf_table_;
    VmTable *vm_table_;
    VnTable *vn_table_;
    SgTable *sg_table_;
    AddrTable *addr_table_;
    MplsTable *mpls_table_;
    AclTable *acl_table_;
    MirrorTable *mirror_table_;
    VrfAssignTable *vrf_assign_table_;

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
    std::string virtual_host_intf_name_;
    CfgListener *cfg_listener_;

    ArpProto *arp_proto_;
    DhcpProto *dhcp_proto_;
    DnsProto *dns_proto_;
    IcmpProto *icmp_proto_;
    FlowProto *flow_proto_;

    Peer *local_peer_;
    Peer *local_vm_peer_;
    Peer *mdata_vm_peer_;
    IFMapAgentParser *ifmap_parser_;
    bool router_id_configured_;

    uint16_t mirror_src_udp_port_;
    LifetimeManager *lifetime_manager_;
    bool test_mode_;
    std::string mgmt_ip_;
    static Agent *singleton_;
    static const std::string null_str_;
    static const std::string fabric_vrf_name_;
    static const std::string fabric_vn_name_;
    static const std::string link_local_vrf_name_;
    static const std::string link_local_vn_name_;
};

class AgentStats {
public:
    AgentStats() 
        : xmpp_reconnect_(), xmpp_in_msgs_(), xmpp_out_msgs_(),
        sandesh_reconnects_(0U), sandesh_in_msgs_(0U), sandesh_out_msgs_(0U),
        sandesh_http_sessions_(0U), nh_count_(0U), pkt_exceptions_(0U),
        pkt_invalid_agent_hdr_(0U), pkt_invalid_interface_(0U), 
        pkt_no_handler_(0U), pkt_dropped_(0U), flow_created_(0U),
        flow_aged_(0U), flow_active_(0U), ipc_in_msgs_(0U), ipc_out_msgs_(0U), 
        in_tpkts_(0U), in_bytes_(0U), out_tpkts_(0U), out_bytes_(0U) {
        assert(singleton_ == NULL);
    }

    static void Init() {
        singleton_ = new AgentStats();
    }

    static AgentStats *GetInstance() {return singleton_;}
    void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    void IncrXmppReconnect(uint8_t idx) {xmpp_reconnect_[idx]++;};
    uint16_t GetXmppReconnect(uint8_t idx) {return xmpp_reconnect_[idx];};

    void IncrXmppInMsgs(uint8_t idx) {xmpp_in_msgs_[idx]++;};
    uint64_t GetXmppInMsgs(uint8_t idx) {return xmpp_in_msgs_[idx];};

    void IncrXmppOutMsgs(uint8_t idx) {xmpp_out_msgs_[idx]++;};
    uint64_t GetXmppOutMsgs(uint8_t idx) {return xmpp_out_msgs_[idx];};

    void IncrSandeshReconnects() {sandesh_reconnects_++;};
    uint16_t GetSandeshReconnects() {
        return sandesh_reconnects_;
    };

    void IncrSandeshInMsgs() {sandesh_in_msgs_++;};
    uint64_t GetSandeshInMsgs() {
        return sandesh_in_msgs_;
    };

    void IncrSandeshOutMsgs() {sandesh_out_msgs_++;};
    uint64_t GetSandeshOutMsgs() {
        return sandesh_out_msgs_;
    };

    void IncrSandeshHttpSessions() {
        sandesh_http_sessions_++;
    };
    uint16_t GetSandeshHttpSessions() {
        return sandesh_http_sessions_;
    };

    void IncrFlowCreated() {flow_created_++;};
    uint64_t GetFlowCreated() {return flow_created_; };

    void IncrFlowAged() {flow_aged_++;};
    uint64_t GetFlowAged() {return flow_aged_;};

    void IncrFlowActive() {flow_active_++;};
    void DecrFlowActive() {flow_active_--;};
    uint64_t GetFlowActive() {return flow_active_;};

    void IncrPktExceptions() {pkt_exceptions_++;};
    uint64_t GetPktExceptions() {return pkt_exceptions_;};

    void IncrPktInvalidAgentHdr() {
        pkt_invalid_agent_hdr_++;
    };
    uint64_t GetPktInvalidAgentHdr() {
        return pkt_invalid_agent_hdr_;
    };

    void IncrPktInvalidInterface() {
        pkt_invalid_interface_++;
    };
    uint64_t GetPktInvalidInterface() {
        return pkt_invalid_interface_;
    };

    void IncrPktNoHandler() {pkt_no_handler_++;};
    uint64_t GetPktNoHandler() {return pkt_no_handler_;};

    void IncrPktDropped() {pkt_dropped_++;};
    uint64_t GetPktDropped() {return pkt_dropped_;};

    void IncrIpcInMsgs() {ipc_in_msgs_++;};
    uint64_t GetIpcInMsgs() {return ipc_out_msgs_;};

    void IncrIpcOutMsgs() {ipc_out_msgs_++;};
    uint64_t GetIpcOutMsgs() {return ipc_out_msgs_;};

    void IncrInPkts(uint64_t count) {
        in_tpkts_ += count;
    };
    uint64_t GetInPkts() {
        return in_tpkts_;
    };
    void IncrInBytes(uint64_t count) {
        in_bytes_ += count;
    };
    uint64_t GetInBytes() {
        return in_bytes_;
    };
    void IncrOutPkts(uint64_t count) {
        out_tpkts_ += count;
    };
    uint64_t GetOutPkts() {
        return out_tpkts_;
    };
    void IncrOutBytes(uint64_t count) {
        out_bytes_ += count;
    };
    uint64_t GetOutBytes() {
        return out_bytes_;
    };
private:
    uint16_t xmpp_reconnect_[MAX_XMPP_SERVERS];
    uint64_t xmpp_in_msgs_[MAX_XMPP_SERVERS];
    uint64_t xmpp_out_msgs_[MAX_XMPP_SERVERS];

    uint16_t sandesh_reconnects_;
    uint64_t sandesh_in_msgs_;
    uint64_t sandesh_out_msgs_;
    uint16_t sandesh_http_sessions_;

    // Number of NH created
    uint32_t nh_count_;

    // Exception packet stats
    uint64_t pkt_exceptions_;
    uint64_t pkt_invalid_agent_hdr_;
    uint64_t pkt_invalid_interface_;
    uint64_t pkt_no_handler_;
    uint64_t pkt_dropped_;

    // Flow stats
    uint64_t flow_created_;
    uint64_t flow_aged_;
    uint64_t flow_active_;

    // Kernel IPC
    uint64_t ipc_in_msgs_;
    uint64_t ipc_out_msgs_;
    uint64_t in_tpkts_;
    uint64_t in_bytes_;
    uint64_t out_tpkts_;
    uint64_t out_bytes_;

    static AgentStats *singleton_;
};

class AgentInit {
public:
    enum State {
        MOD_INIT,
        STATIC_OBJ_OPERDB,
        STATIC_OBJ_PKT,
        CONFIG_INIT,
        CONFIG_RUN,
        INIT_DONE,
    };

    static void Init(bool ksync_init, bool pkt_init, bool services_init,
                     const char *init_file, int sandesh_port, bool log,
                     std::string log_category, std::string log_level,
                     std::string collector_addr, int collector_port, 
                     bool create_vhost) {
        instance_ = new AgentInit(ksync_init, pkt_init, services_init,
                                  init_file, sandesh_port, log, log_category,
                                  log_level, collector_addr, collector_port,
                                  create_vhost);
    }
    void Shutdown() {
        if (instance_)
            delete instance_;
        instance_ = NULL;
    }
    ~AgentInit() {
        for (std::vector<TaskTrigger *>::iterator it = list_.begin();
             it != list_.end(); ++it) {
            (*it)->Reset();
            delete *it;
        }
    }
    bool Run();
    void Trigger() {
        trigger_ = new TaskTrigger(boost::bind(&AgentInit::Run, this),
                       TaskScheduler::GetInstance()->GetTaskId(
                           "db::DBTable"), 0);
        list_.push_back(trigger_);
        trigger_->Set();
    }
    std::string GetState() { 
        std::string StateString[] = {
            "mod_init",
            "static_obj_operdb",
            "static_obj_pkt",
            "config_init",
            "config_run",
            "init_done",
        };
        return StateString[state_]; 
    }
    static AgentInit *GetInstance() { return instance_; }
    std::string GetCollectorServer() { return collector_server_; }
    int GetCollectorPort() { return collector_server_port_; }

private:
    AgentInit(bool ksync_init, bool pkt_init, bool services_init,
              const char *init_file, int sandesh_port, bool log,
              std::string log_category, std::string log_level,
              std::string collector_addr, int collector_port,bool create_vhost) 
            : state_(MOD_INIT), ksync_init_(ksync_init), pkt_init_(pkt_init), 
              services_init_(services_init), init_file_(init_file), 
              sandesh_port_(sandesh_port), 
		      log_locally_(log), log_category_(log_category),
		      log_level_(log_level), collector_server_(collector_addr),
		      collector_server_port_(collector_port), 
              create_vhost_(create_vhost), trigger_(NULL){}
    void InitModules();

    State state_;
    bool ksync_init_;
    bool pkt_init_;
    bool services_init_;
    const char *init_file_;
    int sandesh_port_;
    bool log_locally_;
    std::string log_category_;
    std::string log_level_;
    std::string collector_server_; 
    int collector_server_port_;
    bool create_vhost_;
    TaskTrigger *trigger_;
    std::vector<TaskTrigger *> list_;
    static AgentInit *instance_;
};

static inline bool UnregisterDBTable(DBTable *table, 
                                     DBTableBase::ListenerId id) {
    table->Unregister(id);
    return true;
}

static inline TaskTrigger *SafeDBUnregister(DBTable *table,
                                            DBTableBase::ListenerId id) {
    TaskTrigger *trigger = 
           new TaskTrigger(boost::bind(&UnregisterDBTable, table, id),
               TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    trigger->Set();
    return trigger;
}

static inline void CfgUuidSet(uint64_t ms_long, uint64_t ls_long,
                              boost::uuids::uuid &u) {
    for (int i = 0; i < 8; i++) {
        u.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        u.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
}

extern SandeshTraceBufferPtr OperDBTraceBuf;

#define OPER_TRACE(obj, ...)\
do {\
   Oper##obj::TraceMsg(OperDBTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#define IFMAP_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::IFMAP)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

#define AGENT_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);

#define AGENT_LOG(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_INFO, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);

#endif // vnsw_agent_cmn_hpp

