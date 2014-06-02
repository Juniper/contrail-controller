/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_server_test_util.h"

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <tbb/mutex.h>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/state_machine.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_table.h"
#include "testing/gunit.h"

using namespace boost::asio;
using namespace boost;
using namespace std;

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
    cleanup_config_ = false;
    config_mgr_->Initialize(config_db_.get(), config_graph_.get(), localname);
    rtarget_group_mgr_->Initialize();
    GetIsPeerCloseGraceful_fnc_ =
        boost::bind(&BgpServerTest::BgpServerIsPeerCloseGraceful, this);
}

BgpServerTest::BgpServerTest(EventManager *evm, const string &localname)
        : BgpServer(evm),
          name_(localname),
          config_db_(new DB()),
          config_graph_(new DBGraph()) {
    cleanup_config_ = true;
    IFMapLinkTable_Init(config_db_.get(), config_graph_.get());
    vnc_cfg_Server_ModuleInit(config_db_.get(), config_graph_.get());
    bgp_schema_Server_ModuleInit(config_db_.get(), config_graph_.get());
    config_mgr_->Initialize(config_db_.get(), config_graph_.get(), localname);
    rtarget_group_mgr_->Initialize();
    GetIsPeerCloseGraceful_fnc_ =
        boost::bind(&BgpServerTest::BgpServerIsPeerCloseGraceful, this);
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

void BgpServerTest::Shutdown() {

    //
    // Wait for all pending events to get processed
    //
    task_util::WaitForIdle();

    BgpServer::Shutdown();

    //
    // Wait for server close process to complete
    //
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_EQ(0, routing_instance_mgr()->count());
    TASK_UTIL_ASSERT_EQ(static_cast<BgpSessionManager *>(NULL), session_mgr_);
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
        : BgpPeer(server, rtinst, config) {
    SendUpdate_fnc_ = boost::bind(&BgpPeerTest::BgpPeerSendUpdate, this,
                                  _1, _2);
    MpNlriAllowed_fnc_ = boost::bind(&BgpPeerTest::BgpPeerMpNlriAllowed, this,
                                     _1, _2);
    IsReady_fnc_ = boost::bind(&BgpPeerTest::BgpPeerIsReady, this);
}

BgpPeerTest::~BgpPeerTest() {
}

//
// Enable this if peer name uuid is also required in all bgp peer logs
//
bool BgpPeerTest::verbose_name_ = false;

string BgpPeerTest::ToString() const {
    ostringstream out;

    out << BgpPeer::ToString();
    if (verbose_name_ && config()) {
        out << "(";
        out << config()->name();
        out << ")";
    }
    return out.str();
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
    boost::uuids::string_generator gen;

    if (config_) {
        peer_key.uuid = gen(config_->uuid());
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
    PeerByUuidMap::iterator loc = peers_by_uuid_.find(peer->peer_key().uuid);

    if (loc != peers_by_uuid_.end()) {
        if (peer != loc->second) {
            assert(peer == loc->second);
        }
        return peer;
    }

    peers_by_uuid_.insert(make_pair(peer->peer_key().uuid, peer));
    return peer;
}

void PeerManagerTest::DestroyIPeer(IPeer *ipeer) {
    BgpPeerTest *peer = static_cast<BgpPeerTest *>(ipeer);
    PeerByUuidMap::iterator loc = peers_by_uuid_.find(peer->peer_key().uuid);

    assert(loc != peers_by_uuid_.end());
    peers_by_uuid_.erase(loc);
    PeerManager::DestroyIPeer(ipeer);
}

//
// Server side peer lookup logic. Look into the global map to identify the peer
//
BgpPeer *PeerManagerTest::PeerLookup(ip::tcp::endpoint remote_endpoint) {
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
    } else {
        peer_key.endpoint = remote_endpoint;
        BGP_WARN_UT("Peer key not found in peer_connect_map_, "
                  "remote_endpoint: " << remote_endpoint);
    }

    PeerByUuidMap::iterator loc = peers_by_uuid_.find(peer_key.uuid);
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

// Constructor to create different neighbor configs with custom names.
// Used in unit testing mostly.
BgpNeighborConfig::BgpNeighborConfig(const BgpInstanceConfig *instance,
                                     const string &remote_name,
                                     const string &local_name,
                                     const autogen::BgpRouter *router)
        : instance_(instance), name_(remote_name) {
    const autogen::BgpRouterParams &params = router->parameters();
    peer_config_ = params;
}

void BgpServerTest::GlobalSetUp(void) {
    BgpObjectFactory::Register<BgpPeer>(
        boost::factory<BgpPeerTest *>());
    BgpObjectFactory::Register<PeerManager>(
        boost::factory<PeerManagerTest *>());
}
