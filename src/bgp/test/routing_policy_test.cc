/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy.h"

#include <fstream>

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/routing-policy/routing_policy_action.h"
#include "bgp/routing-policy/routing_policy_match.h"
#include "bgp/routing-policy/routing_policy_types.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "net/community_type.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

template <typename T1, typename T2>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix> Inet6Definition;

class BgpPeerMock : public IPeer {
public:
    BgpPeerMock(const Ip4Address &address)
        : address_(address),
          address_str_(address.to_string()) {
    }
    virtual ~BgpPeerMock() { }
    virtual const std::string &ToString() const { return address_str_; }
    virtual const std::string &ToUVEKey() const { return address_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() {
        return NULL;
    }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() {
        return NULL;
    }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() {
        return NULL;
    }
    virtual const IPeerDebugStats *peer_stats() const {
        return NULL;
    }
    virtual bool IsReady() const {
        return true;
    }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close(bool graceful) { }
    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const {
        return "";
    }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual bool IsRegistrationRequired() const { return true; }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    Ip4Address address_;
    std::string address_str_;
};

#define VERIFY_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)

static const char *bgp_server_config = "\
<config>\
    <bgp-router name=\'localhost\'>\
        <identifier>192.168.0.100</identifier>\
        <address>192.168.0.100</address>\
        <autonomous-system>64496</autonomous-system>\
    </bgp-router>\
</config>\
";

class RoutingPolicyTest : public ::testing::Test {
protected:
    RoutingPolicyTest()
      : config_db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        bgp_server_(new BgpServer(&evm_)),
        parser_(&config_db_) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~RoutingPolicyTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        BgpIfmapConfigManager *config_manager =
                static_cast<BgpIfmapConfigManager *>(
                    bgp_server_->config_manager());
        config_manager->Initialize(&config_db_, &config_graph_, "localhost");
        bgp_server_->rtarget_group_mgr()->Initialize();
        BgpConfigParser bgp_parser(&config_db_);
        bgp_parser.Parse(bgp_server_config);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        bgp_util::NetworkConfigGenerate(&config_db_, instance_names,
                                        connections);
    }


    void DeleteRoutingInstance(const string &instance_name, const string &rt_name) {
        ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", instance_name,
            "virtual-network", instance_name, "virtual-network-routing-instance");
        ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", instance_name,
            "route-target", rt_name, "instance-target");
        ifmap_test_util::IFMapMsgNodeDelete(
            &config_db_, "virtual-network", instance_name);
        ifmap_test_util::IFMapMsgNodeDelete(
            &config_db_, "routing-instance", instance_name);
        ifmap_test_util::IFMapMsgNodeDelete(
            &config_db_, "route-target", rt_name);
        task_util::WaitForIdle();
    }

    void VerifyTableNoExists(const string &table_name) {
        TASK_UTIL_EXPECT_TRUE(
            bgp_server_->database()->FindTable(table_name) == NULL);
    }

    template<typename T>
    void AddRoute(IPeer *peer, const string &table_name,
                  const string &prefix, int localpref,
                  const vector<string> &community_list = vector<string>()) {
        typedef typename T::TableT TableT;
        typedef typename T::PrefixT PrefixT;
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));
        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());
        CommunitySpec spec;
        if (!community_list.empty()) {
            BOOST_FOREACH(string comm, community_list) {
                boost::system::error_code error;
                uint32_t community =
                    CommunityType::CommunityFromString(comm, &error);
                spec.communities.push_back(community);
            }
            attr_spec.push_back(&spec);
        }

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        BgpTable *table = static_cast<BgpTable *>
            (bgp_server_->database()->FindTable(table_name));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    template<typename T>
    void DeleteRoute(IPeer *peer, const string &table_name,
                     const string &prefix) {
        typedef typename T::TableT TableT;
        typedef typename T::PrefixT PrefixT;
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        BgpTable *table = static_cast<BgpTable *>
            (bgp_server_->database()->FindTable(table_name));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    int RouteCount(const string &table_name) const {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(table_name));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    template<typename T>
    BgpRoute *RouteLookup(const string &table_name, const string &prefix) {
        typedef typename T::TableT TableT;
        typedef typename T::PrefixT PrefixT;
        BgpTable *table = dynamic_cast<TableT *>(
            bgp_server_->database()->FindTable(table_name));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    vector<string> GetOriginalCommunityListFromRoute(const BgpPath *path) {
        const Community *comm = path->GetOriginalAttr()->community();
        if (comm == NULL) return vector<string>();
        vector<string> list;
        BOOST_FOREACH(uint32_t community, comm->communities()) {
            list.push_back(CommunityType::CommunityToString(community));
        }
        sort(list.begin(), list.end());
        return list;
    }

    vector<string> GetCommunityListFromRoute(const BgpPath *path) {
        const Community *comm = path->GetAttr()->community();
        if (comm == NULL) return vector<string>();
        vector<string> list;
        BOOST_FOREACH(uint32_t community, comm->communities()) {
            list.push_back(CommunityType::CommunityToString(community));
        }
        sort(list.begin(), list.end());
        return list;
    }

    const RoutingPolicy *FindRoutingPolicy(const string &policy_name) {
        return bgp_server_->routing_policy_mgr()->GetRoutingPolicy(policy_name);
    }

    const RoutingInstance *FindRoutingInstance(const string &inst) {
        return bgp_server_->routing_instance_mgr()->GetRoutingInstance(inst);
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    vector<BgpPeerMock *> peers_;
    BgpConfigParser parser_;
};

TEST_F(RoutingPolicyTest, PolicyPrefixMatchUpdateLocalPref) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyMultiplePrefixMatchUpdateLocalPref) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "10.0.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "20.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "30.1.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    rt = RouteLookup<InetDefinition>("test.inet.0", "20.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    rt = RouteLookup<InetDefinition>("test.inet.0", "30.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 100);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "20.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "30.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyProtocolMatchUpdateLocalPref) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyNoMatchUpdateMed) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_1b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                                   "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_med = orig_attr->med();
    uint32_t policy_med = attr->med();
    ASSERT_TRUE(policy_med == 1234);
    ASSERT_TRUE(original_med == 0);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");
}


TEST_F(RoutingPolicyTest, PolicyCommunityMatchReject) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "10.1.1.1/32",
                        100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    EXPECT_EQ(rt->BestPath()->GetFlags() & BgpPath::RoutingPolicyReject,
              BgpPath::RoutingPolicyReject);
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchAddCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32",
                        100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:44"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchSetCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              vector<string>());
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveMultipleCommunity_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:22")("11:44")("22:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("11:44")("22:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

// Input route has no community to begin with
TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveMultipleCommunity_2) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), vector<string>());
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              vector<string>());
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchAddMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
        list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

// Route has not community to start with
TEST_F(RoutingPolicyTest, PolicyPrefixMatchAddMultipleCommunity_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
        list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              vector<string>());
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}


TEST_F(RoutingPolicyTest, PolicyPrefixMatchSetMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_MultipleCommunityAction) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2g.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("22:44"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:44"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.0.0.0/8", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.0.0.0/8");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.0.0/16", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.0.0/16");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.0.0/16");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_Longer) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.0.0.0/8", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.0.0.0/8");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_Longer_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.0/24", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.0/24");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.0/24");
}
//
// Test the match for exact prefix match
// Route is added with non matching prefix and ensure policy action is not taken
//
TEST_F(RoutingPolicyTest, PolicyPrefixMatch_Exact_NoMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "2.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.1.1.1/32");
}

//
// Test the match for exact prefix match
// Route is added with matching prefix and ensure policy action is not taken
//
TEST_F(RoutingPolicyTest, PolicyPrefixMatch_Exact_Match) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

//
// Test the match for exact prefix match (non /32)
// Route is added with matching prefix and ensure policy action is not taken
//
TEST_F(RoutingPolicyTest, PolicyPrefixMatch_Exact_Match_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.0.0/16", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.0.0/16");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.0.0/16");
}

//
// Test multiple match policy term
// Route is added which matches multiple policy term
// Ensure that all policy action is taken
//
TEST_F(RoutingPolicyTest, PolicyMultipleMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_6.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32",
                        100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

//
// Test multiple match policy term
// Route is added which matches the first policy term. Action of the first term
// add community that matches the second term. Action of second term adds
// community that is matched in third term
// Ensure that all policy action is taken
//
TEST_F(RoutingPolicyTest, PolicyMultipleMatch_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_6a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32",
                        100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

//
// Test two policy term
// Route is added which matches the first policy term. Action of the first term
// add community that matches the second term. Action of second term drops the
// route
// Ensure that path is rejected
//
TEST_F(RoutingPolicyTest, PolicyMultipleMatch_2) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_6b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32",
                        100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

//
// Policy match on ipv6
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// Policy match on ipv6 with v4 policies
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6_V4PolicyMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// Policy with both ipv6 & v4 matches
// Add inet and inet6 route and validate policy
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6Inet4PolicyMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0",
                  "1.1.1.1/32", 100, list_of("11:13"));

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");

    rt = RouteLookup<InetDefinition>("test.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
}

//
// Policy with two match conditions in a single term
// Add inet6 route which matches both condition and validate policy
// Policy action is applied
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6_MultipleMatches) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// Policy with two match conditions in a single term
// Add inet6 route which matches only one condition and validate policy
// Policy action is not applied
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6_MultipleMatches_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Prefix Match .. community no match
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("99:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("99:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("99:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// Policy with two match condition ipv6 in a single term
// Add inet6 route which matches no condition and validate policy
// Policy action is not applied
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6_MultipleMatches_2) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db7:85a3::8a2e:370:7334/128", 100, list_of("99:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db7:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("99:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("99:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db7:85a3::8a2e:370:7334/128");
}

//
// Policy with two match conditions and two actions in single term
// Add inet6 route which matches both condition and validate policy
// Policy action is applied
//
TEST_F(RoutingPolicyTest, PolicyMatch_Inet6_MultipleMatches_MultipleActions) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 999);
    ASSERT_TRUE(original_local_pref == 100);
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatchV6) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchFirstPolicy) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchSecondPolicy) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchFirsTerminal_Reject) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchSecondTerminal_Accept) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchFirsTerminal_Accept) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchSecond_Reject) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13")("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, MultiplePolicies_MatchBoth) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13")("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("22:13")("22:44"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test policy is attached after the route is added
//
TEST_F(RoutingPolicyTest, PolicyUpdate) {
    // Create the RI and routing policy. Association is skipped here
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_8a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_8b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test, new policy is attached to a routing instance which already
// has another routing policy attached
//
TEST_F(RoutingPolicyTest, PolicyUpdate_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_7g.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("44:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    // Remove the attached policy and verify
    content = FileRead("controller/src/bgp/testdata/routing_policy_8b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("44:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}


//
// In this test, routing policy action is updated
// Previously, action was to add a new community. With the update of the policy
// the new action is only accept.
//
TEST_F(RoutingPolicyTest, PolicyUpdate_2) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test, routing policy action is updated
// With the update of the policy, the route is rejected by matching the newly
// added term
//
TEST_F(RoutingPolicyTest, PolicyUpdate_3) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test, routing policy action is updated
// With the update of the policy, the route which was previously rejected will
// be accepted as there is no match to any term
//
TEST_F(RoutingPolicyTest, PolicyUpdate_4) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    content = FileRead("controller/src/bgp/testdata/routing_policy_0b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();


    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test routing policy match condition is updated for prefix match
// Previously matching prefix 10.1.1.1/32 is updated to 10.1.1.5/32
//
TEST_F(RoutingPolicyTest, PolicyUpdate_5) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32",
                        100, list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    content = FileRead("controller/src/bgp/testdata/routing_policy_0j.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == original_local_pref);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.5/32",
                        100, list_of("22:13"));

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "10.0.1.5/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.5/32");
}

//
// In this test routing policy match condition is updated for community match
// Previously community was matched for 22:13 and updated policy matches for
// 11:13
//
TEST_F(RoutingPolicyTest, PolicyUpdate_6) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0i.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "10.1.1.1/32",
                        100, list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);

    content = FileRead("controller/src/bgp/testdata/routing_policy_0c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "10.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.1.1.1/32");
}

//
// In this test routing policy match condition is updated for prefix match
// Previously matching prefix [10.0.1.1/32] is updated to
// [10.0.1.1/32, 20.1.1.0/24].
// Subsequently the policy match is updated back to [10.0.1.1/32]
//
TEST_F(RoutingPolicyTest, PolicyUpdate_7) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32",
                        100, list_of("22:13"));
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "20.1.1.1/32",
                        100, list_of("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    //
    // Verify that the policy matched 10.0.1.1/32 and the local-pref is updated
    // Policy doesn't match 20.1.1.1/32.
    //
    BgpRoute *rt =
        RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    rt = RouteLookup<InetDefinition>("test.inet.0", "20.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == original_local_pref);

    //
    // Update the routing policy match to include 20.1.1.0/24 prefix along with
    // 10.0.1.1/32
    //
    content = FileRead("controller/src/bgp/testdata/routing_policy_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    //
    // Verify that both 10.0.1.1/32 and 20.1.1.1/32 matches the policy and
    // action to update the local-pref is triggered
    //
    rt = RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    rt = RouteLookup<InetDefinition>("test.inet.0", "20.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    content = FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    //
    // Verify that the policy matched 10.0.1.1/32 ONLY and local-pref is updated
    // Policy doesn't match 20.1.1.1/32.
    //
    rt = RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    rt = RouteLookup<InetDefinition>("test.inet.0", "20.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == original_local_pref);


    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "10.0.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "20.1.1.1/32");
}

//
// In this test, routing policy action is updated
// With the update of the policy, the route is rejected by matching the newly
// added term. Selected path or best path no longer points to it
//
TEST_F(RoutingPolicyTest, PolicyUpdate_MultiplePath) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 200, list_of("22:13"));
    task_util::WaitForIdle();
    AddRoute<Inet6Definition>(peers_[1], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// In this test, routing policy action is updated
// With the update of the policy, new path gets selected as action updates the
// local_pref
//
TEST_F(RoutingPolicyTest, PolicyUpdate_MultiplePath_UpdateLocalPref) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 200, list_of("22:13"));
    task_util::WaitForIdle();
    AddRoute<Inet6Definition>(peers_[1], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
}

//
// In this test, routing policy action is updated
// With the update of the policy, new path gets selected as action updates the
// local_pref. Validate the replicated route to verify the notification of route
//
TEST_F(RoutingPolicyTest, PolicyUpdate_MultiplePath_UpdateLocalPref_Notify) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7h.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 200, list_of("22:13"));
    task_util::WaitForIdle();
    AddRoute<Inet6Definition>(peers_[1], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));
    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    // Look in the secondary table for replicated route.
    // Replicated route is updated in the route notification with new selected
    // path due to policy update
    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    DeleteRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test_0.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
}

//
// 1 Route is updated due to policy match.
// 2 Update of the policy to reject on match
//
TEST_F(RoutingPolicyTest, PolicyUpdate_ToReject) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                  "2001:db8:85a3::8a2e:370:7334/128", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    string content_b =
        FileRead("controller/src/bgp/testdata/routing_policy_7f.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                             "2001:db8:85a3::8a2e:370:7334/128");
}

//
// 1. Routing instance is attached two network policies
// 2. Add route that matches both policies.
//    Route matches the first policy and rejected
// 3. Update of the order of network policy on the routing instance
// 4. Route matches first policy and accepted
//
TEST_F(RoutingPolicyTest, MultiplePolicies_UpdateOrder) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13")("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_4e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

//
// 1. Routing instance is attached two network policies
// 2. Add route that matches both policies.
//    Route matches the first policy and accepted
// 3. Update of the order of network policy on the routing instance
// 4. Route matches the second policy and rejected
//
TEST_F(RoutingPolicyTest, MultiplePolicies_UpdateOrder_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    content = FileRead("controller/src/bgp/testdata/routing_policy_4e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100,
                              list_of("11:13")("22:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7334/128");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("22:13"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
}

TEST_F(RoutingPolicyTest, ProtocolMatchServiceChain) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "red.inet.0",
                                   "1.1.2.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0",
                                   "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("red.inet.0"));
    VERIFY_EQ(1, RouteCount("blue.inet.0"));

    // Policy applied on Service Chain routes
    BgpRoute *rt =
        RouteLookup<InetDefinition>("red.inet.0", "192.168.1.1/32");
    ASSERT_TRUE(rt != NULL);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);
    // Bgp routes are not altered
    rt = RouteLookup<InetDefinition>("red.inet.0", "1.1.2.3/32");
    ASSERT_TRUE(rt != NULL);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 100);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "192.168.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.2.3/32");
}

TEST_F(RoutingPolicyTest, ProtocolMatchServiceChain_PolicyUpdate) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "red.inet.0",
                                   "1.1.2.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0",
                                   "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("red.inet.0"));
    VERIFY_EQ(1, RouteCount("blue.inet.0"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0g.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Policy applied on both Service Chain routes and bgp routes
    BgpRoute *rt =
        RouteLookup<InetDefinition>("red.inet.0", "192.168.1.1/32");
    ASSERT_TRUE(rt != NULL);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 999);
    ASSERT_TRUE(original_local_pref == 100);
    rt = RouteLookup<InetDefinition>("red.inet.0", "1.1.2.3/32");
    ASSERT_TRUE(rt != NULL);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 999);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "192.168.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.2.3/32");
}

TEST_F(RoutingPolicyTest, ProtocolMatchStaticRoute) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0h.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "nat.inet.0",
                                   "192.168.1.254/32", 100);
    AddRoute<InetDefinition>(peers_[0], "nat.inet.0",
                                   "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("nat.inet.0"));
    VERIFY_EQ(1, RouteCount("blue.inet.0"));

    // Policy applied on Static routes
    BgpRoute *rt =
        RouteLookup<InetDefinition>("nat.inet.0", "192.168.1.0/24");
    ASSERT_TRUE(rt != NULL);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);
    // Bgp routes are not altered
    rt = RouteLookup<InetDefinition>("nat.inet.0", "192.168.1.1/32");
    ASSERT_TRUE(rt != NULL);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 100);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteRoute<InetDefinition>(peers_[0], "nat.inet.0", "192.168.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "nat.inet.0", "192.168.1.254/32");
}

//
// 1. two routing instance test_0 and test_1
// 2. Routing instnaces are connected with each other,
// 3. two routing policies to edit local pref of matching route.
// Verify that routing policy is not applied to replicated route.
// Also, with above configuration, validate that route notify doesn't trigger
// re-addition of replicated route paths.
//
TEST_F(RoutingPolicyTest, Policy_RouteReplicate) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7i.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    AddRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 100);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_TRUE(rt->BestPath()->IsReplicated() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 200);

    // Take the time stamp of the replicated path on the route.
    // This will be compared with replicated path after triggering dummy
    // notification of the primary route.
    uint64_t rt2_replicated_path_time_stamp;
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->IsReplicated())
            rt2_replicated_path_time_stamp = path->time_stamp_usecs();
    }

    // Trigger dummy change notification on the primary routes
    AddRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    AddRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));

    rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);

    uint64_t rt2_replicated_path_time_stamp_after_change;
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->IsReplicated())
            rt2_replicated_path_time_stamp_after_change
                = path->time_stamp_usecs();
    }
    ASSERT_TRUE(rt2_replicated_path_time_stamp_after_change
                == rt2_replicated_path_time_stamp);

    DeleteRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
}

//
// 1. two routing instance test_0 and test_1
// 2. Routing instnaces are connected with each other,
// 3. two routing policies to edit local pref of matching route.
// Verify that routing policy is not applied to replicated route.
// Now modify the rules of the policy applied on the routing instance and
// verify that update of policy doesn't trigger route change.
//
TEST_F(RoutingPolicyTest, Policy_RouteReplicate_PolicyUpdate_0) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7i.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    AddRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 100);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_TRUE(rt->BestPath()->IsReplicated() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 200);


    content = FileRead("controller/src/bgp/testdata/routing_policy_0k.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Verify that replicated routes are not updated after policy change
    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 100);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_TRUE(rt->BestPath()->IsReplicated() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 200);

    DeleteRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
}

//
// 1. two routing instance test_0 and test_1
// 2. Routing instnaces are connected with each other,
// 3. two routing policies(basic_0 & basic_1) to edit local pref is applied on
// test_0 & test_1 routing instances respectively
// Verify that routing policy is not applied to replicated route.
// Now apply new policy(basic_2) on the routing instance(test_0) and
// verify that update of policy doesn't trigger route change.
//
TEST_F(RoutingPolicyTest, Policy_RouteReplicate_PolicyUpdate_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_7i.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    AddRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                  "2001:db8:85a3::8a2e:370:7335/128", 100, list_of("23:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 100);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_TRUE(rt->BestPath()->IsReplicated() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 200);


    content = FileRead("controller/src/bgp/testdata/routing_policy_0l.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Verify that replicated routes are not updated after policy change
    VERIFY_EQ(1, RouteCount("test_1.inet6.0"));
    VERIFY_EQ(1, RouteCount("test_0.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test_0.inet6.0",
                                            "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 1);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 100);

    // Since the routing policy is not applied on the replicated route,
    // the replicated route in test_1.inet6.0 table retains its
    // local-pref of 200 and remains the best path.
    // So it is no longer replicated to connected VRF test_0.inet6.0 table.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_FALSE(path->IsReplicated());
    }

    rt = RouteLookup<Inet6Definition>("test_1.inet6.0",
                                      "2001:db8:85a3::8a2e:370:7335/128");
    ASSERT_TRUE(rt != NULL);
    ASSERT_TRUE(rt->count() == 2);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_TRUE(rt->BestPath()->IsReplicated() == true);
    attr = rt->BestPath()->GetAttr();
    orig_attr = rt->BestPath()->GetOriginalAttr();
    original_local_pref = orig_attr->local_pref();
    policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 200);
    ASSERT_TRUE(original_local_pref == 200);

    DeleteRoute<Inet6Definition>(peers_[0], "test_0.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    DeleteRoute<Inet6Definition>(peers_[1], "test_1.inet6.0",
                             "2001:db8:85a3::8a2e:370:7335/128");
    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
}



static void ValidateShowRoutingPolicyResponse(
    Sandesh *sandesh, bool *done,
    const vector<ShowRoutingPolicyInfo> &policy_list) {
    ShowRoutingPolicyResp *resp =
        dynamic_cast<ShowRoutingPolicyResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(policy_list.size(), resp->get_routing_policies().size());

    BOOST_FOREACH(const ShowRoutingPolicyInfo &policy, policy_list) {
        bool found = false;
        BOOST_FOREACH(const ShowRoutingPolicyInfo &resp_policy,
            resp->get_routing_policies()) {
            if (policy.get_name() == resp_policy.get_name()) {
                found = true;
                EXPECT_EQ(policy.get_terms().size(),
                          resp_policy.get_terms().size());
                ASSERT_TRUE(std::equal(policy.get_terms().begin(),
                               policy.get_terms().end(),
                               resp_policy.get_terms().begin()));
                break;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << policy.get_name());
    }

    *done = true;
}

static void ValidateShowRoutingInstanceRoutingPolicyResponse(
    Sandesh *sandesh, bool *done,
    const vector<ShowInstanceRoutingPolicyInfo> &policy_list) {
    ShowRoutingInstanceResp *resp =
        dynamic_cast<ShowRoutingInstanceResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(1, resp->get_instances().size());
    EXPECT_EQ(policy_list.size(),
              resp->get_instances()[0].get_routing_policies().size());
    BOOST_FOREACH(const ShowInstanceRoutingPolicyInfo &info, policy_list) {
        bool found = false;
        BOOST_FOREACH(const ShowInstanceRoutingPolicyInfo &resp_info,
                      resp->get_instances()[0].get_routing_policies()) {
            if (info.get_policy_name() == resp_info.get_policy_name()) {
                found = true;
                EXPECT_EQ(info.get_generation(), resp_info.get_generation());
                break;
            }
        }
        EXPECT_TRUE(found);
    }
    *done = true;
}

TEST_F(RoutingPolicyTest, ShowPolicy_0) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_6c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bgp_server_.get();
    Sandesh::set_client_context(&sandesh_context);

    vector<ShowRoutingPolicyInfo> policy_list;

    vector<string> policy_name_list = list_of("basic");
    BOOST_FOREACH(string policy_name, policy_name_list) {
        const RoutingPolicy *policy = FindRoutingPolicy(policy_name);
        ASSERT_TRUE(policy != NULL);
        ShowRoutingPolicyInfo show_policy;
        show_policy.set_name(policy->name());
        show_policy.set_deleted(policy->deleted());
        show_policy.set_generation(policy->generation());
        show_policy.set_ref_count(policy->refcount());
        std::vector<PolicyTermInfo> terms_list;
        BOOST_FOREACH(RoutingPolicy::PolicyTermPtr term, policy->terms()) {
            PolicyTermInfo show_term;
            show_term.set_terminal(term->terminal());
            vector<string> match_list;
            BOOST_FOREACH(RoutingPolicyMatch *match, term->matches()) {
                match_list.push_back(match->ToString());
            }
            show_term.set_matches(match_list);
            vector<string> action_list;
            BOOST_FOREACH(RoutingPolicyAction *action, term->actions()) {
                action_list.push_back(action->ToString());
            }
            show_term.set_actions(action_list);
            terms_list.push_back(show_term);
        }
        show_policy.set_terms(terms_list);
        policy_list.push_back(show_policy);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));

    ShowRoutingPolicyReq *show_req = new ShowRoutingPolicyReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    vector<ShowInstanceRoutingPolicyInfo> instance_policy_list;
    const RoutingInstance *rtinstance = FindRoutingInstance("test");
    BOOST_FOREACH(RoutingPolicyInfo info, rtinstance->routing_policies()) {
        ShowInstanceRoutingPolicyInfo sirpi;
        sirpi.set_generation(info.second);
        sirpi.set_policy_name(info.first->name());
        instance_policy_list.push_back(sirpi);
    }

    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingInstanceRoutingPolicyResponse,
                    _1, &validate_done, instance_policy_list));

    ShowRoutingInstanceReq *show_instance_req = new ShowRoutingInstanceReq;
    show_instance_req->set_search_string("test");
    show_instance_req->HandleRequest();
    show_instance_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);
}

// show policy test with multiple policies
TEST_F(RoutingPolicyTest, ShowPolicy_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_1a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bgp_server_.get();
    Sandesh::set_client_context(&sandesh_context);

    vector<ShowRoutingPolicyInfo> policy_list;

    vector<string> policy_name_list = list_of("basic_0")("basic_1");
    BOOST_FOREACH(string policy_name, policy_name_list) {
        const RoutingPolicy *policy = FindRoutingPolicy(policy_name);
        ASSERT_TRUE(policy != NULL);
        ShowRoutingPolicyInfo show_policy;
        show_policy.set_name(policy->name());
        show_policy.set_deleted(policy->deleted());
        show_policy.set_generation(policy->generation());
        show_policy.set_ref_count(policy->refcount());
        std::vector<PolicyTermInfo> terms_list;
        BOOST_FOREACH(RoutingPolicy::PolicyTermPtr term, policy->terms()) {
            PolicyTermInfo show_term;
            show_term.set_terminal(term->terminal());
            vector<string> match_list;
            BOOST_FOREACH(RoutingPolicyMatch *match, term->matches()) {
                match_list.push_back(match->ToString());
            }
            show_term.set_matches(match_list);
            vector<string> action_list;
            BOOST_FOREACH(RoutingPolicyAction *action, term->actions()) {
                action_list.push_back(action->ToString());
            }
            show_term.set_actions(action_list);
            terms_list.push_back(show_term);
        }
        show_policy.set_terms(terms_list);
        policy_list.push_back(show_policy);
    }

    // Introspect with no search string
    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));
    ShowRoutingPolicyReq *show_req = new ShowRoutingPolicyReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Introspect with empty search string
    validate_done = false;
    show_req = new ShowRoutingPolicyReq;
    show_req->set_search_string("");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Introspect with "a" search string
    validate_done = false;
    show_req = new ShowRoutingPolicyReq;
    show_req->set_search_string("a");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Introspect with "basic_1" search string
    validate_done = false;
    policy_list[0] = policy_list[1];
    policy_list.resize(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));
    show_req = new ShowRoutingPolicyReq;
    show_req->set_search_string("basic_1");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    vector<ShowInstanceRoutingPolicyInfo> instance_policy_list;
    const RoutingInstance *rtinstance = FindRoutingInstance("test");
    BOOST_FOREACH(RoutingPolicyInfo info, rtinstance->routing_policies()) {
        ShowInstanceRoutingPolicyInfo sirpi;
        sirpi.set_generation(info.second);
        sirpi.set_policy_name(info.first->name());
        instance_policy_list.push_back(sirpi);
    }

    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingInstanceRoutingPolicyResponse,
                    _1, &validate_done, instance_policy_list));

    ShowRoutingInstanceReq *show_instance_req = new ShowRoutingInstanceReq;
    show_instance_req->set_search_string("test");
    show_instance_req->HandleRequest();
    show_instance_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);
}

// Show policy after policy update
TEST_F(RoutingPolicyTest, ShowPolicy_2) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_6c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Now update the policy
    content = FileRead("controller/src/bgp/testdata/routing_policy_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bgp_server_.get();
    Sandesh::set_client_context(&sandesh_context);

    vector<ShowRoutingPolicyInfo> policy_list;

    vector<string> policy_name_list = list_of("basic");
    BOOST_FOREACH(string policy_name, policy_name_list) {
        const RoutingPolicy *policy = FindRoutingPolicy(policy_name);
        ASSERT_TRUE(policy != NULL);
        ShowRoutingPolicyInfo show_policy;
        show_policy.set_name(policy->name());
        show_policy.set_deleted(policy->deleted());
        show_policy.set_generation(policy->generation());
        show_policy.set_ref_count(policy->refcount());
        std::vector<PolicyTermInfo> terms_list;
        BOOST_FOREACH(RoutingPolicy::PolicyTermPtr term, policy->terms()) {
            PolicyTermInfo show_term;
            show_term.set_terminal(term->terminal());
            vector<string> match_list;
            BOOST_FOREACH(RoutingPolicyMatch *match, term->matches()) {
                match_list.push_back(match->ToString());
            }
            show_term.set_matches(match_list);
            vector<string> action_list;
            BOOST_FOREACH(RoutingPolicyAction *action, term->actions()) {
                action_list.push_back(action->ToString());
            }
            show_term.set_actions(action_list);
            terms_list.push_back(show_term);
        }
        show_policy.set_terms(terms_list);
        policy_list.push_back(show_policy);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));

    ShowRoutingPolicyReq *show_req = new ShowRoutingPolicyReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    vector<ShowInstanceRoutingPolicyInfo> instance_policy_list;
    const RoutingInstance *rtinstance = FindRoutingInstance("test");
    BOOST_FOREACH(RoutingPolicyInfo info, rtinstance->routing_policies()) {
        ShowInstanceRoutingPolicyInfo sirpi;
        sirpi.set_generation(info.second);
        sirpi.set_policy_name(info.first->name());
        instance_policy_list.push_back(sirpi);
    }

    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingInstanceRoutingPolicyResponse,
                    _1, &validate_done, instance_policy_list));

    ShowRoutingInstanceReq *show_instance_req = new ShowRoutingInstanceReq;
    show_instance_req->set_search_string("test");
    show_instance_req->HandleRequest();
    show_instance_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);
}

//
// Add routes with different hashes such that route belongs to different DBTable
// partition. Validate the routing policy and update of routing policy on
// routing instance
//
TEST_F(RoutingPolicyTest, MultipleRoutes_DifferentPartition) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i << "/32";
        AddRoute<InetDefinition>(peers_[0], "test.inet.0", oss.str(), 100);
        oss.str("");
    }
    task_util::WaitForIdle();
    VERIFY_EQ(255, RouteCount("test.inet.0"));

    content = FileRead("controller/src/bgp/testdata/routing_policy_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Policy matches for 10.0.1.1/32 "exact". The local pref should be updated
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i << "/32";
        DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", oss.str());
        oss.str("");
    }
    task_util::WaitForIdle();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
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
