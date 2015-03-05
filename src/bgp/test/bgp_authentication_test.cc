/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include <pugixml/pugixml.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_peer.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"

#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

#include "testing/gunit.h"

using std::string;
using std::vector;

static void SetBgpRouterParams(autogen::BgpRouterParams *property, int asn,
        const string &id, const string &address, int port, int hold_time,
        const vector<string> &families,
        const vector<autogen::AuthenticationKeyItem> &auth_items,
        int local_asn);
static void SetAutogenAuthKey(autogen::AuthenticationKeyItem *item,
        const string &id, const string &type, const string &value,
        time_t start_time);
static autogen::BgpPeeringAttributes *CreateBgpPeeringAttributes(
        const string &uuid, const vector<string> &families,
        const vector<autogen::AuthenticationKeyItem> &keys);

class BgpServerAuthTestMock {
public:
    BgpServerAuthTestMock(EventManager &evm, const string &fqn_ifmap_id,
            const string &ifmap_id, const string &bgp_id,
            const string &cn_address, int port);
    void AddBgpRouterNode(int asn, const string &fqn_ifmap_id,
            const string &bgp_id, const string &cn_address, int port,
            int local_asn, const vector<string> &families,
            vector<autogen::AuthenticationKeyItem> auth_keys = 
                vector<autogen::AuthenticationKeyItem>());
    void ChangeBgpRouterNode(int asn, const string &fqn_ifmap_id,
            const string &bgp_id, const string &cn_address, int port,
            int local_asn, const vector<string> &families,
            vector<autogen::AuthenticationKeyItem> auth_keys = 
                vector<autogen::AuthenticationKeyItem>());
    void DeleteBgpRouterNode(const string &fqn_ifmap_id);
    void Initialize(int port);
    void Shutdown();
    void IFMapMsgLink(const string &ltype, const string &lid,
            const string &rtype, const string &rid, const string &metadata,
            uint64_t sequence_number, AutogenProperty *content);
    void IFMapMsgUnlink(const string &ltype, const string &lid,
            const string &rtype, const string &rid, const string &metadata);
    IFMapTable *FindTable(const string &type);
    BgpPeer *FindPeerByUuid(int line, const string &uuid);

    const string& fqn_ifmap_id() { return fqn_ifmap_id_; }
    const string& ifmap_id() { return ifmap_id_; }
    const string& bgp_id() { return bgp_id_; }
    const string& address() { return address_; }
    int port() { return port_; }
    const vector<string>& families() { return families_; }
    const BgpServerTest* server() const { return cn_.get(); }
    BgpServerTest* server() { return cn_.get(); }

private:
    DB* db_;
    DBGraph* db_graph_;
    EventManager &evm_;
    std::auto_ptr<BgpServerTest> cn_;
    BgpIfmapConfigManager *cfg_mgr_;
    const string fqn_ifmap_id_;
    const string ifmap_id_;
    const string bgp_id_;
    const string address_;
    int port_;
    vector<string> families_;
};

BgpServerAuthTestMock::BgpServerAuthTestMock(EventManager &evm,
            const string &fqn_ifmap_id, const string &ifmap_id,
            const string &bgp_id, const string &address, int port) :
        db_(NULL), db_graph_(NULL), evm_(evm), fqn_ifmap_id_(fqn_ifmap_id),
        ifmap_id_(ifmap_id), bgp_id_(bgp_id), address_(address) {
    families_ = boost::assign::list_of("inet-vpn");
    Initialize(port);
}

void BgpServerAuthTestMock::Shutdown() {
    cn_->Shutdown(); // will cleanup equivalent to db_util::Clear(&db_)
}

void BgpServerAuthTestMock::Initialize(int port) {
    cn_.reset(new BgpServerTest(&evm_, ifmap_id_));
    db_ = cn_->config_db();
    db_graph_ = cn_->config_graph();
    cn_->session_manager()->Initialize(port);
    LOG(DEBUG, "Created CN " << ifmap_id_ << " at port: " 
        << cn_->session_manager()->GetPort());
    port_ = cn_->session_manager()->GetPort();
    cfg_mgr_ = static_cast<BgpIfmapConfigManager *>(cn_->config_manager());
    //cfg_mgr_->Initialize(&db_, &db_graph_, ifmap_id_);
}

void BgpServerAuthTestMock::AddBgpRouterNode(int asn,
        const string &fqn_ifmap_id, const string &bgp_id, const string &address,
        int port, int local_asn, const vector<string> &families,
        vector<autogen::AuthenticationKeyItem> auth_keys) {
    autogen::BgpRouterParams *prop = new autogen::BgpRouterParams();
    SetBgpRouterParams(prop, asn, bgp_id, address, port, 90, families,
                       auth_keys, asn);
    ifmap_test_util::IFMapMsgNodeAdd(db_, "bgp-router", fqn_ifmap_id, 0,
                                     "bgp-router-parameters", prop);
    task_util::WaitForIdle();
}

void BgpServerAuthTestMock::ChangeBgpRouterNode(int asn,
        const string &fqn_ifmap_id, const string &bgp_id, const string &address,
        int port, int local_asn, const vector<string> &families,
        vector<autogen::AuthenticationKeyItem> auth_keys) {
    autogen::BgpRouterParams *prop = new autogen::BgpRouterParams();
    SetBgpRouterParams(prop, asn, bgp_id, address, port,
                       90, families, auth_keys, asn);
    ifmap_test_util::IFMapMsgNodeAdd(db_, "bgp-router", fqn_ifmap_id, 0,
                                     "bgp-router-parameters", prop);
    task_util::WaitForIdle();
}

void BgpServerAuthTestMock::DeleteBgpRouterNode(const string &fqn_ifmap_id) {
    ifmap_test_util::IFMapMsgNodeDelete(db_, "bgp-router", fqn_ifmap_id, 0,
                                        "bgp-router-parameters");
    task_util::WaitForIdle();
}

void BgpServerAuthTestMock::IFMapMsgLink(const string &ltype, const string &lid,
        const string &rtype, const string &rid, const string &metadata,
        uint64_t sequence_number, AutogenProperty *content) {
    ifmap_test_util::IFMapMsgLink(db_, ltype, lid, rtype, rid, metadata,
                                  sequence_number, content);
}

void BgpServerAuthTestMock::IFMapMsgUnlink(const string &ltype,
        const string &lid, const string &rtype, const string &rid,
        const string &metadata) {
    ifmap_test_util::IFMapMsgUnlink(db_, ltype, lid, rtype, rid, metadata);
}

IFMapTable *BgpServerAuthTestMock::FindTable(const string &table_type) {
    return IFMapTable::FindTable(db_, table_type);
}

BgpPeer *BgpServerAuthTestMock::FindPeerByUuid(int line, const string &uuid) {
    std::cout << "FindPeerByUuid() called from line " << line << std::endl;
    TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
        cn_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
    BgpPeer *peer =
        cn_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
    EXPECT_TRUE(peer != NULL);
    return peer;
}

class BgpAuthenticationTest : public ::testing::Test {
protected:
    static const int asn = 64000;
    BgpAuthenticationTest();
    ~BgpAuthenticationTest();

    virtual void SetUp() {
        task_util::WaitForIdle();

        thread_.Start();
        task_util::WaitForIdle();

        AddBgpRouterNodes();
    }

    virtual void TearDown() {
        DeleteBgpRouterNodes();

        IFMapTable *tbl = cn1_->FindTable("bgp-router");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));
        tbl = cn1_->FindTable("bgp-peering");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));

        tbl = cn2_->FindTable("bgp-router");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));
        tbl = cn2_->FindTable("bgp-peering");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));

        tbl = cn3_->FindTable("bgp-router");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));
        tbl = cn3_->FindTable("bgp-peering");
        TASK_UTIL_EXPECT_EQ(0, TableCount(tbl));

        cn1_->Shutdown();
        task_util::WaitForIdle();
        cn2_->Shutdown();
        task_util::WaitForIdle();
        cn3_->Shutdown();
        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void AddBgpRouterNodes();
    void DeleteBgpRouterNodes();
    int TableCount(DBTable *table);
    void VerifyPeers(int line, BgpServerAuthTestMock *cn1,
            BgpServerAuthTestMock *cn2, size_t verify_kacount,
            const string &id1, const string &id2, as_t asn1, as_t asn2,
            const string &key);

    EventManager evm_;
    ServerThread thread_;
    BgpServerAuthTestMock *cn1_;
    BgpServerAuthTestMock *cn2_;
    BgpServerAuthTestMock *cn3_;
};

BgpAuthenticationTest::BgpAuthenticationTest() : 
    thread_(&evm_),
    cn1_(new BgpServerAuthTestMock(evm_,
         "default-domain:default-project:ip-fabric:__default__:CN1", "CN1",
         "192.168.0.11", "127.0.0.1", 0)),
    cn2_(new BgpServerAuthTestMock(evm_,
         "default-domain:default-project:ip-fabric:__default__:CN2", "CN2",
         "192.168.0.12", "127.0.0.1", 0)),
    cn3_(new BgpServerAuthTestMock(evm_,
         "default-domain:default-project:ip-fabric:__default__:CN3", "CN3",
         "192.168.0.13", "127.0.0.1", 0)) {
}

BgpAuthenticationTest::~BgpAuthenticationTest() {
    delete cn1_;
    delete cn2_;
    delete cn3_;
}

int BgpAuthenticationTest::TableCount(DBTable *table) {
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(table->GetTablePartition(0));
    int count = 0;
    for (DBEntryBase *entry = partition->GetFirst(); entry;
            entry = partition->GetNext(entry)) {
        count++;
    }
    return count;
}

void BgpAuthenticationTest::AddBgpRouterNodes() {
    // No auth keys during init.
    vector<autogen::AuthenticationKeyItem> auth_keys;
    // Add cn1, cn2, cn3 BgpRouter nodes in CN1.
    cn1_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_keys);
    cn1_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_keys);
    cn1_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_keys);
    // Add cn1, cn2, cn3 BgpRouter nodes in CN2.
    cn2_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_keys);
    cn2_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_keys);
    cn2_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_keys);
    // Add cn1, cn2, cn3 BgpRouter nodes in CN3.
    cn3_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_keys);
    cn3_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_keys);
    cn3_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_keys);
}

void BgpAuthenticationTest::DeleteBgpRouterNodes() {
    // Remove cn1, cn2, cn3 BgpRouter nodes from CN1.
    cn1_->DeleteBgpRouterNode(cn1_->fqn_ifmap_id());
    cn1_->DeleteBgpRouterNode(cn2_->fqn_ifmap_id());
    cn1_->DeleteBgpRouterNode(cn3_->fqn_ifmap_id());
    // Remove cn1, cn2, cn3 BgpRouter nodes from CN2.
    cn2_->DeleteBgpRouterNode(cn1_->fqn_ifmap_id());
    cn2_->DeleteBgpRouterNode(cn2_->fqn_ifmap_id());
    cn2_->DeleteBgpRouterNode(cn3_->fqn_ifmap_id());
    // Remove cn1, cn2, cn3 BgpRouter nodes from CN3.
    cn3_->DeleteBgpRouterNode(cn1_->fqn_ifmap_id());
    cn3_->DeleteBgpRouterNode(cn2_->fqn_ifmap_id());
    cn3_->DeleteBgpRouterNode(cn3_->fqn_ifmap_id());
}

void BgpAuthenticationTest::VerifyPeers(int line, BgpServerAuthTestMock *cn1,
        BgpServerAuthTestMock *cn2, size_t kacount, const string &id1,
        const string &id2, as_t asn1, as_t asn2, const string &key) {
    std::cout << "VerifyPeers() called from line number " << line << std::endl;
    BgpProto::BgpPeerType peer_type =
        (asn1 == asn2) ? BgpProto::IBGP : BgpProto::EBGP;

    string uuid = BgpConfigParser::session_uuid(id1, id2, 0);

    // Check the peer on first CN.
    BgpPeer *peer1 = cn1->FindPeerByUuid(__LINE__, uuid);
    ASSERT_TRUE(peer1 != NULL);
    TASK_UTIL_EXPECT_EQ(asn1, peer1->local_as());
    TASK_UTIL_EXPECT_EQ(peer_type, peer1->PeerType());
    BGP_WAIT_FOR_PEER_STATE(peer1, StateMachine::ESTABLISHED);

    // Check the peer on second CN.
    BgpPeer *peer2 = cn2->FindPeerByUuid(__LINE__, uuid);
    ASSERT_TRUE(peer2 != NULL);
    TASK_UTIL_EXPECT_EQ(asn2, peer2->local_as());
    TASK_UTIL_EXPECT_EQ(peer_type, peer2->PeerType());
    BGP_WAIT_FOR_PEER_STATE(peer2, StateMachine::ESTABLISHED);

    // Check the auth keys.
    TASK_UTIL_EXPECT_EQ(0, peer1->GetInuseAuthKeyValue().compare(key));
    TASK_UTIL_EXPECT_EQ(0, peer2->GetInuseAuthKeyValue().compare(key));

    // Make sure that a few keepalives are exchanged.
    if (kacount) {
        TASK_UTIL_EXPECT_TRUE(peer1->get_rx_keepalive() > kacount);
        TASK_UTIL_EXPECT_TRUE(peer1->get_tr_keepalive() > kacount);
        TASK_UTIL_EXPECT_TRUE(peer2->get_rx_keepalive() > kacount);
        TASK_UTIL_EXPECT_TRUE(peer2->get_tr_keepalive() > kacount);
    }
}

// Test init and cleanup for this set of tests.
TEST_F(BgpAuthenticationTest, Noop) {
}

// Basic connection test with keys.
TEST_F(BgpAuthenticationTest, 2CnsWithSameKey) {
    StateMachineTest::set_keepalive_time_msecs(10);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);

    // Create the key and add it to both CN1 and CN2.
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    cn1_cn2_keys.push_back(auth_item);

    // Create peering between CN1 and CN2 on CN1.
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Create peering between CN1 and CN2 on CN2.
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 20, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// The 2 ends have different keys. The connection should not come up.
TEST_F(BgpAuthenticationTest, 2CnsWithDifferentKeys) {
    StateMachineTest::set_keepalive_time_msecs(10);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);

    // Bringup CN1 with key_string1 as key for peering CN1-CN2.
    string key_string1 = "key1";
    vector<autogen::AuthenticationKeyItem> keys1;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string1, 0);
    keys1.push_back(auth_item);
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), keys1);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Bringup CN2 with key_string2 as key for peering CN1-CN2.
    string key_string2 = "key2";
    vector<autogen::AuthenticationKeyItem> keys2;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string2, 0);
    keys2.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys2);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // The keys are different. The connection should not come up.
    TASK_UTIL_EXPECT_TRUE(peer12->get_connect_timer_expired() > 10);
    TASK_UTIL_EXPECT_TRUE(peer21->get_connect_timer_expired() > 10);

    // Change key on CN2 to key_string3 as key for peering CN1-CN2.
    keys2.clear();
    string key_string3 = "key3";
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string3, 0);
    keys2.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys2);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // The keys are still different. The connection should still not come up.
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_TRUE(
            peer12->get_connect_timer_expired() > (old_cn1_ct_count + 10));
    TASK_UTIL_EXPECT_TRUE(
            peer21->get_connect_timer_expired() > (old_cn2_ct_count + 10));

    // Change key on CN1 to key_string2 as key for peering CN1-CN2.
    keys1.clear();
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string2, 0);
    keys1.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys1);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // The keys are still different. The connection should still not come up.
    old_cn1_ct_count = peer12->get_connect_timer_expired();
    old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_TRUE(
            peer12->get_connect_timer_expired() > (old_cn1_ct_count + 10));
    TASK_UTIL_EXPECT_TRUE(
            peer21->get_connect_timer_expired() > (old_cn2_ct_count + 10));

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// Change keys multiple times but with the same keys for all peers.
TEST_F(BgpAuthenticationTest, 2CnsWithChangingSameKeys) {
    StateMachineTest::set_keepalive_time_msecs(10);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Bring up the peers without any keys.
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), no_auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), no_auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    VerifyPeers(__LINE__, cn1_, cn2_, 20, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add cn1cn2key1 as the key on both CN1 and CN2. Peering should come up.
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    cn1_cn2_keys.push_back(auth_item);

    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering came up.
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the key to cn1cn2key2 on both CN1 and CN2. Peering should come up.
    cn1_cn2_keys.clear();
    key_string = "cn1cn2key2";
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    cn1_cn2_keys.push_back(auth_item);

    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the key to cn1cn2key3 on both CN1 and CN2. Peering should come up.
    cn1_cn2_keys.clear();
    key_string = "cn1cn2key3";
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    cn1_cn2_keys.push_back(auth_item);

    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    /*
    // Change the key to nothing on both CN1 and CN2. Peering should come up.
    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), no_auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), no_auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "");
    */

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// Three peerings: CN1-CN2, CN2-CN3, CN1-CN3, all with the same key.
TEST_F(BgpAuthenticationTest, 3CnsWithSameKey) {
    StateMachineTest::set_keepalive_time_msecs(10);

    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> auth_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "common", "MD5", key_string, 0);
    auth_keys.push_back(auth_item);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid12, cn1_->families(), auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid12, cn2_->families(), auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn1_, cn2_, 10, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid13, cn1_->families(), auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid13, cn3_->families(), auth_keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn1_, cn3_, 10, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid23, cn2_->families(), auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid23, cn3_->families(), auth_keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn2_, cn3_, 10, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    // Verify all peerings are still up.
    uint32_t check_count = peer12->get_rx_keepalive() + 20;
    VerifyPeers(__LINE__, cn1_, cn2_, check_count, cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);
    check_count = peer13->get_rx_keepalive() + 20;
    VerifyPeers(__LINE__, cn1_, cn3_, check_count, cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);
    check_count = peer23->get_rx_keepalive() + 20;
    VerifyPeers(__LINE__, cn2_, cn3_, check_count, cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn2_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// 3 pairs of peers. Each pair has a different key.
TEST_F(BgpAuthenticationTest, 3CnsWithDifferentKeyPairs) {
    StateMachineTest::set_keepalive_time_msecs(10);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", "cn1cn2key1", 0);
    cn1_cn2_keys.push_back(auth_item);

    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn1_, cn2_, 10, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "cn1cn2key1");

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    vector<autogen::AuthenticationKeyItem> cn1_cn3_keys;
    SetAutogenAuthKey(&auth_item, "cn1_cn3", "MD5", "cn1cn3key1", 0);
    cn1_cn3_keys.push_back(auth_item);

    uuid = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn3_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid, cn3_->families(), cn1_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn1_, cn3_, 10, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn1cn3key1");

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    vector<autogen::AuthenticationKeyItem> cn2_cn3_keys;
    SetAutogenAuthKey(&auth_item, "cn2_cn3", "MD5", "cn2cn3key1", 0);
    cn2_cn3_keys.push_back(auth_item);

    uuid = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn2_cn3_keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid, cn3_->families(), cn2_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    VerifyPeers(__LINE__, cn2_, cn3_, 10, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn2cn3key1");

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn2_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// Set one key on all 3 peer pairs. Verify. Then change the key on 3 peer pairs
// with the same value. Verify again.
TEST_F(BgpAuthenticationTest, 3CnsWithChangingSameKeys) {
    StateMachineTest::set_keepalive_time_msecs(10);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    string key_string = "first_key";
    vector<autogen::AuthenticationKeyItem> keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    keys.push_back(auth_item);

    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid12, cn1_->families(), keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid12, cn2_->families(), keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 10, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid13, cn1_->families(), keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid13, cn3_->families(), keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn3_, 10, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid23, cn2_->families(), keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid23, cn3_->families(), keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn2_, cn3_, 10, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Get one peer for each peer-pair, just to get counters.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    // Change the key on all 3 peerings.
    keys.clear();
    key_string = "second_key";
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    keys.push_back(auth_item);

    // CN1-CN2
    params = CreateBgpPeeringAttributes(uuid12, cn1_->families(), keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid12, cn2_->families(), keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    size_t count = peer12->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (count + 15), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // CN1-CN3
    params = CreateBgpPeeringAttributes(uuid13, cn1_->families(), keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid13, cn3_->families(), keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    count = peer13->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn3_, (count + 15), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // CN2-CN3
    params = CreateBgpPeeringAttributes(uuid23, cn2_->families(), keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid23, cn3_->families(), keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    count = peer23->get_rx_keepalive();
    VerifyPeers(__LINE__, cn2_, cn3_, (count + 15), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Check once more after all the connections have been verified.
    count = peer12->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);
    count = peer13->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn3_, (count + 10), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);
    count = peer23->get_rx_keepalive();
    VerifyPeers(__LINE__, cn2_, cn3_, (count + 10), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn2_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

TEST_F(BgpAuthenticationTest, 3CnsWithChangingKeys) {
    StateMachineTest::set_keepalive_time_msecs(10);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", "cn1cn2key1", 0);
    cn1_cn2_keys.push_back(auth_item);

    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid12, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid12, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 10, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "cn1cn2key1");

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    vector<autogen::AuthenticationKeyItem> cn1_cn3_keys;
    SetAutogenAuthKey(&auth_item, "cn1_cn3", "MD5", "cn1cn3key1", 0);
    cn1_cn3_keys.push_back(auth_item);

    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid13, cn1_->families(), cn1_cn3_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid13, cn3_->families(), cn1_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn3_, 10, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn1cn3key1");

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    vector<autogen::AuthenticationKeyItem> cn2_cn3_keys;
    SetAutogenAuthKey(&auth_item, "cn2_cn3", "MD5", "cn2cn3key1", 0);
    cn2_cn3_keys.push_back(auth_item);

    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    params = CreateBgpPeeringAttributes(uuid23, cn2_->families(), cn2_cn3_keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid23, cn3_->families(), cn2_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn2_, cn3_, 10, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn2cn3key1");

    // Get one peer for each peer-pair, just to get counters.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    cn1_cn2_keys.clear();
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", "cn1cn2key2", 0);
    cn1_cn2_keys.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid12, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid12, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    size_t count = peer12->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "cn1cn2key2");

    cn1_cn3_keys.clear();
    SetAutogenAuthKey(&auth_item, "cn1_cn3", "MD5", "cn1cn3key2", 0);
    cn1_cn3_keys.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid13, cn1_->families(), cn1_cn3_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid13, cn3_->families(), cn1_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    count = peer13->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn3_, (count + 10), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, "cn1cn3key2");

    cn2_cn3_keys.clear();
    SetAutogenAuthKey(&auth_item, "cn2_cn3", "MD5", "cn2cn3key2", 0);
    cn2_cn3_keys.push_back(auth_item);
    params = CreateBgpPeeringAttributes(uuid23, cn2_->families(), cn2_cn3_keys);
    cn2_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid23, cn3_->families(), cn2_cn3_keys);
    cn3_->IFMapMsgLink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    // Check that the peering came up.
    count = peer23->get_rx_keepalive();
    VerifyPeers(__LINE__, cn2_, cn3_, (count + 10), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, "cn2cn3key2");

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();

    cn2_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn3_->IFMapMsgUnlink("bgp-router", cn2_->fqn_ifmap_id(),
        "bgp-router", cn3_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

// 2 keys. Change keys so that sometimes the keys are the same and sometimes
// they are different. Check that peering is up when they are the keys are the 
// same and that peering is down when the keys are different.
TEST_F(BgpAuthenticationTest, SameDifferentMultipleKeyChanges) {
    StateMachineTest::set_keepalive_time_msecs(10);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Bring up the peers without any keys.
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), no_auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), no_auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    // Check that the peering comes up with no keys.
    VerifyPeers(__LINE__, cn1_, cn2_, 20, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add key_string1 to both CN1 and CN2. Peering should come up.
    string key_string1 = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> keys1;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string1, 0);
    keys1.push_back(auth_item);

    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), keys1);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys1);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    // Check that the peering comes up with key_string1.
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Now, change the key only on CN2 to key_string2. Peering should come down.
    string key_string2 = "somekey";
    vector<autogen::AuthenticationKeyItem> keys2;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string2, 0);
    keys2.push_back(auth_item);

    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys2);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    size_t old_p12_tr_count = peer12->get_tr_keepalive();
    size_t old_p21_tr_count = peer21->get_tr_keepalive();
    size_t old_p12_rx_count = peer12->get_rx_keepalive();
    size_t old_p21_rx_count = peer21->get_rx_keepalive();

    // The peering should come down. Note, we are not using flap_count(), since
    // its 90s by default and changing it within the test could make it flaky.
    TASK_UTIL_EXPECT_TRUE(
        peer12->get_tr_keepalive() > (old_p12_tr_count + 10));
    TASK_UTIL_EXPECT_TRUE(
        peer21->get_tr_keepalive() > (old_p21_tr_count + 10));
    TASK_UTIL_EXPECT_TRUE(peer12->get_rx_keepalive() == old_p12_rx_count);
    TASK_UTIL_EXPECT_TRUE(peer21->get_rx_keepalive() == old_p21_rx_count);

    // Change the key on CN2 back to key_string1. Peering should come up.
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys1);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Change the key on CN1 to key_string2. Peering should come down.
    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), keys2);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    old_p12_tr_count = peer12->get_tr_keepalive();
    old_p21_tr_count = peer21->get_tr_keepalive();
    old_p12_rx_count = peer12->get_rx_keepalive();
    old_p21_rx_count = peer21->get_rx_keepalive();

    // Transmit counters go up but receive counters do not.
    TASK_UTIL_EXPECT_TRUE(
        peer12->get_tr_keepalive() > (old_p12_tr_count + 10));
    TASK_UTIL_EXPECT_TRUE(
        peer21->get_tr_keepalive() > (old_p21_tr_count + 10));
    TASK_UTIL_EXPECT_TRUE(peer12->get_rx_keepalive() == old_p12_rx_count);
    TASK_UTIL_EXPECT_TRUE(peer21->get_rx_keepalive() == old_p21_rx_count);

    // Now change the key on CN2 to key_string2. Peering should come up.
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), keys2);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string2);

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

TEST_F(BgpAuthenticationTest, NoKeyToKeyWithDelay) {
    StateMachineTest::set_keepalive_time_msecs(10);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Create peering between CN1 and CN2 on both CN1 and CN2 with no keys and
    // verify that the peering comes up.
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1_->families(), no_auth_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), no_auth_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();

    VerifyPeers(__LINE__, cn1_, cn2_, 10, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add a key only to CN1. Since, we are moving from no-key to key, the
    // session will be cleared. SM should move away from Established. The
    // session should flap once.
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKey(&auth_item, "cn1_cn2", "MD5", key_string, 0);
    cn1_cn2_keys.push_back(auth_item);

    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    params = CreateBgpPeeringAttributes(uuid, cn1_->families(), cn1_cn2_keys);
    cn1_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));

    size_t old_p12_rx_count = peer12->get_rx_keepalive();
    size_t old_p21_rx_count = peer21->get_rx_keepalive();
    size_t old_p12_tr_count = peer12->get_tr_keepalive();
    size_t old_p21_tr_count = peer21->get_tr_keepalive();

    TASK_UTIL_EXPECT_TRUE(peer21->get_tr_keepalive() > (old_p21_tr_count + 20));
    TASK_UTIL_EXPECT_EQ(peer21->get_rx_keepalive(), old_p21_rx_count);

    // Now add the same key to CN2. Peering should come up.
    params = CreateBgpPeeringAttributes(uuid, cn2_->families(), cn1_cn2_keys);
    cn2_->IFMapMsgLink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering", 0, params);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 10), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    cn1_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
    cn2_->IFMapMsgUnlink("bgp-router", cn1_->fqn_ifmap_id(),
        "bgp-router", cn2_->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

static void SetBgpRouterParams(autogen::BgpRouterParams *property, int asn,
        const string &id, const string &address, int port, int hold_time,
        const vector<string> &families,
        const vector<autogen::AuthenticationKeyItem> &auth_items,
        int local_asn) {
    property->autonomous_system = asn;
    property->identifier = id;
    property->address = address;
    property->port = port;
    property->hold_time = hold_time;
    property->address_families.family = families;
    property->auth_key_chain.auth_key_items = auth_items;
    property->local_autonomous_system = local_asn;
}

static void SetAutogenAuthKey(autogen::AuthenticationKeyItem *item,
        const string &id, const string &type, const string &value,
        time_t start_time) {
    item->key_id = id;
    item->key_type = type;
    item->key = value;
    item->start_time = start_time;
}

static autogen::BgpPeeringAttributes *CreateBgpPeeringAttributes(
        const string &uuid, const vector<string> &families,
        const vector<autogen::AuthenticationKeyItem> &keys) {
    std::ostringstream config;

    config << "<bgp-peering>";
    config << "<session>";
    config << "<uuid>" + uuid + "</uuid>";
    config << "<attributes>";

    config << "<address-families>";
    for (vector<string>::const_iterator iter = families.begin();
         iter != families.end(); ++iter) {
        config << "<family>" << *iter << "</family>";
    }
    config << "</address-families>";

    config << "<auth-key-chain>";
    for (vector<autogen::AuthenticationKeyItem>::const_iterator
         iter = keys.begin(); iter != keys.end(); ++iter) {
        const autogen::AuthenticationKeyItem &auth_item = *iter;
        config << "<auth-key-items>";
        config << "<key-id>" << auth_item.key_id << "</key-id>";
        config << "<key-type>" << auth_item.key_type << "</key-type>";
        config << "<key>" << auth_item.key << "</key>";
        config << "</auth-key-items>";
    }
    config << "</auth-key-chain>";

    config << "</attributes>";
    config << "</session>";
    config << "</bgp-peering>";
    string content = config.str();

    std::istringstream sstream(content);
    pugi::xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load(sstream);
    if (!result) {
        BGP_WARN_UT("Unable to load XML document. (status="
            << result.status << ", offset=" << result.offset << ")");
        assert(0);
    }
    pugi::xml_node node = xdoc.first_child();
    autogen::BgpPeeringAttributes *params =
        new autogen::BgpPeeringAttributes();
    params->XmlParse(node);
    return params;
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

