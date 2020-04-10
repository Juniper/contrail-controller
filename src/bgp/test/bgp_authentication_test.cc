/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/assign/list_of.hpp>
#include <pugixml/pugixml.hpp>

#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"

#include "schema/bgp_schema_types.h"

using std::string;
using std::vector;

static void SetBgpRouterParams(autogen::BgpRouterParams *property, int asn,
        const string &id, const string &address, int port, int hold_time,
        const vector<string> &families,
        const autogen::AuthenticationData &auth_data, int local_asn);
static void SetAutogenAuthKeyItem(int id, const string &value,
        time_t start_time, autogen::AuthenticationKeyItem *item);
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
            const autogen::AuthenticationData &auth_data);
    void ChangeBgpRouterNode(int asn, const string &fqn_ifmap_id,
            const string &bgp_id, const string &cn_address, int port,
            int local_asn, const vector<string> &families,
            const autogen::AuthenticationData &auth_data);
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
    families_ = boost::assign::list_of("inet-vpn")
        .convert_to_container<vector<string> >();
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
}

void BgpServerAuthTestMock::AddBgpRouterNode(int asn,
        const string &fqn_ifmap_id, const string &bgp_id, const string &address,
        int port, int local_asn, const vector<string> &families,
        const autogen::AuthenticationData &auth_data) {
    autogen::BgpRouterParams *prop = new autogen::BgpRouterParams();
    SetBgpRouterParams(prop, asn, bgp_id, address, port, 90, families,
                       auth_data, asn);
    ifmap_test_util::IFMapMsgNodeAdd(db_, "bgp-router", fqn_ifmap_id, 0,
                                     "bgp-router-parameters", prop);
    task_util::WaitForIdle();
}

void BgpServerAuthTestMock::ChangeBgpRouterNode(int asn,
        const string &fqn_ifmap_id, const string &bgp_id, const string &address,
        int port, int local_asn, const vector<string> &families,
        const autogen::AuthenticationData &auth_data) {
    autogen::BgpRouterParams *prop = new autogen::BgpRouterParams();
    SetBgpRouterParams(prop, asn, bgp_id, address, port, 90, families,
                       auth_data, asn);
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
        BgpServerAuthTestMock *cn2, size_t verify_kacount, const string &id1,
        const string &id2, as_t asn1, as_t asn2, const string &key);
    void AddPeering(BgpServerAuthTestMock *cn1, BgpServerAuthTestMock *cn2,
        const string &uuid, const vector<autogen::AuthenticationKeyItem> &keys);
    void DeletePeering(BgpServerAuthTestMock *cn1, BgpServerAuthTestMock *cn2);
    void SetKeys(int id, const string &key, time_t start_time,
         vector<autogen::AuthenticationKeyItem> *keys);
    void ChangeBgpRouterNode(BgpServerAuthTestMock *cn,
        const vector<autogen::AuthenticationKeyItem> &auth_keys);

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
    autogen::AuthenticationData auth_data;
    // Add cn1, cn2, cn3 BgpRouter nodes in CN1.
    cn1_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_data);
    cn1_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_data);
    cn1_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_data);
    // Add cn1, cn2, cn3 BgpRouter nodes in CN2.
    cn2_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_data);
    cn2_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_data);
    cn2_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_data);
    // Add cn1, cn2, cn3 BgpRouter nodes in CN3.
    cn3_->AddBgpRouterNode(asn, cn1_->fqn_ifmap_id(), cn1_->bgp_id(),
            cn1_->address(), cn1_->port(), asn, cn1_->families(), auth_data);
    cn3_->AddBgpRouterNode(asn, cn2_->fqn_ifmap_id(), cn2_->bgp_id(),
            cn2_->address(), cn2_->port(), asn, cn2_->families(), auth_data);
    cn3_->AddBgpRouterNode(asn, cn3_->fqn_ifmap_id(), cn3_->bgp_id(),
            cn3_->address(), cn3_->port(), asn, cn3_->families(), auth_data);
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

void BgpAuthenticationTest::ChangeBgpRouterNode(BgpServerAuthTestMock *cn,
    const vector<autogen::AuthenticationKeyItem> &auth_keys) {

    autogen::AuthenticationData auth_data;
    auth_data.key_type = "MD5";
    auth_data.key_items = auth_keys;
    cn->ChangeBgpRouterNode(asn, cn->fqn_ifmap_id(), cn->bgp_id(),
            cn->address(), cn->port(), asn, cn->families(), auth_data);
}

void BgpAuthenticationTest::VerifyPeers(int line, BgpServerAuthTestMock *cn1,
        BgpServerAuthTestMock *cn2, size_t kacount, const string &id1,
        const string &id2, as_t asn1, as_t asn2, const string &key) {
    string message = "from line " + integerToString(line);
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
    TASK_UTIL_EXPECT_EQ_MSG(0, peer1->GetInuseAuthKeyValue().compare(key),
                            message);
    TASK_UTIL_EXPECT_EQ_MSG(0, peer2->GetInuseAuthKeyValue().compare(key),
                            message);

    // Make sure that a few keepalives are exchanged.
    if (kacount) {
        TASK_UTIL_EXPECT_GT_MSG(peer1->get_rx_keepalive(), kacount, message);
        TASK_UTIL_EXPECT_GT_MSG(peer1->get_tx_keepalive(), kacount, message);
        TASK_UTIL_EXPECT_GT_MSG(peer2->get_rx_keepalive(), kacount, message);
        TASK_UTIL_EXPECT_GT_MSG(peer2->get_tx_keepalive(), kacount, message);
    }
}

// First parameter is the CN we will add the peering on.
void BgpAuthenticationTest::AddPeering(BgpServerAuthTestMock *cn1,
        BgpServerAuthTestMock *cn2, const string &uuid,
        const vector<autogen::AuthenticationKeyItem> &keys) {
    autogen::BgpPeeringAttributes *params =
        CreateBgpPeeringAttributes(uuid, cn1->families(), keys);
    cn1->IFMapMsgLink("bgp-router", cn1->fqn_ifmap_id(),
        "bgp-router", cn2->fqn_ifmap_id(), "bgp-peering", 0, params);
    task_util::WaitForIdle();
}

// First parameter is the CN we will delete the peering from.
void BgpAuthenticationTest::DeletePeering(BgpServerAuthTestMock *cn1,
                                          BgpServerAuthTestMock *cn2) {
    cn1->IFMapMsgUnlink("bgp-router", cn1->fqn_ifmap_id(),
        "bgp-router", cn2->fqn_ifmap_id(), "bgp-peering");
    task_util::WaitForIdle();
}

void BgpAuthenticationTest::SetKeys(int id, const string &key,
        time_t start_time, vector<autogen::AuthenticationKeyItem> *keys) {
    autogen::AuthenticationKeyItem auth_item;
    SetAutogenAuthKeyItem(id, key, start_time, &auth_item);
    keys->push_back(auth_item);
}

// Test init and cleanup for this set of tests.
TEST_F(BgpAuthenticationTest, Noop) {
}

// Basic connection test with keys.
TEST_F(BgpAuthenticationTest, 2CnsWithSameKey) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);

    // Create the key and add it to both CN1 and CN2.
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, key_string, 0, &cn1_cn2_keys);

    // Create peering between CN1 and CN2 on CN1 and on CN2.
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);

    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));

    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// The 2 ends have different keys. The connection should not come up.
TEST_F(BgpAuthenticationTest, 2CnsWithDifferentKeys) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);

    // Bringup CN1 with key_string1 as key for peering CN1-CN2.
    string key_string1 = "key1";
    vector<autogen::AuthenticationKeyItem> keys1;
    SetKeys(0, key_string1, 0, &keys1);
    AddPeering(cn1_, cn2_, uuid, keys1);

    // Bringup CN2 with key_string2 as key for peering CN1-CN2.
    string key_string2 = "key2";
    vector<autogen::AuthenticationKeyItem> keys2;
    SetKeys(0, key_string2, 0, &keys2);
    AddPeering(cn2_, cn1_, uuid, keys2);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // The keys are different. The connection should not come up.
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_GT(peer12->get_connect_timer_expired(), 5U);
    TASK_UTIL_EXPECT_GT(peer21->get_connect_timer_expired(), 5U);

    // Change key on CN2 to key_string3 as key for peering CN1-CN2.
    keys2.clear();
    string key_string3 = "key3";
    SetKeys(0, key_string3, 0, &keys2);
    AddPeering(cn2_, cn1_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string3));

    // The keys are still different. The connection should still not come up.
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Change key on CN1 to key_string2 as key for peering CN1-CN2.
    keys1.clear();
    SetKeys(0, key_string2, 0, &keys1);
    AddPeering(cn2_, cn1_, uuid, keys1);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));

    // The keys are still different. The connection should still not come up.
    old_cn1_ct_count = peer12->get_connect_timer_expired();
    old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Change keys multiple times but with the same keys for all peers.
TEST_F(BgpAuthenticationTest, 2CnsWithChangingSameKeys) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Bring up the peers without any keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add cn1cn2key1 as the key on both CN1 and CN2. Peering should come up.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, key_string, 0, &cn1_cn2_keys);
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the key to cn1cn2key2 on both CN1 and CN2. Peering should come up.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    cn1_cn2_keys.clear();
    key_string = "cn1cn2key2";
    SetKeys(0, key_string, 0, &cn1_cn2_keys);
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the key to cn1cn2key3 on both CN1 and CN2. Peering should come up.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    cn1_cn2_keys.clear();
    key_string = "cn1cn2key3";
    SetKeys(0, key_string, 0, &cn1_cn2_keys);
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the key to nothing on both CN1 and CN2. Peering should come up.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(""));

    // Check that the peering came up.
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "");

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Three peerings: CN1-CN2, CN2-CN3, CN1-CN3, all with the same key.
TEST_F(BgpAuthenticationTest, 3CnsWithSameKey) {
    StateMachineTest::set_keepalive_time_msecs(100);

    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> auth_keys;
    SetKeys(0, key_string, 0, &auth_keys);
    // Create peering between CN1 and CN2 on both CN1 and CN2.
    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    AddPeering(cn1_, cn2_, uuid12, auth_keys);
    AddPeering(cn2_, cn1_, uuid12, auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    AddPeering(cn1_, cn3_, uuid13, auth_keys);
    AddPeering(cn3_, cn1_, uuid13, auth_keys);
    VerifyPeers(__LINE__, cn1_, cn3_, 5, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    AddPeering(cn2_, cn3_, uuid23, auth_keys);
    AddPeering(cn3_, cn2_, uuid23, auth_keys);
    VerifyPeers(__LINE__, cn2_, cn3_, 5, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    // Verify all peerings are still up.
    uint32_t check_count = peer12->get_rx_keepalive() + 5;
    VerifyPeers(__LINE__, cn1_, cn2_, check_count, cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);
    check_count = peer13->get_rx_keepalive() + 5;
    VerifyPeers(__LINE__, cn1_, cn3_, check_count, cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);
    check_count = peer23->get_rx_keepalive() + 5;
    VerifyPeers(__LINE__, cn2_, cn3_, check_count, cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);

    DeletePeering(cn1_, cn3_);
    DeletePeering(cn3_, cn1_);

    DeletePeering(cn2_, cn3_);
    DeletePeering(cn3_, cn2_);
}

// 3 pairs of peers. Each pair has a different key.
TEST_F(BgpAuthenticationTest, 3CnsWithDifferentKeyPairs) {
    StateMachineTest::set_keepalive_time_msecs(100);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, "cn1cn2key1", 0, &cn1_cn2_keys);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "cn1cn2key1");

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    vector<autogen::AuthenticationKeyItem> cn1_cn3_keys;
    SetKeys(0, "cn1cn3key1", 0, &cn1_cn3_keys);
    uuid = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    AddPeering(cn1_, cn3_, uuid, cn1_cn3_keys);
    AddPeering(cn3_, cn1_, uuid, cn1_cn3_keys);
    VerifyPeers(__LINE__, cn1_, cn3_, 5, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn1cn3key1");

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    vector<autogen::AuthenticationKeyItem> cn2_cn3_keys;
    SetKeys(0, "cn2cn3key1", 0, &cn2_cn3_keys);
    uuid = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    AddPeering(cn2_, cn3_, uuid, cn2_cn3_keys);
    AddPeering(cn3_, cn2_, uuid, cn2_cn3_keys);
    VerifyPeers(__LINE__, cn2_, cn3_, 5, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn2cn3key1");

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);

    DeletePeering(cn1_, cn3_);
    DeletePeering(cn3_, cn1_);

    DeletePeering(cn2_, cn3_);
    DeletePeering(cn3_, cn2_);
}

// Set first key on all 3 peer pairs. Verify. Then change the key on 3 peer
// pairs with a second key. Verify again.
TEST_F(BgpAuthenticationTest, 3CnsWithChangingSameKeys) {
    StateMachineTest::set_keepalive_time_msecs(100);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    string key_string = "first_key";
    vector<autogen::AuthenticationKeyItem> keys;
    SetKeys(0, key_string, 0, &keys);
    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    AddPeering(cn1_, cn2_, uuid12, keys);
    AddPeering(cn2_, cn1_, uuid12, keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    AddPeering(cn1_, cn3_, uuid13, keys);
    AddPeering(cn3_, cn1_, uuid13, keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn3_, 5, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    AddPeering(cn2_, cn3_, uuid23, keys);
    AddPeering(cn3_, cn2_, uuid23, keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn2_, cn3_, 5, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, key_string);

    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer32 = cn3_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn1_->FindPeerByUuid(__LINE__, uuid13);
    BgpPeer *peer31 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    // Change the key on all 3 peerings.
    keys.clear();
    key_string = "second_key";
    SetKeys(0, key_string, 0, &keys);

    // CN1-CN2
    uint32_t fc1 = peer12->flap_count();
    uint32_t fc2 = peer21->flap_count();
    AddPeering(cn1_, cn2_, uuid12, keys);
    AddPeering(cn2_, cn1_, uuid12, keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), fc2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // CN1-CN3
    fc1 = peer13->flap_count();
    fc2 = peer31->flap_count();
    AddPeering(cn1_, cn3_, uuid13, keys);
    AddPeering(cn3_, cn1_, uuid13, keys);
    TASK_UTIL_EXPECT_EQ(0, peer13->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer31->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer13->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer31->flap_count(), fc2);
    check_count =
        peer13->get_rx_keepalive() > peer31->get_rx_keepalive() ?
        peer13->get_rx_keepalive() : peer31->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn3_, (check_count + 5), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // CN2-CN3
    fc1 = peer23->flap_count();
    fc2 = peer32->flap_count();
    AddPeering(cn2_, cn3_, uuid23, keys);
    AddPeering(cn3_, cn2_, uuid23, keys);
    TASK_UTIL_EXPECT_EQ(0, peer23->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer32->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer23->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer32->flap_count(), fc2);
    check_count =
        peer23->get_rx_keepalive() > peer32->get_rx_keepalive() ?
        peer23->get_rx_keepalive() : peer32->get_rx_keepalive();
    VerifyPeers(__LINE__, cn2_, cn3_, (check_count + 5), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Check once more after all the connections have been verified.
    check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);
    check_count =
        peer13->get_rx_keepalive() > peer31->get_rx_keepalive() ?
        peer13->get_rx_keepalive() : peer31->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn3_, (check_count + 5), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);
    check_count =
        peer23->get_rx_keepalive() > peer32->get_rx_keepalive() ?
        peer23->get_rx_keepalive() : peer32->get_rx_keepalive();
    VerifyPeers(__LINE__, cn2_, cn3_, (check_count + 5), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);

    DeletePeering(cn1_, cn3_);
    DeletePeering(cn3_, cn1_);

    DeletePeering(cn2_, cn3_);
    DeletePeering(cn3_, cn2_);
}

// Create 3 peer pairs, each with its own separate first-key. Verify. Then
// change the key on each peer pair, each with its own separate second-key.
TEST_F(BgpAuthenticationTest, 3CnsWithChangingKeys) {
    StateMachineTest::set_keepalive_time_msecs(100);

    // Create peering between CN1 and CN2 on both CN1 and CN2.
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, "cn1cn2key1", 0, &cn1_cn2_keys);
    string uuid12 = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    AddPeering(cn1_, cn2_, uuid12, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid12, cn1_cn2_keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "cn1cn2key1");

    // Create peering between CN1 and CN3 on both CN1 and CN3.
    vector<autogen::AuthenticationKeyItem> cn1_cn3_keys;
    SetKeys(0, "cn1cn3key1", 0, &cn1_cn3_keys);
    string uuid13 = BgpConfigParser::session_uuid("CN1", "CN3", 0);
    AddPeering(cn1_, cn3_, uuid13, cn1_cn3_keys);
    AddPeering(cn3_, cn1_, uuid13, cn1_cn3_keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn3_, 5, cn1_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn1cn3key1");

    // Create peering between CN2 and CN3 on both CN2 and CN3.
    vector<autogen::AuthenticationKeyItem> cn2_cn3_keys;
    SetKeys(0, "cn2cn3key1", 0, &cn2_cn3_keys);
    string uuid23 = BgpConfigParser::session_uuid("CN2", "CN3", 0);
    AddPeering(cn2_, cn3_, uuid23, cn2_cn3_keys);
    AddPeering(cn3_, cn2_, uuid23, cn2_cn3_keys);
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn2_, cn3_, 5, cn2_->ifmap_id(), cn3_->ifmap_id(),
                asn, asn, "cn2cn3key1");

    // Get one peer for each peer-pair, just to get counters.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid12);
    BgpPeer *peer23 = cn2_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer32 = cn3_->FindPeerByUuid(__LINE__, uuid23);
    BgpPeer *peer13 = cn3_->FindPeerByUuid(__LINE__, uuid13);
    BgpPeer *peer31 = cn3_->FindPeerByUuid(__LINE__, uuid13);

    // Set a new key on both CN1 and CN2.
    uint32_t fc1 = peer12->flap_count();
    uint32_t fc2 = peer21->flap_count();
    cn1_cn2_keys.clear();
    SetKeys(0, "cn1cn2key2", 0, &cn1_cn2_keys);
    AddPeering(cn1_, cn2_, uuid12, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid12, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare("cn1cn2key2"));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare("cn1cn2key2"));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), fc2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "cn1cn2key2");

    // Set a new key on both CN1 and CN3.
    fc1 = peer13->flap_count();
    fc2 = peer31->flap_count();
    cn1_cn3_keys.clear();
    SetKeys(0, "cn1cn3key2", 0, &cn1_cn3_keys);
    AddPeering(cn1_, cn3_, uuid13, cn1_cn3_keys);
    AddPeering(cn3_, cn1_, uuid13, cn1_cn3_keys);
    TASK_UTIL_EXPECT_EQ(0, peer13->GetInuseAuthKeyValue().compare("cn1cn3key2"));
    TASK_UTIL_EXPECT_EQ(0, peer31->GetInuseAuthKeyValue().compare("cn1cn3key2"));
    TASK_UTIL_EXPECT_GT(peer13->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer31->flap_count(), fc2);
    check_count =
        peer13->get_rx_keepalive() > peer31->get_rx_keepalive() ?
        peer13->get_rx_keepalive() : peer31->get_rx_keepalive();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn1_, cn3_, (check_count + 5), cn1_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, "cn1cn3key2");

    // Set a new key on both CN2 and CN3.
    fc1 = peer23->flap_count();
    fc2 = peer32->flap_count();
    cn2_cn3_keys.clear();
    SetKeys(0, "cn2cn3key2", 0, &cn2_cn3_keys);
    AddPeering(cn2_, cn3_, uuid23, cn2_cn3_keys);
    AddPeering(cn3_, cn2_, uuid23, cn2_cn3_keys);
    TASK_UTIL_EXPECT_EQ(0, peer23->GetInuseAuthKeyValue().compare("cn2cn3key2"));
    TASK_UTIL_EXPECT_EQ(0, peer32->GetInuseAuthKeyValue().compare("cn2cn3key2"));
    TASK_UTIL_EXPECT_GT(peer23->flap_count(), fc1);
    TASK_UTIL_EXPECT_GT(peer32->flap_count(), fc2);
    check_count =
        peer23->get_rx_keepalive() > peer32->get_rx_keepalive() ?
        peer23->get_rx_keepalive() : peer32->get_rx_keepalive();
    // Check that the peering came up.
    VerifyPeers(__LINE__, cn2_, cn3_, (check_count + 5), cn2_->ifmap_id(),
                cn3_->ifmap_id(), asn, asn, "cn2cn3key2");

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);

    DeletePeering(cn1_, cn3_);
    DeletePeering(cn3_, cn1_);

    DeletePeering(cn2_, cn3_);
    DeletePeering(cn3_, cn2_);
}

// 2 keys. Change keys so that sometimes the keys are the same and sometimes
// they are different. Check that peering is up when they are the keys are the
// same and that peering is degraded when the keys are different.
TEST_F(BgpAuthenticationTest, SameDifferentMultipleKeyChanges) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Bring up the peers without any keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    // Check that the peering comes up with no keys.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add key_string1 to both CN1 and CN2. Peering should come up.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    string key_string1 = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> keys1;
    SetKeys(0, key_string1, 0, &keys1);
    AddPeering(cn1_, cn2_, uuid, keys1);
    AddPeering(cn2_, cn1_, uuid, keys1);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Now, change the key only on CN2 to key_string2. Peering should come down.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    string key_string2 = "somekey";
    vector<autogen::AuthenticationKeyItem> keys2;
    SetKeys(0, key_string2, 0, &keys2);
    AddPeering(cn2_, cn1_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Change the key on CN2 back to key_string1. Peering should come up.
    AddPeering(cn2_, cn1_, uuid, keys1);
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string1));
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Change the key on CN1 to key_string2. Peering should come down.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    AddPeering(cn1_, cn2_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    old_cn1_ct_count = peer12->get_connect_timer_expired();
    old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Now change the key on CN2 to key_string2. Peering should come up.
    AddPeering(cn2_, cn1_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string2);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

TEST_F(BgpAuthenticationTest, NoKeyToKeyWithDelay) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Create peering between CN1 and CN2 on both CN1 and CN2 with no keys and
    // verify that the peering comes up.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add a key only to CN1.
    string key_string = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, key_string, 0, &cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Now add the same key to CN2. Peering should come up.
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Add md5 keys to the BgpRouter nodes on both control-nodes and no keys on the
// peering links. The peering should come up with the key in the BgpRouter node.
TEST_F(BgpAuthenticationTest, AddRouterKey) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    string key_string = "somekey";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);

    // Add the same key on the BgpRouter nodes of both CN1 and CN2.
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);

    // Check that the peering came up with the key_string since the peering
    // does not have any key.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Add a key to the BgpRouter node on CN1 and CN2. Then change the keys a few
// times on both the sides with the same key.
TEST_F(BgpAuthenticationTest, MultipleRouterKeyChanges) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Add the same key on the BgpRouter nodes of both CN1 and CN2.
    string key_string = "somekey";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);

    // Check that the peering came up with the key_string since the peering
    // does not have any key.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Change the router key on both sides and verify that the peers come up.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    router_keys.clear();
    key_string = "newkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the router key on both sides and verify that the peers come up.
    router_keys.clear();
    key_string = "onemorenewkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Let the peering come up with no key. Then change keys on both sides multiple
// times on the BgpRouter node.
TEST_F(BgpAuthenticationTest, NoKeyToRouterKey) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add the same key on the BgpRouter nodes of both CN1 and CN2.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    string key_string = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the router key on both sides and verify that the peers come up.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    router_keys.clear();
    key_string = "newkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the router key again but only on CN1.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    router_keys.clear();
    key_string = "onemorenewkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Change the router key on CN2 with the latest key. Peering should come up.
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Add key to CN1's BgpRouter node. Add peering. Connection should not come up.
// Then add key to CN2's BgpRouter node. Now, connection should come up.
TEST_F(BgpAuthenticationTest, RouterKeyWithDelay) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    string key_string = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);

    // Add key only on the BgpRouter node of CN1.
    ChangeBgpRouterNode(cn1_, router_keys);

    // Create peering between CN1 and CN2 on both CN1 and CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // The connection should not come up since CN1 has a key and CN2 does not.
    size_t old_cn1_ct_count = peer12->get_connect_timer_expired();
    size_t old_cn2_ct_count = peer21->get_connect_timer_expired();
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(""));
    TASK_UTIL_EXPECT_GT(
            peer12->get_connect_timer_expired(), (old_cn1_ct_count + 5));
    TASK_UTIL_EXPECT_GT(
            peer21->get_connect_timer_expired(), (old_cn2_ct_count + 5));

    // Now add the same key on the BgpRouter nodes of CN2 too. Peering should
    // come up.
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Configure different keys on both the peering node and the bgp-router node.
// Verify that the peering comes up with the peering key.
TEST_F(BgpAuthenticationTest, PeeringCfgOverrideRouterCfg) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);

    // Add a key on the BgpRouter node of both CN1 and CN2.
    string rkey = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, rkey, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);

    // Create peering between CN1 and CN2 with a different key on CN1 and CN2.
    string pkey = "peering_key";
    vector<autogen::AuthenticationKeyItem> cn1_cn2_keys;
    SetKeys(0, pkey, 0, &cn1_cn2_keys);
    AddPeering(cn1_, cn2_, uuid, cn1_cn2_keys);
    AddPeering(cn2_, cn1_, uuid, cn1_cn2_keys);

    // Check that the peering came up with the pkey since it overrides the
    // BgpRouter key.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, pkey);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Same key added to BgpRouter node on CN1 and peering node on CN2.
TEST_F(BgpAuthenticationTest, RouterKeyAndPeeringKey) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    string key_string = "md5password";
    vector<autogen::AuthenticationKeyItem> keys;
    SetKeys(0, key_string, 0, &keys);

    // Add key_string on CN1's BgpRouter node.
    ChangeBgpRouterNode(cn1_, keys);

    // Create peering between CN1 and CN2 on CN1 and CN2. Add key_string only
    // on CN2. Peering should come up.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, key_string);

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Change the key on CN1's BgpRouter node and CN2's peering node.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    keys.clear();
    key_string = "newpassword";
    SetKeys(0, key_string, 0, &keys);
    ChangeBgpRouterNode(cn1_, keys);
    AddPeering(cn2_, cn1_, uuid, keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

TEST_F(BgpAuthenticationTest, KeyToNokey) {
    StateMachineTest::set_keepalive_time_msecs(100);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    string router_key = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, router_key, 0, &router_keys);

    // Add the same key on the BgpRouter nodes of both CN1 and CN2.
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(router_key));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(router_key));

    // Check that the peering came up with the router_key since the peering
    // does not have any key.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, router_key);

    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    ChangeBgpRouterNode(cn1_, no_auth_keys);
    ChangeBgpRouterNode(cn2_, no_auth_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(""));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "");

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

// Bring up peers with no keys. Add the key on first peer but not the second.
// Keepalives should stop being received. Then, add the key on the second peer.
// The peering should come up. Then remove the key from both sides. The session
// should stay up.
TEST_F(BgpAuthenticationTest, RouterParamNokeyToKeyToNokeyWithDelay) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();

    // Add a key only to CN1.
    string key_string = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    ChangeBgpRouterNode(cn1_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);

    // Now add the same key to CN2. Peering should come up.
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Remove the keys from both ends.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    ChangeBgpRouterNode(cn1_, no_auth_keys);
    ChangeBgpRouterNode(cn2_, no_auth_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(""));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(""));
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, "");

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

TEST_F(BgpAuthenticationTest, SameDifferentMultipleKeyChangesWithFlaps) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Bring up the peers without any keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    // Check that the peering comes up with no keys.
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();

    // Add key_string1 to both CN1 and CN2. Peering should come up.
    string key_string1 = "cn1cn2key1";
    vector<autogen::AuthenticationKeyItem> keys1;
    SetKeys(0, key_string1, 0, &keys1);
    AddPeering(cn1_, cn2_, uuid, keys1);
    AddPeering(cn2_, cn1_, uuid, keys1);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Now, change the key only on CN2 to key_string2. Peering should come down.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    string key_string2 = "somekey";
    vector<autogen::AuthenticationKeyItem> keys2;
    SetKeys(0, key_string2, 0, &keys2);
    AddPeering(cn2_, cn1_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);

    // Change the key on CN2 back to key_string1. Peering should come up.
    AddPeering(cn2_, cn1_, uuid, keys1);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string1));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string1);

    // Change the key on CN1 to key_string2. Peering should come down.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    AddPeering(cn1_, cn2_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);

    // Now change the key on CN2 to key_string2. Peering should come up.
    AddPeering(cn2_, cn1_, uuid, keys2);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string2));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string2);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

TEST_F(BgpAuthenticationTest, NoKeyToRouterKeyWithFlaps) {
    StateMachineTest::set_keepalive_time_msecs(100);
    StateMachineTest::set_hold_time_msecs(1000);
    string uuid = BgpConfigParser::session_uuid("CN1", "CN2", 0);
    vector<autogen::AuthenticationKeyItem> no_auth_keys;

    // Create peering between CN1 and CN2 on CN1 and on CN2 with no keys.
    AddPeering(cn1_, cn2_, uuid, no_auth_keys);
    AddPeering(cn2_, cn1_, uuid, no_auth_keys);
    VerifyPeers(__LINE__, cn1_, cn2_, 5, cn1_->ifmap_id(), cn2_->ifmap_id(),
                asn, asn, "");

    // Get the peers.
    BgpPeer *peer12 = cn1_->FindPeerByUuid(__LINE__, uuid);
    BgpPeer *peer21 = cn2_->FindPeerByUuid(__LINE__, uuid);

    // Add the same key on the BgpRouter nodes of both CN1 and CN2.
    uint32_t flap_count1 = peer12->flap_count();
    uint32_t flap_count2 = peer21->flap_count();
    string key_string = "router_key";
    vector<autogen::AuthenticationKeyItem> router_keys;
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    uint32_t check_count =
        peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the router key on both sides and verify that the peers come up.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    router_keys.clear();
    key_string = "newkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Change the router key again but only on CN1.
    flap_count1 = peer12->flap_count();
    flap_count2 = peer21->flap_count();
    router_keys.clear();
    key_string = "onemorenewkey";
    SetKeys(0, key_string, 0, &router_keys);
    ChangeBgpRouterNode(cn1_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer12->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_GT(peer12->flap_count(), flap_count1);
    TASK_UTIL_EXPECT_GT(peer21->flap_count(), flap_count2);

    // Change the router key on CN2 with the latest key. Peering should come up.
    ChangeBgpRouterNode(cn2_, router_keys);
    TASK_UTIL_EXPECT_EQ(0, peer21->GetInuseAuthKeyValue().compare(key_string));
    TASK_UTIL_EXPECT_EQ(peer12->GetState(), StateMachine::ESTABLISHED);
    TASK_UTIL_EXPECT_EQ(peer21->GetState(), StateMachine::ESTABLISHED);
    check_count = peer12->get_rx_keepalive() > peer21->get_rx_keepalive() ?
        peer12->get_rx_keepalive() : peer21->get_rx_keepalive();
    VerifyPeers(__LINE__, cn1_, cn2_, (check_count + 5), cn1_->ifmap_id(),
                cn2_->ifmap_id(), asn, asn, key_string);

    // Cleanup the added peerings.
    DeletePeering(cn1_, cn2_);
    DeletePeering(cn2_, cn1_);
}

static void SetBgpRouterParams(autogen::BgpRouterParams *property, int asn,
        const string &id, const string &address, int port, int hold_time,
        const vector<string> &families,
        const autogen::AuthenticationData &auth_data, int local_asn) {
    property->autonomous_system = asn;
    property->identifier = id;
    property->address = address;
    property->port = port;
    property->hold_time = hold_time;
    property->address_families.family = families;
    property->auth_data = auth_data;
    property->local_autonomous_system = local_asn;
}

static void SetAutogenAuthKeyItem(int id, const string &value,
        time_t start_time, autogen::AuthenticationKeyItem *item) {
    item->key_id = id;
    item->key = value;
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

    config << "<auth-data>";
    config << "<key-type>MD5</key-type>";
    for (vector<autogen::AuthenticationKeyItem>::const_iterator
         iter = keys.begin(); iter != keys.end(); ++iter) {
        const autogen::AuthenticationKeyItem &auth_item = *iter;
        config << "<key-items>";
        config << "<key-id>" << auth_item.key_id << "</key-id>";
        config << "<key>" << auth_item.key << "</key>";
        config << "</key-items>";
    }
    config << "</auth-data>";

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

