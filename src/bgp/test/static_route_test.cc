/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/static_route.h"
#include "bgp/routing-instance/routing_instance.h"

#include <fstream>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/static_route_types.h"
#include "bgp/security_group/security_group.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include <pugixml/pugixml.hpp>
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using namespace pugi;

#define VERIFY_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)

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

private:
    Ip4Address address_;
};

class StaticRouteTest : public ::testing::Test {
protected:
    StaticRouteTest()
        : bgp_server_(new BgpServer(&evm_)) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }

    ~StaticRouteTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                                  "localhost");
        ri_mgr_ = bgp_server_->routing_instance_mgr();
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
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->Receive(&config_db_, netconf.data(), netconf.length(), 0);
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

    void DisableStaticRouteQ(const string &instance_name) {
        RoutingInstance *rtinstance = 
            ri_mgr_->GetRoutingInstance(instance_name);
        rtinstance->static_route_mgr()->DisableQueue();
    }

    bool IsQueueEmpty(const string  &instance_name) {
        RoutingInstance *rtinstance = 
            ri_mgr_->GetRoutingInstance(instance_name);
        return rtinstance->static_route_mgr()->IsQueueEmpty();
    }

    void EnableStaticRouteQ(const string  &instance_name) {
        RoutingInstance *rtinstance = 
            ri_mgr_->GetRoutingInstance(instance_name);
        rtinstance->static_route_mgr()->EnableQueue();
    }

    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref, 
                      string nexthop="7.8.9.1", 
                      std::set<string> encap = std::set<string>(),
                      std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                      uint32_t flags=0, int label=0) {
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

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        ExtCommunitySpec ext_comm;
        for(std::vector<uint32_t>::iterator it = sglist.begin(); 
            it != sglist.end(); it++) {
            SecurityGroup sgid(0, *it);
            ext_comm.communities.push_back(sgid.GetExtCommunityValue());
        }
        for(std::set<string>::iterator it = encap.begin(); 
            it != encap.end(); it++) {
            TunnelEncap tunnel_encap(*it);
            ext_comm.communities.push_back(tunnel_encap.GetExtCommunityValue());
        }
        attr_spec.push_back(&ext_comm);
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
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


    BgpRoute *InetRouteLookup(const string &instance_name, 
                              const string &prefix) {
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

    set<string> GetRTargetFromPath(const BgpPath *path) {
        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();
        set<string> rtlist;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (!ExtCommunity::is_route_target(comm))
                continue;

            RouteTarget rtarget(comm);
            rtlist.insert(rtarget.ToString());
        }
        return rtlist;
    }

    std::set<std::string> GetTunnelEncapListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        std::set<std::string> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_tunnel_encap(comm))
                continue;
            TunnelEncap encap(comm);

            list.insert(TunnelEncapType::TunnelEncapToString(encap.tunnel_encap()));
        }
        return list;
    }

    std::string GetOriginVnFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            return ri_mgr_->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
        }
        return "unresolved";
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    std::auto_ptr<autogen::StaticRouteEntriesType> 
        GetStaticRouteConfig(std::string filename) {
        std::auto_ptr<autogen::StaticRouteEntriesType> 
            params (new autogen::StaticRouteEntriesType());
        string content = FileRead(filename);
        istringstream sstream(content);
        xml_document xdoc;
        xml_parse_result result = xdoc.load(sstream);
        if (!result) {
            BGP_WARN_UT("Unable to load XML document. (status="
                << result.status << ", offset=" << result.offset << ")");
            assert(0);
        }
        xml_node node = xdoc.first_child(); 
        params->XmlParse(node);
        return params;
    }

    static void ValidateShowStaticRouteResponse(Sandesh *sandesh, 
                                                 std::string &result) {
        ShowStaticRouteResp *resp = 
            dynamic_cast<ShowStaticRouteResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowStaticRouteResp *)NULL, resp);
        validate_done_ = 1;

        TASK_UTIL_EXPECT_EQ(1, resp->get_static_route_entries().size());
        int i = 0;
        cout << "*******************************************************"<<endl;
        BOOST_FOREACH(const StaticRouteEntriesInfo &info, 
                      resp->get_static_route_entries()) {
            TASK_UTIL_EXPECT_EQ(info.get_ri_name(), result);
            cout << info.log() << endl;
            i++;
        }
        cout << "*******************************************************"<<endl;
    }

    void VerifyStaticRouteSandesh(std::string ri_name) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateShowStaticRouteResponse,
                                                   _1, ri_name));
        ShowStaticRouteReq *req = new ShowStaticRouteReq;
        req->set_ri_name(ri_name);
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    RoutingInstanceMgr *ri_mgr_;
    vector<BgpPeerMock *> peers_;
    static int validate_done_;
};

int StaticRouteTest::validate_done_;
//
// Basic Test
// 1. Configure routing instance with static route property
// 2. Add the nexthop route
// 3. Validate the static route in both source (nat) and destination 
// routing instance
TEST_F(StaticRouteTest, Basic) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    VerifyStaticRouteSandesh("nat");
    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, UpdateRtList) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_3.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_3.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    config_list = list_of("target:1:1");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, UpdateNexthop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_4.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.1/32", 100, "5.4.3.2");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "5.4.3.2");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    config_list = list_of("target:64496:1");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    DeleteInetRoute(NULL, "nat", "192.168.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(StaticRouteTest, MultiplePrefix) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_2.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.2.1/32", 100, "9.8.7.6");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    static_rt = InetRouteLookup("blue", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "9.8.7.6");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("blue", "192.168.0.0/16");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "9.8.7.6");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");


    static_rt = InetRouteLookup("nat", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.0.0/16");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    DeleteInetRoute(NULL, "nat", "192.168.2.1/32");
    task_util::WaitForIdle();
}

TEST_F(StaticRouteTest, MultiplePrefixSameNexthopAndUpdateNexthop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");
    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_5.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.2.1/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.3.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("blue", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("blue", "192.168.3.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.3.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    AddInetRoute(NULL, "nat", "192.168.2.1/32", 100, "5.3.4.5");
    task_util::WaitForIdle();

    static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "5.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("blue", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "5.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("blue", "192.168.3.0/24");
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "5.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.3.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.2.1/32");
    task_util::WaitForIdle();
}


TEST_F(StaticRouteTest, ConfigUpdate) {
    vector<string> instance_names = list_of("blue")("nat")("red");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_6.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    AddInetRoute(NULL, "nat", "192.168.2.1/32", 100, "3.4.5.6");
    AddInetRoute(NULL, "nat", "192.168.3.1/32", 100, "9.8.7.6");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.0.0/16");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_7.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.0.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("red", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in red..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.3.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.4.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    AddInetRoute(NULL, "nat", "192.168.4.1/32", 100, "9.8.7.6");
    task_util::WaitForIdle();

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.3.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.4.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    set<string> config_list_1 = list_of("target:64496:3");

    static_rt = InetRouteLookup("nat", "192.168.2.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list_1);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.3.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    static_rt = InetRouteLookup("nat", "192.168.4.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    DeleteInetRoute(NULL, "nat", "192.168.2.1/32");
    DeleteInetRoute(NULL, "nat", "192.168.3.1/32");
    DeleteInetRoute(NULL, "nat", "192.168.4.1/32");
    task_util::WaitForIdle();
}

TEST_F(StaticRouteTest, N_ECMP_PATHADD) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(peers_[0], "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    EXPECT_EQ(static_rt->count(), 1);
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    DeleteInetRoute(peers_[0], "nat", "192.168.1.254/32");
    AddInetRoute(peers_[1], "nat", "192.168.1.254/32", 100, "2.3.1.5");
    AddInetRoute(peers_[2], "nat", "192.168.1.254/32", 100, "2.3.2.5");
    AddInetRoute(peers_[3], "nat", "192.168.1.254/32", 100, "2.3.3.5");
    scheduler->Start();
    task_util::WaitForIdle();

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 3, 1000, 10000, 
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 3);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin(); 
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        assert(path->GetPeer() != peers_[0]);
        set<string> list = GetRTargetFromPath(path);
        EXPECT_EQ(list, config_list);
        EXPECT_EQ(GetOriginVnFromRoute(path), "unresolved");

        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.2.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.2.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.3.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
        }
    }

    // Delete nexthop route
    DeleteInetRoute(peers_[1], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[2], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[3], "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, N_ECMP_PATHDEL) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    AddInetRoute(peers_[0], "nat", "192.168.1.254/32", 100, "2.3.1.5");
    AddInetRoute(peers_[1], "nat", "192.168.1.254/32", 100, "2.3.2.5");
    AddInetRoute(peers_[2], "nat", "192.168.1.254/32", 100, "2.3.3.5");
    AddInetRoute(peers_[3], "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    EXPECT_EQ(static_rt->count(), 4);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 4, 1000, 10000, 
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 4);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin(); 
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        set<string> list = GetRTargetFromPath(path);
        EXPECT_EQ(list, config_list);
        EXPECT_EQ(GetOriginVnFromRoute(path), "unresolved");

        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.2.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.2.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.3.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.4.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
        }
    }


    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    DeleteInetRoute(peers_[0], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[1], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[2], "nat", "192.168.1.254/32");
    scheduler->Start();
    task_util::WaitForIdle();

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 1, 1000, 10000, 
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 1);
    static_path = static_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(static_path->GetPathId()));
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(peers_[3], "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, TunnelEncap) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    set<string> encap = list_of("gre")("vxlan");
    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5", encap);
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> tunnel_encap_list = GetTunnelEncapListFromRoute(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(encap, tunnel_encap_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    encap = list_of("udp");
    // Update Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5", encap);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat..");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    list = GetRTargetFromPath(static_path);
    tunnel_encap_list = GetTunnelEncapListFromRoute(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(encap, tunnel_encap_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}


TEST_F(StaticRouteTest, MultiPathTunnelEncap) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    set<string> encap_1 = list_of("gre");
    set<string> encap_2 = list_of("udp");
    set<string> encap_3 = list_of("vxlan");
    AddInetRoute(peers_[0], "nat", "192.168.1.254/32", 100, "2.3.1.5", encap_1, vector<uint32_t>());
    AddInetRoute(peers_[1], "nat", "192.168.1.254/32", 100, "2.3.2.5", encap_2, vector<uint32_t>());
    AddInetRoute(peers_[2], "nat", "192.168.1.254/32", 100, "2.3.3.5", encap_3, vector<uint32_t>());
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("nat", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    BgpRoute *static_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "blue");

    static_rt = InetRouteLookup("nat", "192.168.1.0/24");
    static_path = static_rt->BestPath();
    set<string> list = GetRTargetFromPath(static_path);
    set<string> config_list = list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(GetOriginVnFromRoute(static_path), "unresolved");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 3, 1000, 10000, 
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 3);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin(); 
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        EXPECT_EQ(GetOriginVnFromRoute(path), "unresolved");
        set<string> list = GetTunnelEncapListFromRoute(path);

        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
            EXPECT_EQ(encap_1, list);
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.2.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.2.5");
            EXPECT_EQ(encap_2, list);
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.3.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
            EXPECT_EQ(encap_3, list);
        }
    }

    // Delete nexthop route
    DeleteInetRoute(peers_[0], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[1], "nat", "192.168.1.254/32");
    DeleteInetRoute(peers_[2], "nat", "192.168.1.254/32");
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, DeleteEntryReuse) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();
    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    DisableStaticRouteQ("nat");
    // Delete nexthop route
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    BgpRoute *nexthop_rt = InetRouteLookup("nat", "192.168.1.254/32");
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             true, 1000, 10000, 
                             "Wait for delete marking of nexthop route in nat");
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             false, 1000, 10000, 
                             "Wait for clear of delete flag on nexthop route ");
    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             true, 1000, 10000, 
                             "Wait for delete marking of nexthop route in nat");
    EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(IsQueueEmpty("nat"));
    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

TEST_F(StaticRouteTest, EntryAfterStop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params = 
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 100, "2.3.4.5");
    task_util::WaitForIdle();
    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");

    DisableStaticRouteQ("nat");

    ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance", 
                                         "nat", "static-route-entries");
    AddInetRoute(NULL, "nat", "192.168.1.254/32", 200, "2.3.4.5");

    EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(IsQueueEmpty("nat"));

    DeleteInetRoute(NULL, "nat", "192.168.1.254/32");
    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Static route in blue..");
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
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
