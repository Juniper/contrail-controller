/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_server_test_util.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_table.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using boost::uuids::nil_generator;
using namespace boost::asio;
using namespace boost;
using namespace std;

int StateMachineTest::hold_time_msecs_ = 0;
int StateMachineTest::keepalive_time_msecs_ = 0;
int XmppStateMachineTest::hold_time_msecs_ = 0;
XmppStateMachineTest::NotifyFn XmppStateMachineTest::notify_fn_;
TcpSession::Event XmppStateMachineTest::skip_tcp_event_ =TcpSession::EVENT_NONE;
TcpSession::Event StateMachineTest::skip_tcp_event_ = TcpSession::EVENT_NONE;

//
// This is a static data structure that maps client tcp end points to configured
// bgp peers. Using this, we can form multiple bgp peering sessions between
// two bgp_server()s even though they use the same loopback IP address.
//
static std::map<ip::tcp::endpoint, BgpPeerKey> peer_connect_map_;
static tbb::mutex peer_connect_map_mutex_;
typedef ip::tcp::socket Socket;

BgpServerTest::BgpServerTest(EventManager *evm, const string &localname,
                             DB *config_db, DBGraph *config_graph) :
    BgpServer(evm), config_db_(config_db), config_graph_(config_graph) {
    ConcurrencyScope scope("bgp::Config");
    BgpIfmapConfigManager *config_manager =
            static_cast<BgpIfmapConfigManager *>(config_mgr_.get());
    config_manager->Initialize(config_db_.get(), config_graph_.get(),
                               localname);
    cleanup_config_ = false;
    rtarget_group_mgr_->Initialize();
}

BgpServerTest::BgpServerTest(EventManager *evm, const string &localname)
    : BgpServer(evm),
      name_(localname),
      config_db_(
          new DB(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"))),
      config_graph_(new DBGraph()) {
    ConcurrencyScope scope("bgp::Config");
    cleanup_config_ = true;
    IFMapLinkTable_Init(config_db_.get(), config_graph_.get());
    vnc_cfg_Server_ModuleInit(config_db_.get(), config_graph_.get());
    bgp_schema_Server_ModuleInit(config_db_.get(), config_graph_.get());
    BgpIfmapConfigManager *config_manager =
            static_cast<BgpIfmapConfigManager *>(config_mgr_.get());
    config_manager->Initialize(config_db_.get(), config_graph_.get(),
                               localname);
    rtarget_group_mgr_->Initialize();
}

void BgpServerTest::PostShutdown() {
    if (!cleanup_config_) return;

    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
        config_db_->FindTable("__ifmap_metadata__.0"));
    ltable->Clear();
    IFMapTable::ClearTables(config_db_.get());

    task_util::WaitForIdle();
    config_db_->Clear();
}

void BgpServerTest::Shutdown(bool verify, bool wait_for_idle) {
    if (wait_for_idle)
        task_util::WaitForIdle();
    BgpServer::Shutdown();
    if (verify)
        VerifyShutdown();
}

void BgpServerTest::VerifyShutdown() const {
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_EQ(0, routing_instance_mgr()->count());
    TASK_UTIL_ASSERT_TRUE(session_mgr_ == NULL);
}

BgpServerTest::~BgpServerTest() {
    PostShutdown();
}

bool BgpServerTest::Configure(const string &config) {
    BgpConfigParser parser(config_db_.get());
    return parser.Parse(config);
}

BgpPeerTest *BgpServerTest::FindPeerByUuid(const char *routing_instance,
                                           const std::string &uuid) {
    RoutingInstance *rti = inst_mgr_->GetRoutingInstance(routing_instance);
    assert(rti != NULL);
    for (PeerManager::BgpPeerKeyMap::iterator iter =
         rti->peer_manager()->peer_map_mutable()->begin();
         iter != rti->peer_manager()->peer_map_mutable()->end(); ++iter) {
        BgpPeer *peer = iter->second;
        if (peer->config() && peer->config()->uuid() == uuid) {
            return static_cast<BgpPeerTest *>(peer);
        }
    }
    return NULL;
}

BgpPeer *BgpServerTest::FindPeer(const char *routing_instance,
                                 const std::string &name) {
    RoutingInstance *rti = inst_mgr_->GetRoutingInstance(routing_instance);
    assert(rti != NULL);
    return rti->peer_manager()->PeerLookup(name);
}

BgpPeer *BgpServerTest::FindMatchingPeer(const string &routing_instance,
                                         const std::string &name) {
    RoutingInstance *rti = inst_mgr_->GetRoutingInstance(routing_instance);
    PeerManager *peer_manager = rti->peer_manager();
    BOOST_FOREACH(PeerManager::BgpPeerNameMap::value_type &it,
        peer_manager->peers_by_name_) {
        if (it.first.find(name) != string::npos)
            return it.second;
    }
    return NULL;
}

void BgpServerTest::DisableAllPeers() {
    ConcurrencyScope scope("bgp::Config");
    for (BgpPeerList::iterator it = peer_list_.begin();
         it != peer_list_.end(); ++it) {
        it->second->SetAdminState(true);
    }
}

void BgpServerTest::EnableAllPeers() {
    ConcurrencyScope scope("bgp::Config");
    for (BgpPeerList::iterator it = peer_list_.begin();
         it != peer_list_.end(); ++it) {
        it->second->SetAdminState(false);
    }
}

string BgpServerTest::ToString() const {
    ostringstream out;

    out << name_ << "(AS: " << autonomous_system_;
    if (session_mgr_) {
        out << ", Port: " << session_mgr_->GetPort();
    }
    out << ") :";
    out << BgpServer::ToString();

    return out.str();
}

bool BgpPeerTest::BgpPeerSendUpdate(const uint8_t *msg, size_t msgsize) {
    return BgpPeer::SendUpdate(msg, msgsize);
}

bool BgpPeerTest::BgpPeerMpNlriAllowed(uint16_t afi, uint8_t safi) {
    return BgpPeer::MpNlriAllowed(afi, safi);
}

bool BgpPeerTest::BgpPeerIsReady() {
    return BgpPeer::IsReady();
}

void BgpPeerTest::SetDataCollectionKey(BgpPeerInfo *peer_info) const {
    BgpPeer::SetDataCollectionKey(peer_info);
    peer_info->set_ip_address(ToString());
}

//
// BgpPeerTest - Test implementation for class BgpPeer
//
BgpPeerTest::BgpPeerTest(BgpServer *server, RoutingInstance *rtinst,
                         const BgpNeighborConfig *config)
        : BgpPeer(server, rtinst, config), id_(0),
          work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
                      boost::bind(&BgpPeerTest::ProcessRequest, this, _1)) {
    SendUpdate_fnc_ = boost::bind(&BgpPeerTest::BgpPeerSendUpdate, this,
                                  _1, _2);
    MpNlriAllowed_fnc_ = boost::bind(&BgpPeerTest::BgpPeerMpNlriAllowed, this,
                                     _1, _2);
    IsReady_fnc_ = boost::bind(&BgpPeerTest::BgpPeerIsReady, this);
}

BgpPeerTest::~BgpPeerTest() {
}

// Process requests and run them off bgp::Config exclusive task
bool BgpPeerTest::ProcessRequest(Request *request) {
    CHECK_CONCURRENCY("bgp::Config");
    switch (request->type) {
        case ADMIN_UP:
            BgpPeer::SetAdminState(false, request->subcode);
            request->result = true;
            break;
        case ADMIN_DOWN:
            BgpPeer::SetAdminState(true, request->subcode);
            request->result = true;
            break;
    }

    // Notify waiting caller with the result
    tbb::mutex::scoped_lock lock(work_mutex_);
    cond_var_.notify_all();
    return true;
}

//
// Enable this if peer name uuid is also required in all bgp peer logs
//
bool BgpPeerTest::verbose_name_ = false;

const string &BgpPeerTest::ToString() const {
    if (to_str_.empty()) {
        ostringstream out;
        out << BgpPeer::ToString();
        if (verbose_name_ && config()) {
            out << "(";
            out << config()->name();
            out << ")";
        }
        to_str_ = out.str();
    }
    return to_str_;
}

//
// Bind to a random available port for the bgp client side of the connection.
// Resulting local end point is used as a key to update the global peer map
//
void BgpPeerTest::BindLocalEndpoint(BgpSession *session) {
    boost::system::error_code err;
    int local_port = 10000;
    ip::tcp::endpoint local_endpoint;
    local_endpoint.address(ip::address::from_string("127.0.0.1", err));

    //
    // Try random port numbers until we can successfully bind.
    //
    while (true) {
        local_port += 1 + (rand() % 100);
        local_endpoint.port(local_port);
        session->socket()->bind(local_endpoint, err);
        if (!err) break;
        BGP_DEBUG_UT("Bind failure, will retry with a different port: " <<
            err.message());
    }

    if (err) {
        BGP_WARN_UT("Bind failure: " << err.message());
        return;
    } else {
        BGP_DEBUG_UT("BindLocalEndpoint():bind successful, local port  "
            << local_port);
    }

    //
    // Using local end of the tcp connection as key, update the global peer
    // map structure, which the server side looks up.
    BgpPeerKey peer_key;
    peer_key.endpoint.address(
        ip::address::from_string("127.0.0.1", err));
    peer_key.endpoint.port(server()->session_manager()->GetPort());

    if (config_) {
        if (!config_->uuid().empty()) {
            boost::uuids::string_generator gen;
            peer_key.uuid = gen(config_->uuid());
        } else {
            boost::uuids::nil_generator nil;
            peer_key.uuid == nil();
        }
        tbb::mutex::scoped_lock lock(peer_connect_map_mutex_);
        peer_connect_map_[local_endpoint] = peer_key;
    }
    return;
}

//
// We need this in order to feed our test peer structure into the database.
//
PeerManagerTest::PeerManagerTest(RoutingInstance *instance)
    : PeerManager(instance) {
}

BgpPeer *PeerManagerTest::PeerLocate(
    BgpServer *server, const BgpNeighborConfig *config) {
    BgpPeer *peer = PeerManager::PeerLocate(server, config);
    boost::uuids::nil_generator nil;
    if (peer->peer_key().uuid != nil()) {
        PeerByUuidMap::iterator loc = peers_by_uuid_.find(peer->peer_key().uuid);
        assert(loc == peers_by_uuid_.end() || peer == loc->second);
        peers_by_uuid_.insert(make_pair(peer->peer_key().uuid, peer));
    }
    return peer;
}

void PeerManagerTest::DestroyIPeer(IPeer *ipeer) {
    BgpPeerTest *peer = static_cast<BgpPeerTest *>(ipeer);
    boost::uuids::nil_generator nil;
    if (peer->peer_key().uuid != nil()) {
        PeerByUuidMap::iterator loc =
            peers_by_uuid_.find(peer->peer_key().uuid);
        assert(loc != peers_by_uuid_.end());
        peers_by_uuid_.erase(loc);
    }
    PeerManager::DestroyIPeer(ipeer);
}

//
// Server side peer lookup logic. Look into the global map to identify the peer
//
BgpPeer *PeerManagerTest::PeerLookup(
    TcpSession::Endpoint remote_endpoint) const {
    BgpPeerKey peer_key;
    bool present;

    {
        tbb::mutex::scoped_lock lock(peer_connect_map_mutex_);

        present = peer_connect_map_.count(remote_endpoint) > 0;
        if (present) {
            peer_key = peer_connect_map_.at(remote_endpoint);
        }
    }

    if (present) {
        BGP_DEBUG_UT("Peer key found in peer_connect_map_, peer_key.endpoint: "
            << peer_key.endpoint << ", uuid: " <<  peer_key.uuid);
        boost::uuids::nil_generator nil;
        if (peer_key.uuid == nil())
            return PeerManager::PeerLookup(remote_endpoint);
    } else {
        peer_key.endpoint = remote_endpoint;
        BGP_WARN_UT("Peer key not found in peer_connect_map_, "
                  "remote_endpoint: " << remote_endpoint);
    }

    PeerByUuidMap::const_iterator loc = peers_by_uuid_.find(peer_key.uuid);
    if (loc == peers_by_uuid_.end()) {
        return NULL;
    }

    BGP_DEBUG_UT("Peer found in peers_ config map: "
                     << loc->second->ToString());
    return loc->second;
}

BgpInstanceConfigTest *BgpTestUtil::CreateBgpInstanceConfig(
        const string &name,
        const string import_targets, const string export_targets,
        const string virtual_network, int virtual_network_index) {
    BgpInstanceConfigTest *inst = new BgpInstanceConfigTest(name);

    std::vector<string> import_list;
    if (import_targets != "")
        split(import_list, import_targets, is_any_of(", "), token_compress_on);

    std::vector<string> export_list;
    if (export_targets != "")
        split(export_list, export_targets, is_any_of(", "), token_compress_on);

    BOOST_FOREACH(string import_target, import_list) {
        inst->mutable_import_list()->insert(import_target);
    }
    BOOST_FOREACH(string export_target, export_list) {
        inst->mutable_export_list()->insert(export_target);
    }

    inst->set_virtual_network(virtual_network);
    inst->set_virtual_network_index(virtual_network_index);

    return inst;
}

void BgpTestUtil::UpdateBgpInstanceConfig(BgpInstanceConfigTest *inst,
        const string import_targets, const string export_targets) {
    std::vector<string> import_list;
    if (import_targets != "")
        split(import_list, import_targets, is_any_of(", "), token_compress_on);

    std::vector<string> export_list;
    if (export_targets != "")
        split(export_list, export_targets, is_any_of(", "), token_compress_on);

    inst->mutable_import_list()->clear();
    BOOST_FOREACH(string import_target, import_list) {
        inst->mutable_import_list()->insert(import_target);
    }

    inst->mutable_export_list()->clear();
    BOOST_FOREACH(string export_target, export_list) {
        inst->mutable_export_list()->insert(export_target);
    }
}

void BgpTestUtil::UpdateBgpInstanceConfig(BgpInstanceConfigTest *inst,
        const string virtual_network, int virtual_network_index) {
    inst->set_virtual_network(virtual_network);
    inst->set_virtual_network_index(virtual_network_index);
}

void BgpTestUtil::SetUserData(std::string key, boost::any &value) {
    user_data_.insert(make_pair(key, value));
}

boost::any BgpTestUtil::GetUserData(std::string key) {
    std::map<std::string, boost::any>::iterator iter = user_data_.find(key);
    if (iter != user_data_.end()) {
        return iter->second;
    } else {
        return boost::any();
    }
}

void BgpServerTest::GlobalSetUp(void) {
    BgpObjectFactory::Register<BgpPeer>(
        boost::factory<BgpPeerTest *>());
    BgpObjectFactory::Register<PeerManager>(
        boost::factory<PeerManagerTest *>());
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
}
