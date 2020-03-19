/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/lifetime.h>
#include <base/misc_utils.h>
#include <io/event_manager.h>
#include <ifmap/ifmap_link.h>
#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <base/sandesh/task_types.h>

#include <init/agent_param.h>
#include <cmn/agent_signal.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cmn/agent.h>
#include <cmn/event_notifier.h>
#include <cmn/xmpp_server_address_parser.h>
#include <controller/controller_init.h>
#include <controller/controller_peer.h>

#include <oper/operdb_init.h>
#include <oper/config_manager.h>
#include <oper/interface_common.h>
#include <oper/health_check.h>
#include <oper/metadata_ip.h>
#include <oper/multicast.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <oper/mpls.h>
#include <oper/peer.h>
#include <xmpp/xmpp_client.h>

#include <filter/acl.h>

#include <cmn/agent_factory.h>
#include <net/if.h>

const std::string Agent::null_string_ = "";
const std::set<std::string> Agent::null_string_list_;
const std::string Agent::fabric_vn_name_ =
    "default-domain:default-project:ip-fabric";
std::string Agent::fabric_vrf_name_ =
    "default-domain:default-project:ip-fabric:__default__";
std::string Agent::fabric_policy_vrf_name_ =
    "default-domain:default-project:ip-fabric:ip-fabric";
const std::string Agent::link_local_vn_name_ =
    "default-domain:default-project:__link_local__";
const std::string Agent::link_local_vrf_name_ =
    "default-domain:default-project:__link_local__:__link_local__";
const MacAddress Agent::vrrp_mac_(0x00, 0x00, 0x5E, 0x00, 0x01, 0x00);
// use the following MAC when sending data to left or right SI interfaces
const MacAddress Agent::left_si_mac_(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);
const MacAddress Agent::right_si_mac_(0x02, 0x00, 0x00, 0x00, 0x00, 0x02);
const MacAddress Agent::pkt_interface_mac_(0x00, 0x00, 0x00, 0x00, 0x00, 0x01);
const std::string Agent::bcast_mac_ = "FF:FF:FF:FF:FF:FF";
const std::string Agent::xmpp_dns_server_connection_name_prefix_ = "dns-server:";
const std::string Agent::xmpp_control_node_connection_name_prefix_ = "control-node:";
const uint16_t Agent::kDefaultVmiVmVnUveInterval = 30; //in seconds
const std::string Agent::v4_link_local_subnet_ = "169.254.0.0";
const std::string Agent::v6_link_local_subnet_ = "FE80::";

SandeshTraceBufferPtr TaskTraceBuf(SandeshTraceBufferCreate("TaskTrace", 5000));
Agent *Agent::singleton_;

IpAddress Agent::GetMirrorSourceIp(const IpAddress &dest) {
    IpAddress sip;
    if (dest.is_v4()) {
        if (router_id() == dest) {
            // If source IP and dest IP are same,
            // linux kernel will drop the packet.
            // Hence we will use link local IP address as sip.
            sip = Ip4Address(METADATA_IP_ADDR);
        } else {
            sip = router_id();
        }
    } else if (dest.is_v6()) {
        sip = Ip6Address::v4_mapped(router_id());
    }
    return sip;
}

const string &Agent::GetHostInterfaceName() const {
    // There is single host interface.  Its addressed by type and not name
    return Agent::null_string_;
};

std::string Agent::GetUuidStr(boost::uuids::uuid uuid_val) const {
    std::ostringstream str;
    str << uuid_val;
    return str.str();
}

const string &Agent::vhost_interface_name() const {
    return vhost_interface_name_;
};

bool Agent::is_vhost_interface_up() const {
    if (tor_agent_enabled() || test_mode()|| isMockMode()) {
        return true;
    }

    static const int log_rate_limit = 15;
    struct ifreq ifr;
    static int err_count = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, vhost_interface_name().c_str());
    int err = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if (err < 0 || !(ifr.ifr_flags & IFF_UP)) {
        close(sock);
        if ((err_count % log_rate_limit) == 0) {
            LOG(DEBUG, "vhost is down");
        }
        err_count++;
        return false;
    }
    err = ioctl(sock, SIOCGIFADDR, &ifr);
    if (err < 0) {
        close(sock);
        if ((err_count % log_rate_limit) == 0) {
            LOG(DEBUG, "vhost is up. but ip is not set");
        }
        err_count++;
        return false;
    }
    close(sock);
    return true;
}

bool Agent::isXenMode() {
    return params_->isXenMode();
}

bool Agent::isKvmMode() {
    return params_->isKvmMode();
}

bool Agent::isDockerMode() {
    return params_->isDockerMode();
}

bool Agent::isMockMode() const {
    return params_->cat_is_agent_mocked();
}

std::string Agent::AgentGUID() const {
   char *envval = NULL;
   assert(params_);
   return std::string(((envval = getenv("LOGNAME"))== NULL)? "" : envval)
   + "_" +  agent_name() + "_" + integerToString(getpid());
}

static void SetTaskPolicyOne(const char *task, const char *exclude_list[],
                             int count) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskPolicy policy;
    for (int i = 0; i < count; ++i) {
        int task_id = scheduler->GetTaskId(exclude_list[i]);
        policy.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId(task), policy);
}

void Agent::SetAgentTaskPolicy() {
    /*
     * TODO(roque): this method should not be called by the agent constructor.
     */
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    const char *db_exclude_list[] = {
        kTaskFlowEvent,
        kTaskFlowKSync,
        kTaskFlowUpdate,
        kTaskFlowDelete,
        kTaskFlowAudit,
        kTaskFlowStatsUpdate,
        "Agent::Services",
        "Agent::StatsCollector",
        kTaskFlowStatsCollector,
        kTaskSessionStatsCollector,
        kTaskSessionStatsCollectorEvent,
        "sandesh::RecvQueue",
        "Agent::Uve",
        "Agent::KSync",
        "Agent::PktFlowResponder",
        "Agent::Profile",
        "Agent::PktHandler",
        "Agent::Diag",
        "http::RequestHandlerTask",
        "Agent::RestApi",
        kTaskHealthCheck,
        kTaskCryptTunnel,
        kTaskDBExclude,
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME,
        AGENT_SANDESH_TASKNAME,
        kTaskConfigManager,
        INSTANCE_MANAGER_TASK_NAME,
        kAgentResourceBackUpTask,
        kAgentResourceRestoreTask,
        kTaskMacLearning
    };
    SetTaskPolicyOne("db::DBTable", db_exclude_list,
                     sizeof(db_exclude_list) / sizeof(char *));

    // ConfigManager task
    const char *config_manager_exclude_list[] = {
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskConfigManager, config_manager_exclude_list,
                     sizeof(config_manager_exclude_list) / sizeof(char *));

    const char *flow_table_exclude_list[] = {
        "Agent::PktFlowResponder",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskFlowEvent, flow_table_exclude_list,
                     sizeof(flow_table_exclude_list) / sizeof(char *));

    SetTaskPolicyOne(kTaskFlowKSync, flow_table_exclude_list,
                     sizeof(flow_table_exclude_list) / sizeof(char *));

    SetTaskPolicyOne(kTaskFlowUpdate, flow_table_exclude_list,
                     sizeof(flow_table_exclude_list) / sizeof(char *));

    SetTaskPolicyOne(kTaskFlowDelete, flow_table_exclude_list,
                     sizeof(flow_table_exclude_list) / sizeof(char *));

    const char *sandesh_exclude_list[] = {
        "db::DBTable",
        "Agent::Services",
        "Agent::StatsCollector",
        "io::ReaderTask",
        "Agent::PktFlowResponder",
        "Agent::Profile",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME,
        AGENT_SANDESH_TASKNAME
    };
    SetTaskPolicyOne("sandesh::RecvQueue", sandesh_exclude_list,
                     sizeof(sandesh_exclude_list) / sizeof(char *));

    const char *xmpp_config_exclude_list[] = {
        "Agent::Services",
        "Agent::StatsCollector",
        "sandesh::RecvQueue",
        "io::ReaderTask",
        "Agent::ControllerXmpp",
        "db::Walker",
        "db::DBTable",
        "xmpp::StateMachine",
        "bgp::ShowCommand",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("bgp::Config", xmpp_config_exclude_list,
                     sizeof(xmpp_config_exclude_list) / sizeof(char *));

    const char *controller_xmpp_exclude_list[] = {
        "Agent::Services",
        "io::ReaderTask",
        "db::DBTable",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("Agent::ControllerXmpp", controller_xmpp_exclude_list,
                     sizeof(controller_xmpp_exclude_list) / sizeof(char *));

    const char *walker_exclude_list[] = {
        "Agent::ControllerXmpp",
        "db::DBTable",
        // For ToR Agent Agent::KSync and db::Walker both task tries
        // to modify route path list inline (out of DB table context) to
        // manage route exports from dynamic peer before release the peer
        // which is resulting in parallel access, for now we will avoid this
        // race by adding task exclusion policy.
        // TODO(prabhjot): need to remove this task exclusion one dynamic peer
        // handling is done.
        "Agent::KSync",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("db::Walker", walker_exclude_list,
                     sizeof(walker_exclude_list) / sizeof(char *));

    const char *ksync_exclude_list[] = {
        "db::DBTable",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("Agent::KSync", ksync_exclude_list,
                     sizeof(ksync_exclude_list) / sizeof(char *));

    const char *stats_collector_exclude_list[] = {
        "Agent::PktFlowResponder",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("Agent::StatsCollector", stats_collector_exclude_list,
                     sizeof(stats_collector_exclude_list) / sizeof(char *));

    const char *flow_stats_exclude_list[] = {
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskFlowStatsCollector, flow_stats_exclude_list,
                     sizeof(flow_stats_exclude_list) / sizeof(char *));

    const char *session_stats_exclude_list[] = {
        kTaskSessionStatsCollectorEvent,
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskSessionStatsCollector, session_stats_exclude_list,
                     sizeof(session_stats_exclude_list) / sizeof(char *));

    const char *session_stats_event_exclude_list[] = {
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskSessionStatsCollectorEvent, session_stats_exclude_list,
                     sizeof(session_stats_event_exclude_list) / sizeof(char *));
    const char *metadata_exclude_list[] = {
        "xmpp::StateMachine",
        "http::RequestHandlerTask"
    };
    SetTaskPolicyOne("http client", metadata_exclude_list,
                     sizeof(metadata_exclude_list) / sizeof(char *));

    const char *xmpp_state_machine_exclude_list[] = {
        "io::ReaderTask"
    };
    SetTaskPolicyOne("xmpp::StateMachine", xmpp_state_machine_exclude_list,
                     sizeof(xmpp_state_machine_exclude_list) / sizeof(char *));

    const char *agent_init_exclude_list[] = {
        "xmpp::StateMachine",
        "http client",
        "db::DBTable",
        AGENT_SANDESH_TASKNAME,
        AGENT_SHUTDOWN_TASKNAME,
        kAgentResourceBackUpTask,
        kAgentResourceRestoreTask
    };
    SetTaskPolicyOne(AGENT_INIT_TASKNAME, agent_init_exclude_list,
                     sizeof(agent_init_exclude_list) / sizeof(char *));

    const char *flow_stats_manager_exclude_list[] = {
        "Agent::StatsCollector",
        kTaskFlowStatsCollector,
        kTaskFlowMgmt,
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(AGENT_FLOW_STATS_MANAGER_TASK,
                     flow_stats_manager_exclude_list,
                     sizeof(flow_stats_manager_exclude_list) / sizeof(char *));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->RegisterLog(boost::bind(&Agent::TaskTrace, this,
                                       _1, _2, _3, _4, _5));

    const char *db_exclude_task_exclude_list[] = {
        "Agent::Uve",
        "sandesh::RecvQueue",
        "Agent::ControllerXmpp",
        "bgp::Config",
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kTaskDBExclude, db_exclude_task_exclude_list,
                     sizeof(db_exclude_task_exclude_list) / sizeof(char *));

    const char *flow_stats_update_exclude_list[] = {
        "Agent::Uve"
    };
    SetTaskPolicyOne(kTaskFlowStatsUpdate, flow_stats_update_exclude_list,
                     sizeof(flow_stats_update_exclude_list) / sizeof(char *));

    const char *profile_task_exclude_list[] = {
        AGENT_FLOW_STATS_MANAGER_TASK,
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne("Agent::Profile", profile_task_exclude_list,
                     sizeof(profile_task_exclude_list) / sizeof(char *));

    //event notify exclude list
    const char *event_notify_exclude_list[] = {
        "Agent::ControllerXmpp",
        "db::DBTable",
        kAgentResourceBackUpTask,
        AGENT_SHUTDOWN_TASKNAME,
        AGENT_INIT_TASKNAME
    };
    SetTaskPolicyOne(kEventNotifierTask, event_notify_exclude_list,
                     sizeof(event_notify_exclude_list) / sizeof(char *));

    // Health Check task
    const char *health_check_exclude_list[] = {
        kTaskFlowMgmt
    };
    SetTaskPolicyOne(kTaskHealthCheck, health_check_exclude_list,
                     sizeof(health_check_exclude_list) / sizeof(char *));

}

void Agent::CreateLifetimeManager() {
    lifetime_manager_ = new LifetimeManager(
            TaskScheduler::GetInstance()->GetTaskId("db::DBTable"));
}

void Agent::ShutdownLifetimeManager() {
    delete lifetime_manager_;
    lifetime_manager_ = NULL;
}

uint32_t Agent::GenerateHash(std::vector<std::string> &list) {

    std::string concat_servers;
    std::vector<std::string>::iterator iter;
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter;
    }

    boost::hash<std::string> string_hash;
    return(string_hash(concat_servers));
}

void Agent::InitControllerList() {
    XmppServerAddressParser parser(XMPP_SERVER_PORT, MAX_XMPP_SERVERS);
    parser.ParseAddresses(controller_list_, xs_addr_, xs_port_);
}

void Agent::InitDnsList() {
    XmppServerAddressParser parser(XMPP_DNS_SERVER_PORT, MAX_XMPP_SERVERS);
    parser.ParseAddresses(dns_list_, dns_addr_, dns_port_);
}

void Agent::InitializeFilteredParams() {
    InitControllerList();
    InitDnsList();
}

void Agent::CopyFilteredParams() {

    // Controller
    // 1. Save checksum of the Configured List
    // 2. Randomize the Configured List
    std::vector<string> list = params_->controller_server_list();
    uint32_t new_chksum = GenerateHash(list);
    if (new_chksum != controller_chksum_) {
        controller_chksum_ = new_chksum;
        controller_list_ = params_->controller_server_list();
        std::random_shuffle(controller_list_.begin(), controller_list_.end());
    }

    // Dns
    // 1. Save checksum of the Configured List
    // 2. Pick first two DNS Servers to connect
    list.clear();
    list = params_->dns_server_list();
    new_chksum = GenerateHash(list);
    if (new_chksum != dns_chksum_) {
        dns_chksum_ = new_chksum;
        dns_list_ = params_->dns_server_list();
    }

    // Collector
    // 1. Save checksum of the Configured List
    // 2. Randomize the Configured List
    list.clear();
    list = params_->collector_server_list();
    new_chksum = GenerateHash(list);
    if (new_chksum != collector_chksum_) {
        collector_chksum_ = new_chksum;
        collector_list_ = params_->collector_server_list();
        std::random_shuffle(collector_list_.begin(), collector_list_.end());
    }
}

// Get configuration from AgentParam into Agent
void Agent::CopyConfig(AgentParam *params) {
    params_ = params;

    xs_auth_enable_ = params_->xmpp_auth_enabled();
    dns_auth_enable_ = params_->xmpp_dns_auth_enabled();
    xs_server_cert_ = params_->xmpp_server_cert();
    xs_server_key_ = params_->xmpp_server_key();
    xs_ca_cert_ = params_->xmpp_ca_cert();
    subcluster_name_ = params_->subcluster_name();

    CopyFilteredParams();
    InitializeFilteredParams();

    vhost_interface_name_ = params_->vhost_name();
    ip_fabric_intf_name_ = params_->eth_port();
    crypt_intf_name_ = params_->crypt_port();
    host_name_ = params_->host_name();
    agent_name_ = params_->host_name();
    prog_name_ = params_->program_name();
    introspect_port_ = params_->http_server_port();
    prefix_len_ = params_->vhost_plen();
    gateway_id_ = params_->vhost_gw();
    router_id_ = params_->vhost_addr();
    if (router_id_.to_ulong()) {
        router_id_configured_ = false;
    }

    compute_node_ip_ = router_id_;
    if (params_->tunnel_type() == "MPLSoUDP")
        TunnelType::SetDefaultType(TunnelType::MPLS_UDP);
    else if (params_->tunnel_type() == "VXLAN")
        TunnelType::SetDefaultType(TunnelType::VXLAN);
    else
        TunnelType::SetDefaultType(TunnelType::MPLS_GRE);

    simulate_evpn_tor_ = params->simulate_evpn_tor();
    test_mode_ = params_->test_mode();
    tsn_no_forwarding_enabled_ = (params_->isTsnAgent() &&
                                  !params_->IsForwardingEnabled());
    tsn_enabled_ = params_->isTsnAgent();
    tor_agent_enabled_ = params_->isTorAgent();
    forwarding_enabled_ = params_->IsForwardingEnabled();
    server_gateway_mode_ = params_->isServerGatewayMode();
    vcpe_gateway_mode_ = params_->isVcpeGatewayMode();
    pbb_gateway_mode_ = params_->isPbbGatewayMode();
    flow_thread_count_ = params_->flow_thread_count();
    flow_trace_enable_ = params_->flow_trace_enable();
    flow_add_tokens_ = params_->flow_add_tokens();
    flow_ksync_tokens_ = params_->flow_ksync_tokens();
    flow_del_tokens_ = params_->flow_del_tokens();
    flow_update_tokens_ = params_->flow_update_tokens();
    tbb_keepawake_timeout_ = params_->tbb_keepawake_timeout();
    task_monitor_timeout_msec_ = params_->task_monitor_timeout_msec();
    vr_limit_high_watermark_ = params_->vr_object_high_watermark();
    vr_limit_low_watermark_ = vr_limit_high_watermark_ - 5;
    vr_limits_exceeded_map_.insert(VrLimitData("vr_nexthops", "Normal"));
    vr_limits_exceeded_map_.insert(VrLimitData("vr_mpls_labels", "Normal"));
}

void Agent::set_cn_mcast_builder(AgentXmppChannel *peer) {
    cn_mcast_builder_ =  peer;
}

void Agent::InitCollector() {
    /* We need to do sandesh initialization here */

    /* If collector configuration is specified, use that for connection to
     * collector. If not we still need to invoke InitGenerator to initialize
     * introspect.
     */
    Module::type module = static_cast<Module::type>(module_type_);
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    if (GetCollectorlist().size() != 0) {
        Sandesh::InitGenerator(module_name(),
                host_name(),
                g_vns_constants.NodeTypeNames.find(node_type)->second,
                instance_id_,
                event_manager(),
                params_->http_server_port(),
                GetCollectorlist(),
                NULL, params_->derived_stats_map(),
                params_->sandesh_config());
    } else {
        Sandesh::InitGenerator(module_name(),
                host_name(),
                g_vns_constants.NodeTypeNames.find(node_type)->second,
                instance_id_,
                event_manager(),
                params_->http_server_port(),
                NULL, params_->derived_stats_map(),
                params_->sandesh_config());
    }

    if (params_->cat_is_agent_mocked()) {
        std::cout << "Agent Name: " << params_->agent_name()
        << " Introspect Port: " << Sandesh::http_port() << std::endl;
        std::string sub("{\"introspectport\":");
        std::string pidstring = integerToString(getpid());

        pidstring += ".json";
        sub += integerToString(Sandesh::http_port()) + "}";

        std::ofstream outfile(pidstring.c_str(), std::ofstream::out);
        outfile << sub;
        outfile.close();
     }
}

void Agent::ReConnectCollectors() {
    // ReConnect Collectors
    Sandesh::ReConfigCollectors(
        Agent::GetInstance()->GetCollectorlist());
}

void Agent::InitDone() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Its observed that sometimes TBB scheduler misses spawn events
    // and doesnt schedule a task till its triggered again. As a work around
    // dummy timer is created at scheduler with default timeout of 1 sec
    // that fires and awake TBB periodically. Modify the timeout if required.
    if (tbb_keepawake_timeout_) {
        scheduler->ModifyTbbKeepAwakeTimeout(tbb_keepawake_timeout_);
    }

    // Its observed that sometimes TBB stops scheduling tasks altogether.
    // Initiate a monitor which asserts if no task is spawned for a given time.
    if (task_monitor_timeout_msec_) {
        scheduler->EnableMonitor(event_manager(), tbb_keepawake_timeout_,
                                 task_monitor_timeout_msec_, 400);
    }
}

static bool interface_exist(string &name) {
    struct if_nameindex *ifs = NULL;
    struct if_nameindex *head = NULL;
    bool ret = false;
    string tname = "";

    ifs = if_nameindex();
    if (ifs == NULL) {
        LOG(INFO, "No interface exists!");
        return ret;
    }
    head = ifs;
    while (ifs->if_name && ifs->if_index) {
        tname = ifs->if_name;
        if (string::npos != tname.find(name)) {
            ret = true;
            name = tname;
            break;
        }
        ifs++;
    }
    if_freenameindex(head);
    return ret;
}

void Agent::InitXenLinkLocalIntf() {
    if (!params_->isXenMode() || params_->xen_ll_name() == "")
        return;

    string dev_name = params_->xen_ll_name();
    if(!interface_exist(dev_name)) {
        LOG(INFO, "Interface " << dev_name << " not found");
        return;
    }
    params_->set_xen_ll_name(dev_name);

    //We create a kernel visible interface to support xapi
    //Once we support dpdk on xen, we should change
    //the transport type to KNI
    InetInterface::Create(intf_table_, params_->xen_ll_name(),
                          InetInterface::LINK_LOCAL, link_local_vrf_name_,
                          params_->xen_ll_addr(), params_->xen_ll_plen(),
                          params_->xen_ll_gw(), NullString(), link_local_vrf_name_,
                          Interface::TRANSPORT_ETHERNET);
}

void Agent::InitPeers() {
    // Create peer entries
    local_peer_.reset(new Peer(Peer::LOCAL_PEER, LOCAL_PEER_NAME, false));
    local_vm_peer_.reset(new Peer(Peer::LOCAL_VM_PEER, LOCAL_VM_PEER_NAME,
                                  false));
    linklocal_peer_.reset(new Peer(Peer::LINKLOCAL_PEER, LINKLOCAL_PEER_NAME,
                                   false));
    ecmp_peer_.reset(new Peer(Peer::ECMP_PEER, ECMP_PEER_NAME, true));
    vgw_peer_.reset(new Peer(Peer::VGW_PEER, VGW_PEER_NAME, true));
    evpn_routing_peer_.reset(new EvpnRoutingPeer());
    evpn_peer_.reset(new EvpnPeer());
    inet_evpn_peer_.reset(new InetEvpnPeer());
    multicast_peer_.reset(new Peer(Peer::MULTICAST_PEER, MULTICAST_PEER_NAME,
                                   false));
    multicast_tor_peer_.reset(new Peer(Peer::MULTICAST_TOR_PEER,
                                       MULTICAST_TOR_PEER_NAME, false));
    multicast_tree_builder_peer_.reset(
                                 new Peer(Peer::MULTICAST_FABRIC_TREE_BUILDER,
                                          MULTICAST_FABRIC_TREE_BUILDER_NAME,
                                          false));
    mac_vm_binding_peer_.reset(new Peer(Peer::MAC_VM_BINDING_PEER,
                              MAC_VM_BINDING_PEER_NAME, false));
    mac_learning_peer_.reset(new Peer(Peer::MAC_LEARNING_PEER,
                                      MAC_LEARNING_PEER_NAME,
                                      false));
    fabric_rt_export_peer_.reset(new Peer(Peer::LOCAL_VM_PEER,
                                          FABRIC_RT_EXPORT,
                                          true));
    local_vm_export_peer_.reset(new Peer(Peer::LOCAL_VM_PEER,
                                         LOCAL_VM_EXPORT_PEER,
                                         true));
}

void Agent::ReconfigSignalHandler(boost::system::error_code ec, int signum) {
    LOG(DEBUG, "Received SIGHUP to apply updated configuration");
    // Read the configuration
    params_->ReInit();
    // Generates checksum, randomizes and saves the list
    CopyFilteredParams();
    // ReConnnect only ones whose checksums are different
    controller()->ReConnect();
    // ReConnect Collectors
    ReConnectCollectors();
}

void Agent::DebugSignalHandler(boost::system::error_code ec, int signum) {
        LOG(INFO, "Received SIGUSR1 to update debug configuration");
        //Read the debug configuration
        params_->DebugInit();
}

Agent::Agent() :
    params_(NULL), cfg_(NULL), stats_(NULL), ksync_(NULL), uve_(NULL),
    stats_collector_(NULL), flow_stats_manager_(NULL), pkt_(NULL),
    services_(NULL), vgw_(NULL), rest_server_(NULL), port_ipc_handler_(NULL),
    oper_db_(NULL), diag_table_(NULL), controller_(NULL), resource_manager_(),
    event_notifier_(), event_mgr_(NULL),
    agent_xmpp_channel_(), ifmap_channel_(),
    xmpp_client_(), xmpp_init_(), dns_xmpp_channel_(), dns_xmpp_client_(),
    dns_xmpp_init_(), agent_stale_cleaner_(NULL), cn_mcast_builder_(NULL),
    metadata_server_port_(0), host_name_(""), agent_name_(""),
    prog_name_(""), introspect_port_(0),
    instance_id_(g_vns_constants.INSTANCE_ID_DEFAULT),
    module_type_(Module::VROUTER_AGENT), module_name_(),
    db_(NULL), task_scheduler_(NULL), agent_init_(NULL), fabric_vrf_(NULL),
    fabric_policy_vrf_(NULL),
    intf_table_(NULL), health_check_table_(NULL), metadata_ip_allocator_(NULL),
    nh_table_(NULL), uc_rt_table_(NULL), mc_rt_table_(NULL),
    evpn_rt_table_(NULL), l2_rt_table_(NULL), vrf_table_(NULL),
    vm_table_(NULL), vn_table_(NULL), sg_table_(NULL), mpls_table_(NULL),
    acl_table_(NULL), mirror_table_(NULL), vrf_assign_table_(NULL),
    vxlan_table_(NULL), service_instance_table_(NULL),
    physical_device_table_(NULL), physical_device_vn_table_(NULL),
    config_manager_(), mirror_cfg_table_(NULL),
    intf_mirror_cfg_table_(NULL), router_id_(0), prefix_len_(0),
    gateway_id_(0), compute_node_ip_(0), xs_cfg_addr_(""), xs_idx_(0),
    xs_addr_(), xs_port_(),
    xs_stime_(), xs_auth_enable_(false), xs_dns_idx_(0), dns_addr_(),
    dns_port_(), dns_auth_enable_(false),
    controller_chksum_(0), dns_chksum_(0), collector_chksum_(0),
    ip_fabric_intf_name_(""), crypt_intf_name_(""),
    vhost_interface_name_(""),
    pkt_interface_name_("pkt0"), arp_proto_(NULL),
    dhcp_proto_(NULL), dns_proto_(NULL), icmp_proto_(NULL),
    dhcpv6_proto_(NULL), icmpv6_proto_(NULL), flow_proto_(NULL),
    mac_learning_proto_(NULL), mac_learning_module_(NULL),
    local_peer_(NULL), local_vm_peer_(NULL), linklocal_peer_(NULL),
    ecmp_peer_(NULL), vgw_peer_(NULL), evpn_routing_peer_(NULL),
    evpn_peer_(NULL), multicast_peer_(NULL),
    multicast_tor_peer_(NULL), multicast_tree_builder_peer_(NULL),
    mac_vm_binding_peer_(NULL), ifmap_parser_(NULL),
    router_id_configured_(false), mirror_src_udp_port_(0),
    lifetime_manager_(NULL), ksync_sync_mode_(false), mgmt_ip_(""),
    vxlan_network_identifier_mode_(AUTOMATIC), vhost_interface_(NULL),
    crypt_interface_(NULL), connection_state_(NULL), test_mode_(false),
    xmpp_dns_test_mode_(false),
    init_done_(false), resource_manager_ready_(false),
    simulate_evpn_tor_(false), tsn_no_forwarding_enabled_(false),
    tsn_enabled_(false),
    tor_agent_enabled_(false), forwarding_enabled_(true),
    server_gateway_mode_(false), pbb_gateway_mode_(false),
    inet_labeled_enabled_(false),
    flow_table_size_(0), flow_thread_count_(0), flow_trace_enable_(true),
    max_vm_flows_(0), ovsdb_client_(NULL), vrouter_server_ip_(0),
    vrouter_server_port_(0), vrouter_max_labels_(0), vrouter_max_nexthops_(0),
    vrouter_max_interfaces_(0), vrouter_max_vrfs_(0),
    vrouter_max_mirror_entries_(0), vrouter_max_bridge_entries_(0),
    vrouter_max_oflow_bridge_entries_(0), vrouter_priority_tagging_(true),
    flow_stats_req_handler_(NULL),
    tbb_keepawake_timeout_(kDefaultTbbKeepawakeTimeout),
    task_monitor_timeout_msec_(kDefaultTaskMonitorTimeout),
    vr_limit_high_watermark_(kDefaultHighWatermark),
    vr_limit_low_watermark_(kDefaultLowWatermark) {

    assert(singleton_ == NULL);
    singleton_ = this;
    db_ = new DB();
    assert(db_);

    event_mgr_ = new EventManager();
    assert(event_mgr_);

    SetAgentTaskPolicy();
    CreateLifetimeManager();

    Module::type module = static_cast<Module::type>(module_type_);
    module_name_ = g_vns_constants.ModuleNames.find(module)->second;

    agent_signal_.reset(
        AgentObjectFactory::Create<AgentSignal>(event_mgr_));
    agent_signal_.get()->RegisterSigHupHandler(
        boost::bind(&Agent::ReconfigSignalHandler, this, _1, _2));
    agent_signal_.get()->RegisterDebugSigHandler(
        boost::bind(&Agent::DebugSignalHandler, this, _1, _2));

    config_manager_.reset(new ConfigManager(this));
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        (agent_xmpp_channel_[count]).reset();
    }
}

Agent::~Agent() {
    uve_ = NULL;
    flow_stats_manager_ = NULL;

    agent_signal_->Terminate();
    agent_signal_.reset();

    ShutdownLifetimeManager();

    delete db_;
    db_ = NULL;
    singleton_ = NULL;

    if (task_scheduler_) {
        task_scheduler_->Terminate();
        task_scheduler_ = NULL;
    }

    delete event_mgr_;
    event_mgr_ = NULL;
}

AgentConfig *Agent::cfg() const {
    return cfg_;
}

void Agent::set_cfg(AgentConfig *cfg) {
    cfg_ = cfg;
}

DiagTable *Agent::diag_table() const {
    return diag_table_;
}

void Agent::set_diag_table(DiagTable *table) {
    diag_table_ = table;
}

AgentStats *Agent::stats() const {
    return stats_;
}

void Agent::set_stats(AgentStats *stats) {
    stats_ = stats;
}

ConfigManager *Agent::config_manager() const {
    return config_manager_.get();
}

EventNotifier *Agent::event_notifier() const {
    return event_notifier_;
}

void Agent::set_event_notifier(EventNotifier *val) {
    event_notifier_ = val;
}

KSync *Agent::ksync() const {
    return ksync_;
}

void Agent::set_ksync(KSync *ksync) {
    ksync_ = ksync;
}

AgentUveBase *Agent::uve() const {
    return uve_;
}

void Agent::set_uve(AgentUveBase *uve) {
    uve_ = uve;
}

AgentStatsCollector *Agent::stats_collector() const {
    return stats_collector_;
}

void Agent::set_stats_collector(AgentStatsCollector *asc) {
    stats_collector_ = asc;
}

FlowStatsManager *Agent::flow_stats_manager() const {
    return flow_stats_manager_;
}

void Agent::set_flow_stats_manager(FlowStatsManager *aging_module) {
    flow_stats_manager_ = aging_module;
}

HealthCheckTable *Agent::health_check_table() const {
    return health_check_table_;
}

void Agent::set_health_check_table(HealthCheckTable *table) {
    health_check_table_ = table;
}

BridgeDomainTable* Agent::bridge_domain_table() const {
    return bridge_domain_table_;
}

void Agent::set_bridge_domain_table(BridgeDomainTable *table) {
    bridge_domain_table_ = table;
}

MetaDataIpAllocator *Agent::metadata_ip_allocator() const {
    return metadata_ip_allocator_.get();
}

void Agent::set_metadata_ip_allocator(MetaDataIpAllocator *allocator) {
    metadata_ip_allocator_.reset(allocator);
}

PktModule *Agent::pkt() const {
    return pkt_;
}

void Agent::set_pkt(PktModule *pkt) {
    pkt_ = pkt;
}

ServicesModule *Agent::services() const {
    return services_;
}

void Agent::set_services(ServicesModule *services) {
    services_ = services;
}

VNController *Agent::controller() const {
    return controller_;
}

void Agent::set_controller(VNController *val) {
    controller_ = val;
}

VirtualGateway *Agent::vgw() const {
    return vgw_;
}

void Agent::set_vgw(VirtualGateway *vgw) {
    vgw_ = vgw;
}

RESTServer *Agent::rest_server() const {
    return rest_server_;
}

void Agent::set_rest_server(RESTServer *r) {
    rest_server_ = r;
}

PortIpcHandler *Agent::port_ipc_handler() const {
    return port_ipc_handler_;
}

void Agent::set_port_ipc_handler(PortIpcHandler *r) {
    port_ipc_handler_ = r;
}

OperDB *Agent::oper_db() const {
    return oper_db_;
}

void Agent::set_oper_db(OperDB *oper_db) {
    oper_db_ = oper_db;
}

ResourceManager *Agent::resource_manager() const {
    return resource_manager_;
}

void Agent::set_resource_manager(ResourceManager *val) {
    resource_manager_ = val;
}

DomainConfig *Agent::domain_config_table() const {
    return oper_db_->domain_config_table();
}

bool Agent::isVmwareMode() const {
    return params_->isVmwareMode();
}

bool Agent::isVmwareVcenterMode() const {
    if (isVmwareMode() == false)
        return false;

    return params_->isVmwareVcenterMode();
}

void Agent::ConcurrencyCheck() {
    if (test_mode_) {
       CHECK_CONCURRENCY("db::DBTable", "Agent::KSync", AGENT_INIT_TASKNAME,
                         kTaskFlowMgmt, kTaskFlowUpdate,
                         kTaskFlowEvent, kTaskFlowDelete, kTaskFlowKSync,
                         kTaskHealthCheck, kTaskCryptTunnel, kAgentResourceRestoreTask);
    }
}

bool Agent::vrouter_on_nic_mode() const {
    return params_->vrouter_on_nic_mode();
}

bool Agent::vrouter_on_host_dpdk() const {
    return params_->vrouter_on_host_dpdk();
}

bool Agent::vrouter_on_host() const {
    return params_->vrouter_on_host();
}

uint16_t
Agent::ProtocolStringToInt(const std::string &proto_arg) {
    std::string proto = proto_arg;

    std::transform(proto.begin(), proto.end(), proto.begin(), ::tolower);

    if (proto == "tcp") {
        return IPPROTO_TCP;
    }

    if (proto == "udp") {
        return IPPROTO_UDP;
    }

    if (proto == "sctp") {
        return IPPROTO_SCTP;
    }

    if (proto =="icmp") {
        return IPPROTO_ICMP;
    }

    if (proto == "icmp6") {
        return IPPROTO_ICMPV6;
    }

    return atoi(proto.c_str());
}

Agent::ForwardingMode Agent::TranslateForwardingMode
(const std::string &mode) const {
    if (mode == "l2")
        return Agent::L2;
    else if (mode == "l3")
        return Agent::L3;
    else if (mode == "l2_l3")
        return Agent::L2_L3;

    return Agent::NONE;
}

void Agent::set_flow_table_size(uint32_t count) {
    flow_table_size_ = count;
    if (params_->max_vm_flows() >= 100) {
        max_vm_flows_ = 0;
    } else {
        max_vm_flows_ = (count * params_->max_vm_flows()) / 100;
    }
}

void Agent::set_controller_xmpp_channel(AgentXmppChannel *channel, uint8_t idx) {
    assert(channel != NULL);
    (agent_xmpp_channel_[idx]).reset(channel);
}

void Agent::reset_controller_xmpp_channel(uint8_t idx) {
    (agent_xmpp_channel_[idx]).reset();
}

boost::shared_ptr<AgentXmppChannel> Agent::controller_xmpp_channel_ref(uint8_t idx) {
    return agent_xmpp_channel_[idx];
}

void Agent::TaskTrace(const char *file_name, uint32_t line_no,
                      const Task *task, const char *description,
                      uint64_t delay) {
    TaskTrace::TraceMsg(TaskTraceBuf, file_name, line_no,
                        task->GetTaskId(), task->GetTaskInstance(),
                        description, delay, task->Description());
}

bool Agent::MeasureQueueDelay() {
    return params_->measure_queue_delay();
}

void Agent::SetMeasureQueueDelay(bool val) {
    return params_->set_measure_queue_delay(val);
}

void Agent::SetXmppDscp(uint8_t val) {
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        XmppClient *client = xmpp_client_[count];
        if (client) {
            client->SetDscpValue(val, XmppInit::kControlNodeJID);
        }
        client = dns_xmpp_client_[count];
        if (client) {
            client->SetDscpValue(val, XmppInit::kDnsNodeJID);
        }
    }
}

VrouterObjectLimits Agent::GetVrouterObjectLimits() {
   VrouterObjectLimits vr_limits;
   vr_limits.set_max_labels(vrouter_max_labels());
   vr_limits.set_max_nexthops(vrouter_max_nexthops());
   vr_limits.set_max_interfaces(vrouter_max_interfaces());
   vr_limits.set_max_vrfs(vrouter_max_vrfs());
   vr_limits.set_max_mirror_entries(vrouter_max_mirror_entries());
   vr_limits.set_vrouter_max_bridge_entries(vrouter_max_bridge_entries());
   vr_limits.set_vrouter_max_oflow_bridge_entries(
           vrouter_max_oflow_bridge_entries());
   vr_limits.set_vrouter_build_info(vrouter_build_info());
   vr_limits.set_vrouter_max_flow_entries(vrouter_max_flow_entries());
   vr_limits.set_vrouter_max_oflow_entries(vrouter_max_oflow_entries());
   vr_limits.set_vrouter_priority_tagging(vrouter_priority_tagging());
   return vr_limits;
}

void Agent::SetResourceManagerReady() {
    resource_manager_ready_ = true;
    config_manager_->Start();
}

uint8_t Agent::GetInterfaceTransport() const {
    if (params()->vrouter_on_host_dpdk()) {
        return Interface::TRANSPORT_PMD;
    }
    return Interface::TRANSPORT_ETHERNET;
}
