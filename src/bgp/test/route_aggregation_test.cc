/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/route_aggregate.h"

#include <fstream>

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
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

class RouteAggregationTest : public ::testing::Test {
protected:
    RouteAggregationTest() : bgp_server_(new BgpServer(&evm_)),
        parser_(&config_db_) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~RouteAggregationTest() {
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

        BgpTable *table = static_cast<BgpTable *>
            (bgp_server_->database()->FindTable(table_name));
        ASSERT_TRUE(table != NULL);

        int index = table->routing_instance()->index();
        boost::system::error_code ec;
        Ip4Address nh_addr = Ip4Address::from_string("99.99.99.99", ec);
        BgpAttrNextHop nh_spec(nh_addr);
        attr_spec.push_back(&nh_spec);
        BgpAttrSourceRd source_rd_spec(
            RouteDistinguisher(nh_addr.to_ulong(), index));
        attr_spec.push_back(&source_rd_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 88));
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

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    vector<BgpPeerMock *> peers_;
    BgpConfigParser parser_;
};

TEST_F(RouteAggregationTest, Basic) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, Basic_DeleteNexthop) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible() == false);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, Basic_MoreSpecificDelete) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, Basic_LastMoreSpecificDelete) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.3.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(5, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test.inet.0"));

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.3.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("test.inet.0"));

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    task_util::WaitForIdle();
}

//
// Remove the route-aggregate object from routing instance and ensure that
// aggregated route is deleted
//
TEST_F(RouteAggregationTest, ConfigDelete) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "routing-instance-route-aggregate");
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, ConfigUpdatePrefix) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_0b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, ConfigUpdateNexthop) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible() == false);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregationTest, ConfigUpdatePrefixLen) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_0c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32");
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
