/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string.hpp>
#include <boost/assign/std/vector.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/addr_test_util.h"
#include "base/test/task_test_util.h"
#include "base/util.h"

#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_debug.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"

#include "db/db.h"
#include "io/event_manager.h"

#include "sandesh/common/vns_types.h"
#include "sandesh/sandesh_http.h"
#include "sandesh/sandesh_types.h"
#include "schema/xmpp_unicast_types.h"
#include "schema/vnc_cfg_types.h"
#include "tbb/task_scheduler_init.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_state_machine.h"

#include "bgp/test/bgp_stress_test.h"

#define BGP_STRESS_TEST_LOG(str)                                 \
do {                                                             \
    log4cplus::Logger logger = log4cplus::Logger::getRoot();     \
    LOG4CPLUS_DEBUG(logger, "BGP_STRESS_TEST_LOG: "              \
                    << __FILE__  << ":"  << __FUNCTION__ << "()" \
                    << ":"  << __LINE__ << " " << str);          \
} while (false)

#undef __BGP_PROFILE__

#if defined(__BGP_PROFILE__) && ! defined(__APPLE__)

#include "gperftools/heap-profiler.h"
#include "gperftools/heap-checker.h"

#define HEAP_PROFILER_START(prefix)  \
    do {                             \
        if (!d_profile_heap_) break; \
        HeapProfilerStart(prefix);   \
    } while (false)

#define HEAP_PROFILER_STOP()         \
    do {                             \
        if (!d_profile_heap_) break; \
        HeapProfilerStop();          \
    } while (false)

#define HEAP_PROFILER_DUMP(reason)   \
    do {                             \
        if (!d_profile_heap_) break; \
        HeapProfilerDump(reason);    \
    } while (false)

#else

#define HEAP_PROFILER_START(prefix)
#define HEAP_PROFILER_STOP()
#define HEAP_PROFILER_DUMP(reason)

#endif

#define XMPP_CONTROL_SERV "bgp.contrail.com"
#define PUBSUB_NODE_ADDR  "bgp-node.contrail.com"

using namespace std;
using namespace boost::asio;
using namespace boost::assign;
using namespace boost::program_options;
using boost::any_cast;
using boost::scoped_ptr;
using ::testing::TestWithParam;
using ::testing::Bool;
using ::testing::ValuesIn;
using ::testing::Combine;

static int d_events_ = 50;
static vector<float> d_events_weight_ = boost::assign::list_of
   (50.0) // ADD_BGP_ROUTE
   (50.0) // DELETE_BGP_ROUTE
   (70.0) // ADD_XMPP_ROUTE
   (70.0) // DELETE_XMPP_ROUTE
   (20.0) // BRING_UP_XMPP_AGENT
   (20.0) // BRING_DOWN_XMPP_AGENT
   (20.0) // CLEAR_XMPP_AGENT
   (20.0) // SUBSCRIBE_ROUTING_INSTANCE
   (20.0) // UNSUBSCRIBE_ROUTING_INSTANCE
   (20.0) // SUBSCRIBE_CONFIGURATION
   (20.0) // UNSUBSCRIBE_CONFIGURATION
   (10.0) // ADD_BGP_PEER
   (10.0) // DELETE_BGP_PEER
   (20.0) // CLEAR_BGP_PEER
   (30.0) // ADD_ROUTING_INSTANCE
   (30.0) // DELETE_ROUTING_INSTANCE
   (10.0) // ADD_ROUTE_TARGET
   (10.0) // DELETE_ROUTE_TARGET
   (10.0) // CHANGE_SOCKET_BUFFER_SIZE
   (10.0) // SHOW_ALL_ROUTES
;

//
// Parameters
//
// ninstances: Number of routing-instances configured in DUT (BgpServer)
// nroutes:    Number of routes advertised by each agent
// npeers:     Number of BGP Peers that form session over inet and inet-vpn
// nagents:    Number of XMPP agents that subscribe to various routing-instances
// ntargets:   Number of import/export route-targets attached to each instance
//

static bool d_external_mode_ = false;
static int d_instances_ = 1;
static int d_routes_ = 1;
static int d_peers_ = 1;
static int d_agents_ = 1;
static int d_targets_ = 1;
static int d_test_id_ = 0;
static int d_vms_count_ = 0;
static bool d_close_from_control_node_ = false;
static bool d_pause_after_initial_setup_ = false;
static bool d_profile_heap_ = false;
static bool d_no_mcast_routes_ = false;
static bool d_no_sandesh_server_ = false;
static string d_xmpp_server_ = "127.0.0.1";
static string d_xmpp_source_ = "127.0.0.1";
static string d_xmpp_rt_nexthop_ = "";
static bool d_xmpp_rt_nexthop_vary_ = false;
static bool d_xmpp_rt_format_large_ = false;
static string d_xmpp_rt_prefix_ = "";
static string d_instance_name_ = "";
static int d_xmpp_port_ = 0;
static int d_http_port_ = 0;
static string d_feed_events_file_ = "";
static string d_routes_send_trigger_ = "";
static string d_log_category_ = "";
static string d_log_level_ = "SYS_DEBUG";
static bool d_log_local_disable_ = false;
static bool d_log_disable_ = false;
static bool d_no_verify_routes_ = false;
static bool d_no_agent_updates_processing_ = false;
static bool d_no_agent_messages_processing_ = false;
static float d_events_proportion_ = 0.0;

static vector<int>  n_instances = boost::assign::list_of(d_instances_);
static vector<int>  n_routes    = boost::assign::list_of(d_routes_);
static vector<int>  n_peers     = boost::assign::list_of(d_peers_);
static vector<int>  n_agents    = boost::assign::list_of(d_agents_);
static vector<int>  n_targets   = boost::assign::list_of(d_targets_);
static vector<bool> xmpp_close_from_control_node =
                        boost::assign::list_of(d_close_from_control_node_);

static int d_db_walker_wait_ = 0;
static int d_wait_for_idle_ = 30; // Seconds

static const char **gargv;
static int gargc;

static void WaitForIdle() {
    if (d_wait_for_idle_) {
        usleep(10);
        task_util::WaitForIdle(d_wait_for_idle_);
    }
}

PeerCloseManagerTest::PeerCloseManagerTest(IPeer *peer) :
        PeerCloseManager(peer) {
}

PeerCloseManagerTest::~PeerCloseManagerTest() {
}

void PeerCloseManagerTest::StartStaleTimer() {
}

void StateMachineTest:: StartConnectTimer(int seconds) {
    connect_timer_->Start(100,
            boost::bind(&StateMachine::ConnectTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachineTest::StartOpenTimer(int seconds) {
    open_timer_->Start(100,
            boost::bind(&StateMachine::OpenTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachineTest::StartIdleHoldTimer() {
    if (idle_hold_time_ <= 0) return;

    idle_hold_timer_->Start(100,
            boost::bind(&StateMachine::IdleHoldTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void XmppStateMachineTest::StartConnectTimer(int seconds) {
    connect_timer_->Start(10,
        boost::bind(&XmppStateMachine::ConnectTimerExpired, this),
        boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
}

void XmppStateMachineTest::StartOpenTimer(int seconds) {
    open_timer_->Start(10,
        boost::bind(&XmppStateMachine::OpenTimerExpired, this),
        boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
}

static string GetRouterName(int router_id) {
    return "A" + boost::lexical_cast<string>(router_id);
}

BgpNullPeer::BgpNullPeer(BgpServerTest *server, int peer_id) {
    for (int i = 0; i < 20; i++) {
        ribout_creation_complete_.push_back(false);
    }

    string uuid = BgpConfigParser::session_uuid(
            GetRouterName(0), GetRouterName(peer_id), 1);
    TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
                        static_cast<BgpPeerTest *>(
                        server->FindPeerByUuid(
                            BgpConfigManager::kMasterInstance, uuid)));
    peer_ = static_cast<BgpPeerTest *>(
                server->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                       uuid));
    peer_id_ = peer_id;
}

BgpXmppChannelManagerMock::BgpXmppChannelManagerMock(XmppServerTest *x,
                                                     BgpServer *b) :
        BgpXmppChannelManager(x, b), channel_(NULL) {
}

BgpXmppChannelManagerMock::~BgpXmppChannelManagerMock() {
}

BgpXmppChannel *BgpXmppChannelManagerMock::CreateChannel(XmppChannel *channel) {
    channel_ = new BgpXmppChannel(channel, bgp_server_, this);
    return channel_;
}

string BgpStressTestEvent::ToString(BgpStressTestEvent::EventType event) {
    switch (event) {
        case ADD_BGP_ROUTE:
            return "ADD_BGP_ROUTE";
        case DELETE_BGP_ROUTE:
            return  "DELETE_BGP_ROUTE";
        case ADD_XMPP_ROUTE:
            return  "ADD_XMPP_ROUTE";
        case DELETE_XMPP_ROUTE:
            return  "DELETE_XMPP_ROUTE";
        case BRING_UP_XMPP_AGENT:
            return  "BRING_UP_XMPP_AGENT";
        case BRING_DOWN_XMPP_AGENT:
            return  "BRING_DOWN_XMPP_AGENT";
        case CLEAR_XMPP_AGENT:
            return  "CLEAR_XMPP_AGENT";
        case SUBSCRIBE_ROUTING_INSTANCE:
            return "SUBSCRIBE_ROUTING_INSTANCE";
        case UNSUBSCRIBE_ROUTING_INSTANCE:
            return "UNSUBSCRIBE_ROUTING_INSTANCE";
        case SUBSCRIBE_CONFIGURATION:
            return "SUBSCRIBE_CONFIGURATION";
        case UNSUBSCRIBE_CONFIGURATION:
            return "UNSUBSCRIBE_CONFIGURATION";
        case ADD_BGP_PEER:
            return "ADD_BGP_PEER";
        case DELETE_BGP_PEER:
            return "DELETE_BGP_PEER";
        case CLEAR_BGP_PEER:
            return "CLEAR_BGP_PEER";
        case ADD_ROUTING_INSTANCE:
            return "ADD_ROUTING_INSTANCE";
        case DELETE_ROUTING_INSTANCE:
            return "DELETE_ROUTING_INSTANCE";
        case ADD_ROUTE_TARGET:
            return "ADD_ROUTE_TARGET";
        case DELETE_ROUTE_TARGET:
            return "DELETE_ROUTE_TARGET";
        case CHANGE_SOCKET_BUFFER_SIZE:
            return "CHANGE_SOCKET_BUFFER_SIZE";
        case SHOW_ALL_ROUTES:
            return "SHOW_ALL_ROUTES";
        case PAUSE:
            return "PAUSE";
    }

    assert(false);
}

BgpStressTestEvent::EventType BgpStressTestEvent::FromString(
                                                      const string event) {
    static EventStringMap from_string_map_ = map_list_of
            ("ADD_BGP_ROUTE", ADD_BGP_ROUTE)
            ("DELETE_BGP_ROUTE", DELETE_BGP_ROUTE)
            ("ADD_XMPP_ROUTE", ADD_XMPP_ROUTE)
            ("DELETE_XMPP_ROUTE", DELETE_XMPP_ROUTE)
            ("BRING_UP_XMPP_AGENT", BRING_UP_XMPP_AGENT)
            ("BRING_DOWN_XMPP_AGENT", BRING_DOWN_XMPP_AGENT)
            ("CLEAR_XMPP_AGENT", CLEAR_XMPP_AGENT)
            ("SUBSCRIBE_ROUTING_INSTANCE", SUBSCRIBE_ROUTING_INSTANCE)
            ("UNSUBSCRIBE_ROUTING_INSTANCE", UNSUBSCRIBE_ROUTING_INSTANCE)
            ("SUBSCRIBE_CONFIGURATION", SUBSCRIBE_CONFIGURATION)
            ("UNSUBSCRIBE_CONFIGURATION", UNSUBSCRIBE_CONFIGURATION)
            ("ADD_BGP_PEER", ADD_BGP_PEER)
            ("DELETE_BGP_PEER", DELETE_BGP_PEER)
            ("CLEAR_BGP_PEER", CLEAR_BGP_PEER)
            ("ADD_ROUTING_INSTANCE", ADD_ROUTING_INSTANCE)
            ("DELETE_ROUTING_INSTANCE", DELETE_ROUTING_INSTANCE)
            ("ADD_ROUTE_TARGET", ADD_ROUTE_TARGET)
            ("DELETE_ROUTE_TARGET", DELETE_ROUTE_TARGET)
            ("CHANGE_SOCKET_BUFFER_SIZE", CHANGE_SOCKET_BUFFER_SIZE)
            ("SHOW_ALL_ROUTES", SHOW_ALL_ROUTES)
            ("PAUSE", PAUSE)
        ;

    EventStringMap::iterator iter = from_string_map_.find(event);
    if (iter == from_string_map_.end()) return EVENT_INVALID;

    return iter->second;
}

void BgpStressTestEvent::ReadEventsFromFile(string events_file) {
    struct noop {
        void operator()(...) const {}
    };

    istream *input;
    if (events_file == "-") {
        input = &cin;
    } else {
        input = new ifstream(events_file.c_str());
    }


    while (input->good()) {
        string line;
        getline(*input, line);

        size_t pos = line.find("event: ");
        if (pos == string::npos) continue;

        //
        // Get the actual event string
        //
        EventType event = FromString(line.c_str() + pos + 7);

        if (event != EVENT_INVALID) {
            d_events_list_.push_back(event);
        }
    }

    if (input != &cin) {
        delete input;
    }

    //
    // Set the number of events read from the file
    //
    if (d_events_list_.size()) {
        d_events_ = d_events_list_.size();
    }
}
float BgpStressTestEvent::GetEventWeightSum() {
    static float event_weight_sum_ = 0;

    if (!event_weight_sum_) {
        for(int i = 0; i < NUM_TEST_EVENTS; i++) {
            event_weight_sum_ += d_events_weight_[i];
        }
    }
    return event_weight_sum_;
}

BgpStressTestEvent::EventType BgpStressTestEvent::GetTestEvent(int count) {
    float rnd;
    int     i;
    EventType event;

    //
    // Check if events to be fed are known (from a file..)
    //
    if (!d_events_list_.empty()) {
        event = d_events_list_[count - 1];
    } else {
        rnd = random((int) GetEventWeightSum());
        for(i = 0; i < NUM_TEST_EVENTS; i++) {
            if(rnd < d_events_weight_[i]) {
                break;
            }
            rnd -= d_events_weight_[i];
        }

        event = static_cast<EventType>(i + 1);
    }

    ostringstream out;
    out << "Feed " << count << "th event: " << ToString(event);
    d_events_played_list_.push_back(out.str());

    BGP_STRESS_TEST_LOG(out.str());

    return event;
}

int BgpStressTestEvent::random(int limit) {
    return std::rand() % limit;
}

vector<int> BgpStressTestEvent::GetEventItems(int nitems, int inc) {
    vector<int> event_ids_list;

    if (!nitems) return event_ids_list;

    int event_ids = 1;
    if (d_events_proportion_) {
        event_ids  = nitems * d_events_proportion_;
        if (!event_ids) event_ids = 1;
    }

    for (int i = 0; i < event_ids; i++) {
        while (true) {
            int item = BgpStressTestEvent::random(nitems);
            if (std::find(event_ids_list.begin(),
                            event_ids_list.end(), item) ==
                    event_ids_list.end()) {
                event_ids_list.push_back(item + inc);
                break;
            }
        }
    }

    return event_ids_list;
}

void BgpStressTestEvent::clear_events() {
    d_events_played_list_.clear();
}

vector<BgpStressTestEvent::EventType> BgpStressTestEvent::d_events_list_;
vector<string> BgpStressTestEvent::d_events_played_list_;

void BgpStressTest::IFMapInitialize() {
    if (d_external_mode_) return;

    config_db_ = new DB();
    config_graph_ = new DBGraph();

    ifmap_server_.reset(new IFMapServer(config_db_, config_graph_,
                                        evm_.io_service()));
    IFMapLinkTable_Init(ifmap_server_->database(), ifmap_server_->graph());
    IFMapServerParser *ifmap_parser =
        IFMapServerParser::GetInstance("vnc_cfg");
    vnc_cfg_ParserInit(ifmap_parser);
    vnc_cfg_Server_ModuleInit(ifmap_server_->database(),
                                ifmap_server_->graph());
    bgp_schema_ParserInit(ifmap_parser);
    bgp_schema_Server_ModuleInit(ifmap_server_->database(),
                                    ifmap_server_->graph());
    ifmap_server_->Initialize();
}

void BgpStressTest::IFMapCleanUp() {
    if (d_external_mode_) return;

    ifmap_server_->Shutdown();
    WaitForIdle();
    IFMapLinkTable_Clear(config_db_);
    IFMapTable::ClearTables(config_db_);
    WaitForIdle();

    config_db_->Clear();
    IFMapServerParser::GetInstance("vnc_cfg")->MetadataClear("vnc_cfg");
}

void BgpStressTest::SetUp() {
    BgpStressTestEvent::clear_events();
    socket_buffer_size_ = 1 << 10;

    sandesh_context_.reset(new BgpSandeshContext());
    if (!d_no_sandesh_server_) {

        //
        // Initialize SandeshServer
        //
        sandesh_server_ = new SandeshServerTest(&evm_);
        sandesh_server_->Initialize(0);

        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
        Sandesh::InitGenerator("BgpUnitTestSandeshClient", hostname, 
                               "BgpTest", "Test", &evm_,
                                d_http_port_, sandesh_context_.get());
        Sandesh::ConnectToCollector("127.0.0.1",
                                    sandesh_server_->GetPort());
        BGP_STRESS_TEST_LOG("Introspect at http://localhost:" <<
                            Sandesh::http_port());
    } else {
        Sandesh::set_client_context(sandesh_context_.get());
        sandesh_server_ = NULL;
    }

    IFMapInitialize();

    if (!d_external_mode_) {
        server_.reset(new BgpServerTest(&evm_, "A0", config_db_,
                                        config_graph_));
    } else {
        server_.reset(new BgpServerTest(&evm_, "A0"));
    }
    xmpp_server_test_ = new XmppServerTest(&evm_, XMPP_CONTROL_SERV);

    if (!d_external_mode_) {
        ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_test_,
                                            ifmap_server_.get()));
        ifmap_server_->set_ifmap_channel_manager(ifmap_channel_mgr_.get());
        sandesh_context_->ifmap_server = ifmap_server_.get();
    }

    channel_manager_.reset(new BgpXmppChannelManagerMock(
                                    xmpp_server_test_, server_.get()));
    master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
        BgpConfigManager::kMasterInstance, "", ""));
    rtinstance_ = static_cast<RoutingInstance *>(
        server_->routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance));
    n_families_ = 2;
    families_.push_back(Address::INET);
    families_.push_back(Address::INETVPN);

    server_->session_manager()->Initialize(0);
    xmpp_server_test_->Initialize(0, false);

    sandesh_context_->bgp_server = server_.get();
    sandesh_context_->xmpp_peer_manager = channel_manager_.get();

    thread_.Start();
}

void BgpStressTest::TearDown() {
    WaitForIdle();
    xmpp_server_test_->Shutdown();
    WaitForIdle();
    if (n_agents_) {
        TASK_UTIL_EXPECT_EQ(0, xmpp_server_test_->ConnectionCount());
    }
    AgentCleanup();
    channel_manager_.reset();
    WaitForIdle();

    TcpServerManager::DeleteServer(xmpp_server_test_);
    xmpp_server_test_ = NULL;
    BOOST_FOREACH(BgpServerTest *peer_server, peer_servers_) {
        if (peer_server) {
            peer_server->Shutdown();
            WaitForIdle();
            delete peer_server;
        }
    }
    WaitForIdle();
    server_->Shutdown();
    WaitForIdle();
    Cleanup();
    WaitForIdle();
    IFMapCleanUp();
    WaitForIdle();

    evm_.Shutdown();
    thread_.Join();
    task_util::WaitForIdle();
}

void BgpStressTest::AgentCleanup() {
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Delete();
    }
    WaitForIdle();
}
void BgpStressTest::Cleanup() {
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (npeer) {
            delete npeer;
        }
    }

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        delete agent;
    }
    WaitForIdle();
    SandeshShutdown();
    WaitForIdle();
}

void BgpStressTest::SandeshShutdown() {
    if (d_no_sandesh_server_) return;

    Sandesh::Uninit();
    WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(sandesh_server_->HasSessions());
    sandesh_server_->Shutdown();
    WaitForIdle();
    TcpServerManager::DeleteServer(sandesh_server_);
    sandesh_server_ = NULL;
    WaitForIdle();
}

void BgpStressTest::VerifyRoutingInstances() {
    for (int instance_id = 0; instance_id < (int) instances_.size();
            instance_id++) {
        if (instances_[instance_id]) {
            TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                    server_->routing_instance_mgr()->\
                        GetRoutingInstance(GetInstanceName(instance_id)));
        } else {
            TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                    server_->routing_instance_mgr()->\
                        GetRoutingInstance(GetInstanceName(instance_id)));
        }
    }
}

Ip4Prefix BgpStressTest::GetAgentRoute(int agent_id, int instance_id,
                                       int route_id) {
    Ip4Prefix prefix;

    if (!d_xmpp_rt_prefix_.empty()) {
        prefix = Ip4Prefix::FromString(d_xmpp_rt_prefix_);
    } else {
        unsigned long address;
        if (d_xmpp_rt_format_large_) {

            // <0-7> 3 bits for test-id
            // <0-31> 5 bits for instance-id
            // <0-4095> 12 bits for agent-id
            // <0-4095> 12 bits for prefix
            address = (d_test_id_ << 29) |
                        ((instance_id) << 24) |
                        (((agent_id >> 4) & 0xff) << 16) |
                        ((agent_id & 0xf) << 12) | 1;
        } else {

            // <0-511> 9  bits for test-id
            // <0-63>  6  bits for instance-id
            // <0-127> 7  bits for agent-id
            // <0-511> 10 bits for prefix
            address = (d_test_id_ << 23) |
                        (instance_id << 17) |
                        (agent_id << 10) | 1;
        }
        prefix = Ip4Prefix(Ip4Address(address), 32);
    }

    prefix = task_util::Ip4PrefixIncrement(prefix, route_id);

    return prefix;
}

void BgpStressTest::Configure(string config) {
    BGP_DEBUG_UT("Applying config: " << config);
    server_->Configure(config.c_str());
    VerifyRoutingInstances();
}

XmppChannelConfig *BgpStressTest::CreateXmppChannelCfg(const char *address,
                                                       int port,
                                                       const string &from,
                                                       const string &to,
                                                       bool isClient) {
    XmppChannelConfig *cfg = new XmppChannelConfig(isClient);
    boost::system::error_code ec;
    cfg->endpoint.address(ip::address::from_string(address, ec));
    cfg->endpoint.port(port);
    cfg->ToAddr = to;
    cfg->FromAddr = from;
    if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
    return cfg;
}

string BgpStressTest::GetInstanceName(int instance_id, int vn_id) {
    if (!instance_id) return BgpConfigManager::kMasterInstance;
    ostringstream out;

    out << "default-domain:demo";

    //
    // Check if instance name is provided by the user
    //
    if (!d_instance_name_.empty()) {
        out << ":" << d_instance_name_ << instance_id;
        out << ":" << d_instance_name_ << instance_id;
        return out.str();
    }

    out << ":network" << vn_id << "." << "instance" << instance_id;
    out << ":network" << vn_id << "." << "instance" << instance_id;

    return out.str();
}

void BgpStressTest::VerifyPeer(BgpServerTest *server, BgpNullPeer *npeer) {
    string uuid = BgpConfigParser::session_uuid(
                      server->config_manager()->localname(),
                      GetRouterName(npeer->peer_id()), 1);
    TASK_UTIL_EXPECT_EQ(npeer->peer(), static_cast<BgpPeerTest *>(
                                 server->FindPeerByUuid(
                                     BgpConfigManager::kMasterInstance, uuid)));

    if (npeer->peer()) {
        BGP_WAIT_FOR_PEER_STATE(npeer->peer(), StateMachine::ESTABLISHED);
        BGP_STRESS_TEST_LOG("BGP Peer " << npeer->peer()->ToString() << " up");
    }
}

void BgpStressTest::VerifyPeers() {
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (npeer) {
            VerifyPeer(server_.get(), npeer);
        }
    }
}

void BgpStressTest::VerifyNoPeer(int peer_id, string peer_name) {
    if (peer_id >= (int) peers_.size() || !peers_[peer_id]) return;

    BgpNullPeer *npeer = peers_[peer_id];
    npeer->set_peer(NULL);
    VerifyPeer(server_.get(), npeer);
    BGP_STRESS_TEST_LOG("BGP Peer " << peer_name << " down");
    delete peers_[peer_id];
    peers_[peer_id] = NULL;

    delete peer_servers_[peer_id];
    peer_servers_[peer_id] = NULL;
}

void BgpStressTest::VerifyNoPeers() {
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (npeer) {
            npeer->set_peer(NULL);
            VerifyPeer(server_.get(), npeer);
        }
    }
}

void BgpStressTest::VerifyControllerRoutes(int ninstances, int nagents,
                                           int count) {
    if (!n_peers_) return;

    for (int i = 0; i < n_families_; i++) {
        BgpTable *tb = rtinstance_->GetTable(families_[i]);
        if (count && (n_agents_ || n_peers_) &&
                families_[i] == Address::INETVPN) {
            int npeers = n_peers_;
            npeers += nagents;

            //
            // Each BGP peer sends its own route
            //
            // For each route received by the agent, we inject a route from
            // each of the bgp peer as well. RD's are chosen such that half
            // of those BGP routes have the same RD, and rest have unique RD
            //
            int bgp_routes = n_peers_;
            bgp_routes += (nagents + (n_peers_/2) * nagents) * ninstances;
            BGP_VERIFY_ROUTE_COUNT(tb, count *
                ((n_agents_ ? (ninstances * nagents) : 0) + bgp_routes));
        } else {
            BGP_VERIFY_ROUTE_COUNT(tb, count);
        }
    }
}

void BgpStressTest::VerifyRibOutCreationCompletion() {
    return;
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (!npeer) continue;
        for (int i = 0; i < n_families_; i++) {
            EXPECT_TRUE(npeer->ribout_creation_complete(families_[i]));
        }
    }
}

string BgpStressTest::GetRouterConfig(int router_id, int peer_id,
                                      bool skip_rtr_config) {

    int local_port;

    if (!router_id) {
        local_port = server_->session_manager()->GetPort();
    } else {
        local_port = peer_servers_[router_id]->session_manager()->GetPort();
    }

    ostringstream out;

    out << "<bgp-router name=\'";
    out << GetRouterName(router_id);;
    out << "\'>";

    if (!skip_rtr_config) {
        out << "<identifier>";
        out << "192.168.0." + boost::lexical_cast<string>(router_id);
        out << "</identifier>";
        out << "<address>127.0.0.1</address>";
        out << "<port>" << local_port << "</port>";
    }

    //
    // Cannot have a session to self
    //
    if (GetRouterName(router_id) != GetRouterName(peer_id)) {
        out << "<session to=\'";
        out << GetRouterName(peer_id);
        out << "\'>";
        out << "<address-families>";
        out << "<family>inet</family>";
        out << "<family>inet-vpn</family>";
        out << "<family>e-vpn</family>";
        out << "<family>erm-vpn</family>";
        out << "<family>route-target</family>";
        out << "</address-families>";
        out << "</session>";
    }

    out << "</bgp-router>";
    return out.str();
}

string BgpStressTest::GetInstanceConfig(int instance_id, int ntargets) {
    ostringstream out;
    int vn_id = 1;

    string instance_name = GetInstanceName(instance_id);
    out << "<routing-instance name='" << instance_name << "'>\n";

    for (int j = 1; j <= ntargets; j++) {
        out << "    <vrf-target>target:" << vn_id << ":";
        out << j << "</vrf-target>\n";
    }
    out << "</routing-instance>\n";
    return out.str();
}

BgpAttr *BgpStressTest::CreatePathAttr() {
    BgpAttrSpec attr_spec;
    BgpAttrDB *db = server_->attr_db();
    BgpAttr *attr = new BgpAttr(db, attr_spec);

    attr->set_origin(BgpAttrOrigin::IGP);
    attr->set_med(5);
    attr->set_local_pref(10);

    AsPathSpec as_path;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(30);
    as_path.path_segments.push_back(ps);

    attr->set_as_path(&as_path);

    return attr;
}

ExtCommunitySpec *BgpStressTest::CreateRouteTargets(int ntargets) {
    auto_ptr<ExtCommunitySpec> commspec(new ExtCommunitySpec());

    for (int i = 1; i <= ntargets; i++) {
        RouteTarget tgt = RouteTarget::FromString(
                "target:1:" + boost::lexical_cast<string>(i));
        const ExtCommunity::ExtCommunityValue &extcomm =
            tgt.GetExtCommunity();
        uint64_t value = get_value(extcomm.data(), extcomm.size());
        commspec->communities.push_back(value);
    }

    if (commspec->communities.empty()) return NULL;
    return commspec.release();
}

void BgpStressTest::AddBgpRoute(int family, int peer_id, int route_id,
                                int ntargets) {
    string start_prefix;

    uint32_t label = 20000 + route_id;
    start_prefix = "20." + boost::lexical_cast<string>(peer_id) + ".1.1/32";
    AddBgpRouteInternal(family, peer_id, ntargets, route_id, start_prefix,
                        label);

    for (int i = 1; i <= n_instances_; i++) {
        for (int agent = 1; agent <= n_agents_; agent++) {
            Ip4Prefix prefix = GetAgentRoute(agent, i, 0);
            AddBgpRouteInternal(family, peer_id, ntargets, route_id,
                                prefix.ToString(), 30000 + route_id);
        }
    }
}

void BgpStressTest::AddBgpRoute(vector<int> families, vector<int> peer_ids,
                                vector<int> route_ids, int ntargets) {
    BOOST_FOREACH(int family, families) {
        BOOST_FOREACH(int peer_id, peer_ids) {
            BOOST_FOREACH(int route_id, route_ids) {
                AddBgpRoute(family, peer_id, route_id, ntargets);
            }
        }
    }
}

void BgpStressTest::AddBgpRouteInternal(int family, int peer_id, int ntargets,
                                        int route_id, string start_prefix,
                                        int label) {
    DBRequest req;
    boost::scoped_ptr<ExtCommunitySpec> commspec;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;

    if (peer_id >= (int) peers_.size() || !peers_[peer_id]) return;
    BgpTable *table = rtinstance_->GetTable(families_[family]);
    if (!table) return;

    if (!peers_[peer_id]) return;
    IPeer *peer = peers_[peer_id]->peer();
    if (!peer) return;

    BGP_WAIT_FOR_PEER_STATE(peers_[peer_id]->peer(), StateMachine::ESTABLISHED);

    Ip4Prefix prefix(Ip4Prefix::FromString("192.168.255.0/24"));

    //
    // Use routes with same RD as well as with different RD to cover more
    // scenarios
    //
    InetVpnPrefix vpn_prefix(InetVpnPrefix::FromString("123:" +
                ((peer_id & 1) ? "999" :
        boost::lexical_cast<string>(peer_id)) + ":" + start_prefix));

    prefix = task_util::Ip4PrefixIncrement(prefix, route_id);
    vpn_prefix = task_util::InetVpnPrefixIncrement(vpn_prefix, route_id);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    // int localpref = 1 + (std::rand() % (npeers + nagents));
    int localpref = 100; // Default preference
    BgpAttrSpec attr_spec;
    local_pref.reset(new BgpAttrLocalPref(localpref));
    attr_spec.push_back(local_pref.get());

    BgpAttrNextHop nexthop(0x66010000 + peer_id); // 0x66 is 'B' for BGP
    attr_spec.push_back(&nexthop);
    TunnelEncap tun_encap(std::string("gre"));

    switch (table->family()) {
        case Address::INET:
            commspec.reset(new ExtCommunitySpec());
            commspec->communities.push_back(
                    get_value(tun_encap.GetExtCommunity().begin(), 8));
            attr_spec.push_back(commspec.get());
            req.key.reset(new InetTable::RequestKey(prefix, peer));
            break;
        case Address::INETVPN:
            req.key.reset(new InetVpnTable::RequestKey(vpn_prefix, peer));
            commspec.reset(CreateRouteTargets(ntargets));
            if (!commspec.get()) {
                commspec.reset(new ExtCommunitySpec());
            }
            commspec->communities.push_back(
                    get_value(tun_encap.GetExtCommunity().begin(), 8));
            attr_spec.push_back(commspec.get());
            break;
        default:
            assert(0);
            break;
    }

    BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
    req.data.reset(new InetTable::RequestData(attr, 0, label));
    table->Enqueue(&req);
}

void BgpStressTest::AddBgpRoutes(int family, int peer_id, int nroutes,
                                 int ntargets) {
    DBRequest req;
    boost::scoped_ptr<ExtCommunitySpec> commspec;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;

    for (int rt = 0; rt < nroutes; rt++) {
        AddBgpRoute(family, peer_id, rt, ntargets);
    }
}

void BgpStressTest::AddAllBgpRoutes(int nroutes, int ntargets) {
    bool fed = false;
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (!npeer) continue;

        for (int family = 0; family < n_families_; family++) {
            AddBgpRoutes(family, npeer->peer_id(), nroutes, ntargets);
            if (nroutes) fed = true;
        }
    }

    if (fed) BGP_STRESS_TEST_LOG("All BGP Routes fed");
}

void BgpStressTest::DeleteBgpRoute(int family, int peer_id, int route_id,
                                   int ntargets) {
    string start_prefix;

    start_prefix = "20." + boost::lexical_cast<string>(peer_id) + ".1.1/32";
    DeleteBgpRouteInternal(family, peer_id, route_id, start_prefix, ntargets);

    for (int i = 1; i <= n_instances_; i++) {
        for (int agent = 1; agent <= n_agents_; agent++) {
            Ip4Prefix prefix = GetAgentRoute(agent, i, 0);
            DeleteBgpRouteInternal(family, peer_id, route_id, prefix.ToString(),
                                   ntargets);
        }
    }
}

void BgpStressTest::DeleteBgpRoute(vector<int> families, vector<int> peer_ids,
                                   vector<int> route_ids, int ntargets) {
    BOOST_FOREACH(int family, families) {
        BOOST_FOREACH(int peer_id, peer_ids) {
            BOOST_FOREACH(int route_id, route_ids) {
                DeleteBgpRoute(family, peer_id, route_id, ntargets);
            }
        }
    }
}

void BgpStressTest::DeleteBgpRouteInternal(int family, int peer_id,
                                           int route_id, string start_prefix,
                                           int ntargets) {
    DBRequest req;
    boost::scoped_ptr<ExtCommunitySpec> commspec;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;

    if (peer_id >= (int) peers_.size() || !peers_[peer_id]) return;
    BgpTable *table = rtinstance_->GetTable(families_[family]);
    if (!table) return;
    IPeer *peer = peers_[peer_id]->peer();

    req.oper = DBRequest::DB_ENTRY_DELETE;

    Ip4Prefix prefix(Ip4Prefix::FromString("192.168.255.0/24"));

    // Use routes with same RD as well as with different RD to cover more
    // scenarios.
    ostringstream os;

    os << "123:";
    if (peer_id & 1) {
        os << "999";
    } else {
        os << peer_id;
    }
    os << ":" << start_prefix;

    InetVpnPrefix vpn_prefix(InetVpnPrefix::FromString(os.str()));
    prefix = task_util::Ip4PrefixIncrement(prefix, route_id);
    vpn_prefix = task_util::InetVpnPrefixIncrement(vpn_prefix, route_id);

    switch (table->family()) {
        case Address::INET:
            req.key.reset(new InetTable::RequestKey(prefix, peer));
            break;
        case Address::INETVPN:
            req.key.reset(new InetVpnTable::RequestKey(vpn_prefix, peer));
            break;
        default:
            assert(0);
            break;
    }

    table->Enqueue(&req);
}

void BgpStressTest::DeleteBgpRoutes(int family, int peer_id, int nroutes,
                                    int ntargets) {
    DBRequest req;
    boost::scoped_ptr<ExtCommunitySpec> commspec;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;

    for (int rt = 0; rt < nroutes; rt++) {
        DeleteBgpRoute(family, peer_id, rt, ntargets);
    }
}

void BgpStressTest::DeleteAllBgpRoutes(int nroutes, int ntargets, int npeers,
                                    int nagents) {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (!npeer) continue;
        for (int family = 0; family < n_families_; family++) {
            DeleteBgpRoutes(family, npeer->peer_id(), nroutes, ntargets);
        }
    }
}

bool BgpStressTest::IsAgentEstablished(test::NetworkAgentMock *agent) {
    return agent && agent->IsEstablished();
}

void BgpStressTest::SubscribeConfiguration(int agent_id, bool verify) {
    if (!d_vms_count_) return;

    if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
        return;
    // if (!IsAgentEstablished(xmpp_agents_[agent_id])) return;
    string agent_name = GetAgentConfigName(agent_id);
    if (xmpp_agents_[agent_id]->vrouter_mgr_->HasSubscribed(agent_name)) return;

    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0,
        xmpp_agents_[agent_id]->vrouter_mgr_->Count(agent_name));

    xmpp_agents_[agent_id]->vrouter_mgr_->Subscribe(agent_name, 0, false);

    for (int i = 0; i < d_vms_count_; i++) {
        string vm_uuid = GetAgentVmConfigName(agent_id, i);
        TASK_UTIL_EXPECT_EQ(0, xmpp_agents_[agent_id]->vm_mgr_->Count(vm_uuid));
        xmpp_agents_[agent_id]->vm_mgr_->Subscribe(vm_uuid, 0, false);
    }

    int pending = d_vms_count_;
    if (verify) VerifyConfiguration(agent_id, pending);
}

void BgpStressTest::VerifyConfiguration(int agent_id, int &pending) {
   if (!d_vms_count_ || d_no_agent_messages_processing_) {
       return;
   }

    for (int i = 0; i < d_vms_count_; i++) {
        string vm_uuid = GetAgentVmConfigName(agent_id, i);
        TASK_UTIL_EXPECT_EQ_MSG(1,
            xmpp_agents_[agent_id]->vm_mgr_->Count(vm_uuid),
            "Pending VMs: " << pending);
        --pending;
    }

    string agent_name = GetAgentConfigName(agent_id);
    TASK_UTIL_EXPECT_EQ(1,
        xmpp_agents_[agent_id]->vrouter_mgr_->Count(agent_name));
}

void BgpStressTest::SubscribeConfiguration(vector<int> agent_ids, bool verify) {
    BOOST_FOREACH(int agent_id, agent_ids) {
        SubscribeConfiguration(agent_id, false);
    }

    if (verify) {
        int pending = agent_ids.size() * d_vms_count_;
        BOOST_FOREACH(int agent_id, agent_ids) {
            VerifyConfiguration(agent_id, pending);
        }
    }
}

void BgpStressTest::UnsubscribeConfiguration(int agent_id, bool verify) {
    if (!d_vms_count_) return;

    if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
        return;
    // if (!IsAgentEstablished(xmpp_agents_[agent_id])) return;

    string agent_name = GetAgentConfigName(agent_id);
    if (!xmpp_agents_[agent_id]->vrouter_mgr_->HasSubscribed(agent_name))
        return;

    TASK_UTIL_EXPECT_NE(0,
        xmpp_agents_[agent_id]->vrouter_mgr_->Count(agent_name));

    // xmpp_agents_[agent_id]->vrouter_mgr_->Unsubscribe(agent_name);
    xmpp_agents_[agent_id]->vrouter_mgr_->Clear();

    for (int i = 0; i < d_vms_count_; i++) {
        string vm_uuid = GetAgentVmConfigName(agent_id, i);
        TASK_UTIL_EXPECT_NE(0, xmpp_agents_[agent_id]->vm_mgr_->Count(vm_uuid));
        xmpp_agents_[agent_id]->vm_mgr_->Unsubscribe(vm_uuid, 0, false);
    }

    int pending = 1;
    if (verify) VerifyNoConfiguration(agent_id, pending);
}

void BgpStressTest::VerifyNoConfiguration(int agent_id, int &pending) {

    for (int i = 0; i < d_vms_count_; i++) {
        string vm_uuid = GetAgentVmConfigName(agent_id, i);
        TASK_UTIL_EXPECT_EQ_MSG(0,
            xmpp_agents_[agent_id]->vm_mgr_->Count(vm_uuid),
            "Pending VM Configs to receive: " << pending);
        --pending;
    }

    string agent_name = GetAgentConfigName(agent_id);
    TASK_UTIL_EXPECT_EQ(0,
        xmpp_agents_[agent_id]->vrouter_mgr_->Count(agent_name));

}

void BgpStressTest::UnsubscribeConfiguration(vector<int> agent_ids,
                                             bool verify) {
    BOOST_FOREACH(int agent_id, agent_ids) {
        UnsubscribeConfiguration(agent_id, false);
    }

    if (verify) {
        int pending = agent_ids.size() * d_vms_count_;
        BOOST_FOREACH(int agent_id, agent_ids) {
            VerifyNoConfiguration(agent_id, pending);
        }
    }
}

void BgpStressTest::SubscribeAgentsConfiguration(int nagents, bool verify) {
    vector<int> agents;

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        agents.push_back(agent_id);
    }
    SubscribeConfiguration(agents, verify);
}

void BgpStressTest::UnsubscribeAgentsConfiguration(int nagents, bool verify) {
    vector<int> agents;

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        agents.push_back(agent_id);
    }
    UnsubscribeConfiguration(agents, verify);
}

void BgpStressTest::SubscribeRoutingInstance(vector<int> agent_ids,
                                             vector<int> instance_ids,
                                             bool check_agent_state) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        BOOST_FOREACH(int agent_id, agent_ids) {
            if (agent_id >= (int) xmpp_agents_.size() ||
                    !xmpp_agents_[agent_id])
                continue;

            if (check_agent_state) {
                if (!IsAgentEstablished(xmpp_agents_[agent_id])) continue;
            }
            if (xmpp_agents_[agent_id]->route_mgr_->HasSubscribed(
                        GetInstanceName(instance_id)))
                continue;

            BGP_STRESS_TEST_LOG("Subscribing agent "
                    << xmpp_agents_[agent_id]->ToString()
                    << " to instance " << GetInstanceName(instance_id));
            xmpp_agents_[agent_id]->route_mgr_->Subscribe(
                    GetInstanceName(instance_id), instance_id,
                                    check_agent_state);
        }
    }
}

void BgpStressTest::SubscribeRoutingInstance(int agent_id, int instance_id,
                                             bool check_agent_state) {
    vector<int> agent_ids;
    vector<int> instance_ids;

    agent_ids.push_back(agent_id);
    instance_ids.push_back(instance_id);

    SubscribeRoutingInstance(agent_ids, instance_ids, check_agent_state);
}

void BgpStressTest::SubscribeAgents(int ninstances, int nagents) {

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        for (int instance_id = 0; instance_id <= ninstances; instance_id++) {
            SubscribeRoutingInstance(agent_id, instance_id, false);
        }
    }
}

void BgpStressTest::UnsubscribeRoutingInstance(vector<int> agent_ids,
                                               vector<int> instance_ids) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        BOOST_FOREACH(int agent_id, agent_ids) {
            if (agent_id >= (int) xmpp_agents_.size() ||
                    !xmpp_agents_[agent_id])
                continue;
            if (!IsAgentEstablished(xmpp_agents_[agent_id])) continue;
            if (!xmpp_agents_[agent_id]->route_mgr_->HasSubscribed(
                        GetInstanceName(instance_id)))
                continue;

            BGP_STRESS_TEST_LOG("Unsubscribing agent "
                    << xmpp_agents_[agent_id]->ToString()
                    << " to instance " << GetInstanceName(instance_id));
            xmpp_agents_[agent_id]->route_mgr_->Unsubscribe(
                    GetInstanceName(instance_id));
        }
    }
}

void BgpStressTest::UnsubscribeRoutingInstance(int agent_id, int instance_id) {
    vector<int> agent_ids;
    vector<int> instance_ids;

    agent_ids.push_back(agent_id);
    instance_ids.push_back(instance_id);

    UnsubscribeRoutingInstance(agent_ids, instance_ids);
}

void BgpStressTest::UnsubscribeAgents(int nagents, int ninstances) {

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        for (int instance_id = 0; instance_id <= ninstances; instance_id++) {
            UnsubscribeRoutingInstance(agent_id, instance_id);
        }
    }
    VerifyAgentRoutes(nagents, ninstances, 0);
}

string BgpStressTest::GetAgentNexthop(int agent_id, int route_id) {
    string nexthop_str;

    // Use a specific next-hop if provided.
    if (!d_xmpp_rt_nexthop_.empty()) {
        nexthop_str = d_xmpp_rt_nexthop_;
    } else {
        nexthop_str = xmpp_agents_[agent_id]->local_address();
    }

    // Compute the nextop. Vary the next-hop as desired.
    Ip4Prefix nexthop = Ip4Prefix::FromString(nexthop_str + "/32");
    return task_util::Ip4PrefixIncrement(nexthop,
                d_xmpp_rt_nexthop_vary_ ? route_id : 0).ip4_addr().to_string();
}

void BgpStressTest::AddXmppRoute(int instance_id, int agent_id, int route_id) {
    if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
        return;

    // if (!IsAgentEstablished(xmpp_agents_[agent_id])) return;
    if (!xmpp_agents_[agent_id]->route_mgr_->HasSubscribed(
                GetInstanceName(instance_id)))
        return;

    string mcast_addr =
        "225." + boost::lexical_cast<string>(instance_id) + ".1.1";

    Ip4Prefix mcast_prefix(Ip4Prefix::FromString(mcast_addr + "/32"));
    Ip4Prefix prefix = GetAgentRoute(agent_id + 1, instance_id, route_id);
    mcast_prefix = task_util::Ip4PrefixIncrement(mcast_prefix, route_id);

    xmpp_agents_[agent_id]->AddRoute(GetInstanceName(instance_id),
                                     prefix.ToString(),
                                     GetAgentNexthop(agent_id, route_id));

    if (!d_no_mcast_routes_) {
        xmpp_agents_[agent_id]->AddMcastRoute(GetInstanceName(instance_id),
                            "1/8/" + mcast_prefix.ip4_addr().to_string() +
                            "," + prefix.ip4_addr().to_string(),
                            prefix.ip4_addr().to_string(),
                            "10000-20000");
    }
}

void BgpStressTest::AddXmppRoute(vector<int> instance_ids,
                                 vector<int> agent_ids,
                                 vector<int> route_ids) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        BOOST_FOREACH(int agent_id, agent_ids) {
            BOOST_FOREACH(int route_id, route_ids) {
                AddXmppRoute(instance_id, agent_id, route_id);
            }
        }
    }
}

void BgpStressTest::AddXmppRoutes(int instance_id, int agent_id, int nroutes) {
    for (int rt = 0; rt < nroutes; rt++) {
        AddXmppRoute(instance_id, agent_id, rt);
    }
}

void BgpStressTest::AddAllXmppRoutes(int ninstances, int nagents, int nroutes) {

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        for (int instance_id = 1; instance_id <= ninstances; instance_id++) {
            AddXmppRoutes(instance_id, agent_id, nroutes);
        }
    }
}

void BgpStressTest::DeleteXmppRoute(int instance_id, int agent_id,
                                    int route_id) {
    if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
        return;

    // if (!IsAgentEstablished(xmpp_agents_[agent_id])) return;
    if (!xmpp_agents_[agent_id]->route_mgr_->HasSubscribed(
                GetInstanceName(instance_id)))
        return;

    Ip4Prefix prefix = GetAgentRoute(agent_id + 1, instance_id, route_id);
    string mcast_addr =
        "225." + boost::lexical_cast<string>(instance_id) + ".1.1";

    Ip4Prefix mcast_prefix(Ip4Prefix::FromString(mcast_addr + "/32"));
    mcast_prefix = task_util::Ip4PrefixIncrement(mcast_prefix, route_id);

    xmpp_agents_[agent_id]->DeleteRoute(GetInstanceName(instance_id),
                                        prefix.ToString(),
                                        GetAgentNexthop(agent_id, route_id));
    xmpp_agents_[agent_id]->DeleteMcastRoute(GetInstanceName(instance_id),
                        "1/8/" + mcast_prefix.ip4_addr().to_string() +
                        "," + prefix.ToString());
}

void BgpStressTest::DeleteXmppRoute(vector<int> instance_ids,
                                    vector<int> agent_ids,
                                    vector<int> route_ids) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        BOOST_FOREACH(int agent_id, agent_ids) {
            BOOST_FOREACH(int route_id, route_ids) {
                DeleteXmppRoute(instance_id, agent_id, route_id);
            }
        }
    }
}

void BgpStressTest::DeleteXmppRoutes(int ninstances, int agent_id,
                                     int nroutes) {
    for (int instance_id = 1; instance_id <= ninstances; instance_id++) {
        for (int rt = 0; rt < nroutes; rt++) {
            DeleteXmppRoute(instance_id, agent_id, rt);
        }
    }
    // VerifyAgentRoutes(ninstances, 0);
}

void BgpStressTest::DeleteAllXmppRoutes(int ninstances, int nagents,
                                        int nroutes) {

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        DeleteXmppRoutes(ninstances, agent_id, nroutes);
    }
    // VerifyAgentRoutes(ninstances, 0);
}

size_t BgpStressTest::GetAllAgentRouteCount(int nagents, int ninstances) {
    size_t count = 0;
    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        for (int instance_id = 1; instance_id <= ninstances; instance_id++) {
            if (!xmpp_agents_[agent_id]) continue;
            count += xmpp_agents_[agent_id]->route_mgr_->Count(
                         GetInstanceName(instance_id));
        }
    }

    return count;
}

void BgpStressTest::VerifyAgentRoutes(int nagents, int ninstances, int routes) {
   if (d_no_agent_messages_processing_) return;

    size_t expected = 0;
    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        for (int instance_id = 1; instance_id <= ninstances; instance_id++) {
            if (!xmpp_agents_[agent_id]) continue;
            expected += routes;
        }
    }

    TASK_UTIL_EXPECT_EQ_MSG(expected,
         GetAllAgentRouteCount(nagents, ninstances),
         "Wait until all routes are received at the agents");
}

void BgpStressTest::VerifyXmppRouteNextHops() {
    if (d_no_agent_messages_processing_) return;

    // if (!n_agents_ || n_agents_ != n_peers_) return;

    int agent_id = 0;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent_id++;
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = GetInstanceName(i);
            TASK_UTIL_EXPECT_EQ_MSG(n_instances_ * n_agents_ * n_routes_ +
                                    n_peers_ * n_routes_,
                                    agent->route_mgr_->Count(instance_name),
                                    "Wait for routes in " + instance_name);
            if (d_no_agent_updates_processing_) {
                continue;
            }
            for (int rt = 0; rt < n_routes_; rt++) {
                Ip4Prefix prefix = GetAgentRoute(agent_id, i, rt);
                TASK_UTIL_EXPECT_NE(
                    static_cast<test::NetworkAgentMock::RouteEntry *>(NULL),
                    agent->route_mgr_->Lookup(instance_name,
                                              prefix.ToString()));
                const test::NetworkAgentMock::RouteEntry *entry;
                entry = agent->route_mgr_->Lookup(instance_name,
                                                  prefix.ToString());
                if (!entry) continue;

                // We expect more next-hops, one from the agent and the rest
                // the bgp peers.
                TASK_UTIL_EXPECT_EQ((size_t) (1 + n_peers_),
                  agent->route_mgr_->Lookup(instance_name,
                    prefix.ToString())->entry.next_hops.next_hop.size());
            }
        }
    }
}

string BgpStressTest::GetAgentConfigName(int agent_id) {
    ostringstream config;

    config << "virtual-router:" << GetAgentName(agent_id);

    return config.str();
}

string BgpStressTest::GetAgentVmConfigName(int agent_id, int vm_id) {
    string vm_uuid =  BgpConfigParser::session_uuid(
            "A" + boost::lexical_cast<string>(agent_id + 1),
            "B" + boost::lexical_cast<string>(vm_id + 1), vm_id + 1);
    ostringstream config;

    config << "virtual-machine:" << vm_uuid;

    return config.str();
}

string BgpStressTest::GetAgentName(int agent_id) {
    return "agent" + boost::lexical_cast<string>(d_test_id_) + "." +
        boost::lexical_cast<string>(agent_id + 1) + "@vnsw.contrailsystems.com";
}

bool BgpStressTest::XmppClientIsEstablished(const string &client_name) {
    XmppConnection *connection = xmpp_server_test_->FindConnection(client_name);
    if (connection == NULL) {
        return false;
    }
    return (connection->GetStateMcState() == xmsm::ESTABLISHED); 
}

void BgpStressTest::BringUpXmppAgent(vector<int> agent_ids, bool verify_state) {
    BOOST_FOREACH(int agent_id, agent_ids) {

        if (agent_id < (int) xmpp_agents_.size() && xmpp_agents_[agent_id]) {
            xmpp_agents_[agent_id]->SessionUp();
            continue;
        }

        if (agent_id >= (int) xmpp_agents_.size()) {
            xmpp_agents_.resize(agent_id + 1, NULL);
        }

        string agent_name = GetAgentName(agent_id);
        string source_addr(d_xmpp_source_);
        Ip4Prefix prefix(Ip4Prefix::FromString(source_addr + "/32"));
        prefix = task_util::Ip4PrefixIncrement(prefix, agent_id);

        // if (d_vms_count_) FeedIfmapConfig(agent_id, d_vms_count_, true);

        // create an XMPP client in server A
        xmpp_agents_[agent_id] = new test::NetworkAgentMock(&evm_,
                agent_name, d_xmpp_port_ ?: xmpp_server_test_->GetPort(),
                prefix.ip4_addr().to_string(), d_xmpp_server_);

        if (d_no_agent_updates_processing_) {
            xmpp_agents_[agent_id]->set_skip_updates_processing(true);
        }

        if (xmpp_close_from_control_node_) {
            TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpXmppChannel *>(NULL),
                channel_manager_->channel(),
                "Waiting for channel_manager_->channel() to be set");
            if (agent_id >= (int) xmpp_peers_.size()) {
                xmpp_peers_.resize(agent_id + 1, NULL);
            }
            xmpp_peers_[agent_id] = channel_manager_->channel();
            channel_manager_->set_channel(NULL);
        }
    }

    if (!verify_state) return;

    BOOST_FOREACH(int agent_id, agent_ids) {
        string agent_name = GetAgentName(agent_id);

        //
        // server connection
        //
        if (!d_external_mode_) {
            TASK_UTIL_EXPECT_TRUE(XmppClientIsEstablished(agent_name));
        }

        //
        // Client side
        //
        TASK_UTIL_EXPECT_EQ(true, IsAgentEstablished(xmpp_agents_[agent_id]));

        // verify ifmap_server client is not created until config subscribe
        // TASK_UTIL_EXPECT_TRUE(ifmap_server_->FindClient(agent_name) == NULL);
        BGP_STRESS_TEST_LOG("Agent " << agent_name << " : "
            << xmpp_agents_[agent_id]->ToString() << " up");
        if (d_no_agent_messages_processing_) {
            xmpp_agents_[agent_id]->DisableRead(true);
        }
    }
}

void BgpStressTest::BringUpXmppAgents(int nagents) {
    BGP_STRESS_TEST_LOG("Starting bringing up " << nagents << " agents");
    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        vector<int> agent_ids;

        agent_ids.push_back(agent_id);
        BringUpXmppAgent(agent_ids, false);
    }

    int count = 0;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        count += 1;
        TASK_UTIL_EXPECT_EQ_MSG(true, IsAgentEstablished(agent),
            "Waiting for " << count << ": agent " << agent->ToString()
            << " to come up");
        BGP_STRESS_TEST_LOG("Agent " << agent->ToString() << " up");
        if (d_no_agent_messages_processing_) {
            agent->DisableRead(true);
        }
    }
    BGP_STRESS_TEST_LOG("End bringing up " << nagents << " agents");
}

void BgpStressTest::BringDownXmppAgent(vector<int> agent_ids,
                                       bool verify_state) {
    BOOST_FOREACH(int agent_id, agent_ids) {
        if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
            continue;

        // if (d_vms_count_) FeedIfmapConfig(agent_id, d_vms_count_, false);

        // agent->client()->Shutdown();
        if (d_no_agent_messages_processing_) {
            xmpp_agents_[agent_id]->DisableRead(false);
        }
        xmpp_agents_[agent_id]->SessionDown();
    }

    if (!verify_state) return;

    int count = 0;
    BOOST_FOREACH(int agent_id, agent_ids) {
        if (agent_id >= (int) xmpp_agents_.size() || !xmpp_agents_[agent_id])
            continue;
        count += 1;
        TASK_UTIL_EXPECT_FALSE_MSG(IsAgentEstablished(xmpp_agents_[agent_id]),
            "Waiting for " << count << ": agent " <<
            xmpp_agents_[agent_id]->ToString() << " to go down");
        BGP_STRESS_TEST_LOG("Agent " << xmpp_agents_[agent_id]->ToString()
                            << " down");
    }
}

void BgpStressTest::BringDownXmppAgents(int nagents) {
    if (xmpp_close_from_control_node_) {
        BOOST_FOREACH(BgpXmppChannel *peer, xmpp_peers_) {
            peer->Peer()->Close();
        }
    } else {
        vector<int> agent_ids;
        for (int agent_id = 0; agent_id < nagents; agent_id++) {
            agent_ids.push_back(agent_id);
        }
        BringDownXmppAgent(agent_ids, false);
    }

    for (int agent_id = 0; agent_id < nagents; agent_id++) {
        TASK_UTIL_EXPECT_TRUE(!IsAgentEstablished(xmpp_agents_[agent_id]));
        if (!d_external_mode_) {
            TASK_UTIL_EXPECT_FALSE(XmppClientIsEstablished(
                                       GetAgentName(agent_id)));
        }
        BGP_STRESS_TEST_LOG("Agent " << xmpp_agents_[agent_id]->ToString()
                            << " down");
    }
}

void BgpStressTest::AddBgpPeer(int peer_id, bool verify_state) {
    assert(peer_id);

    if (peer_id < (int) peers_.size() && peers_[peer_id]) return;

    if (peer_id >= (int) peers_.size()) {
        peers_.resize(peer_id + 1, NULL);
        peer_servers_.resize(peer_id + 1, NULL);
    }

    if (!peer_servers_[peer_id]) {
        peer_servers_[peer_id] = new BgpServerTest(&evm_,
                                                   GetRouterName(peer_id));
        peer_servers_[peer_id]->session_manager()->Initialize(0);
    }

    ostringstream out;
    out << "<config>";

    out << GetRouterConfig(0, peer_id, false);
    out << GetRouterConfig(peer_id, 0, false);
    out << "</config>";

    BGP_DEBUG_UT("Applying config" << out.str());
    BGP_DEBUG_UT("uuid: " << BgpConfigParser::session_uuid(
                               GetRouterName(0), GetRouterName(peer_id), 1));
    server_->Configure(out.str());
    peer_servers_[peer_id]->Configure(out.str());

    BgpNullPeer *npeer = new BgpNullPeer(server_.get(), peer_id);
    peers_[peer_id] = npeer;

    if (verify_state) {
        VerifyPeer(server_.get(), npeer);
    }
}

void BgpStressTest::AddBgpPeer(vector<int> peer_ids, bool verify_state) {
    BOOST_FOREACH(int peer_id, peer_ids) {
        AddBgpPeer(peer_id, false);
    }

    if (verify_state) {
        BOOST_FOREACH(int peer_id, peer_ids) {
            VerifyPeer(server_.get(), peers_[peer_id]);
        }
    }
}

void BgpStressTest::AddBgpPeers(int npeers) {
    int p;

    if (npeers) {
        BGP_STRESS_TEST_LOG("Starting bringing up " << npeers << " bgp peers");
    }

    for (p = 1; p <= npeers; p++) {
        AddBgpPeer(p, false);
    }

    for (p = 1; p <= npeers; p++) {
        VerifyPeer(server_.get(), peers_[p]);
    }

    if (npeers) {
        BGP_STRESS_TEST_LOG("End bringing up " << npeers << " bgp peers");
    }
}

void BgpStressTest::DeleteBgpPeer(int peer_id, bool verify_state) {
    assert(peer_id);

    if (peer_id >= (int) peers_.size() || !peers_[peer_id]) return;

    ostringstream out;
    out << "<delete>";
    string peer_name = peers_[peer_id]->peer()->ToString();

    out << GetRouterConfig(0, peer_id, true);
    out << GetRouterConfig(peer_id, 0, true);
    out << "</delete>";

    as_t as = server_->autonomous_system();
    uint32_t bgp_identifier = server_->bgp_identifier();

    BGP_DEBUG_UT("Applying config" << out.str());
    server_->Configure(out.str());
    peer_servers_[peer_id]->Configure(out.str());
    usleep(10000);

    //
    // Restore server's AS and bgp identifier
    //
    server_->set_autonomous_system(as);
    server_->set_bgp_identifier(bgp_identifier);
    WaitForIdle();
    peer_servers_[peer_id]->Shutdown();

    if (verify_state) {
        VerifyNoPeer(peer_id, peer_name);
    }
}

void BgpStressTest::DeleteBgpPeer(vector<int> peer_ids, bool verify_state) {
    map<int, string> peer_names;

    BOOST_FOREACH(int peer_id, peer_ids) {
        if (peer_id >= (int) peers_.size() || !peers_[peer_id]) return;
        peer_names.insert(make_pair(peer_id,
                                    peers_[peer_id]->peer()->ToString()));
        DeleteBgpPeer(peer_id, false);
    }

    if (verify_state) {
        BOOST_FOREACH(int peer_id, peer_ids) {
            VerifyNoPeer(peer_id, peer_names[peer_id]);
        }
    }
}

void BgpStressTest::DeleteBgpPeers(int npeers) {
    for (int p = 1; p <= npeers; p++) {
        DeleteBgpPeer(p, true);
    }
}

void BgpStressTest::ClearBgpPeer(vector<int> peer_ids) {
    map<int, bool> established;
    map<int, uint32_t> flap_count;

    BOOST_FOREACH(int peer_id, peer_ids) {
        if (peer_id >= (int) peers_.size() || !peers_[peer_id]) continue;

        flap_count.insert(make_pair(peer_id,
                                    peers_[peer_id]->peer()->flap_count()));
        established.insert(make_pair(peer_id,
            peers_[peer_id]->peer()->GetState() == StateMachine::ESTABLISHED));
        peers_[peer_id]->peer()->Clear(BgpProto::Notification::AdminReset);
    }

    BOOST_FOREACH(int peer_id, peer_ids) {
        if (peer_id >= (int) peers_.size() || !peers_[peer_id]) continue;

        //
        // If the peer was established, first make sure that it did flap
        //
        if (established[peer_id]) {
            TASK_UTIL_EXPECT_TRUE(peers_[peer_id]->peer()->flap_count() >
                                flap_count[peer_id]);
        }

        //
        // Wait for the peer to come back up
        //
        BGP_WAIT_FOR_PEER_STATE(peers_[peer_id]->peer(),
                                StateMachine::ESTABLISHED);
    }
}

void BgpStressTest::ClearBgpPeers(int npeers) {
    vector<int> peers;

    for (int p = 1; p <= npeers; p++) {
        peers.push_back(p);
    }
    ClearBgpPeer(peers);
}

void BgpStressTest::AddRouteTarget(int instance_id, int target) {
    ifmap_test_util::IFMapMsgLink(server_->config_db(), "routing-instance",
                         GetInstanceName(instance_id),
                         "route-target",
                         "target:1:" + boost::lexical_cast<string>(target),
                         "instance-target");

    //
    // Adding route-target above automatically creates the instances as well
    //
    instances_[instance_id] = true;

    //
    // Update vpn routes
    //
    for (int i = 1; i <= n_peers_; i++) {
        for (int j = 0; j < n_routes_; j++) {
            AddBgpRoute(1, i, j, n_targets_); // Address::INETVPN
        }
    }
}

void BgpStressTest::RemoveRouteTarget(int instance_id, int target) {
    ifmap_test_util::IFMapMsgUnlink(server_->config_db(), "routing-instance",
                         GetInstanceName(instance_id),
                         "route-target",
                         "target:1:" + boost::lexical_cast<string>(target),
                         "instance-target");

    //
    // Update vpn routes
    //
    for (int i = 1; i <= n_peers_; i++) {
        for (int j = 0; j < n_routes_; j++) {
            AddBgpRoute(1, i, j, n_targets_); // Address::INETVPN
        }
    }
}

void BgpStressTest::AddRoutingInstance(int instance_id, int ntargets) {
    if (instance_id >= (int) instances_.size()) {
        instances_.resize(instance_id + 1, false);
    }

    if (instances_[instance_id]) return;

    instances_[instance_id] = true;

    if (instance_id) {
        ostringstream out;
        out << "<config>";
        out << GetInstanceConfig(instance_id, ntargets);
        out << "</config>";
        Configure(out.str());
    }
    TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                        server_->routing_instance_mgr()->GetRoutingInstance(
                            GetInstanceName(instance_id)));
}

void BgpStressTest::AddRoutingInstance(vector<int> instance_ids, int ntargets) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        AddRoutingInstance(instance_id, ntargets);
    }
}

void BgpStressTest::AddRoutingInstances(int ninstances, int ntargets) {
    if (d_external_mode_) return;
    for (int instance_id = 0; instance_id <= ninstances; instance_id++) {
        if (instance_id < (int) instances_.size() && instances_[instance_id])
            continue;

        AddRoutingInstance(instance_id, ntargets);
    }

    BGP_DEBUG_UT("All intances' configuration fed");
}

void BgpStressTest::DeleteRoutingInstance(int instance_id, int ntargets) {
    if (instance_id >= (int) instances_.size() || !instances_[instance_id])
        return;

    for (int agent_id = 0; agent_id < n_agents_; agent_id++) {
        UnsubscribeRoutingInstance(agent_id, instance_id);
    }

    instances_[instance_id] = false;
    if (!instance_id) {
        RoutingInstance *rtinstance = server_->routing_instance_mgr()->
                                         GetRoutingInstance(GetInstanceName(0));
        rtinstance->deleter()->Delete();
    } else {
        ostringstream out;
        out << "<delete>";
        out << GetInstanceConfig(instance_id, ntargets);
        out << "</delete>";
        Configure(out.str());
    }

    // Need not wait for the instance to completely get destroyed.
#if 0
    TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                        server_->routing_instance_mgr()->GetRoutingInstance(
                            GetInstanceName(instance_id)));
#endif
}

void BgpStressTest::DeleteRoutingInstance(vector<int> instance_ids,
                                          int ntargets) {
    BOOST_FOREACH(int instance_id, instance_ids) {
        DeleteRoutingInstance(instance_id, ntargets);
    }
}

void BgpStressTest::DeleteRoutingInstances() {
    for (int instance_id = 0; instance_id <= n_instances_; instance_id++) {
        DeleteRoutingInstance(instance_id, n_targets_);
    }

    TASK_UTIL_EXPECT_EQ_MSG(0, server_->routing_instance_mgr()->count(),
        "Waiting for the completion of routing-instances' deletion");
}

void BgpStressTest::AddAllRoutes(int ninstances, int npeers, int nagents,
                                 int nroutes, int ntargets) {
    BGP_STRESS_TEST_LOG("Start injecting BGP and/or XMPP routes");
    AddAllBgpRoutes(nroutes, ntargets);
    BGP_STRESS_TEST_LOG("End injecting BGP and/or XMPP routes");

    //
    // Add XmppPeers with routes as well
    //
    BGP_STRESS_TEST_LOG("Start subscribing all XMPP Agents");
    SubscribeAgents(ninstances, nagents);
    BGP_STRESS_TEST_LOG("End subscribing all XMPP Agents");

    if (d_vms_count_) {
        BGP_STRESS_TEST_LOG("Start subscribing all agents' "
                            "IFMAP configuration");
        SubscribeAgentsConfiguration(nagents, true);
        BGP_STRESS_TEST_LOG("End subscribing all agents' "
                            "IFMAP configuration");
    }

    if (!d_routes_send_trigger_.empty()) {

        // Wait for external trigger, before sending routes.
        while(access(d_routes_send_trigger_.c_str(), F_OK)) {
            BGP_STRESS_TEST_LOG("Trigger route sends by doing "
                                "('touch " << d_routes_send_trigger_ << "')");
            sleep(3);
        }

        // Remove the trigger file.
        remove(d_routes_send_trigger_.c_str());
    }

    BGP_STRESS_TEST_LOG("Start feeding all routes from all XMPP agents");
    usleep(10000);
    AddAllXmppRoutes(ninstances, nagents, nroutes);
    BGP_STRESS_TEST_LOG("End feeding all routes from all XMPP agents");

    if (d_no_verify_routes_) return;

    VerifyPeers();
    BGP_STRESS_TEST_LOG("Start verifying XMPP Routes at the controller");
    VerifyControllerRoutes(ninstances, nagents, nroutes);
    BGP_STRESS_TEST_LOG("End verifying XMPP Routes at the controller");

    //
    // We get routes added by agents as well as those from bgp peers
    //
    BGP_STRESS_TEST_LOG("Start verifying XMPP routes at the agents");
    VerifyAgentRoutes(nagents, ninstances, ninstances * nagents * nroutes +
                                           npeers * nroutes);
    BGP_STRESS_TEST_LOG("End verifying XMPP routes at the agents");

    BGP_STRESS_TEST_LOG("Start verifying XMPP routes nexthops at the agents");
    VerifyXmppRouteNextHops();
    BGP_STRESS_TEST_LOG("End verifying XMPP routes nexthops at the agents");

    VerifyRibOutCreationCompletion();
}

void BgpStressTest::DeleteAllRoutes(int ninstances, int npeers, int nagents,
                                    int nroutes, int ntargets) {
    DeleteAllBgpRoutes(nroutes, ntargets, npeers, nagents);
    DeleteAllXmppRoutes(ninstances, nagents, nroutes);
    VerifyAgentRoutes(nagents, ninstances, 0);
    VerifyControllerRoutes(ninstances, nagents, 0);

    UnsubscribeAgents(nagents, ninstances);
    UnsubscribeAgentsConfiguration(nagents, true);
    VerifyPeers();
    VerifyRibOutCreationCompletion();
}

void BgpStressTest::ValidateShowNeighborStatisticsResponse(
        size_t expected_count, Sandesh *sandesh) {
    ShowNeighborStatisticsResp *resp;
    resp = dynamic_cast<ShowNeighborStatisticsResp *>(sandesh);

    EXPECT_NE(static_cast<ShowNeighborStatisticsResp *>(NULL), resp);
    EXPECT_EQ(expected_count, resp->get_count());

    sandesh_response_validation_complete_ = true;
}

// Verify show neighbors statistics command output
void BgpStressTest::ShowNeighborStatistics() {

    // Skip if connection is to an external xmpp server.
    if (d_xmpp_port_) return;

    ShowNeighborStatisticsReq *req;

    // Calculate total neighbors count
    size_t bgp_peers_count = 0;
    size_t xmpp_agents_count = 0;

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        if (agent && agent->IsEstablished()) {
            xmpp_agents_count++;
        }
    }

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        if (npeer && npeer->peer() && npeer->peer()->IsReady()) {
            bgp_peers_count++;
        }
    }

    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowNeighborStatisticsResponse,
                   this, bgp_peers_count + xmpp_agents_count, _1));
    sandesh_response_validation_complete_ = false;
    req = new ShowNeighborStatisticsReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);

    // Check with BGP peer type and instance/domain name filter
    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowNeighborStatisticsResponse,
                   this, bgp_peers_count, _1));
    sandesh_response_validation_complete_ = false;
    req = new ShowNeighborStatisticsReq;
    req->set_bgp_or_xmpp("bgp");
    req->set_domain(GetInstanceName(0));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);

    // Repeat above, for only down peers
    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowNeighborStatisticsResponse,
                   this, 0, _1));
    sandesh_response_validation_complete_ = false;
    req = new ShowNeighborStatisticsReq;
    req->set_bgp_or_xmpp("bgp");
    req->set_up_or_down("down");
    req->set_domain(GetInstanceName(0));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);

    // Repeat above but with incorrect instance name
    // Check with BGP peer type and instance/domain name filter
    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowNeighborStatisticsResponse,
                   this, 0, _1));
    sandesh_response_validation_complete_ = false;
    req = new ShowNeighborStatisticsReq;
    req->set_bgp_or_xmpp("bgp");
    req->set_domain("instance1");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);

    // Check with XMPP peer type filter
    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowNeighborStatisticsResponse,
                   this, xmpp_agents_count, _1));
    sandesh_response_validation_complete_ = false;
    req = new ShowNeighborStatisticsReq;
    req->set_bgp_or_xmpp("xmpp");
    if (n_instances_) {
        req->set_domain(GetInstanceName(1));
    }
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);
}

void BgpStressTest::ValidateShowRouteSandeshResponse(Sandesh *sandesh) {
    ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
    EXPECT_NE(static_cast<ShowRouteResp *>(NULL), resp);

    for (size_t i = 0; i < resp->get_tables().size(); i++) {
        BGP_DEBUG_UT("***********************************************" << endl);
        BGP_DEBUG_UT(resp->get_tables()[i].routing_instance << " "
             << resp->get_tables()[i].routing_table_name << endl);
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                BGP_DEBUG_UT(resp->get_tables()[i].routes[j].prefix << " "
                     << resp->get_tables()[i].routes[j].paths.size() << endl);
            }
    }
    sandesh_response_validation_complete_ = true;
}

void BgpStressTest::ShowAllRoutes() {
    Sandesh::set_response_callback(
       boost::bind(&BgpStressTest::ValidateShowRouteSandeshResponse, this, _1));
    ShowRouteReq *show_req = new ShowRouteReq;
    sandesh_response_validation_complete_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_TRUE(sandesh_response_validation_complete_);
}

void BgpStressTest::UpdateSocketBufferSize() {
    int new_size;

    if (socket_buffer_size_ == 1 << 10) {
        new_size = 1 << 0;
    } else {
        new_size = 1 << 10;
    }
    ostringstream out;
    out << new_size;
    if (!setenv("TCP_SESSION_SOCKET_BUFFER_SIZE", out.str().c_str(), 1)) {
        BGP_DEBUG_UT("Socket buffer size changed from " <<
                     socket_buffer_size_ << " to " << socket_buffer_size_);
        socket_buffer_size_ = new_size;
    }
}

TEST_P(BgpStressTest, RandomEvents) {
    vector<int> event_ids_list;
    int target;
    int i;

    SCOPED_TRACE(__FUNCTION__);
    InitParams();

    AddRoutingInstances(n_instances_, n_targets_);

    boost::posix_time::ptime time_start(
                                 boost::posix_time::second_clock::local_time());
    BringUpXmppAgents(n_agents_);
    AddBgpPeers(n_peers_);
    AddAllRoutes(n_instances_, n_peers_, n_agents_, n_routes_, n_targets_);
    boost::posix_time::ptime time_end(
                                 boost::posix_time::second_clock::local_time());
    BGP_STRESS_TEST_LOG("Time taken for initial setup: " <<
        boost::posix_time::time_duration(time_end - time_start));

    ShowAllRoutes();
    ShowNeighborStatistics();

    if (d_pause_after_initial_setup_) {
        BGP_DEBUG_UT("Test PAUSED. Exit (Ctrl-d) from python shell to resume");
        system("/usr/bin/python");
    }

    HEAP_PROFILER_DUMP("bgp_stress_test");

    for (int count = 1; !d_events_ || count <= d_events_; count++) {
        switch (BgpStressTestEvent::GetTestEvent(count)) {
            case BgpStressTestEvent::ADD_BGP_ROUTE:
                if (d_external_mode_) break;
                if (!n_peers_ || !n_families_ || !n_routes_) break;
                AddBgpRoute(BgpStressTestEvent::GetEventItems(n_families_),
                            BgpStressTestEvent::GetEventItems(n_peers_, 1),
                            BgpStressTestEvent::GetEventItems(n_routes_),
                            n_targets_);
                break;

            case BgpStressTestEvent::DELETE_BGP_ROUTE:
                if (d_external_mode_) break;
                if (!n_peers_ || !n_families_ || !n_routes_) break;
                DeleteBgpRoute(BgpStressTestEvent::GetEventItems(n_families_),
                               BgpStressTestEvent::GetEventItems(n_peers_, 1),
                               BgpStressTestEvent::GetEventItems(n_routes_),
                               n_targets_);
                break;

            case BgpStressTestEvent::ADD_XMPP_ROUTE:
                if (!n_instances_ || !n_agents_ || !n_routes_) break;
                AddXmppRoute(BgpStressTestEvent::GetEventItems(n_instances_),
                             BgpStressTestEvent::GetEventItems(n_agents_),
                             BgpStressTestEvent::GetEventItems(n_routes_));
                break;

            case BgpStressTestEvent::DELETE_XMPP_ROUTE:
                if (!n_instances_ || !n_agents_ || !n_routes_) break;
                DeleteXmppRoute(BgpStressTestEvent::GetEventItems(n_instances_),
                                BgpStressTestEvent::GetEventItems(n_agents_),
                                BgpStressTestEvent::GetEventItems(n_routes_));
                break;

            case BgpStressTestEvent::BRING_UP_XMPP_AGENT:
                if (!n_agents_) break;
                BringUpXmppAgent(BgpStressTestEvent::GetEventItems(n_agents_),
                                 true);
                break;

            case BgpStressTestEvent::BRING_DOWN_XMPP_AGENT:
                if (!n_agents_) break;
                BringDownXmppAgent(
                        BgpStressTestEvent::GetEventItems(n_agents_), true);
                break;

            case BgpStressTestEvent::CLEAR_XMPP_AGENT:
                if (!n_agents_) break;

                event_ids_list = BgpStressTestEvent::GetEventItems(n_agents_);
                BringDownXmppAgent(event_ids_list, true);
                ShowAllRoutes();
                BringUpXmppAgent(event_ids_list, true);
                break;

            case BgpStressTestEvent::SUBSCRIBE_ROUTING_INSTANCE:
                if (!n_agents_ || !n_instances_) break;
                SubscribeRoutingInstance(
                    BgpStressTestEvent::GetEventItems(n_agents_),
                    BgpStressTestEvent::GetEventItems(n_instances_));
                break;

            case BgpStressTestEvent::UNSUBSCRIBE_ROUTING_INSTANCE:
                if (!n_agents_ || !n_instances_) break;
                UnsubscribeRoutingInstance(
                    BgpStressTestEvent::GetEventItems(n_agents_),
                    BgpStressTestEvent::GetEventItems(n_instances_));
                break;

            case BgpStressTestEvent::SUBSCRIBE_CONFIGURATION:
                if (!n_agents_) break;
                SubscribeConfiguration(
                        BgpStressTestEvent::GetEventItems(n_agents_), true);
                break;

            case BgpStressTestEvent::UNSUBSCRIBE_CONFIGURATION:
                if (!n_agents_) break;
                UnsubscribeConfiguration(
                        BgpStressTestEvent::GetEventItems(n_agents_), true);
                break;

            case BgpStressTestEvent::ADD_BGP_PEER:
                if (d_external_mode_) break;
                if (!n_peers_) break;
                AddBgpPeer(BgpStressTestEvent::GetEventItems(n_peers_, 1),
                           true);
                break;

            case BgpStressTestEvent::DELETE_BGP_PEER:
                if (d_external_mode_) break;
                if (!n_peers_) break;
                DeleteBgpPeer(BgpStressTestEvent::GetEventItems(n_peers_, 1),
                              true);
                break;

            case BgpStressTestEvent::CLEAR_BGP_PEER:
                if (d_external_mode_) break;
                if (!n_peers_) break;
                ClearBgpPeer(BgpStressTestEvent::GetEventItems(n_peers_, 1));
                break;

            case BgpStressTestEvent::ADD_ROUTING_INSTANCE:
                if (d_external_mode_) break;
                if (!n_instances_) break;
                if (!n_targets_) {
                    target = ++n_targets_;
                    for (i = 1; i <= n_instances_; i++) {
                        AddRouteTarget(i, target);
                    }
                }

                AddRoutingInstance(
                        BgpStressTestEvent::GetEventItems(n_instances_, 1),
                        n_targets_);
                break;

            case BgpStressTestEvent::DELETE_ROUTING_INSTANCE:
                if (d_external_mode_) break;
                if (!n_instances_) break;
                DeleteRoutingInstance(
                    BgpStressTestEvent::GetEventItems(n_instances_, 1),
                    n_targets_);
                break;

            case BgpStressTestEvent::ADD_ROUTE_TARGET:
                if (d_external_mode_) break;
                target = ++n_targets_;
                for (i = 1; i <= n_instances_; i++) {
                    AddRouteTarget(i, target);
                }
                break;

            case BgpStressTestEvent::DELETE_ROUTE_TARGET:
                if (d_external_mode_) break;
                if (!n_targets_) break;
                target = n_targets_--;

                for (i = 1; i <= n_instances_; i++) {
                    if (!n_targets_) {
                        DeleteRoutingInstance(i, 1);
                    }
                    RemoveRouteTarget(i, target);
                }
                break;

            case BgpStressTestEvent::CHANGE_SOCKET_BUFFER_SIZE:
                UpdateSocketBufferSize();
                break;

            case BgpStressTestEvent::SHOW_ALL_ROUTES:
                if (d_external_mode_) break;
                ShowAllRoutes();
                break;

            case BgpStressTestEvent::PAUSE:
                BGP_DEBUG_UT("Test PAUSED. Exit (Ctrl-d) from python shell "
                             "to resume");
                system("/usr/bin/python");
                break;
        }
    }
}

static void process_command_line_args(int argc, const char **argv) {
    static bool cmd_line_processed;
    const string log_file = "<stdout>";
    const unsigned long log_file_size = 1*1024*1024*1024; // 1GB
    const unsigned int log_file_index = 10;
    bool log_file_uniquefy = false;

    if (cmd_line_processed) return;
    cmd_line_processed = true;

    bool cmd_line_arg_set = false;

    // Declare the supported options.
    options_description desc(

    "\nBgpStressTest\n"
    "===============\n"
    "Use this test to stress test control-node in unit-test environment by \n"
    "feeding various test events randomly.\n\n"
    "Many events are supported such as add bgp route, add xmpp route, \n"
    "bring up/down bgp/xmpp peer, etc.\n\n"
    "By default, test feeds events randomly. To reproduce crashes, \n"
    "events can be fed through a file (- for stdin). Simply grep \"Feed \" from"
    " \nthe test output, and feed those lines back to replay the events.\n\n"
    "Events can be retrieved from core-file generated from bgp_stress_test\n"
    "using this bash function.\n\n"
    "get_events () { # Input is the core file\n"
    "   gdb --batch --eval-command=\"print BgpStressTestEvent::d_events_played_list_\" build/debug/bgp/test/bgp_stress_test $1 | \\grep \\$1 | sed 's/\",\\?/\\n/g' | \\grep Feed\n"
    "}\n"
    "Those events can be played back as shown below.\n"
    "    get_events() <core-file> | build/debug/bgp/test/bgp_stress_test --feed-events -\n\n"
    "Scaling\n"
    "=======\n"
    "Tweak nagents, npeers, nroutes, ninstances, ntargets as desired\n\n"
    "Usage:"
    );
    desc.add_options()
        ("help", "produce help message")
        ("db-walker-wait-usecs", value<int>()->default_value(d_db_walker_wait_),
            "set usecs delay in walker cb")
        ("close-from-control-node", bool_switch(&d_close_from_control_node_),
             "Initiate xmpp session close from control-node")
        ("event-proportion",
         value<float>()->default_value(d_events_proportion_),
             "Proportion of objects for event feed, such as 0.10")
        ("feed-events", value<string>(),
             "Input file with a list of events to feed, Use - for stdin")
        ("http-port", value<int>(), "set http introspect server port number")
        ("instance-name", value<string>(), "set instance name string start")
        ("log-category", value<string>()->default_value(d_log_category_),
            "set log category")
        ("log-disable", bool_switch(&d_log_disable_),
             "Disable logging")
        ("log-file", value<string>()->default_value(log_file),
             "Filename for the logs to be written to")
        ("log-file-index",
             value<unsigned int>()->default_value(log_file_index),
             "Maximum log file roll over index")
        ("log-file-uniquefy", bool_switch(&log_file_uniquefy),
             "Use pid to make log-file name unique")
        ("log-file-size",
             value<unsigned long>()->default_value(log_file_size),
             "Maximum size of the log file")
        ("log-level", value<string>()->default_value(d_log_level_),
            "set log level ")
        ("log-local-disable", bool_switch(&d_log_local_disable_),
             "Disable local logging")
        ("nagents", value<int>()->default_value(d_agents_),
            "set number of xmpp agents")
        ("nevents", value<int>()->default_value(d_events_),
            "set number of random events to feed, 0 for infinity")
        ("ninstances", value<int>()->default_value(d_instances_),
            "set number of routing instances")
        ("npeers", value<int>()->default_value(d_peers_),
            "set number of bgp peers")
        ("nroutes", value<int>()->default_value(d_routes_),
            "set number of routes")
        ("ntargets", value<int>()->default_value(d_targets_),
            "set number of route targets")
        ("nvms", value<int>()->default_value(d_vms_count_),
            "set number of VMs (for configuration download)")
        ("no-multicast", bool_switch(&d_no_mcast_routes_),
             "Do not add multicast routes")
        ("no-verify-routes", bool_switch(&d_no_verify_routes_),
             "Do not verify routes")
        ("no-agents-updates-processing",
             bool_switch(&d_no_agent_updates_processing_),
             "Do not store updates received by the mock agents")
        ("no-agents-messages-processing",
             bool_switch(&d_no_agent_messages_processing_),
             "Do not process messages received by the mock agents")
        ("no-sandesh-server", bool_switch(&d_no_sandesh_server_),
             "Do not add multicast routes")
        ("pause", bool_switch(&d_pause_after_initial_setup_),
             "Pause after initial setup, before injecting events")
        ("profile-heap", bool_switch(&d_profile_heap_),
             "Profile heap memory")
        ("routes-send-trigger",
             value<string>()->default_value(d_routes_send_trigger_),
             "File whose presence triggers the start of routes sending process")

        ("wait-for-idle-time", value<int>()->default_value(d_wait_for_idle_),
             "WaitForIdle() wait time, 0 for no wait")
        ("weight-add-bgp-route",
             value<float>()->default_value(
                 d_events_weight_[BgpStressTestEvent::ADD_BGP_ROUTE-1]),
             "Set ADD_BGP_ROUTE event weight")
        ("weight-delete-bgp-route",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::DELETE_BGP_ROUTE-1]),
             "Set DELETE_BGP_ROUTE event weight")
        ("weight-add-xmpp-routE",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::ADD_XMPP_ROUTE-1]),
             "Set ADD_XMPP_ROUTE event weight")
        ("weight-delete-xmpp-route",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::DELETE_XMPP_ROUTE-1]),
             "Set DELETE_XMPP_ROUTE event weight")
        ("weight-bring-up-xmpp-agent",
           value<float>()->default_value(d_events_weight_[
               BgpStressTestEvent::BRING_UP_XMPP_AGENT-1]),
           "Set BRING_UP_XMPP_AGENT event weight")
        ("weight-bring-down-xmpp-agent",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::
                 BRING_DOWN_XMPP_AGENT-1]),
             "Set BRING_DOWN_XMPP_AGENT event weight")
        ("weight-clear-xmpp-agent",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::CLEAR_XMPP_AGENT-1]),
             "Set CLEAR_XMPP_AGENT event weight")
        ("weight-subscribe-routing-instance",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::SUBSCRIBE_ROUTING_INSTANCE-1]),
             "Set SUBSCRIBE_ROUTING_INSTANCE event weight")
        ("weight-unsubscribe-routing-instance",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::UNSUBSCRIBE_ROUTING_INSTANCE-1]),
             "Set UNSUBSCRIBE_ROUTING_INSTANCE event weight")
        ("weight-subscribe-configuration",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::SUBSCRIBE_CONFIGURATION-1]),
             "Set SUBSCRIBE_CONFIGURATION event weight")
        ("weight-unsubscribe-configuration",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::UNSUBSCRIBE_CONFIGURATION-1]),
             "Set UNSUBSCRIBE_CONFIGURATION event weight")
        ("weight-add-bgp-peer",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::ADD_BGP_PEER-1]),
             "Set ADD_BGP_PEER event weight")
        ("weight-delete-bgp-peer",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::DELETE_BGP_PEER-1]),
             "Set DELETE_BGP_PEER event weight")
        ("weight-clear-bgp-peer",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::CLEAR_BGP_PEER-1]),
             "Set CLEAR_BGP_PEER event weight")
        ("weight-add-routing-instance",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::
                 ADD_ROUTING_INSTANCE-1]),
             "Set ADD_ROUTING_INSTANCE event weight")
        ("weight-delete-routing-instance",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::
                 DELETE_ROUTING_INSTANCE-1]),
             "Set DELETE_ROUTING_INSTANCE event weight")
        ("weight-add-route-target",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::ADD_ROUTE_TARGET-1]),
             "Set ADD_ROUTE_TARGET event weight")
        ("weight-delete-route-target",
           value<float>()->default_value(d_events_weight_[
               BgpStressTestEvent::DELETE_ROUTE_TARGET-1]),
             "Set DELETE_ROUTE_TARGET event weight")
        ("weight-change-socket-buffer-size",
             value<float>()->default_value(d_events_weight_[
                 BgpStressTestEvent::
                 CHANGE_SOCKET_BUFFER_SIZE-1]),
             "Set CHANGE_SOCKET_BUFFER_SIZE event weight")
        ("weight-show-all-routes",
           value<float>()->default_value(d_events_weight_[
               BgpStressTestEvent::SHOW_ALL_ROUTES-1]),
             "Set SHOW_ALL_ROUTES event weight")
        ("test-id", value<int>()->default_value(d_test_id_),
              "set start xmpp agent id <0 - 15>")
        ("xmpp-nexthop", value<string>()->default_value(d_xmpp_rt_nexthop_),
              "set xmpp route nexthop IP address")
        ("xmpp-nexthop-vary", bool_switch(&d_xmpp_rt_nexthop_vary_),
             "Vary nexthop advertised for each route")
        ("xmpp-port", value<int>(), "set xmpp server port number")
        ("xmpp-prefix", value<string>(), "set xmpp route IP prefix start")
        ("xmpp-prefix-format-large", bool_switch(&d_xmpp_rt_format_large_),
             "Support large number routes per agent (4K - 1), 1024 otherwise")
        ("xmpp-server", value<string>()->default_value(d_xmpp_server_),
              "set xmpp server IP address")
        ("xmpp-source", value<string>()->default_value(d_xmpp_source_),
              "set xmpp connection source IP address")
        ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);
    HEAP_PROFILER_START("control-node");

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    if (d_close_from_control_node_) {
        cmd_line_arg_set = true;
    }

    //
    // Retrieve logging params
    //
    if (vm.count("log-category")) {
        d_log_category_ = vm["log-category"].as<string>();
    }
    if (vm.count("log-level")) {
        d_log_level_ = vm["log-level"].as<string>();
    }

    //
    // Disable logging in bgp and xmpp altogether, if desired
    //
    if (d_log_disable_) {
        SetLoggingDisabled(true);
    }

    if (vm.count("ninstances")) {
        d_instances_ = vm["ninstances"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nroutes")) {
        d_routes_ = vm["nroutes"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("npeers")) {
        d_peers_ = vm["npeers"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nagents")) {
        d_agents_ = vm["nagents"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("ntargets")) {
        d_targets_ = vm["ntargets"].as<int>();
        cmd_line_arg_set = true;
    }
    bool nvms_set = false;
    if (vm.count("nvms")) {
        d_vms_count_ = vm["nvms"].as<int>();
        nvms_set = true;
    }
    if (vm.count("xmpp-nexthop")) {
        d_xmpp_rt_nexthop_ = vm["xmpp-nexthop"].as<string>();
    }
    if (vm.count("xmpp-prefix")) {
        d_xmpp_rt_prefix_ = vm["xmpp-prefix"].as<string>();
    }
    if (vm.count("instance-name")) {
        d_instance_name_ = vm["instance-name"].as<string>();
    }
    if (vm.count("xmpp-server")) {
        d_xmpp_server_ = vm["xmpp-server"].as<string>();
    }
    if (vm.count("xmpp-source")) {
        d_xmpp_source_ = vm["xmpp-source"].as<string>();
    }
    if (vm.count("test-id")) {
        d_test_id_ = vm["test-id"].as<int>();
        if (d_xmpp_source_ == "127.0.0.1") {

            //
            // Set d_test_id_ as the second nibble
            //
            d_xmpp_source_ = "127." +
                boost::lexical_cast<string>(d_test_id_) + ".0.1";
        }
    }
    bool xmpp_port_set = false;
    if (vm.count("xmpp-port")) {
        d_xmpp_port_ = vm["xmpp-port"].as<int>();
        xmpp_port_set = true;
    }

    if (vm.count("http-port")) {
        d_http_port_ = vm["http-port"].as<int>();
    }

    if (vm.count("db-walker-wait-usecs")) {
        d_db_walker_wait_ = vm["db-walker-wait-usecs"].as<int>();
    }
    if (vm.count("wait-for-idle-time")) {
        d_wait_for_idle_ = vm["wait-for-idle-time"].as<int>();
    }
    if (vm.count("nevents")) {
        d_events_ = vm["nevents"].as<int>();
    }

    if (vm.count("feed-events")) {
        d_feed_events_file_ = vm["feed-events"].as<string>();
        BgpStressTestEvent::ReadEventsFromFile(d_feed_events_file_);
    }

    if (vm.count("routes-send-trigger")) {
        d_routes_send_trigger_ = vm["routes-send-trigger"].as<string>();
    }

    if (vm.count("weight-add-bgp-route")) {
         d_events_weight_[BgpStressTestEvent::ADD_BGP_ROUTE-1] =
             vm["weight-add-bgp-route"].as<float>();
    }
    if (vm.count("weight-delete-bgp-route")) {
         d_events_weight_[
             BgpStressTestEvent::DELETE_BGP_ROUTE-1] =
             vm["weight-delete-bgp-route"].as<float>();
    }
    if (vm.count("weight-add-xmpp-route")) {
         d_events_weight_[
             BgpStressTestEvent::ADD_XMPP_ROUTE-1] =
             vm["weight-add-xmpp-route"].as<float>();
    }
    if (vm.count("weight-delete-xmpp-route")) {
        d_events_weight_[
            BgpStressTestEvent::DELETE_XMPP_ROUTE-1] =
            vm["weight-delete-xmpp-route"].as<float>();
    }
    if (vm.count("weight-bring-up-xmpp-agent")) {
        d_events_weight_[
            BgpStressTestEvent::BRING_UP_XMPP_AGENT-1] =
            vm["weight-bring-up-xmpp-agent"].as<float>();
    }
    if (vm.count("weight-bring-down-xmpp-agent")) {
        d_events_weight_[
            BgpStressTestEvent::BRING_DOWN_XMPP_AGENT-1] =
            vm["weight-bring-down-xmpp-agent"].as<float>();
    }
    if (vm.count("weight-clear-xmpp-agent")) {
        d_events_weight_[
            BgpStressTestEvent::CLEAR_XMPP_AGENT-1] =
            vm["weight-clear-xmpp-agent"].as<float>();
    }
    if (vm.count("weight-subscribe-routing-instance")) {
        d_events_weight_[
            BgpStressTestEvent::SUBSCRIBE_ROUTING_INSTANCE-1] =
            vm["weight-subscribe-routing-instance"].as<float>();
    }
    if (vm.count("weight-unsubscribe-routing-instance")) {
        d_events_weight_[
            BgpStressTestEvent::UNSUBSCRIBE_ROUTING_INSTANCE-1] =
            vm["weight-unsubscribe-routing-instance"].as<float>();
    }
    if (vm.count("weight-subscribe-configuration")) {
        d_events_weight_[
            BgpStressTestEvent::SUBSCRIBE_CONFIGURATION-1] =
            vm["weight-subscribe-configuration"].as<float>();
    }
    if (vm.count("weight-unsubscribe-configuration")) {
        d_events_weight_[
            BgpStressTestEvent::UNSUBSCRIBE_CONFIGURATION-1] =
            vm["weight-unsubscribe-configuration"].as<float>();
    }
    if (vm.count("weight-add-bgp-peer")) {
        d_events_weight_[
            BgpStressTestEvent::ADD_BGP_PEER-1] =
            vm["weight-add-bgp-peer"].as<float>();
    }
    if (vm.count("weight-delete-bgp-peer")) {
        d_events_weight_[
            BgpStressTestEvent::DELETE_BGP_PEER-1] =
            vm["weight-delete-bgp-peer"].as<float>();
    }
    if (vm.count("weight-clear-bgp-peer")) {
        d_events_weight_[
            BgpStressTestEvent::CLEAR_BGP_PEER-1] =
            vm["weight-clear-bgp-peer"].as<float>();
    }
    if (vm.count("weight-add-routing-instance")) {
        d_events_weight_[
            BgpStressTestEvent::ADD_ROUTING_INSTANCE-1] =
            vm["weight-add-routing-instance"].as<float>();
    }
    if (vm.count("weight-delete-routing-instance")) {
        d_events_weight_[
            BgpStressTestEvent::DELETE_ROUTING_INSTANCE-1] =
            vm["weight-delete-routing-instance"].as<float>();
    }
    if (vm.count("weight-add-route-target")) {
        d_events_weight_[
            BgpStressTestEvent::ADD_ROUTE_TARGET-1] =
            vm["weight-add-route-target"].as<float>();
    }
    if (vm.count("weight-delete-route-target")) {
        d_events_weight_[
            BgpStressTestEvent::DELETE_ROUTE_TARGET-1] =
            vm["weight-delete-route-target"].as<float>();
    }
    if (vm.count("weight-change-socket-buffer-size")) {
        d_events_weight_[
            BgpStressTestEvent::CHANGE_SOCKET_BUFFER_SIZE-1] =
            vm["weight-change-socket-buffer-size"].as<float>();
    }

    // Make sure that parameters fall within the boundaries to avoid prefix
    // overlap and counting issues.
    if (d_xmpp_rt_prefix_.empty()) {
        int max_test_id, max_ninstances, max_nagents, max_nroutes;

        if (d_xmpp_rt_format_large_) {
            max_test_id = (1 << 3) - 1;
            max_ninstances = (1 << 5) - 1;
            max_nagents = (1 << 12) - 1;
            max_nroutes = (1 << 12) - 1;
        } else {
            max_test_id = (1 << 9) - 1;
            max_ninstances = (1 << 6) - 1;
            max_nagents = (1 << 7) - 1;
            max_nroutes = (1 << 10) - 1;
        }

        if (d_test_id_ > max_test_id) {
            cout << "Invalid test-id. ";
            cout << "Valid range <0 - " << max_test_id << ">" << endl;
            exit(-1);
        }
        if (d_instances_ > max_ninstances) {
            cout << "Invalid ninstances. ";
            cout << "Valid range <0 - " << max_ninstances << ">" << endl;
            exit(-1);
        }
        if (d_agents_ > max_nagents) {
            cout << "Invalid nagents. ";
            cout << "Valid range <0 - " << max_nagents << ">" << endl;
            exit(-1);
        }
        if (d_routes_ > max_nroutes) {
            cout << "Invalid nroutes. ";
            cout << "Valid range <0 - " << max_nroutes << ">" << endl;
            exit(-1);
        }
    }

    if (!vm.count("log-file") || vm["log-file"].as<string>() == "<stdout>") {
        bgp_log_test::init();
    } else {
        ostringstream log_file;
        log_file << vm["log-file"].as<string>();
        if (log_file_uniquefy) {
            boost::system::error_code error;
            string hostname(boost::asio::ip::host_name(error));
            log_file << "." << hostname << "." << getpid();
        }

        Module::type module = Module::CONTROL_NODE;
        string module_name = g_vns_constants.ModuleNames.find(module)->second;
        bgp_log_test::init(log_file.str(),
            vm.count("log-file-size") ?
                vm["log-file-size"].as<unsigned long>() : log_file_size,
            vm.count("log-file-index") ?
                vm["log-file-index"].as<unsigned int>() : log_file_index,
                !d_log_disable_, d_log_level_, module_name);
    }

    //
    // Set Sandesh log category and level
    //
    Sandesh::SetLoggingParams(!d_log_local_disable_, d_log_category_,
                              d_log_level_);

    //
    // If we are connecting to external xmpp server, PAUSE at the end of the
    // test, unless events are being fed through a file
    //
    if (xmpp_port_set) {
#if 0
        if (BgpStressTestEvent::d_events_list_.empty()) {
            BgpStressTestEvent::d_events_list_.push_back(
                BgpStressTestEvent::PAUSE);
            d_events_ = 1;
        }
#endif
        d_peers_ = 0;
        d_external_mode_ = true;
        cmd_line_arg_set = true;
        if (!nvms_set) d_vms_count_ = 0;
    }

    if (cmd_line_arg_set) {
        n_instances.clear();
        n_instances.push_back(d_instances_);

        n_routes.clear();
        n_routes.push_back(d_routes_);

        n_peers.clear();
        n_peers.push_back(d_peers_);

        n_targets.clear();
        n_targets.push_back(d_targets_);

        n_agents.clear();
        n_agents.push_back(d_agents_);

        xmpp_close_from_control_node.clear();
        xmpp_close_from_control_node.push_back(d_close_from_control_node_);
    }
}

static vector<int> GetInstanceParameters() {
    process_command_line_args(gargc, gargv);
    return n_instances;
}

static vector<int> GetAgentParameters() {
    process_command_line_args(gargc, gargv);
    return n_agents;
}

static vector<int> GetRouteParameters() {
    process_command_line_args(gargc, gargv);
    return n_routes;
}

static vector<int> GetPeerParameters() {
    process_command_line_args(gargc, gargv);
    return n_peers;
}

static vector<int> GetTargetParameters() {
    process_command_line_args(gargc, gargv);
    return n_targets;
}

void BgpStressTest::InitParams() {
    n_instances_ = ::std::tr1::get<0>(GetParam());
    n_routes_ = ::std::tr1::get<1>(GetParam());
    n_peers_ = ::std::tr1::get<2>(GetParam());
    n_agents_ = ::std::tr1::get<3>(GetParam());
    n_targets_ = ::std::tr1::get<4>(GetParam());
    xmpp_close_from_control_node_ = ::std::tr1::get<5>(GetParam());
}

#define COMBINE_PARAMS \
    Combine(ValuesIn(GetInstanceParameters()),                      \
            ValuesIn(GetRouteParameters()),                         \
            ValuesIn(GetPeerParameters()),                          \
            ValuesIn(GetAgentParameters()),                         \
            ValuesIn(GetTargetParameters()),                        \
            ValuesIn(xmpp_close_from_control_node))

INSTANTIATE_TEST_CASE_P(BgpStressTestWithParams, BgpStressTest,
                        COMBINE_PARAMS);

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    std::srand(std::time(0));
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<PeerCloseManager>(
        boost::factory<PeerCloseManagerTest *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    IFMapFactory::Register<IFMapXmppChannel>(
        boost::factory<IFMapXmppChannelTest *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int bgp_stress_test_main(int argc, const char **argv) {
    gargc = argc;
    gargv = argv;

    ::testing::InitGoogleTest(&gargc, const_cast<char **>(gargv));
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    HEAP_PROFILER_DUMP("bgp_stress_test");
    HEAP_PROFILER_STOP();
    return result;
}

#ifndef __BGP_STRESS_TEST_SUITE__

int main(int argc, char **argv) {
    return bgp_stress_test_main(argc, const_cast<const char **>(argv));
}
#endif
