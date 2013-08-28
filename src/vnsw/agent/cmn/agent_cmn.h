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
    static const std::string &GetHostName();
    static const std::string GetBuildInfo();
    static void SetHostName(const std::string &name);
    static const std::string &GetProgramName() {return prog_name_;};
    static void SetProgramName(const char *name) { prog_name_ = name; };
    static const std::string &NullString() {return null_str_;};
    static InterfaceTable *GetInterfaceTable() {return intf_table_;};
    static MirrorCfgTable *GetMirrorCfgTable() {return mirror_cfg_table_;};
    static IntfMirrorCfgTable *GetIntfMirrorCfgTable() {return intf_mirror_cfg_table_;};
    static NextHopTable *GetNextHopTable() {return nh_table_;};
    static Inet4UcRouteTable *GetDefaultInet4UcRouteTable() {return uc_rt_table_;};
    static Inet4McRouteTable *GetDefaultInet4McRouteTable() {return mc_rt_table_;};
    static VrfTable *GetVrfTable() { return vrf_table_;};
    static VmTable *GetVmTable() { return vm_table_;};
    static VnTable *GetVnTable() { return vn_table_;};
    static SgTable *GetSgTable() { return sg_table_;};
    static AddrTable *GetAddressTable() { return addr_table_;};
    static MplsTable *GetMplsTable() { return mpls_table_;};
    static AclTable *GetAclTable() { return acl_table_;};
    static MirrorTable *GetMirrorTable() { return mirror_table_;};
    static CfgIntTable *GetIntfCfgTable() {return intf_cfg_table_;};
    static VrfAssignTable *GetVrfAssignTable() {return vrf_assign_table_;};
    static void SetSandeshPort(int sandesh_port) { sandesh_port_ = sandesh_port; };
    static int GetSandeshPort() { return sandesh_port_;};
    static std::string GetCollector() { return collector_; }
    static void SetCollector(std::string srv) { collector_ = srv; }
    static int GetCollectorPort() { return collector_port_; }
    static void SetCollectorPort(int port) { collector_port_ = port; }

    static EventManager *GetEventManager() {return event_mgr_;};
    static DB *GetDB() {return db_;};
    static const char *GetHostIfname() {return "pkt0";};

    static uint16_t GetMirrorPort() {return mirror_src_udp_port_;};
    static Ip4Address GetRouterId() {return router_id_; };
    static void SetRouterId(const Ip4Address &addr) {
        router_id_ = addr;
        SetRouterIdConfigured(true);
    };

    static uint32_t GetPrefixLen() {return prefix_len_;};
    static void SetPrefixLen(uint32_t plen) {prefix_len_ = plen;};

    static bool GetRouterIdConfigured() { return router_id_configured_; }
    static LifetimeManager *GetLifetimeManager() { return lifetime_manager_;};

    static Ip4Address GetGatewayId() {return gateway_id_; };
    static void SetGatewayId(const Ip4Address &addr) {gateway_id_ = addr;};

    static const std::string &GetIpFabricItfName() {
        return ip_fabric_intf_name_;
    };
    static void SetIpFabricItfName(const std::string &name) {
        ip_fabric_intf_name_ = name;
    };

    static const std::string &GetXmppCfgServer() {return xs_cfg_addr_; };
    static const int8_t &GetXmppCfgServerIdx() {return xs_idx_; };
    static void SetXmppCfgServer(const std::string &addr, uint8_t xs_idx) {
        xs_cfg_addr_ = addr;
        xs_idx_ = xs_idx;
    };
    static void ResetXmppCfgServer() {
        xs_cfg_addr_.clear();
        xs_idx_ = -1;
    }
    static const std::string &GetXmppServer(uint8_t idx) {return xs_addr_[idx]; };
    static const uint32_t GetXmppPort(uint8_t idx) {return xs_port_[idx]; };
    static void SetXmppServer(const std::string &addr, uint8_t idx) {
        xs_addr_[idx] = addr;
    };

    static void SetXmppPort(uint32_t port, uint8_t idx) {
        xs_port_[idx] = port;
    };

    static const uint64_t GetAgentXmppChannelSetupTime(uint8_t idx) {return xs_stime_[idx];}
    static void SetAgentXmppChannelSetupTime(uint64_t time, uint8_t idx) {xs_stime_[idx] = time;}
 
    static const int8_t &GetXmppDnsCfgServerIdx() {return xs_dns_idx_; };
    static void SetXmppDnsCfgServer(uint8_t xs_idx) { xs_dns_idx_ = xs_idx; };
    static const std::string &GetDnsXmppServer(uint8_t idx) {
        return xs_dns_addr_[idx]; 
    }
    static void SetDnsXmppServer(const std::string &addr, uint8_t idx) {
        xs_dns_addr_[idx] = addr;
    }

    static const uint32_t GetDnsXmppPort(uint8_t idx) {
        return xs_dns_port_[idx]; 
    }
    static void SetDnsXmppPort(uint32_t port, uint8_t idx) {
        xs_dns_port_[idx] = port;
    }

    /* Discovery Server, port, service-instances */
    static const std::string &GetDiscoveryServer() {
        return dss_addr_; 
    }
    static void SetDiscoveryServer(const std::string &addr) {
        dss_addr_ = addr;
    }
    static const uint32_t GetDiscoveryServerPort() {
        return dss_port_; 
    }
    static void SetDiscoveryServerPort(uint32_t port) {
        dss_port_ = port;
    }
    static const int GetDiscoveryXmppServerInstances() {
        return dss_xs_instances_; 
    }
    static void SetDiscoveryXmppServerInstances(int xs_instances) {
        dss_xs_instances_ = xs_instances;
    }
   
   
    static const std::string &GetAgentMcastLabelRange(uint8_t idx) { 
        return label_range_[idx]; 
    };
    static void SetAgentMcastLabelRange(uint8_t idx) {
        std::stringstream str;
        str << (MULTICAST_LABEL_RANGE_START + (idx * MULTICAST_LABEL_BLOCK_SIZE)) << "-"
            << (MULTICAST_LABEL_RANGE_START + ((idx + 1) * MULTICAST_LABEL_BLOCK_SIZE) - 1); 
        label_range_[idx] = str.str();
    };
    static void ResetAgentMcastLabelRange(uint8_t idx) {
        label_range_[idx].clear();
    }

    static AgentXmppChannel* GetControlNodeMulticastBuilder() {
        return cn_mcast_builder_;
    };
    static void SetControlNodeMulticastBuilder(AgentXmppChannel *peer) { 
        cn_mcast_builder_ =  peer;
    };

    static const std::string &GetFabricVnName() {return fabric_vn_name_;};
    static const std::string &GetDefaultVrf() {return fabric_vrf_name_;};
    static const std::string &GetLinkLocalVnName() {return link_local_vn_name_;}
    static const std::string &GetLinkLocalVrfName() {return link_local_vrf_name_;}

    static const std::string &GetHostInterfaceName();
    static void SetHostInterfaceName(const std::string &name);

    static const std::string &GetVirtualHostInterfaceName();
    static void SetVirtualHostInterfaceName(const std::string &name);

    static AgentXmppChannel *GetAgentXmppChannel(uint8_t idx) { 
        return agent_xmpp_channel_[idx];
    };
    static AgentIfMapXmppChannel *GetAgentIfMapXmppChannel(uint8_t idx) { 
        return ifmap_channel_[idx];
    };
    static XmppClient *GetAgentXmppClient(uint8_t idx) {
        return xmpp_client_[idx];
    };
    static XmppInit *GetAgentXmppInit(uint8_t idx) {
        return xmpp_init_[idx];
    };
    static AgentDnsXmppChannel *GetAgentDnsXmppChannel(uint8_t idx) { 
        return dns_xmpp_channel_[idx];
    };
    static XmppClient *GetAgentDnsXmppClient(uint8_t idx) {
        return dns_xmpp_client_[idx];
    };
    static XmppInit *GetAgentDnsXmppInit(uint8_t idx) {
        return dns_xmpp_init_[idx];
    };
    static DiscoveryServiceClient *GetDiscoveryServiceClient() {
        return ds_client_; 
    };
    static IFMapAgentParser *GetIfMapAgentParser() {return ifmap_parser_;};
    static IFMapAgentStaleCleaner *GetIfMapAgentStaleCleaner() {return agent_stale_cleaner_;};
    
    static const Peer *GetLocalPeer() {return local_peer_;};
    static const Peer *GetLocalVmPeer() {return local_vm_peer_;};
    static const Peer *GetMdataPeer() {return mdata_vm_peer_;};

    static void SetInterfaceTable(InterfaceTable *table) {
         intf_table_ = table;
    };

    static void SetNextHopTable(NextHopTable *table) {
        nh_table_ = table;
    };

    static void SetDefaultInet4UcRouteTable(Inet4UcRouteTable *table) {
        uc_rt_table_ = table;
    };

    static void SetDefaultInet4McRouteTable(Inet4McRouteTable *table) {
        mc_rt_table_ = table;
    };

    static void SetVrfTable(VrfTable *table) {
        vrf_table_ = table;
    };

    static void SetVmTable(VmTable *table) {
        vm_table_ = table;
    };

    static void SetVnTable(VnTable *table) {
        vn_table_ = table;
    };

    static void SetSgTable(SgTable *table) {
        sg_table_ = table;
    }

    static void SetAddressTable(AddrTable *table) { 
        addr_table_ = table;
    };

    static void SetMplsTable(MplsTable *table) { 
        mpls_table_ = table;
    };
    
    static void SetAclTable(AclTable *table) { 
        acl_table_ = table;
    };
    
    static void SetIntfCfgTable(CfgIntTable *table) {
        intf_cfg_table_ = table;
    };

    static void SetMirrorCfgTable(MirrorCfgTable *table) {
        mirror_cfg_table_ = table;
    }

    static void SetIntfMirrorCfgTable(IntfMirrorCfgTable *table) {
        intf_mirror_cfg_table_ = table;
    }

    static void SetMirrorTable(MirrorTable *table) {
        mirror_table_ = table;
    };

    static void SetVrfAssignTable(VrfAssignTable *table) {
        vrf_assign_table_ = table;
    };

    static void SetMirrorPort(uint16_t mirr_port) {
        mirror_src_udp_port_ = mirr_port;
    }

    static void SetAgentXmppChannel(AgentXmppChannel *channel, uint8_t idx) {
        agent_xmpp_channel_[idx] = channel;
    };

    static void SetAgentIfMapXmppChannel(AgentIfMapXmppChannel *channel, 
                                         uint8_t idx) {
        ifmap_channel_[idx] = channel;
    };

    static void SetAgentXmppClient(XmppClient *client, uint8_t idx) {
        xmpp_client_[idx] = client;
    };

    static void SetAgentXmppInit(XmppInit *init, uint8_t idx) {
        xmpp_init_[idx] = init;
    };

    static void SetAgentDnsXmppChannel(AgentDnsXmppChannel *chnl, uint8_t idx) { 
        dns_xmpp_channel_[idx] = chnl;
    };

    static void SetAgentDnsXmppClient(XmppClient *client, uint8_t idx) {
        dns_xmpp_client_[idx] = client;
    };

    static void SetAgentDnsXmppInit(XmppInit *xmpp, uint8_t idx) {
        dns_xmpp_init_[idx] = xmpp;
    };

    static void SetDiscoveryServiceClient(DiscoveryServiceClient *client) {
        ds_client_ = client;
    };

    static void SetAgentStaleCleaner(IFMapAgentStaleCleaner *cl) {
        agent_stale_cleaner_ = cl;
    };

    static void SetIfMapAgentParser(IFMapAgentParser *parser) {
        ifmap_parser_ = parser;
    };

    static void SetLocalPeer(Peer *peer) {
        local_peer_ = peer;
    };

    static void SetLocalVmPeer(Peer *peer) {
        local_vm_peer_ = peer;
    };

    static void SetMdataPeer(Peer *peer) {
        mdata_vm_peer_ = peer;
    };

    static void SetRouterIdConfigured(bool value) {
        router_id_configured_ = value;
    }

    static void SetEventManager(EventManager *evm) {
        event_mgr_ = evm;
    }

    static void SetCfgListener(CfgListener *cfg_listener) {
        cfg_listener_ = cfg_listener;
    }

    static CfgListener* GetCfgListener() {
        return cfg_listener_;
    }

    static std::string GetUuidStr(uuid uuid_val) {
        std::ostringstream str;
        str << uuid_val;
        return str.str();
    }

    static bool IsTestMode() {
        return test_mode_;
    }

    static void SetTestMode() {
        test_mode_ = true;
    }

    static bool isXenMode();

    static void Init() {
        db_ = new DB();
        assert(db_);
        EventManager *evm;

        evm = new EventManager();
        assert(evm);
        SetEventManager(evm);

        SetAgentTaskPolicy();
        CreateLifetimeManager();
    };

    static void Shutdown() {
        delete GetEventManager();
        SetEventManager(NULL);

        ShutdownLifetimeManager();

        delete db_;
        db_ = NULL;
    }

    static void CreateLifetimeManager();
    static void ShutdownLifetimeManager();
    static void SetAgentTaskPolicy();

private:
    static EventManager *event_mgr_;
    static AgentXmppChannel *agent_xmpp_channel_[MAX_XMPP_SERVERS];
    static AgentIfMapXmppChannel *ifmap_channel_[MAX_XMPP_SERVERS];
    static XmppClient *xmpp_client_[MAX_XMPP_SERVERS];
    static XmppInit *xmpp_init_[MAX_XMPP_SERVERS];
    static AgentDnsXmppChannel *dns_xmpp_channel_[MAX_XMPP_SERVERS];
    static XmppClient *dns_xmpp_client_[MAX_XMPP_SERVERS];
    static XmppInit *dns_xmpp_init_[MAX_XMPP_SERVERS];
    static IFMapAgentStaleCleaner *agent_stale_cleaner_;
    static AgentXmppChannel *cn_mcast_builder_;
    static DiscoveryServiceClient *ds_client_;
    static std::string host_name_;
    static std::string prog_name_;
    static int sandesh_port_;

    static std::string null_str_;

    // DB handles
    static DB *db_;
    static InterfaceTable *intf_table_;
    static NextHopTable *nh_table_;
    static Inet4UcRouteTable *uc_rt_table_;
    static Inet4McRouteTable *mc_rt_table_;
    static VrfTable *vrf_table_;
    static VmTable *vm_table_;
    static VnTable *vn_table_;
    static SgTable *sg_table_;
    static AddrTable *addr_table_;
    static MplsTable *mpls_table_;
    static AclTable *acl_table_;
    static MirrorTable *mirror_table_;
    static VrfAssignTable *vrf_assign_table_;

    // Mirror config table
    static MirrorCfgTable *mirror_cfg_table_;
    // Interface Mirror config table
    static IntfMirrorCfgTable *intf_mirror_cfg_table_;
    
    // Config DB Table handles
    static CfgIntTable *intf_cfg_table_;

    static Ip4Address router_id_;
    static uint32_t prefix_len_;
    static Ip4Address gateway_id_;
    static std::string fabric_vrf_name_;
    static std::string fabric_vn_name_;
    static std::string link_local_vrf_name_;
    static std::string link_local_vn_name_;
    static std::string xs_cfg_addr_;
    static int8_t xs_idx_;
    static std::string xs_addr_[MAX_XMPP_SERVERS];
    static uint32_t xs_port_[MAX_XMPP_SERVERS];
    static uint64_t xs_stime_[MAX_XMPP_SERVERS];
    static int8_t xs_dns_idx_;
    static std::string xs_dns_addr_[MAX_XMPP_SERVERS];
    static uint32_t xs_dns_port_[MAX_XMPP_SERVERS];
    static std::string dss_addr_;
    static uint32_t dss_port_;
    static int dss_xs_instances_;
    static std::string label_range_[MAX_XMPP_SERVERS];
    static std::string ip_fabric_intf_name_;
    static std::string virtual_host_intf_name_;
    static CfgListener *cfg_listener_;

    static Peer *local_peer_;
    static Peer *local_vm_peer_;
    static Peer *mdata_vm_peer_;
    static IFMapAgentParser *ifmap_parser_;
    static bool router_id_configured_;

    static uint16_t mirror_src_udp_port_;
    static LifetimeManager *lifetime_manager_;
    static bool test_mode_;
    static std::string collector_;
    static int collector_port_;
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

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static void IncrXmppReconnect(uint8_t idx) {singleton_->xmpp_reconnect_[idx]++;};
    static uint16_t GetXmppReconnect(uint8_t idx) {return singleton_->xmpp_reconnect_[idx];};

    static void IncrXmppInMsgs(uint8_t idx) {singleton_->xmpp_in_msgs_[idx]++;};
    static uint64_t GetXmppInMsgs(uint8_t idx) {return singleton_->xmpp_in_msgs_[idx];};

    static void IncrXmppOutMsgs(uint8_t idx) {singleton_->xmpp_out_msgs_[idx]++;};
    static uint64_t GetXmppOutMsgs(uint8_t idx) {return singleton_->xmpp_out_msgs_[idx];};

    static void IncrSandeshReconnects() {singleton_->sandesh_reconnects_++;};
    static uint16_t GetSandeshReconnects() {
        return singleton_->sandesh_reconnects_;
    };

    static void IncrSandeshInMsgs() {singleton_->sandesh_in_msgs_++;};
    static uint64_t GetSandeshInMsgs() {
        return singleton_->sandesh_in_msgs_;
    };

    static void IncrSandeshOutMsgs() {singleton_->sandesh_out_msgs_++;};
    static uint64_t GetSandeshOutMsgs() {
        return singleton_->sandesh_out_msgs_;
    };

    static void IncrSandeshHttpSessions() {
        singleton_->sandesh_http_sessions_++;
    };
    static uint16_t GetSandeshHttpSessions() {
        return singleton_->sandesh_http_sessions_;
    };

    static void IncrFlowCreated() {singleton_->flow_created_++;};
    static uint64_t GetFlowCreated() {return singleton_->flow_created_; };

    static void IncrFlowAged() {singleton_->flow_aged_++;};
    static uint64_t GetFlowAged() {return singleton_->flow_aged_;};

    static void IncrFlowActive() {singleton_->flow_active_++;};
    static void DecrFlowActive() {singleton_->flow_active_--;};
    static uint64_t GetFlowActive() {return singleton_->flow_active_;};

    static void IncrPktExceptions() {singleton_->pkt_exceptions_++;};
    static uint64_t GetPktExceptions() {return singleton_->pkt_exceptions_;};

    static void IncrPktInvalidAgentHdr() {
        singleton_->pkt_invalid_agent_hdr_++;
    };
    static uint64_t GetPktInvalidAgentHdr() {
        return singleton_->pkt_invalid_agent_hdr_;
    };

    static void IncrPktInvalidInterface() {
        singleton_->pkt_invalid_interface_++;
    };
    static uint64_t GetPktInvalidInterface() {
        return singleton_->pkt_invalid_interface_;
    };

    static void IncrPktNoHandler() {singleton_->pkt_no_handler_++;};
    static uint64_t GetPktNoHandler() {return singleton_->pkt_no_handler_;};

    static void IncrPktDropped() {singleton_->pkt_dropped_++;};
    static uint64_t GetPktDropped() {return singleton_->pkt_dropped_;};

    static void IncrIpcInMsgs() {singleton_->ipc_in_msgs_++;};
    static uint64_t GetIpcInMsgs() {return singleton_->ipc_out_msgs_;};

    static void IncrIpcOutMsgs() {singleton_->ipc_out_msgs_++;};
    static uint64_t GetIpcOutMsgs() {return singleton_->ipc_out_msgs_;};

    static void IncrInPkts(uint64_t count) {
        singleton_->in_tpkts_ += count;
    };
    static uint64_t GetInPkts() {
        return singleton_->in_tpkts_;
    };
    static void IncrInBytes(uint64_t count) {
        singleton_->in_bytes_ += count;
    };
    static uint64_t GetInBytes() {
        return singleton_->in_bytes_;
    };
    static void IncrOutPkts(uint64_t count) {
        singleton_->out_tpkts_ += count;
    };
    static uint64_t GetOutPkts() {
        return singleton_->out_tpkts_;
    };
    static void IncrOutBytes(uint64_t count) {
        singleton_->out_bytes_ += count;
    };
    static uint64_t GetOutBytes() {
        return singleton_->out_bytes_;
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
    static void Shutdown() {
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

static inline std::string UuidToString(const boost::uuids::uuid &id)
{
    std::stringstream uuidstring;
    uuidstring << id;
    return uuidstring.str();
}

static inline boost::uuids::uuid StringToUuid(const std::string &str)
{
    boost::uuids::uuid u = boost::uuids::nil_uuid();
    std::stringstream uuidstring(str);
    uuidstring >> u;
    return u;
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

