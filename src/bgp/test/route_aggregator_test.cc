/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/route_aggregator.h"

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
#include "bgp/routing-instance/route_aggregate_types.h"
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

class RouteAggregatorTest : public ::testing::Test {
protected:
    RouteAggregatorTest()
      : config_db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        bgp_server_(new BgpServer(&evm_)),
        parser_(&config_db_),
        validate_done_(false) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~RouteAggregatorTest() {
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
        TASK_UTIL_ASSERT_EQ(0, bgp_server_->routing_instance_mgr()->count());
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

    string GetOriginVnFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            RoutingInstanceMgr *ri_mgr = bgp_server_->routing_instance_mgr();
            return ri_mgr->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
        }
        return "unresolved";
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

    template <typename RespT>
    void ValidateResponse(Sandesh *sandesh, string &result, bool empty) {
        RespT *resp = dynamic_cast<RespT *>(sandesh);
        TASK_UTIL_EXPECT_NE((RespT *) NULL, resp);

        if (empty) {
            TASK_UTIL_EXPECT_EQ(0, resp->get_aggregate_route_entries().size());
        } else {
            TASK_UTIL_EXPECT_EQ(1, resp->get_aggregate_route_entries().size());
        }
        int i = 0;
        BOOST_FOREACH(const AggregateRouteEntriesInfo &info,
                      resp->get_aggregate_route_entries()) {
            TASK_UTIL_EXPECT_EQ(info.get_instance_name(), result);
            i++;
        }
        validate_done_ = true;
    }

    void VerifyRouteAggregateSandesh(std::string ri_name, bool empty = false) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);

        Sandesh::set_response_callback(boost::bind(
            &RouteAggregatorTest::ValidateResponse<ShowRouteAggregateResp>,
            this, _1, ri_name, empty));
        ShowRouteAggregateReq *req = new ShowRouteAggregateReq;
        req->set_search_string(ri_name);
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);

        Sandesh::set_response_callback(boost::bind(
            &RouteAggregatorTest::ValidateResponse<ShowRouteAggregateSummaryResp>,
            this, _1, ri_name, empty));
        ShowRouteAggregateSummaryReq *sreq = new ShowRouteAggregateSummaryReq;
        sreq->set_search_string(ri_name);
        validate_done_ = false;
        sreq->HandleRequest();
        sreq->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    vector<BgpPeerMock *> peers_;
    BgpConfigParser parser_;
    bool validate_done_;
};

//
// Validate the route aggregation functionality for default route
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
TEST_F(RouteAggregatorTest, Default) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0i.xml");
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
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "0.0.0.0/0");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    TASK_UTIL_EXPECT_TRUE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "0.0.0.0/0"));
    TASK_UTIL_EXPECT_FALSE(IsAggregateRoute<InetDefinition>("test",
                                           "test.inet.0", "1.1.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("test",
                                            "test.inet.0", "1.1.1.1/32"));
    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Validate the route aggregation functionality
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
TEST_F(RouteAggregatorTest, Basic) {
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
    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, Basic_0) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, Basic_1) {
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
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::Aggregate);

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
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::Aggregate);
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(RouteAggregatorTest, Basic_NoReplication) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0e.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Link the two routing instances
    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add more specific route
    AddRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();

    // Aggregate route is created with infeasible path  and not replicated till
    // nexthop is resolved
    VERIFY_EQ(2, RouteCount("test_0.inet.0"));
    VERIFY_EQ(0, RouteCount("test_1.inet.0"));

    // Add nexthop route
    AddRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.2.1/32", 100);
    task_util::WaitForIdle();

    // Verify that aggregate route is created
    VERIFY_EQ(3, RouteCount("test_0.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test_0.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::Aggregate);

    // Verify that aggregate route is replicated
    VERIFY_EQ(2, RouteCount("test_1.inet.0"));
    rt = RouteLookup<InetDefinition>("test_1.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    // Verify that nexthop route is replicated
    rt = RouteLookup<InetDefinition>("test_1.inet.0", "2.2.2.1/32");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    // Verify that more specific route is no longer replicated
    rt = RouteLookup<InetDefinition>("test_1.inet.0", "2.2.1.1/32");
    ASSERT_TRUE(rt == NULL);

    DeleteRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test_0.inet.0", "2.2.1.1/32");
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test_0",
                                  "routing-instance", "test_1", "connection");
    task_util::WaitForIdle();
}
//
// Validate the route aggregation config handling
// Verify that aggregate route config with multiple aggregate prefix is handled
//
TEST_F(RouteAggregatorTest, Basic_MultipleAggregatePrefix) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0f.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    //
    // Routes are added to trigger both aggregate routes
    // Nexthop route is not added. So aggregare route will be added without
    // path resolution
    //
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    rt = RouteLookup<InetDefinition>("test.inet.0", "3.3.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.0.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.0.1/32");
    task_util::WaitForIdle();
}

//
// Validate the route aggregation config handling
// Verify that aggregate route config is ignored if the nexthop and routes
// belong to different family
// Nexthop is Inet & Prefixes are both Inet and Inet6
// Aggregate route should be generated only for Inet
//
TEST_F(RouteAggregatorTest, Basic_ErrConfig_DiffFamily) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0h.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add both inet and inet6 more specific routes
    // Nexthop route is not added. So aggregare route will be added without
    // path resolution
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.9.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(2, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    // Verify that inet6 route aggregation is not done
    VERIFY_EQ(1, RouteCount("test.inet6.0"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.9.1/32");
    task_util::WaitForIdle();
}

//
// Validate the route aggregation config handling
// Verify that aggregate route config is ignored if the nexthop and routes
// belong to different family
// Nexthop is Inet6 & Prefixes are both Inet and Inet6
// Aggregate route should be generated only for Inet6
//
TEST_F(RouteAggregatorTest, Basic_ErrConfig_DiffFamily_1) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0g.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    //
    // Add both inet and inet6 more specific routes
    // Nexthop route is not added. So aggregare route will be added without
    // path resolution
    //
    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2001:db8:85a3::8a2e:370:7334/128", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.9.1/32", 100);
    task_util::WaitForIdle();
    // Verify that inet6 route aggregation is not done
    VERIFY_EQ(2, RouteCount("test.inet6.0"));
    BgpRoute *rt =
        RouteLookup<Inet6Definition>("test.inet6.0", "2001:db8:85a3::/64");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);

    // Verify that inet route aggregation is not done
    VERIFY_EQ(1, RouteCount("test.inet.0"));

    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2001:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.9.1/32");
    task_util::WaitForIdle();
}


//
// Delete the nexthop route and verify that aggregate route's best path is
// not feasible. Now delete the more specific route and validate that aggregate
// route is removed
//
TEST_F(RouteAggregatorTest, Basic_DeleteNexthop) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("test.inet.0"));

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt == NULL);
}

//
// Delete the more specific route and validate that aggregate route is deleted
//

TEST_F(RouteAggregatorTest, Basic_MoreSpecificDelete) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, Basic_LastMoreSpecificDelete) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.3.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("test.inet.0"));

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.1/32");
    task_util::WaitForIdle();
}

//
// Add route aggregate config to routing instance with service chain config
// Ensure that origin vn is set correctly on the aggregate route
//
TEST_F(RouteAggregatorTest, ServiceChain) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(4, RouteCount("red.inet.0"));
    VERIFY_EQ(4, RouteCount("blue.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("blue.inet.0", "2.2.2.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(GetOriginVnFromRoute(rt->BestPath()) == "red-vn");
    rt = RouteLookup<InetDefinition>("blue.inet.0", "2.2.2.1/32");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ServiceChain);
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("blue",
                                            "blue.inet.0", "2.2.2.1/32"));

    rt = RouteLookup<InetDefinition>("red.inet.0", "1.1.1.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(GetOriginVnFromRoute(rt->BestPath()) == "blue-vn");
    rt = RouteLookup<InetDefinition>("red.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ServiceChain);
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("red",
                                            "red.inet.0", "1.1.1.1/32"));

    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.3/32");
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.3/32");
    task_util::WaitForIdle();
}

//
// Add route aggregate config to aggregate to 0/0 to routing instance with
// service chain config
// 1. Ensure that origin vn is set correctly on the aggregate route
// 2. Ensure that contributing routes are considered only if the origin vn
// matches dest VN of service chain
//
TEST_F(RouteAggregatorTest, ServiceChain_0) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_4a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add connected routes
    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.3/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(1, RouteCount("red.inet.0"));
    VERIFY_EQ(1, RouteCount("blue.inet.0"));

    // No route aggregation
    BgpRoute *rt = RouteLookup<InetDefinition>("blue.inet.0", "0/0");
    ASSERT_TRUE(rt == NULL);
    rt = RouteLookup<InetDefinition>("red.inet.0", "0/0");
    ASSERT_TRUE(rt == NULL);

    // Add more specific route to RED
    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "2.2.2.1/32", 100);
    task_util::WaitForIdle();

    // Route aggregation is triggered in blue
    VERIFY_EQ(3, RouteCount("blue.inet.0"));

    // Route aggregation is NOT triggered in red
    VERIFY_EQ(2, RouteCount("red.inet.0"));

    rt = RouteLookup<InetDefinition>("blue.inet.0", "0/0");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(GetOriginVnFromRoute(rt->BestPath()) == "red-vn");

    rt = RouteLookup<InetDefinition>("blue.inet.0", "2.2.2.1/32");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ServiceChain);
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("blue",
                                            "blue.inet.0", "2.2.2.1/32"));

    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("blue",
                                            "blue.inet.0", "1.1.1.3/32"));

    // Verify the sandesh
    VerifyRouteAggregateSandesh("blue");

    // Add more specific route to blue
    AddRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.1/32", 100);
    // Delete the more specific route in red
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "2.2.2.1/32");
    task_util::WaitForIdle();

    // service chain vm-route + connected route + aggregate route
    VERIFY_EQ(3, RouteCount("red.inet.0"));

    // Aggregated route is removed and Route Agggregation is not triggered on
    // VM route from blue vn
    VERIFY_EQ(2, RouteCount("blue.inet.0"));

    rt = RouteLookup<InetDefinition>("red.inet.0", "0/0");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(GetOriginVnFromRoute(rt->BestPath()) == "blue-vn");

    rt = RouteLookup<InetDefinition>("red.inet.0", "1.1.1.1/32");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 1);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::ServiceChain);
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("red",
                                            "red.inet.0", "1.1.1.1/32"));

    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("red",
                                            "red.inet.0", "1.1.1.3/32"));

    // Verify the sandesh
    VerifyRouteAggregateSandesh("red");

    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.3/32");
    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "1.1.1.3/32");
    task_util::WaitForIdle();
}

//
// Route aggregation is triggered only when route's origin VN matches
//
TEST_F(RouteAggregatorTest, OriginVnCheck) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_4b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.254/32", 100);
    task_util::WaitForIdle();

    AddRoute<InetDefinition>(peers_[0], "blue.inet.0", "2.2.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(2, RouteCount("red.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("red.inet.0", "0/0");
    ASSERT_TRUE(rt == NULL);

    AddRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(4, RouteCount("red.inet.0"));
    rt = RouteLookup<InetDefinition>("red.inet.0", "0/0");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    TASK_UTIL_EXPECT_FALSE(IsContributingRoute<InetDefinition>("red",
                                            "red.inet.0", "2.2.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(IsContributingRoute<InetDefinition>("red",
                                            "red.inet.0", "1.1.1.1/32"));

    // Verify the sandesh
    VerifyRouteAggregateSandesh("red");

    DeleteRoute<InetDefinition>(peers_[0], "blue.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "red.inet.0", "1.1.1.254/32");
    task_util::WaitForIdle();
}


//
// Remove the route-aggregate object from routing instance and ensure that
// aggregated route is deleted
//
TEST_F(RouteAggregatorTest, ConfigDelete) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, ConfigUpdatePrefix) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "3.3.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the route-aggregate config to modify the nexthop
// Validate that aggregate route is updated with new nexthop
//
TEST_F(RouteAggregatorTest, ConfigUpdateNexthop) {
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
TEST_F(RouteAggregatorTest, ConfigDelete_Add) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    // Link the route aggregate config from vrf
    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();
    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    EnableUnregResolveTask("test", Address::INET);
    TASK_UTIL_EXPECT_EQ(GetUnregResolveListSize("test", Address::INET), 0);

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "1.1.1.254/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.1.1/32");
    task_util::WaitForIdle();
}

//
// Update the route-aggregate config to modify the prefix length of the
// aggregate prefix. Verify the new aggregate route
//
TEST_F(RouteAggregatorTest, ConfigUpdatePrefixLen) {
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
TEST_F(RouteAggregatorTest, ConfigUpdate_AddNew) {
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
TEST_F(RouteAggregatorTest, ConfigUpdate_UpdateExisting) {
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
// Config with multiple overlapping prefixes
//
TEST_F(RouteAggregatorTest, OverlappingPrefixes) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(5, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.2.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32");
    task_util::WaitForIdle();
}

//
// Config update to add multiple overlapping prefixes
//
TEST_F(RouteAggregatorTest, ConfigUpdate_OverlappingPrefixes) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_0d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    content = FileRead("controller/src/bgp/testdata/route_aggregate_3c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    VERIFY_EQ(5, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.2.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32");
    task_util::WaitForIdle();
}

//
// Config update to remove multiple overlapping prefixes
//
TEST_F(RouteAggregatorTest, ConfigUpdate_RemoveOverlappingPrefixes) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_3c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(5, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "2.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.2.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_1", "route-aggregate-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_2", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(GetUpdateAggregateListSize("test", Address::INET), 0);
    TASK_UTIL_EXPECT_EQ(GetUnregResolveListSize("test", Address::INET), 0);

    VERIFY_EQ(3, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.2.0/24");
    ASSERT_TRUE(rt == NULL);
    rt = RouteLookup<InetDefinition>("test.inet.0", "2.0.0.0/8");
    ASSERT_TRUE(rt == NULL);

    rt = RouteLookup<InetDefinition>("test.inet.0", "2.2.0.0/16");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.2/32");
    task_util::WaitForIdle();
}

//
// With multiple route aggregate config object linked to the routing instance,
// update the config to remove longer prefix
// RA-1:
//   9.0.1.0/24
// RA-2:
//   9.0.0.0/8
//   21.1.1.0/24
//   21.1.0.0/16
//   21.0.0.0/8
// Step 1: Attach RA-1 to routing instance
// Step 2: Attach RA-2 to routing instance. Now routing instance has both 9.0/16
// and 9/8 route aggregate prefix
// Step 3: Remove RA-1. Routing instance is only left with 9/8.
// Ensure valid route aggregate objects after config update
//
TEST_F(RouteAggregatorTest, ConfigUpdate_OverlappingPrefixes_1) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_4c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "9.0.1.1/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "9.0.1.10/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.3/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.4/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.5/32", 100);
    AddRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Link the route aggregate config vn_subnet_1 to test
    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_1", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    VERIFY_EQ(8, RouteCount("test.inet.0"));
    BgpRoute *rt = RouteLookup<InetDefinition>("test.inet.0", "9.0.1.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    VerifyRouteAggregateSandesh("test");

    // Link the route aggregate config vn_subnet_2 to test
    ifmap_test_util::IFMapMsgLink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_2", "route-aggregate-routing-instance");
    task_util::WaitForIdle();


    VERIFY_EQ(11, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "9.0.1.0/24");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    rt = RouteLookup<InetDefinition>("test.inet.0", "9.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");


    // Unlink the route aggregate config vn_subnet_1 from test
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_1", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    VERIFY_EQ(10, RouteCount("test.inet.0"));
    rt = RouteLookup<InetDefinition>("test.inet.0", "9.0.1.0/24");
    ASSERT_TRUE(rt == NULL);
    rt = RouteLookup<InetDefinition>("test.inet.0", "9.0.0.0/8");
    ASSERT_TRUE(rt != NULL);
    TASK_UTIL_EXPECT_EQ(rt->count(), 2);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());
    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "2.2.2.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "9.0.1.1/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "9.0.1.10/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.3/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.4/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.5/32");
    DeleteRoute<InetDefinition>(peers_[0], "test.inet.0", "21.1.1.0/24");
    task_util::WaitForIdle();
}

//
// Update the config to update the prefix len of route-aggregate
// Higher prefix len to lower
//
TEST_F(RouteAggregatorTest, ConfigUpdate_UpdateExisting_1) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");

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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test");
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
TEST_F(RouteAggregatorTest, ConfigUpdate_DeleteExisting) {
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
        "route-aggregate", "vn_subnet_1", "route-aggregate-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_2", "route-aggregate-routing-instance");
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
TEST_F(RouteAggregatorTest, ConfigUpdatePrefix_MultipleInstanceRef) {
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

    // Verify the sandesh
    VerifyRouteAggregateSandesh("test_0");
    VerifyRouteAggregateSandesh("test_1");
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

    VerifyRouteAggregateSandesh("test_0");
    VerifyRouteAggregateSandesh("test_1");

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
TEST_F(RouteAggregatorTest, MultipleRoutes_DifferentPartition) {
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

    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, ConfigDelete_DelayedRouteProcessing) {
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

    VerifyRouteAggregateSandesh("test");

    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(GetUnregResolveListSize("test", Address::INET), 0);
    VerifyRouteAggregateSandesh("test", true);

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
TEST_F(RouteAggregatorTest, ConfigDelete_DelayedRouteProcessing_1) {
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

    VerifyRouteAggregateSandesh("test");

    DisableRouteAggregateUpdate("test", Address::INET);
    // Unlink the route aggregate config from vrf
    ifmap_test_util::IFMapMsgUnlink(&config_db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    VerifyRouteAggregateSandesh("test", true);

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
TEST_F(RouteAggregatorTest, ConfigDelete_DelayedRouteProcessing_2) {
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

    VerifyRouteAggregateSandesh("test");

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

    VerifyRouteAggregateSandesh("test", true);

    EnableRouteAggregateUpdate("test", Address::INET);

    TASK_UTIL_EXPECT_TRUE(bgp_server_->database()->FindTable("test.inet.0")
                          == NULL);

    TASK_UTIL_EXPECT_TRUE(
      bgp_server_->routing_instance_mgr()->GetRoutingInstance("test") == NULL);

    VerifyRouteAggregateSandesh("test", true);
}

//
// Validate the route aggregation functionality with inet6 default route
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
TEST_F(RouteAggregatorTest, DefaultInet6) {
    string content =
        FileRead("controller/src/bgp/testdata/route_aggregate_1c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7334/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("test.inet6.0"));

    AddRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                              "2002:db8:85a3::8a2e:370:7335/128", 100);
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("test.inet6.0"));
    BgpRoute *rt = RouteLookup<Inet6Definition>("test.inet6.0", "::/0");
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
// Add nexthop route and more specific route for aggregate prefix
// Verify that aggregate route is published
//
TEST_F(RouteAggregatorTest, BasicInet6) {
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

    VerifyRouteAggregateSandesh("test");

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
TEST_F(RouteAggregatorTest, BasicInet6_0) {
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
// triggered
//
TEST_F(RouteAggregatorTest, BasicInet6_1) {
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
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::Aggregate);

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
    TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetSource() == BgpPath::Aggregate);
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7334/128");
    DeleteRoute<Inet6Definition>(peers_[0], "test.inet6.0",
                                 "2002:db8:85a3::8a2e:370:7335/128");
    task_util::WaitForIdle();
}

//
// Validate route aggregation functionality got multiple routing instances
// Add nexthop route and more specific for aggregate prefix in each instance
// Verify that aggregate route is published
//
TEST_F(RouteAggregatorTest, MultipleInstances1) {
    static const int kInstanceCount = 32;

    ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding\"utf-\"?>\n";
    oss << "<config>\n";
    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        oss << "<virtual-network name=\"test-vn" << idx << "\">\n";
        oss << "  <network-id>" << idx << "</network-id>\n";
        oss << "</virtual-network>\n";
        oss << "<routing-instance name=\"test" << idx << "\">\n";
        oss << "  <virtual-network>test-vn" << idx << "</virtual-network>\n";
        oss << "  <vrf-target>target:1:" << idx << "</vrf-target>\n";
        oss << "  <route-aggregate to=\"test-aggregate" << idx << "\"/>\n";
        oss << "</routing-instance>\n";
        oss << "<route-aggregate name=\"test-aggregate" << idx << "\">\n";
        oss << "  <aggregate-route-entries>\n";
        oss << "    <route>2.2.0.0/16</route>\n";
        oss << "  </aggregate-route-entries>\n";
        oss << "  <nexthop>1.1.1.1</nexthop>\n";
        oss << "</route-aggregate>\n";
    }
    oss << "</config>\n";

    string content = oss.str();
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    BgpPeerMock *peer =
        new BgpPeerMock(Ip4Address::from_string("10.1.1.1", ec));
    peers_.push_back(peer);

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        AddRoute<InetDefinition>(peer, table, "1.1.1.1/32", 100);
        AddRoute<InetDefinition>(peer, table, "2.2.1.1/32", 100);
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        TASK_UTIL_EXPECT_EQ(3, RouteCount(table));
        TASK_UTIL_EXPECT_TRUE(
            RouteLookup<InetDefinition>(table, "2.2.0.0/16") != NULL);
        BgpRoute *rt = RouteLookup<InetDefinition>(table, "2.2.0.0/16");
        TASK_UTIL_EXPECT_EQ(2, rt->count());
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

        TASK_UTIL_EXPECT_TRUE(
            IsAggregateRoute<InetDefinition>(instance, table, "2.2.0.0/16"));
        TASK_UTIL_EXPECT_FALSE(
            IsAggregateRoute<InetDefinition>(instance, table, "1.1.1.1/32"));
        TASK_UTIL_EXPECT_TRUE(
            IsContributingRoute<InetDefinition>(instance, table, "2.2.1.1/32"));
        TASK_UTIL_EXPECT_FALSE(
            IsContributingRoute<InetDefinition>(instance, table, "1.1.1.1/32"));
    }

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        DeleteRoute<InetDefinition>(peer, table, "1.1.1.1/32");
        DeleteRoute<InetDefinition>(peer, table, "2.2.1.1/32");
    }

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Validate route aggregation functionality got multiple routing instances
// Add nexthop route and more specific for aggregate prefix in each instance
// Verify that aggregate route is published
// Change nexthop for aggregate and verify that aggregate route is updated
//
TEST_F(RouteAggregatorTest, MultipleInstances2) {
    static const int kInstanceCount = 32;

    ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding\"utf-\"?>\n";
    oss << "<config>\n";
    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        oss << "<virtual-network name=\"test-vn" << idx << "\">\n";
        oss << "  <network-id>" << idx << "</network-id>\n";
        oss << "</virtual-network>\n";
        oss << "<routing-instance name=\"test" << idx << "\">\n";
        oss << "  <virtual-network>test-vn" << idx << "</virtual-network>\n";
        oss << "  <vrf-target>target:1:" << idx << "</vrf-target>\n";
        oss << "  <route-aggregate to=\"test-aggregate" << idx << "\"/>\n";
        oss << "</routing-instance>\n";
        oss << "<route-aggregate name=\"test-aggregate" << idx << "\">\n";
        oss << "  <aggregate-route-entries>\n";
        oss << "    <route>2.2.0.0/16</route>\n";
        oss << "  </aggregate-route-entries>\n";
        oss << "  <nexthop>1.1.1.1</nexthop>\n";
        oss << "</route-aggregate>\n";
    }
    oss << "</config>\n";

    string content = oss.str();
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::system::error_code ec;
    BgpPeerMock *peer =
        new BgpPeerMock(Ip4Address::from_string("10.1.1.1", ec));
    peers_.push_back(peer);

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        AddRoute<InetDefinition>(peer, table, "1.1.1.1/32", 100);
        AddRoute<InetDefinition>(peer, table, "2.2.1.1/32", 100);
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        TASK_UTIL_EXPECT_EQ(3, RouteCount(table));
        TASK_UTIL_EXPECT_TRUE(
            RouteLookup<InetDefinition>(table, "2.2.0.0/16") != NULL);
        BgpRoute *rt = RouteLookup<InetDefinition>(table, "2.2.0.0/16");
        TASK_UTIL_EXPECT_EQ(2, rt->count());
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

        TASK_UTIL_EXPECT_TRUE(
            IsAggregateRoute<InetDefinition>(instance, table, "2.2.0.0/16"));
        TASK_UTIL_EXPECT_FALSE(
            IsAggregateRoute<InetDefinition>(instance, table, "1.1.1.1/32"));
        TASK_UTIL_EXPECT_TRUE(
            IsContributingRoute<InetDefinition>(instance, table, "2.2.1.1/32"));
        TASK_UTIL_EXPECT_FALSE(
            IsContributingRoute<InetDefinition>(instance, table, "1.1.1.1/32"));
    }

    boost::replace_all(content, "1.1.1.1", "1.1.1.2");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        DeleteRoute<InetDefinition>(peer, table, "1.1.1.1/32");
        AddRoute<InetDefinition>(peer, table, "1.1.1.2/32", 100);
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        TASK_UTIL_EXPECT_EQ(3, RouteCount(table));
        TASK_UTIL_EXPECT_TRUE(
            RouteLookup<InetDefinition>(table, "2.2.0.0/16") != NULL);
        BgpRoute *rt = RouteLookup<InetDefinition>(table, "2.2.0.0/16");
        TASK_UTIL_EXPECT_EQ(2, rt->count());
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsFeasible());

        TASK_UTIL_EXPECT_TRUE(
            IsAggregateRoute<InetDefinition>(instance, table, "2.2.0.0/16"));
        TASK_UTIL_EXPECT_FALSE(
            IsAggregateRoute<InetDefinition>(instance, table, "1.1.1.2/32"));
        TASK_UTIL_EXPECT_TRUE(
            IsContributingRoute<InetDefinition>(instance, table, "2.2.1.1/32"));
        TASK_UTIL_EXPECT_FALSE(
            IsContributingRoute<InetDefinition>(instance, table, "1.1.1.2/32"));
    }

    for (int idx = 1; idx <= kInstanceCount; ++idx) {
        string instance = string("test") + integerToString(idx);
        string table = instance + ".inet.0";
        DeleteRoute<InetDefinition>(peer, table, "1.1.1.2/32");
        DeleteRoute<InetDefinition>(peer, table, "2.2.1.1/32");
    }

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
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
