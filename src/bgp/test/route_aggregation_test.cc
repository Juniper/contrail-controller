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
    bool IsContributingRoute(const string &instance, const string &table,
                             const string &prefix) {
        BgpRoute *rt = RouteLookup<T>(table, prefix);
        if (rt) {
            RoutingInstance *rti =
              bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
            BgpTable *table = static_cast<BgpTable *>(rt->get_table());
            return rti->IsContributingRoute(table, rt);
        }
        return false;
    }

    template<typename T>
    bool IsAggregateRoute(const string &instance, const string &table,
                             const string &prefix) {
        BgpRoute *rt = RouteLookup<T>(table, prefix);
        if (rt) {
            RoutingInstance *rti =
              bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
            BgpTable *table = static_cast<BgpTable *>(rt->get_table());
            return rti->IsAggregateRoute(table, rt);
        }
        return false;
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

    void DisableUnregResolveTask(const string &instance, Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        rti->route_aggregator(fmly)->DisableUnregResolveTask();
    }

    void EnableUnregResolveTask(const string &instance, Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        rti->route_aggregator(fmly)->EnableUnregResolveTask();
    }

    size_t GetUnregResolveListSize(const string &instance,
                                   Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        return rti->route_aggregator(fmly)->GetUnregResolveListSize();
    }

    void DisableRouteAggregateUpdate(const string &instance,
                                     Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        rti->route_aggregator(fmly)->DisableRouteAggregateUpdate();
    }

    void EnableRouteAggregateUpdate(const string &instance,
                                    Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        rti->route_aggregator(fmly)->EnableRouteAggregateUpdate();
    }

    size_t GetUpdateAggregateListSize(const string &instance,
                                      Address::Family fmly) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        return rti->route_aggregator(fmly)->GetUpdateAggregateListSize();
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

//
// Validate the route aggregation functionality
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
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

    TASK_UTIL_EXPECT_TRUE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "2.2.0.0/16"));
    TASK_UTIL_EXPECT_FALSE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "1.1.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "1.1.1.1/32"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// The nexthop is also a more specific route
// Verify that the aggregate route is not published when only nexthop route is
// added to the bgp table.
// Also verify the aggregate route is published when other more specific routes
// are published
//
TEST_F(RouteAggregationTest, Basic_0) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    TASK_UTIL_EXPECT_TRUE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "2.2.0.0/16"));
    TASK_UTIL_EXPECT_FALSE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "2.2.2.1/32"));
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.2.1/32"));
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Aggregate route is not published when route with aggregate route prefix is
// added to bgp table
// Also verify the aggregate route is published when more specific routes
// are added to bgp table
//
TEST_F(RouteAggregationTest, Basic_1) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add nexthop route and aggregate prefix route
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.0/16", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::BGP_XMPP);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 3);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ResolvedRoute);

    TASK_UTIL_EXPECT_TRUE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "2.2.0.0/16"));
    TASK_UTIL_EXPECT_FALSE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.2.1/32"));
    // Delete the aggregate prefix route
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.0/16");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    // Two paths in aggregate route. one for resolved and other for aggregation
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ResolvedRoute);
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Delete the nexthop route and verify that aggregate route's best path is
// not feasible. Now delete the more specific route and validate that aggregate
// route is removed
//
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

    VERIFY_EQ(0, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);
}

//
// Delete the more specific route and validate that aggregate route is deleted
//

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

//
// Add multiple more specific route for a given aggreagate prefix and delete
// Verify that aggregate route is deleted only after last aggregate prefix
// delete
//
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
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the route-aggregate config to modify the aggregate-route prefix
// Validate that new aggregate route is published and route for previous prefix
// is deleted
//
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

//
// Update the route-aggregate config to modify the nexthop
// Validate that aggregate route is updated with new nexthop
//
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

//
// Delete the route aggregate config and add it back before the match condition
// is unregistered. To simulate the delay in unregister processing disable to
// task trigger that process the resolve and unregister process.
//
TEST_F(RouteAggregationTest, ConfigDelete_Add) {
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

    DisableUnregResolveTask("test", Address::INET);

    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    // Link the route aggregate config from vrf
    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    EnableUnregResolveTask("test", Address::INET);
    TASK_UTIL_EXPECT_EQ(GetUnregResolveListSize("test", Address::INET), 0);

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the route-aggregate config to modify the prefix length of the
// aggregate prefix. Verify the new aggregate route
//
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

//
// Update the config to add new route-aggregate config to routing instance
//
TEST_F(RouteAggregationTest, ConfigUpdate_AddNew) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(7, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test.inet.0", "4.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the config to update the prefix len of route-aggregate
//
TEST_F(RouteAggregationTest, DISABLED_ConfigUpdate_UpdateExisting) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_3b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/17");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(7, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/17");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test.inet.0", "4.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the config to update the prefix len of route-aggregate
// Higher prefix len to lower
//
TEST_F(RouteAggregationTest, DISABLED_ConfigUpdate_UpdateExisting_1) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/17");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_3a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(7, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test.inet.0", "4.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the config to delete existing route-aggregate config from
// routing instance
//
TEST_F(RouteAggregationTest, ConfigUpdate_DeleteExisting) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.2.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(5, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "4.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_1", "routing-instance-route-aggregate");
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_2", "routing-instance-route-aggregate");
    task_util::WaitForIdle();

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "4.0.0.0/8");
    ASSERT_TRUE(rt == NULL);
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(5, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "4.3.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32");
    task_util::WaitForIdle();
}
//
// With route-aggregate config referred by multiple routing instance, update the
// route aggregate config to modify the prefix. Validate the new aggregate route
// in both routing instances
//
TEST_F(RouteAggregationTest, ConfigUpdatePrefix_MultipleInstanceRef) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test_0.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.1.1/32", 100);

    AddRoute<InetDefinition>(peers_[0], "test_1.inet.0", "1.1.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test_1.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test_0.inet.0"));
    VERIFY_EQ(3, RouteCount("test_1.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test_0.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test_1.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    content = FileRead("controller/src/bgp/testdata/route_aggregate_2a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("test_0.inet.0"));
    rt = RouteLookup<InetDefinition>("test_0.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    VERIFY_EQ(2, RouteCount("test_1.inet.0"));
    rt = RouteLookup<InetDefinition>("test_1.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "test_0.inet.0", "3.3.0.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test_1.inet.0", "3.3.0.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test_0.inet.0"));
    VERIFY_EQ(4, RouteCount("test_1.inet.0"));
    rt = RouteLookup<InetDefinition>("test_0.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test_1.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test_0.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test_0.inet.0", "3.3.0.1/32");

    DeleteRoute<InetDefinition>(peers_[0], "test_1.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test_1.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test_1.inet.0", "3.3.0.1/32");
    task_util::WaitForIdle();
}

//
// Add routes with different hashes such that route belongs to different DBTable
// partition. Validate the aggregate route
//
TEST_F(RouteAggregationTest, MultipleRoutes_DifferentPartition) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "2.2.1." << i << "/32";
        AddRoute<InetDefinition>(peers_[0], "test.inet.0", oss.str(), 100);
        oss.str("");
    }
    task_util::WaitForIdle();
    VERIFY_EQ(257, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "2.2.1." << i << "/32";
        DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", oss.str());
        oss.str("");
    }
    task_util::WaitForIdle();
    task_util::WaitForIdle();
}

//
// Disable the route-aggregation route processing. Add more-specific route and
// nexthop route. Delete the route-aggregate config.
// Ensure that aggregate route is not created after enabling the aggregate route
// processing
//
TEST_F(RouteAggregationTest, ConfigDelete_DelayedRouteProcessing) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    DisableRouteAggregateUpdate("test", Address::INET);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();

    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    EnableRouteAggregateUpdate("test", Address::INET);
    TASK_UTIL_EXPECT_EQ(GetUpdateAggregateListSize("test", Address::INET), 0);

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    task_util::WaitForIdle();
}

//
// Delete the route-aggregate config with route aggregation route processing
// disabled. Ensure that aggregate route is deleted after enabling the route
// processing
//
TEST_F(RouteAggregationTest, ConfigDelete_DelayedRouteProcessing_1) {
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

    DisableRouteAggregateUpdate("test", Address::INET);
    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    EnableRouteAggregateUpdate("test", Address::INET);
    TASK_UTIL_EXPECT_EQ(GetUpdateAggregateListSize("test", Address::INET), 0);

    VERIFY_EQ(2, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    task_util::WaitForIdle();
}

//
// Delete the more specific route with route-aggregation route process disabled
// Delete the config for routing instance and route aggregate.
// Ensure that route-aggregator object for INET table is not deleted (since the
// route processing is pending).
// After enabling the route processing, ensure that routing instance and route
// table is deleted
//
TEST_F(RouteAggregationTest, ConfigDelete_DelayedRouteProcessing_2) {
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

    DisableRouteAggregateUpdate("test", Address::INET);

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti =
        bgp_server_->routing_instance_mgr()->GetRoutingInstance("test");
    TASK_UTIL_EXPECT_TRUE(rti->route_aggregator(Address::INET6) == NULL);
    TASK_UTIL_EXPECT_TRUE(rti->route_aggregator(Address::INET) != NULL);

    EnableRouteAggregateUpdate("test", Address::INET);

    TASK_UTIL_EXPECT_TRUE(bgp_server_->database()->FindTable("test.inet.0")
                          == NULL);
    TASK_UTIL_EXPECT_TRUE(
      bgp_server_->routing_instance_mgr()->GetRoutingInstance("test") == NULL);
}

//
// Validate the route aggregation functionality with inet6
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
TEST_F(RouteAggregationTest, BasicInet6) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_1a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100);
    task_util::WaitForIdle();
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7334/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2001:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7334/128");
    task_util::WaitForIdle();
}

//
// Validate the route aggregation functionality with inet6
// Add nexthop route which is also a more specific route
// Verify that aggregate route is not published with only nexthop route
//
TEST_F(RouteAggregationTest, BasicInet6_0) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_1b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7334/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2002:db8:85a3::/64");
    ASSERT_TRUE(rt == NULL);

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7335/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2002:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7335/128");
    task_util::WaitForIdle();
}

//
// Validate the route aggregation functionality with inet6
// Add a route with aggregate prefix and verify that route aggregation is not
// triggerred
//
TEST_F(RouteAggregationTest, BasicInet6_1) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_1b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::/64", 100);
    task_util::WaitForIdle();
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7334/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(2, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2002:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::BGP_XMPP);

    // Now add more specific route
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7335/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2002:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 3);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ResolvedRoute);

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::/64");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet6.0"));
    rt = RouteLookup<Inet6Definition>("test.inet6.0",
                                                "2002:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ResolvedRoute);
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7335/128");
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
