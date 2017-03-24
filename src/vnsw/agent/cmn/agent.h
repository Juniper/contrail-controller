/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_hpp
#define vnsw_agent_hpp

#include <vector>
#include <stdint.h>
#include <string>
#include <net/ethernet.h>
#include <boost/intrusive_ptr.hpp>
#include <base/intrusive_ptr_back_ref.h>
#include <cmn/agent_cmn.h>
#include <base/connection_info.h>
#include "net/mac_address.h"
#include "oper/agent_types.h"

class Agent;
class AgentParam;
class AgentConfig;
class AgentStats;
class KSync;
class AgentUveBase;
class PktModule;
class VirtualGateway;
class ServicesModule;
class MulticastHandler;
class AgentDBEntry;
class XmppClient;
class OperDB;
class AgentRoute;
class TaskScheduler;
class AgentInit;
class AgentStatsCollector;
class FlowStatsCollector;
class FlowStatsManager;
class MetaDataIpAllocator;
class ResourceManager;
namespace OVSDB {
class OvsdbClient;
};
class ConfigManager;
class EventNotifier;

class Interface;
typedef boost::intrusive_ptr<Interface> InterfaceRef;
typedef boost::intrusive_ptr<const Interface> InterfaceConstRef;
void intrusive_ptr_release(const Interface* p);
void intrusive_ptr_add_ref(const Interface* p);
typedef IntrusivePtrRef<Interface> InterfaceBackRef;
void intrusive_ptr_add_back_ref(const IntrusiveReferrer ref, const Interface* p);
void intrusive_ptr_del_back_ref(const IntrusiveReferrer ref, const Interface* p);

class VmEntry;
typedef boost::intrusive_ptr<VmEntry> VmEntryRef;
typedef boost::intrusive_ptr<const VmEntry> VmEntryConstRef;
void intrusive_ptr_release(const VmEntry* p);
void intrusive_ptr_add_ref(const VmEntry* p);
typedef IntrusivePtrRef<VmEntry> VmEntryBackRef;
typedef IntrusivePtrRef<const VmEntry> VmEntryConstBackRef;
void intrusive_ptr_add_back_ref(const IntrusiveReferrer ref, const VmEntry* p);
void intrusive_ptr_del_back_ref(const IntrusiveReferrer ref, const VmEntry* p);

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
typedef IntrusivePtrRef<VrfEntry> VrfEntryRef;
typedef IntrusivePtrRef<const VrfEntry> VrfEntryConstRef;
void intrusive_ptr_add_back_ref(const IntrusiveReferrer ref, const VrfEntry* p);
void intrusive_ptr_del_back_ref(const IntrusiveReferrer ref, const VrfEntry* p);
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

class InetUnicastRouteEntry;
class Inet4MulticastRouteEntry;
class EvpnRouteEntry;
class BridgeRouteEntry;
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

class PhysicalDevice;
typedef boost::intrusive_ptr<PhysicalDevice> PhysicalDeviceRef;
typedef boost::intrusive_ptr<const PhysicalDevice> PhysicalDeviceConstRef;
void intrusive_ptr_release(const PhysicalDevice *p);
void intrusive_ptr_add_ref(const PhysicalDevice *p);

class PhysicalDeviceVn;
typedef boost::intrusive_ptr<PhysicalDeviceVn> PhysicalDeviceVnRef;
typedef boost::intrusive_ptr<const PhysicalDeviceVn> PhysicalDeviceVnConstRef;
void intrusive_ptr_release(const PhysicalDeviceVn *p);
void intrusive_ptr_add_ref(const PhysicalDeviceVn *p);

class HealthCheckService;
typedef boost::intrusive_ptr<HealthCheckService> HealthCheckServiceRef;
void intrusive_ptr_release(const HealthCheckService* p);
void intrusive_ptr_add_ref(const HealthCheckService* p);

class ForwardingClass;
typedef boost::intrusive_ptr<ForwardingClass> ForwardingClassRef;
typedef boost::intrusive_ptr<const ForwardingClass> ForwardingClassConstRef;
void intrusive_ptr_release(const ForwardingClass *p);
void intrusive_ptr_add_ref(const ForwardingClass *p);

class AgentQosConfig;
typedef boost::intrusive_ptr<AgentQosConfig> AgentQosConfigRef;
typedef boost::intrusive_ptr<const AgentQosConfig> AgentQosConfigConstRef;
void intrusive_ptr_release(const AgentQosConfig *p);
void intrusive_ptr_add_ref(const AgentQosConfig *p);

class QosQueue;
typedef boost::intrusive_ptr<QosQueue> QosQueueRef;
typedef boost::intrusive_ptr<const QosQueue> QosQueueConstRef;
void intrusive_ptr_release(const QosQueueRef *p);
void intrusive_ptr_add_ref(const QosQueueRef *p);

class BridgeDomainEntry;
typedef boost::intrusive_ptr<BridgeDomainEntry> BridgeDomainRef;
typedef boost::intrusive_ptr<const BridgeDomainEntry> BridgeDomainConstRef;
void intrusive_ptr_release(const BridgeDomainEntry *p);
void intrusive_ptr_add_ref(const BridgeDomainEntry *p);

//class SecurityGroup;
typedef std::vector<int> SecurityGroupList;
typedef std::vector<std::string> CommunityList;

typedef std::set<std::string> VnListType;

class AgentDBTable;
class InterfaceTable;
class HealthCheckTable;
class NextHopTable;
class VmTable;
class VnTable;
class SgTable;
class VrfTable;
class MplsTable;
class RouteTable;
class AgentRouteTable;
class InetUnicastAgentRouteTable;
class Inet4MulticastAgentRouteTable;
class EvpnAgentRouteTable;
class BridgeAgentRouteTable;
class AclTable;
class MirrorTable;
class VrfAssignTable;
class DomainConfig;
class VxLanTable;
class MulticastGroupObject;
class PhysicalDeviceTable;
class PhysicalDeviceVnTable;
class ForwardingClassTable;
class AgentQosConfigTable;
class QosQueueTable;
class MirrorCfgTable;
class IntfMirrorCfgTable;
class BridgeDomainTable;

class XmppInit;
class AgentXmppChannel;
class AgentIfMapXmppChannel;
class AgentDnsXmppChannel;
class EventManager;
class TaskTbbKeepAwake;
class IFMapAgentStaleCleaner;

class ArpProto;
class DhcpProto;
class Dhcpv6Proto;
class DnsProto;
class IcmpProto;
class Icmpv6Proto;
class FlowProto;

class Peer;
class LifetimeManager;
class DiagTable;
class VNController;
class AgentSignal;
class ServiceInstanceTable;
class Agent;
class RESTServer;
class PortIpcHandler;
class MacLearningProto;
class MacLearningModule;

extern void RouterIdDepInit(Agent *agent);

#define MULTICAST_LABEL_RANGE_START 1024
#define MULTICAST_LABEL_BLOCK_SIZE 2048        

#define MIN_UNICAST_LABEL_RANGE 4098
#define MAX_XMPP_SERVERS 2
#define XMPP_SERVER_PORT 5269
#define METADATA_IP_ADDR ntohl(inet_addr("169.254.169.254"))
#define METADATA_PORT 8775
#define METADATA_NAT_PORT 80
#define AGENT_INIT_TASKNAME "Agent::Init"
#define INSTANCE_MANAGER_TASK_NAME "Agent::InstanceManager"
#define AGENT_SHUTDOWN_TASKNAME "Agent::Shutdown"
#define AGENT_FLOW_STATS_MANAGER_TASK "Agent::FlowStatsManager"
#define AGENT_SANDESH_TASKNAME "Agent::Sandesh"
#define IPV4_MULTICAST_BASE_ADDRESS "224.0.0.0"
#define IPV6_MULTICAST_BASE_ADDRESS "ff00::"
#define MULTICAST_BASE_ADDRESS_PLEN 8

#define VROUTER_SERVER_PORT 20914

/****************************************************************************
 * Definitions related to config/resource backup/restore
 ****************************************************************************/
#define CFG_BACKUP_DIR "/var/lib/contrail/backup"
#define CFG_BACKUP_COUNT 2
#define CFG_BACKUP_IDLE_TIMEOUT (10*1000)
#define CFG_RESTORE_AUDIT_TIMEOUT (15*1000)

/****************************************************************************
 * Task names
 ****************************************************************************/
#define kTaskFlowEvent "Agent::FlowEvent"
#define kTaskFlowKSync "Agent::FlowKSync"
#define kTaskFlowUpdate "Agent::FlowUpdate"
#define kTaskFlowDelete "Agent::FlowDelete"
#define kTaskFlowMgmt "Agent::FlowMgmt"
#define kTaskFlowAudit "KSync::FlowAudit"
#define kTaskFlowLogging "Agent::FlowLogging"
#define kTaskFlowStatsCollector "Flow::StatsCollector"
#define kTaskFlowStatsUpdate "Agent::FlowStatsUpdate"

#define kTaskHealthCheck "Agent::HealthCheck"

#define kTaskDBExclude "Agent::DBExcludeTask"
#define kTaskConfigManager "Agent::ConfigManager"
#define kTaskHttpRequstHandler "http::RequestHandlerTask"
#define kAgentResourceRestoreTask    "Agent::ResoureRestore"
#define kAgentResourceBackUpTask     "Agent::ResourceBackup"
#define kTaskMacLearning "Agent::MacLearning"
#define kTaskMacLearningMgmt "Agent::MacLearningMgmt"
#define kTaskMacAging "Agent::MacAging"

#define kInterfaceDbTablePrefix "db.interface"
#define kVnDbTablePrefix  "db.vn"
#define kVmDbTablePrefix  "db.vm"
#define kVrfDbTablePrefix "db.vrf.0"
#define kMplsDbTablePrefix "db.mpls"
#define kAclDbTablePrefix  "db.acl"
#define kV4UnicastRouteDbTableSuffix "uc.route.0"
#define kV6UnicastRouteDbTableSuffix "uc.route6.0"
#define kL2RouteDbTableSuffix  "l2.route.0"
#define kMcastRouteDbTableSuffix "mc.route.0"
#define kEvpnRouteDbTableSuffix  "evpn.route.0"
#define kEventNotifierTask "Agent::EventNotifier"

class Agent {
public:
    static const uint32_t kDefaultMaxLinkLocalOpenFds = 2048;
    // max open files in the agent, excluding the linklocal bind ports
    static const uint32_t kMaxOtherOpenFds = 512;
    // max BGP-as-a-server sessions, for which local ports are reserved
    static const uint32_t kMaxBgpAsAServerSessions = 512;
    // default timeout zero means, this timeout is not used
    static const uint32_t kDefaultFlowCacheTimeout = 0;
    // default number of flow index-manager events logged per flow
    static const uint32_t kDefaultFlowIndexSmLogCount = 0;
    // default number of threads for flow setup
    static const uint32_t kDefaultFlowThreadCount = 1;
    // Log a message if latency in processing flow queue exceeds limit
    static const uint32_t kDefaultFlowLatencyLimit = 0;
    // Max number of threads
    static const uint32_t kMaxTbbThreads = 8;
    static const uint32_t kDefaultTbbKeepawakeTimeout = (20); //time-millisecs
    // Default number of tx-buffers on pkt0 interface
    static const uint32_t kPkt0TxBufferCount = 1000;
    // Default value for cleanup of stale interface entries
    static const uint32_t kDefaultStaleInterfaceCleanupTimeout = 60;

    static const uint32_t kFlowAddTokens = 50;
    static const uint32_t kFlowKSyncTokens = 25;
    static const uint32_t kFlowDelTokens = 16;
    static const uint32_t kFlowUpdateTokens = 16;
    static const uint32_t kMacLearningDefaultTokens = 256;
    static const uint8_t kInvalidQueueId = 255;
    static const int kInvalidCpuId = -1;
    enum ForwardingMode {
        NONE,
        L2_L3,
        L2,
        L3
    };

    enum VxLanNetworkIdentifierMode {
        AUTOMATIC,
        CONFIGURED
    };

    enum RouteTableType {
        INVALID = 0,
        INET4_UNICAST,
        INET4_MULTICAST,
        EVPN,
        BRIDGE,
        INET6_UNICAST,
        ROUTE_TABLE_MAX
    };

    typedef void (*FlowStatsReqHandler)(Agent *agent,
                       uint32_t proto, uint32_t port,
                       uint64_t timeout);

    Agent();
    virtual ~Agent();

    static Agent *GetInstance() {return singleton_;}
    static const std::string &NullString() {return null_string_;}
    static const std::set<std::string> &NullStringList() {return null_string_list_;}
    static const MacAddress &vrrp_mac() {return vrrp_mac_;}
    static const MacAddress &pkt_interface_mac() {return pkt_interface_mac_;}
    static const std::string &BcastMac() {return bcast_mac_;}
    static const std::string &xmpp_dns_server_prefix() {
        return xmpp_dns_server_connection_name_prefix_;
    }
    static const std::string &xmpp_control_node_prefix() {
        return xmpp_control_node_connection_name_prefix_;
    }

    const std::string &program_name() const {return prog_name_;}
    const std::string &config_file() const {return config_file_;}
    const std::string &log_file() const {return log_file_;}

    // DB Table accessor methods
    IpAddress GetMirrorSourceIp(const IpAddress &dest);
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
    
    ForwardingClassTable *forwarding_class_table() const {
        return forwarding_class_table_;
    }
    void set_forwarding_class_table(ForwardingClassTable *table) {
        forwarding_class_table_ = table;
    }

    AgentQosConfigTable *qos_config_table() const {
        return qos_config_table_;
    }

    void set_qos_config_table(AgentQosConfigTable *qos_config_table) {
        qos_config_table_ = qos_config_table;
    }

    QosQueueTable *qos_queue_table() const {
        return qos_queue_table_;
    }
    void set_qos_queue_table(QosQueueTable *table) {
        qos_queue_table_ = table;
    }

    DomainConfig *domain_config_table() const;

    IntfMirrorCfgTable *interface_mirror_cfg_table() const {
        return intf_mirror_cfg_table_;
    }
    void set_interface_mirror_cfg_table(IntfMirrorCfgTable *table) {
        intf_mirror_cfg_table_ = table;
    }

    InetUnicastAgentRouteTable *fabric_inet4_unicast_table() const {
        return uc_rt_table_;
    }
    void set_fabric_inet4_unicast_table(InetUnicastAgentRouteTable *
                                                 table) {
        uc_rt_table_ = table;
    }
    void set_fabric_inet4_unicast_table(RouteTable * table) {
        uc_rt_table_ = (InetUnicastAgentRouteTable *)table;
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

    EvpnAgentRouteTable *fabric_evpn_table() const {
        return evpn_rt_table_;
    }
    void set_fabric_evpn_table(RouteTable *table) {
        evpn_rt_table_ = (EvpnAgentRouteTable *)table;
    }

    BridgeAgentRouteTable *fabric_l2_unicast_table() const {
        return l2_rt_table_;
    }
    void set_fabric_l2_unicast_table(BridgeAgentRouteTable *table) {
        l2_rt_table_ = table;
    }
    void set_fabric_l2_unicast_table(RouteTable *table) {
        l2_rt_table_ = (BridgeAgentRouteTable *)table;
    }

    PhysicalDeviceTable *physical_device_table() const {
        return physical_device_table_;
    }
    void set_physical_device_table(PhysicalDeviceTable *table) {
         physical_device_table_ = table;
    }

    PhysicalDeviceVnTable *physical_device_vn_table() const {
        return physical_device_vn_table_;
    }
    void set_physical_device_vn_table(PhysicalDeviceVnTable *table) {
         physical_device_vn_table_ = table;
    }

    // VHOST related
    uint32_t vhost_prefix_len() const {return prefix_len_;}
    void set_vhost_prefix_len(uint32_t plen) {prefix_len_ = plen;}

    Ip4Address vhost_default_gateway() const {return gateway_id_;}
    void set_vhost_default_gateway(const Ip4Address &addr) {
        gateway_id_ = addr;
    }

    Ip4Address router_id() const {return router_id_;}
    const Ip4Address *router_ip_ptr() const {return &router_id_;}
    void set_router_id(const Ip4Address &addr) {
        router_id_ = addr;
        set_router_id_configured(true);
    }
    bool router_id_configured() { return router_id_configured_; }
    void set_router_id_configured(bool value) {
        router_id_configured_ = value;
    }

    Ip4Address compute_node_ip() const {return compute_node_ip_;}
    void set_compute_node_ip(const Ip4Address &addr) {
        compute_node_ip_ = addr;
    }

    AgentSignal *agent_signal() const { return agent_signal_.get(); }

    std::vector<string> &GetControllerlist() {
        return (controller_list_);
    }

    uint32_t GetControllerlistChksum() {
        return (controller_chksum_);
    }

    std::vector<string> &GetDnslist() {
        return (dns_list_);
    }

    uint32_t GetDnslistChksum() {
        return (dns_chksum_);
    }

    std::vector<string> &GetCollectorlist() {
        return (collector_list_);
    }

    uint32_t GetCollectorlistChksum() {
        return (collector_chksum_);
    }

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
    const bool xmpp_auth_enabled() const {
        return xs_auth_enable_;
    }
    const std::string &xmpp_server_cert() const {
        return xs_server_cert_;
    }
    const std::string &xmpp_server_key() const {
        return xs_server_key_;
    }
    const std::string &xmpp_ca_cert() const {
        return xs_ca_cert_;
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
 
    boost::shared_ptr<AgentXmppChannel> controller_xmpp_channel_ref(uint8_t idx);
    AgentXmppChannel *controller_xmpp_channel(uint8_t idx) const {
        return (agent_xmpp_channel_[idx]).get();
    }

    void set_controller_xmpp_channel(AgentXmppChannel *channel, uint8_t idx);
    void reset_controller_xmpp_channel(uint8_t idx);

    // Service instance
   ServiceInstanceTable *service_instance_table() const {
       return service_instance_table_;
   }

   void set_service_instance_table(ServiceInstanceTable *table) {
       service_instance_table_= table;
   }

    // DNS XMPP Server
    const bool dns_auth_enabled() const {
        return dns_auth_enable_;
    }

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
    bool is_dns_xmpp_channel(AgentDnsXmppChannel *channel) {
        if (channel == dns_xmpp_channel_[0] || channel == dns_xmpp_channel_[1])
            return true;;
        return false;
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

    const std::string &host_name() const {return host_name_; }
    const std::string &agent_name() const {
        return agent_name_;
    }

    void set_agent_name(const std::string &name) {
        agent_name_ = name;
    }

    const std::string &instance_id() const { return instance_id_; }
    void set_instance_id(const std::string &id) { instance_id_ = id; }

    const int &module_type() const { return module_type_; }
    void set_module_type(int id) { module_type_ = id; }

    const std::string &module_name() const { return module_name_; }
    void set_module_name(const std::string &name) { module_name_ = name; }

    AgentXmppChannel* mulitcast_builder() {
        return cn_mcast_builder_;
    };
    void set_cn_mcast_builder(AgentXmppChannel *peer);

    // Fabric related
    const std::string &fabric_vn_name() const {return fabric_vn_name_; }

    const std::string &fabric_vrf_name() const { return fabric_vrf_name_; }
    void set_fabric_vrf_name(const std::string &name) {
        fabric_vrf_name_ = name;
    }

    VrfEntry *fabric_vrf() const { return fabric_vrf_; }
    void set_fabric_vrf(VrfEntry *vrf) { fabric_vrf_ = vrf; }

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
    process::ConnectionState* connection_state() const {
        return connection_state_;
    }
    void set_connection_state(process::ConnectionState* state) {
        connection_state_ = state;
    }
    uint16_t metadata_server_port() const {return metadata_server_port_;}
    void set_metadata_server_port(uint16_t port) {
        metadata_server_port_ = port;
    }

    void set_vrouter_server_ip(Ip4Address ip) {
        vrouter_server_ip_ = ip;
    }
    const Ip4Address vrouter_server_ip() const {
        return vrouter_server_ip_;
    }
    void set_vrouter_server_port(uint32_t port) {
        vrouter_server_port_ = port;
    }
    const uint32_t vrouter_server_port() const {
        return vrouter_server_port_;
    }

    // Protocol objects
    ArpProto *GetArpProto() const { return arp_proto_; }
    void SetArpProto(ArpProto *proto) { arp_proto_ = proto; }

    DhcpProto *GetDhcpProto() const { return dhcp_proto_; }
    void SetDhcpProto(DhcpProto *proto) { dhcp_proto_ = proto; }

    Dhcpv6Proto *dhcpv6_proto() const { return dhcpv6_proto_; }
    void set_dhcpv6_proto(Dhcpv6Proto *proto) { dhcpv6_proto_ = proto; }

    DnsProto *GetDnsProto() const { return dns_proto_; }
    void SetDnsProto(DnsProto *proto) { dns_proto_ = proto; }

    IcmpProto *GetIcmpProto() const { return icmp_proto_; }
    void SetIcmpProto(IcmpProto *proto) { icmp_proto_ = proto; }

    Icmpv6Proto *icmpv6_proto() const { return icmpv6_proto_; }
    void set_icmpv6_proto(Icmpv6Proto *proto) { icmpv6_proto_ = proto; }

    FlowProto *GetFlowProto() const { return flow_proto_; }
    void SetFlowProto(FlowProto *proto) { flow_proto_ = proto; }

    MacLearningProto* mac_learning_proto() const {
        return mac_learning_proto_;
    }

    void set_mac_learning_proto(MacLearningProto *mac_learning_proto) {
        mac_learning_proto_ = mac_learning_proto;
    }

    MacLearningModule* mac_learning_module() const {
        return mac_learning_module_;
    }

    void set_mac_learning_module(MacLearningModule *mac_learning_module) {
        mac_learning_module_ = mac_learning_module;
    }

    // Peer objects
    const Peer *local_peer() const {return local_peer_.get();}
    const Peer *local_vm_peer() const {return local_vm_peer_.get();}
    const Peer *link_local_peer() const {return linklocal_peer_.get();}
    const Peer *ecmp_peer() const {return ecmp_peer_.get();}
    const Peer *vgw_peer() const {return vgw_peer_.get();}
    const Peer *evpn_peer() const {return evpn_peer_.get();}
    const Peer *multicast_peer() const {return multicast_peer_.get();}
    const Peer *multicast_tor_peer() const {return multicast_tor_peer_.get();}
    const Peer *multicast_tree_builder_peer() const {
        return multicast_tree_builder_peer_.get();}
    const Peer *mac_vm_binding_peer() const {return mac_vm_binding_peer_.get();}
    const Peer *inet_evpn_peer() const {return inet_evpn_peer_.get();}
    const Peer *mac_learning_peer() const {return mac_learning_peer_.get();}

    // Agent Modules
    AgentConfig *cfg() const; 
    void set_cfg(AgentConfig *cfg);

    AgentStats *stats() const;
    void set_stats(AgentStats *stats);

    KSync *ksync() const;
    void set_ksync(KSync *ksync);

    AgentUveBase *uve() const;
    void set_uve(AgentUveBase *uve);

    AgentStatsCollector *stats_collector() const;
    void set_stats_collector(AgentStatsCollector *asc);

    FlowStatsManager *flow_stats_manager() const;
    void set_flow_stats_manager(FlowStatsManager *fsc);

    HealthCheckTable *health_check_table() const;
    void set_health_check_table(HealthCheckTable *table);

    BridgeDomainTable *bridge_domain_table() const;
    void set_bridge_domain_table(BridgeDomainTable *table);

    MetaDataIpAllocator *metadata_ip_allocator() const;
    void set_metadata_ip_allocator(MetaDataIpAllocator *allocator);

    PktModule *pkt() const;
    void set_pkt(PktModule *pkt);

    ServicesModule *services() const;
    void set_services(ServicesModule *services);

    VirtualGateway *vgw() const;
    void set_vgw(VirtualGateway *vgw);

    RESTServer *rest_server() const;
    void set_rest_server(RESTServer *r);

    PortIpcHandler *port_ipc_handler() const;
    void set_port_ipc_handler(PortIpcHandler *r);

    OperDB *oper_db() const;
    void set_oper_db(OperDB *oper_db);

    VNController *controller() const;
    void set_controller(VNController *val);

    ResourceManager *resource_manager() const;
    void set_resource_manager(ResourceManager *resource_manager);

    EventNotifier *event_notifier() const;
    void set_event_notifier(EventNotifier *mgr);

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

    int introspect_port() const { return introspect_port_;}

    uint32_t sandesh_send_rate_limit() { return send_ratelimit_; }

    DB *db() const {return db_;}

    TaskScheduler *task_scheduler() const { return task_scheduler_; }
    void set_task_scheduler(TaskScheduler *t) { task_scheduler_ = t; }

    AgentInit *agent_init() const { return agent_init_; }
    void set_agent_init(AgentInit *init) { agent_init_ = init; }

    OVSDB::OvsdbClient *ovsdb_client() const { return ovsdb_client_; }
    void set_ovsdb_client(OVSDB::OvsdbClient *client) { ovsdb_client_ = client; }

    const std::string &fabric_interface_name() const {
        return ip_fabric_intf_name_;
    }

    VxLanNetworkIdentifierMode vxlan_network_identifier_mode() const {
        return vxlan_network_identifier_mode_;
    }
    void set_vxlan_network_identifier_mode(VxLanNetworkIdentifierMode mode) {
        vxlan_network_identifier_mode_ = mode;
    }

    bool simulate_evpn_tor() const {return simulate_evpn_tor_;}
    void set_simulate_evpn_tor(bool mode) {simulate_evpn_tor_ = mode;}

    bool tsn_enabled() const {return tsn_enabled_;}
    void set_tsn_enabled(bool val) {tsn_enabled_ = val;}
    bool tor_agent_enabled() const {return tor_agent_enabled_;}
    void set_tor_agent_enabled(bool val) {tor_agent_enabled_ = val;}
    bool server_gateway_mode() const {return server_gateway_mode_;}
    void set_server_gateway_mode(bool val) {server_gateway_mode_ = val;}

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

    bool xmpp_dns_test_mode() const { return xmpp_dns_test_mode_; }
    void set_xmpp_dns_test_mode(bool xmpp_dns_test_mode) {
        xmpp_dns_test_mode_ = xmpp_dns_test_mode;
    }

    uint32_t flow_table_size() const { return flow_table_size_; }
    void set_flow_table_size(uint32_t count);

    uint16_t flow_thread_count() const { return flow_thread_count_; }
    bool flow_trace_enable() const { return flow_trace_enable_; }

    uint32_t max_vm_flows() const { return max_vm_flows_; }
    void set_max_vm_flows(uint32_t count) { max_vm_flows_ = count; }

    uint32_t flow_add_tokens() const { return flow_add_tokens_; }
    uint32_t flow_ksync_tokens() const { return flow_ksync_tokens_; }
    uint32_t flow_del_tokens() const { return flow_del_tokens_; }
    uint32_t flow_update_tokens() const { return flow_update_tokens_; }

    bool init_done() const { return init_done_; }
    void set_init_done(bool done) { init_done_ = done; }

    ConfigManager *config_manager() const;

    AgentParam *params() const { return params_; }

    bool isXenMode();
    bool isKvmMode();
    bool isDockerMode();
    // Agent param accessor functions
    bool isVmwareMode() const;
    bool isVmwareVcenterMode() const;
    bool vrouter_on_nic_mode() const;
    bool vrouter_on_host_dpdk() const;
    bool vrouter_on_host() const;
    void SetAgentTaskPolicy();
    void CopyConfig(AgentParam *params);
    void CopyFilteredParams();
    bool ResourceManagerReady() const { return resource_manager_ready_; }
    void SetResourceManagerReady();

    void Init(AgentParam *param);
    void InitPeers();
    void InitDone();
    void InitXenLinkLocalIntf();
    void InitCollector();
    void ReConnectCollectors();
    void ReconfigSignalHandler(boost::system::error_code , int);

    LifetimeManager *lifetime_manager() { return lifetime_manager_;}
    void CreateLifetimeManager();
    void ShutdownLifetimeManager();

    // Default concurrency checker. Checks for "Agent::KSync" and "db::DBTable"
    void ConcurrencyCheck();

    uint32_t vrouter_max_labels() const {
        return vrouter_max_labels_;
    }
    void set_vrouter_max_labels(uint32_t max_labels) {
        vrouter_max_labels_ = max_labels;
    }

    uint32_t vrouter_max_nexthops() const {
        return vrouter_max_nexthops_;
    }
    void set_vrouter_max_nexthops(uint32_t max_nexthop) {
        vrouter_max_nexthops_ = max_nexthop;
    }

    uint32_t vrouter_max_interfaces() const {
        return vrouter_max_interfaces_;
    }
    void set_vrouter_max_interfaces(uint32_t max_interfaces) {
        vrouter_max_interfaces_ = max_interfaces;
    }

    uint32_t vrouter_max_vrfs() const {
        return vrouter_max_vrfs_;
    }
    void set_vrouter_max_vrfs(uint32_t max_vrf) {
        vrouter_max_vrfs_ = max_vrf;
    }

    uint32_t vrouter_max_mirror_entries() const {
        return vrouter_max_mirror_entries_;
    }
    void set_vrouter_max_mirror_entries(uint32_t max_mirror_entries) {
        vrouter_max_mirror_entries_ = max_mirror_entries;
    }


    uint32_t vrouter_max_bridge_entries() const {
        return vrouter_max_bridge_entries_;
    }
    void set_vrouter_max_bridge_entries(uint32_t bridge_entries) {
        vrouter_max_bridge_entries_ = bridge_entries;
    }

    uint32_t vrouter_max_oflow_bridge_entries() const {
        return vrouter_max_oflow_bridge_entries_;
    }
    void set_vrouter_max_oflow_bridge_entries(uint32_t
                                                  oflow_bridge_entries) {
        vrouter_max_oflow_bridge_entries_ = oflow_bridge_entries;
    }

    uint32_t vrouter_max_flow_entries() const {
        return vrouter_max_flow_entries_;
    }
    void set_vrouter_max_flow_entries(uint32_t value) {
        vrouter_max_flow_entries_ = value;
    }

    uint32_t vrouter_max_oflow_entries() const {
        return vrouter_max_oflow_entries_;
    }
    void set_vrouter_max_oflow_entries(uint32_t value) {
        vrouter_max_oflow_entries_ = value;
    }
    void set_vrouter_build_info(std::string version) {
        vrouter_build_info_ = version;
    }
    std::string vrouter_build_info() const {
        return vrouter_build_info_;
    }
    Agent::ForwardingMode TranslateForwardingMode(const std::string &mode) const;

    FlowStatsReqHandler& flow_stats_req_handler() {
        return flow_stats_req_handler_;
    }

    void set_flow_stats_req_handler(FlowStatsReqHandler req) {
        flow_stats_req_handler_ = req;
    }
 
    void SetMeasureQueueDelay(bool val);
    bool MeasureQueueDelay();
    void TaskTrace(const char *file_name, uint32_t line_no, const Task *task,
                   const char *description, uint32_t delay);

    static uint16_t ProtocolStringToInt(const std::string &str);
    VrouterObjectLimits GetVrouterObjectLimits();
    void SetXmppDscp(uint8_t val);

private:

    uint32_t GenerateHash(std::vector<std::string> &);
    void InitializeFilteredParams();
    void InitControllerList();
    void InitDnsList();

    AgentParam *params_;
    AgentConfig *cfg_;
    AgentStats *stats_;
    KSync *ksync_;
    AgentUveBase *uve_;
    AgentStatsCollector *stats_collector_;
    FlowStatsManager *flow_stats_manager_;
    PktModule *pkt_;
    ServicesModule *services_;
    VirtualGateway *vgw_;
    RESTServer *rest_server_;
    PortIpcHandler *port_ipc_handler_;
    OperDB *oper_db_;
    DiagTable *diag_table_;
    VNController *controller_;
    ResourceManager *resource_manager_;
    EventNotifier *event_notifier_;

    EventManager *event_mgr_;
    boost::shared_ptr<AgentXmppChannel> agent_xmpp_channel_[MAX_XMPP_SERVERS];
    AgentIfMapXmppChannel *ifmap_channel_[MAX_XMPP_SERVERS];
    XmppClient *xmpp_client_[MAX_XMPP_SERVERS];
    XmppInit *xmpp_init_[MAX_XMPP_SERVERS];
    AgentDnsXmppChannel *dns_xmpp_channel_[MAX_XMPP_SERVERS];
    XmppClient *dns_xmpp_client_[MAX_XMPP_SERVERS];
    XmppInit *dns_xmpp_init_[MAX_XMPP_SERVERS];
    IFMapAgentStaleCleaner *agent_stale_cleaner_;
    AgentXmppChannel *cn_mcast_builder_;
    uint16_t metadata_server_port_;
    // Host name of node running the daemon
    std::string host_name_;
    // Unique name of the agent. When multiple instances are running, it will
    // use instance-id to make unique name
    std::string agent_name_;
    std::string prog_name_;
    int introspect_port_;
    std::string instance_id_;
    int module_type_;
    std::string module_name_;
    uint32_t send_ratelimit_;
    // DB handles
    DB *db_;
    TaskScheduler *task_scheduler_;
    AgentInit *agent_init_;
    VrfEntry *fabric_vrf_;
    InterfaceTable *intf_table_;
    HealthCheckTable *health_check_table_;
    BridgeDomainTable *bridge_domain_table_;
    std::auto_ptr<MetaDataIpAllocator> metadata_ip_allocator_;
    NextHopTable *nh_table_;
    InetUnicastAgentRouteTable *uc_rt_table_;
    Inet4MulticastAgentRouteTable *mc_rt_table_;
    EvpnAgentRouteTable *evpn_rt_table_;
    BridgeAgentRouteTable *l2_rt_table_;
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
    PhysicalDeviceTable *physical_device_table_;
    PhysicalDeviceVnTable *physical_device_vn_table_;
    ForwardingClassTable *forwarding_class_table_;
    QosQueueTable *qos_queue_table_;
    AgentQosConfigTable *qos_config_table_;
    std::auto_ptr<ConfigManager> config_manager_;
 
    // Mirror config table
    MirrorCfgTable *mirror_cfg_table_;
    // Interface Mirror config table
    IntfMirrorCfgTable *intf_mirror_cfg_table_;
    
    Ip4Address router_id_;
    uint32_t prefix_len_;
    Ip4Address gateway_id_;

    // IP address on the compute node used by agent to run services such
    // as metadata service. This is different than router_id when vhost0
    // is un-numbered interface in host-os
    // The compute_node_ip_ is used only in adding Flow NAT rules.
    Ip4Address compute_node_ip_;
    std::string xs_cfg_addr_;
    int8_t xs_idx_;
    std::string xs_addr_[MAX_XMPP_SERVERS];
    uint32_t xs_port_[MAX_XMPP_SERVERS];
    uint64_t xs_stime_[MAX_XMPP_SERVERS];
    bool xs_auth_enable_;
    std::string xs_server_cert_;
    std::string xs_server_key_;
    std::string xs_ca_cert_;
    int8_t xs_dns_idx_;
    std::string dns_addr_[MAX_XMPP_SERVERS];
    uint32_t dns_port_[MAX_XMPP_SERVERS];
    bool dns_auth_enable_;
    // Config
    std::vector<std::string>controller_list_;
    uint32_t controller_chksum_;
    std::vector<std::string>dns_list_;
    uint32_t dns_chksum_;
    std::vector<std::string>collector_list_;
    uint32_t collector_chksum_;
    std::string ip_fabric_intf_name_;
    std::string vhost_interface_name_;
    std::string pkt_interface_name_;

    ArpProto *arp_proto_;
    DhcpProto *dhcp_proto_;
    DnsProto *dns_proto_;
    IcmpProto *icmp_proto_;
    Dhcpv6Proto *dhcpv6_proto_;
    Icmpv6Proto *icmpv6_proto_;
    FlowProto *flow_proto_;
    MacLearningProto *mac_learning_proto_;
    MacLearningModule *mac_learning_module_;

    std::auto_ptr<Peer> local_peer_;
    std::auto_ptr<Peer> local_vm_peer_;
    std::auto_ptr<Peer> linklocal_peer_;
    std::auto_ptr<Peer> ecmp_peer_;
    std::auto_ptr<Peer> vgw_peer_;
    std::auto_ptr<Peer> evpn_peer_;
    std::auto_ptr<Peer> multicast_peer_;
    std::auto_ptr<Peer> multicast_tor_peer_;
    std::auto_ptr<Peer> multicast_tree_builder_peer_;
    std::auto_ptr<Peer> mac_vm_binding_peer_;
    std::auto_ptr<Peer> inet_evpn_peer_;
    std::auto_ptr<Peer> mac_learning_peer_;

    std::auto_ptr<AgentSignal> agent_signal_;

    IFMapAgentParser *ifmap_parser_;
    bool router_id_configured_;

    uint16_t mirror_src_udp_port_;
    LifetimeManager *lifetime_manager_;
    bool ksync_sync_mode_;
    std::string mgmt_ip_;
    static Agent *singleton_;
    VxLanNetworkIdentifierMode vxlan_network_identifier_mode_;
    const Interface *vhost_interface_;
    process::ConnectionState* connection_state_;
    bool test_mode_;
    bool xmpp_dns_test_mode_;
    bool init_done_;
    bool resource_manager_ready_;
    bool simulate_evpn_tor_;
    bool tsn_enabled_;
    bool tor_agent_enabled_;
    bool server_gateway_mode_;

    // Flow information
    uint32_t flow_table_size_;
    uint16_t flow_thread_count_;
    bool flow_trace_enable_;
    uint32_t max_vm_flows_;
    uint32_t flow_add_tokens_;
    uint32_t flow_ksync_tokens_;
    uint32_t flow_del_tokens_;
    uint32_t flow_update_tokens_;

    // OVSDB client ptr
    OVSDB::OvsdbClient *ovsdb_client_;

    //IP address to be used for sending vrouter sandesh messages
    Ip4Address vrouter_server_ip_;
    //TCP port number to be used for sending vrouter sandesh messages
    uint32_t vrouter_server_port_;
    //Max label space of vrouter
    uint32_t vrouter_max_labels_;
    //Max nexthop supported by vrouter
    uint32_t vrouter_max_nexthops_;
    //Max interface supported by vrouter
    uint32_t vrouter_max_interfaces_;
    //Max VRF supported by vrouter
    uint32_t vrouter_max_vrfs_;
    //Max Mirror entries
    uint32_t vrouter_max_mirror_entries_;
    //Bridge entries that can be porgrammed in vrouter
    uint32_t vrouter_max_bridge_entries_;
    uint32_t vrouter_max_oflow_bridge_entries_;
    //Max Flow entries
    uint32_t vrouter_max_flow_entries_;
    //Max OverFlow entries
    uint32_t vrouter_max_oflow_entries_;
    std::string vrouter_build_info_;
    FlowStatsReqHandler flow_stats_req_handler_;

    uint32_t tbb_keepawake_timeout_;
    // Constants
public:
    static const std::string config_file_;
    static const std::string log_file_;
    static const std::string null_string_;
    static const std::set<std::string> null_string_list_;
    static std::string fabric_vrf_name_;
    static const std::string fabric_vn_name_;
    static const std::string link_local_vrf_name_;
    static const std::string link_local_vn_name_;
    static const MacAddress vrrp_mac_;
    static const MacAddress pkt_interface_mac_;
    static const std::string bcast_mac_;
    static const std::string xmpp_dns_server_connection_name_prefix_;
    static const std::string xmpp_control_node_connection_name_prefix_;
    static const std::string dpdk_exception_pkt_path_;
    static const std::string vnic_exception_pkt_interface_;
};

#endif // vnsw_agent_hpp
