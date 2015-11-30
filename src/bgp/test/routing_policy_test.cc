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
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
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

class BgpPeerMock : public IPeer {
public:
    BgpPeerMock(const Ip4Address &address) : address_(address) { }
    virtual ~BgpPeerMock() { }
    virtual std::string ToString() const {
        return address_.to_string();
    }
    virtual std::string ToUVEKey() const {
        return address_.to_string();
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() {
        return NULL;
    }
    virtual IPeerClose *peer_close() {
        return NULL;
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
    virtual void Close() {
    }
    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const {
        return "";
    }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }

private:
    Ip4Address address_;
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
    RoutingPolicyTest() : bgp_server_(new BgpServer(&evm_)),
        parser_(&config_db_) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~RoutingPolicyTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
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
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->MetadataClear("schema");
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names, connections));
        IFMapServerParser *parser =
            IFMapServerParser::GetInstance("schema");
        parser->Receive(&config_db_, netconf.data(), netconf.length(), 0);
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

    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref,
                      const vector<string> &community_list = vector<string>()) {
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));
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
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void DeleteInetRoute(IPeer *peer, const string &instance_name,
                         const string &prefix) {
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    int RouteCount(const string &instance_name) const {
        string tablename(instance_name);
        tablename.append(".inet.0");
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(tablename));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    BgpRoute *InetRouteLookup(const string &instance_name, const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }


    vector<string> GetOriginalCommunityListFromRoute(const BgpPath *path) {
        const Community *comm = path->GetOriginalPathAttr()->community();
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

    AddInetRoute(peers_[0], "test", "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    const BgpAttr *orig_attr = rt->BestPath()->GetOriginalPathAttr();
    uint32_t original_local_pref = orig_attr->local_pref();
    uint32_t policy_local_pref = attr->local_pref();
    ASSERT_TRUE(policy_local_pref == 102);
    ASSERT_TRUE(original_local_pref == 100);

    DeleteInetRoute(peers_[0], "test", "10.0.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyCommunityMatchReject) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "10.1.1.1/32", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "10.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    EXPECT_EQ(rt->BestPath()->GetFlags() & BgpPath::RoutingPolicyReject,
              BgpPath::RoutingPolicyReject);
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == false);

    DeleteInetRoute(peers_[0], "test", "10.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchAddCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:44"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchSetCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("23:13")("23:44"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()), list_of("11:13"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("23:13")("23:44"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchRemoveMultipleCommunity_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:22")("11:44")("22:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("11:44")("22:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchAddMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
        list_of("11:13")("11:22")("11:44")("22:44")("33:66")("44:88")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatchSetMultipleCommunity) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.0.0.0/8", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.0.0.0/8");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_2f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.0.0/16", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.0.0/16");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.0.0/16");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_Longer) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.0.0.0/8", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.0.0.0/8");
}

TEST_F(RoutingPolicyTest, PolicyPrefixMatch_SubnetMatch_Longer_1) {
    string content =
        FileRead("controller/src/bgp/testdata/routing_policy_5.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "test", "1.1.1.0/24", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.0/24");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.0/24");
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

    AddInetRoute(peers_[0], "test", "2.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "2.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "2.1.1.1/32");
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

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
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

    AddInetRoute(peers_[0], "test", "1.1.0.0/16", 100,
                 list_of("11:13")("11:44")("33:66")("77:88"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.0.0/16");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:44")("33:66")("77:88"));
    DeleteInetRoute(peers_[0], "test", "1.1.0.0/16");
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

    AddInetRoute(peers_[0], "test", "1.1.1.1/32", 100, list_of("11:13"));
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test"));
    BgpRoute *rt = InetRouteLookup("test", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());
    ASSERT_TRUE(rt->BestPath()->IsFeasible() == true);
    ASSERT_EQ(GetCommunityListFromRoute(rt->BestPath()),
              list_of("11:13")("11:22")("22:44")("44:88"));
    ASSERT_EQ(GetOriginalCommunityListFromRoute(rt->BestPath()),
              list_of("11:13"));
    DeleteInetRoute(peers_[0], "test", "1.1.1.1/32");
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
