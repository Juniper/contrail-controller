/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/istatic_route_mgr.h"
#include "bgp/routing-instance/routing_instance.h"

#include <algorithm>
#include <boost/regex.hpp>
#include <fstream>
#include <iostream>
#include <string>


#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
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
#include "net/community_type.h"
#include <pugixml/pugixml.hpp>
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

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

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
  typedef T3 RouteT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix, InetRoute> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix, Inet6Route> Inet6Definition;

//
// Fixture class template - instantiated later for each TypeDefinition.
//
template <typename T>
class StaticRouteTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;

    StaticRouteTest()
        : bgp_server_(new BgpServer(&evm_)),
        family_(GetFamily()),
        ipv6_prefix_("::ffff:"),
        ri_mgr_(NULL),
        validate_done_(false) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }

    ~StaticRouteTest() {
        STLDeleteValues(&peers_);
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            return instance + ".inet.0";
        }
        if (family_ == Address::INET6) {
            return instance + ".inet6.0";
        }
        assert(false);
        return "";
    }

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr;// + "/32";
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;// + "/128";
        }
        assert(false);
        return "";
    }

    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    string BuildNextHopAddress(const string &ipv4_addr) const {
        // return BuildHostAddress(ipv4_addr);
        return ipv4_addr;
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
        BgpIfmapConfigManager *config_manager =
                static_cast<BgpIfmapConfigManager *>(
                    bgp_server_->config_manager());
        config_manager->Initialize(&config_db_, &config_graph_, "localhost");
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

    void DisableResolveTrigger(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        rtinstance->static_route_mgr(family_)->DisableResolveTrigger();
    }

    void EnableResolveTrigger(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        if (rtinstance)
            rtinstance->static_route_mgr(family_)->EnableResolveTrigger();
    }

    void DisableStaticRouteQ(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        rtinstance->static_route_mgr(family_)->DisableQueue();
    }

    bool IsQueueEmpty(const string  &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        return rtinstance->static_route_mgr(family_)->IsQueueEmpty();
    }

    void EnableStaticRouteQ(const string  &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        rtinstance->static_route_mgr(family_)->EnableQueue();
    }

    void AddRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref,
                      string nexthop_str = "",
                      std::set<string> encap = std::set<string>(),
                      std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                      uint32_t flags=0, int label=0,
                      const LoadBalance &lb = LoadBalance()) {
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

        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr;
        if (nexthop_str.empty())
            nexthop_str = this->BuildNextHopAddress("7.8.9.1");

        IpAddress chain_addr = Ip4Address::from_string(nexthop_str, error);
        nexthop_attr.reset(new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
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

        if (!lb.IsDefault())
            ext_comm.communities.push_back(lb.GetExtCommunityValue());

        attr_spec.push_back(&ext_comm);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(64513);
        path_seg->path_segment.push_back(64514);
        path_seg->path_segment.push_back(64515);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, flags, label));

        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void DeleteRoute(IPeer *peer, const string &instance_name,
                         const string &prefix) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }


    BgpRoute *RouteLookup(const string &instance_name,
                              const string &prefix) {
        BgpTable *table = GetTable(instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        return static_cast<BgpRoute *>(table->Find(&key));
    }

    set<string> GetRTargetFromPath(const BgpPath *path) {
        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();
        set<string> rtlist;
        if (ext_community) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_community->communities()) {
                if (!ExtCommunity::is_route_target(comm))
                    continue;
                RouteTarget rtarget(comm);
                rtlist.insert(rtarget.ToString());
            }
        }
        return rtlist;
    }

    std::set<std::string> GetTunnelEncapListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        std::set<std::string> list;
        if  (ext_comm) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_comm->communities()) {
                if (!ExtCommunity::is_tunnel_encap(comm))
                    continue;
                TunnelEncap encap(comm);
                list.insert(encap.ToXmppString());
            }
        }
        return list;
    }

    std::string GetOriginVnFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        if  (ext_comm) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_comm->communities()) {
                if (!ExtCommunity::is_origin_vn(comm))
                    continue;
                OriginVn origin_vn(comm);
                return ri_mgr_->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
            }
        }
        return "unresolved";
    }

    string GetNextHopAddress(BgpAttrPtr attr) {
        return attr->nexthop().to_v4().to_string();
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
        string content;

        // Convert IPv4 Prefix to IPv6 for IPv6 tests
        if (family_ == Address::INET6) {
            std::ifstream input(filename.c_str());
            boost::regex e1 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)\\/(\\d+)(.*$)");
            boost::regex e2 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)(.*$)");
            for (string line; getline(input, line);) {
                boost::cmatch cm;
                if (boost::regex_match(line.c_str(), cm, e1)) {
                    const string prefix(cm[2].first, cm[2].second);
                    content += string(cm[1].first, cm[1].second) +
                        BuildPrefix(prefix, atoi(string(cm[3].first,
                                                    cm[3].second).c_str())) +
                        string(cm[4].first, cm[4].second);
                } else if (boost::regex_match(line.c_str(), cm, e2)) {
                    content += string(cm[1].first, cm[1].second) +
                        BuildHostAddress(string(cm[2].first, cm[2].second)) +
                        string(cm[3].first, cm[3].second);
                } else {
                    content += line;
                }
            }
        } else {
            content = FileRead(filename);
        }
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
                                                string &result,
                                                StaticRouteTest *self) {
        ShowStaticRouteResp *resp =
            dynamic_cast<ShowStaticRouteResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowStaticRouteResp *)NULL, resp);
        self->validate_done_ = true;

        TASK_UTIL_EXPECT_EQ(1, resp->get_static_route_entries().size());
        int i = 0;
        BOOST_FOREACH(const StaticRouteEntriesInfo &info,
                      resp->get_static_route_entries()) {
            TASK_UTIL_EXPECT_EQ(info.get_ri_name(), result);
            i++;
        }
    }

    void VerifyStaticRouteSandesh(std::string ri_name) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
                boost::bind(ValidateShowStaticRouteResponse, _1, ri_name,
                    this));
        ShowStaticRouteReq *req = new ShowStaticRouteReq;
        req->set_search_string(ri_name);
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }

    BgpTable *GetTable(std::string instance_name) {
        return static_cast<BgpTable *>(bgp_server_->database()->FindTable(
                    GetTableName(instance_name)));

    }

    void VerifyTableNoExists(const string &table_name) {
        TASK_UTIL_EXPECT_EQ(static_cast<BgpTable *>(NULL),
                            GetTable(table_name));
    }

    void VerifyStaticRouteCount(uint32_t count) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_static_routes());
    }

    void VerifyDownStaticRouteCount(uint32_t count) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_down_static_routes());
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    RoutingInstanceMgr *ri_mgr_;
    vector<BgpPeerMock *> peers_;
    bool validate_done_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family StaticRouteTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family StaticRouteTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(StaticRouteTest, TypeDefinitionList);

TYPED_TEST(StaticRouteTest, InvalidNextHop) {
    vector<string> instance_names = list_of("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_9a.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_,
            "routing-instance", "nat", "static-route-entries",
            params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_9b.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_9c.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
}

TYPED_TEST(StaticRouteTest, InvalidPrefix) {
    vector<string> instance_names = list_of("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_10a.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_10b.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_10c.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
}

TYPED_TEST(StaticRouteTest, InvalidRouteTarget) {
    vector<string> instance_names = list_of("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
            100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_11a.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    BgpRoute *static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    set<string> config_list = list_of("target:64496:1")("target:64496:3");
    TASK_UTIL_EXPECT_TRUE(this->GetRTargetFromPath(static_path) == config_list);

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_11b.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    config_list = list_of("target:64496:2")("target:64496:3");
    TASK_UTIL_EXPECT_TRUE(this->GetRTargetFromPath(static_path) == config_list);

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_11c.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
        "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");
    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    config_list = list_of("target:64496:1")("target:64496:2");
    TASK_UTIL_EXPECT_TRUE(this->GetRTargetFromPath(static_path) == config_list);

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
}

//
// Basic Test
// 1. Configure routing instance with static route property
// 2. Add the nexthop route
// 3. Validate the static route in both source (nat) and destination
// routing instance
TYPED_TEST(StaticRouteTest, Basic) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat",
                       this->BuildPrefix("192.168.1.254", 32), 100,
                       this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup("nat",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    this->VerifyStaticRouteSandesh("nat");
    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, UpdateRtList) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_3.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_3.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    list = this->GetRTargetFromPath(static_path);
    config_list = list_of("target:1:1");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, UpdateNexthop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    static_rt =
    this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
    list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    params = this->GetStaticRouteConfig(
        "controller/src/bgp/testdata/static_route_4.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32), 100,
                       this->BuildNextHopAddress("5.4.3.2"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->GetNextHopAddress(attr),
              this->BuildNextHopAddress("5.4.3.2"));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    list = this->GetRTargetFromPath(static_path);
    config_list = list_of("target:64496:1");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32));
    task_util::WaitForIdle();
}

TYPED_TEST(StaticRouteTest, MultiplePrefix) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");

    this->VerifyStaticRouteCount(0);
    this->VerifyDownStaticRouteCount(0);

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_2.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(3);

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(2);

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
    this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
                       this->BuildNextHopAddress("9.8.7.6"));
    task_util::WaitForIdle();

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(0);

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
        this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24)),
        NULL, 1000, 10000, "Wait for Static route in blue..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.0.0", 16)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("9.8.7.6"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.0.0", 16));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("9.8.7.6"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.0.0", 16));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));
    task_util::WaitForIdle();

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(3);
}

TYPED_TEST(StaticRouteTest, MultiplePrefixSameNexthopAndUpdateNexthop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");
    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_5.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
                       this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->GetNextHopAddress(attr),
              this->BuildNextHopAddress("2.3.4.5"));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.3.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
                       this->BuildNextHopAddress("5.3.4.5"));
    task_util::WaitForIdle();

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("5.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("5.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.3.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("5.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));
    task_util::WaitForIdle();
}


TYPED_TEST(StaticRouteTest, ConfigUpdate) {
    vector<string> instance_names = list_of("blue")("nat")("red");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    set<string> config_list = list_of("target:64496:1");

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_6.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
                       this->BuildNextHopAddress("3.4.5.6"));
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.3.1", 32), 100,
                       this->BuildNextHopAddress("9.8.7.6"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.0.0", 16)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.0.0", 16));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_7.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.0.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("red", this->BuildPrefix("192.168.2.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in red..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.4.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.4.1", 32), 100,
                       this->BuildNextHopAddress("9.8.7.6"));
    task_util::WaitForIdle();

    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.3.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.4.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    set<string> config_list_1 = list_of("target:64496:3");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.2.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list_1);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.3.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.4.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.3.1", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.4.1", 32));
    task_util::WaitForIdle();
}

TYPED_TEST(StaticRouteTest, N_ECMP_PATHADD) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.1"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.2"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.3"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.4"), ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(this->peers_[0], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    EXPECT_EQ(static_rt->count(), 1);
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    this->DeleteRoute(this->peers_[0], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    this->AddRoute(this->peers_[1], "nat",
                       this->BuildPrefix("192.168.1.254", 32), 100,
                       this->BuildNextHopAddress("2.3.1.5"));
    this->AddRoute(this->peers_[2], "nat",
                       this->BuildPrefix("192.168.1.254", 32), 100,
                       this->BuildNextHopAddress("2.3.2.5"));
    this->AddRoute(this->peers_[3], "nat",
                       this->BuildPrefix("192.168.1.254", 32), 100,
                       this->BuildNextHopAddress("2.3.3.5"));
    scheduler->Start();
    task_util::WaitForIdle();

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 3, 1000, 10000,
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 3);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin();
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        assert(path->GetPeer() != this->peers_[0]);
        set<string> list = this->GetRTargetFromPath(path);
        EXPECT_EQ(list, config_list);
        EXPECT_EQ(this->GetOriginVnFromRoute(path), "unresolved");

        if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.1.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.1.5"));
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.2.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.2.5"));
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.3.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.3.5"));
        }
    }

    // Delete nexthop route
    this->DeleteRoute(
            this->peers_[1], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
            this->peers_[2], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
            this->peers_[3], "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, N_ECMP_PATHDEL) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.1"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.2"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.3"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.4"), ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    this->AddRoute(this->peers_[0], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.1.5"));
    this->AddRoute(this->peers_[1], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.2.5"));
    this->AddRoute(this->peers_[2], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.3.5"));
    this->AddRoute(this->peers_[3], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.1.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    EXPECT_EQ(static_rt->count(), 4);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 4, 1000, 10000,
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 4);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin();
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        set<string> list = this->GetRTargetFromPath(path);
        EXPECT_EQ(list, config_list);
        EXPECT_EQ(this->GetOriginVnFromRoute(path), "unresolved");

        if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.1.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.1.5"));
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.2.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.2.5"));
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.3.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.3.5"));
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.4.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.4.5"));
        }
    }

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    this->DeleteRoute(this->peers_[0], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[1], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[2], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    scheduler->Start();
    task_util::WaitForIdle();

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 1, 1000, 10000,
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 1);
    static_path = static_rt->BestPath();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              BgpPath::PathIdString(static_path->GetPathId()));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(
            this->peers_[3], "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, TunnelEncap) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    set<string> encap = list_of("gre")("vxlan");
    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"), encap);
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->GetNextHopAddress(attr),
              this->BuildNextHopAddress("2.3.4.5"));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> tunnel_encap_list =
        this->GetTunnelEncapListFromRoute(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(encap, tunnel_encap_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    encap = list_of("udp");
    // Update Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"), encap);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat..");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    tunnel_encap_list = this->GetTunnelEncapListFromRoute(static_path);
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(encap, tunnel_encap_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, LoadBalance) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Create non-default load balance attribute
    LoadBalance lb = LoadBalance();
    lb.SetL3SourceAddress(false);

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                   100, this->BuildNextHopAddress("2.3.4.5"),
                   std::set<string>(), std::vector<uint32_t>(), 0, 0, lb);
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->GetNextHopAddress(attr),
              this->BuildNextHopAddress("2.3.4.5"));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    LoadBalance static_path_lb = LoadBalance(static_path);
    EXPECT_EQ(lb, static_path_lb);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    lb.SetL3DestinationAddress(false);
    // Update Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                   100, this->BuildNextHopAddress("2.3.4.5"),
                   std::set<string>(), std::vector<uint32_t>(), 0, 0, lb);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat..");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    list = this->GetRTargetFromPath(static_path);
    EXPECT_EQ(list, config_list);
    static_path_lb = LoadBalance(static_path);
    EXPECT_EQ(lb, static_path_lb);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}


TYPED_TEST(StaticRouteTest, MultiPathTunnelEncap) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.1"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.2"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.3"), ec)));
    this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
                    this->BuildHostAddress("192.168.0.4"), ec)));

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Add Nexthop Route
    set<string> encap_1 = list_of("gre");
    set<string> encap_2 = list_of("udp");
    set<string> encap_3 = list_of("vxlan");
    this->AddRoute(this->peers_[0], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.1.5"), encap_1, vector<uint32_t>());
    this->AddRoute(this->peers_[1], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.2.5"), encap_2, vector<uint32_t>());
    this->AddRoute(this->peers_[2], "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.3.5"), encap_3, vector<uint32_t>());
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->GetNextHopAddress(attr),
              this->BuildNextHopAddress("2.3.1.5"));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    static_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    set<string> list = this->GetRTargetFromPath(static_path);
    set<string> config_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    EXPECT_EQ(list, config_list);
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    // Check for static route count
    TASK_UTIL_WAIT_EQ_NO_MSG(static_rt->count(), 3, 1000, 10000,
                             "Wait for all paths in static route ..");
    EXPECT_EQ(static_rt->count(), 3);
    for (Route::PathList::iterator it = static_rt->GetPathList().begin();
         it != static_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        EXPECT_EQ(this->GetOriginVnFromRoute(path), "unresolved");
        set<string> list = this->GetTunnelEncapListFromRoute(path);

        if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.1.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.1.5"));
            EXPECT_EQ(encap_1, list);
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.2.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.2.5"));
            EXPECT_EQ(encap_2, list);
        } else if (BgpPath::PathIdString(path->GetPathId()) ==
                this->BuildNextHopAddress("2.3.3.5")) {
            EXPECT_EQ(this->GetNextHopAddress(attr),
                      this->BuildNextHopAddress("2.3.3.5"));
            EXPECT_EQ(encap_3, list);
        }
    }

    // Delete nexthop route
    this->DeleteRoute(this->peers_[0], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[1], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[2], "nat",
                          this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, DeleteEntryReuse) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat",
            this->BuildPrefix("192.168.1.254", 32), 100,
            this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();
    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->DisableStaticRouteQ("nat");
    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    BgpRoute *nexthop_rt =
        this->RouteLookup("nat", this->BuildPrefix("192.168.1.254", 32));
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             true, 1000, 10000,
                             "Wait for delete marking of nexthop route in nat");
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             false, 1000, 10000,
                             "Wait for clear of delete flag on nexthop route ");
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    TASK_UTIL_WAIT_EQ_NO_MSG(nexthop_rt->IsDeleted(),
                             true, 1000, 10000,
                             "Wait for delete marking of nexthop route in nat");
    this->EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat"));
    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

TYPED_TEST(StaticRouteTest, EntryAfterStop) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                                         "nat", "static-route-entries",
                                         params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();
    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->DisableStaticRouteQ("nat");

    ifmap_test_util::IFMapMsgPropertyDelete(
            &this->config_db_, "routing-instance",
            "nat", "static-route-entries");
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       200, this->BuildNextHopAddress("2.3.4.5"));

    this->EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat"));

    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

//
// Delete the routing instance that imports the static route and make sure
// the inet table gets deleted. Objective is to check that the static route
// is removed from the table even though the static route config has not
// changed.
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance) {
    vector<string> instance_names = list_of("blue")("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Delete the configuration for the blue instance.
    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "blue",
            "virtual-network", "blue", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "blue", "route-target", "target:64496:1", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "blue");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "blue");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:1");
    task_util::WaitForIdle();

    // Make sure that the blue inet table is gone.
    this->VerifyTableNoExists(this->GetTableName("blue"));

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
}

//
// Delete the static route config and instance with resolve_trigger disabled
// Allow the routing instance to get deleted with Resolve trigger
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance_DisabledResolveTrigger) {
    vector<string> instance_names = list_of("blue")("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Disable resolve trigger
    this->DisableResolveTrigger("nat");

    // Delete the configuration for the nat instance.
    ifmap_test_util::IFMapMsgPropertyDelete(
            &this->config_db_, "routing-instance",
            "nat", "static-route-entries");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat",
            "virtual-network", "nat", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    this->EnableResolveTrigger("nat");
}

//
// Delete the static route config and instance with resolve_trigger disabled
// Routing instance is not destroyed when the task trigger is enabled.
// Verify that enabling the task trigger ignores the deleted routing instance
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance_DisabledResolveTrigger_1) {
    vector<string> instance_names = list_of("blue")("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Disable resolve trigger
    this->DisableResolveTrigger("nat");

    // Delete the configuration for the nat instance.
    ifmap_test_util::IFMapMsgPropertyDelete(
            &this->config_db_, "routing-instance",
            "nat", "static-route-entries");

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Delete the nat routing instance
    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat",
            "virtual-network", "nat", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    RoutingInstance *nat_inst = this->ri_mgr_->GetRoutingInstance("nat");
    TASK_UTIL_WAIT_EQ_NO_MSG(nat_inst->deleted(),
            true, 1000, 10000, "Wait for nat instance to be marked deleted");
    //
    // Since the nexthop route is not yet deleted, routing instance is
    // not destroyed
    //
    this->EnableResolveTrigger("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_EQ_NO_MSG(this->ri_mgr_->GetRoutingInstance("nat"),
            NULL, 1000, 10000, "Wait for nat instance to get destroyed");
}

//
// Add the routing instance that imports the static route after the static
// route has already been added. Objective is to check that the static route
// is replicated to the table in the new instance without any triggers to
// the static route module.
//
TYPED_TEST(StaticRouteTest, AddRoutingInstance) {
    vector<string> instance_names = list_of("nat");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat..");

    // Add the blue instance.
    // Make sure that the id and route target for nat instance don't change.
    instance_names = list_of("nat")("blue");
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();
}

//
// Validate static route functionality in VN's default routing instance
//
// 1. Configure VN's default routing instance with static route property
// 2. Add the nexthop route in the same instance
// 3. Validate the static route in the VN's default routing instance
//
TYPED_TEST(StaticRouteTest, DefaultRoutingInstance) {
    vector<string> instance_names = list_of("blue");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_12.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "blue", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    // Add Nexthop Route
    set<string> encap = list_of("gre")("udp");
    this->AddRoute(NULL, "blue",
                   this->BuildPrefix("192.168.1.254", 32), 100,
                   this->BuildNextHopAddress("2.3.4.5"), encap);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(this->BuildNextHopAddress("2.3.4.5"),
              this->GetNextHopAddress(attr));
    EXPECT_EQ(0, this->GetRTargetFromPath(static_path).size());
    EXPECT_EQ(encap, this->GetTunnelEncapListFromRoute(static_path));
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");
    EXPECT_TRUE(attr->as_path() == NULL);
    EXPECT_TRUE(attr->community() != NULL);
    EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::AcceptOwnNexthop));

    this->VerifyStaticRouteSandesh("blue");

    // Delete nexthop route
    this->DeleteRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

//
// Verify that a change in VN index is reflected in static routes for VN's
// default routing instance.
//
TYPED_TEST(StaticRouteTest, VirtualNetworkIndexChange) {
    vector<string> instance_names = list_of("blue");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    autogen::VirtualNetwork::NtProperty *blue_vni_0 =
        new autogen::VirtualNetwork::NtProperty;
    blue_vni_0->data = 0;
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_,
        "virtual-network", "blue", "virtual-network-network-id", blue_vni_0);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_12.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "blue", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Nexthop Route
    this->AddRoute(NULL, "blue",
                   this->BuildPrefix("192.168.1.254", 32), 100,
                   this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    BgpRoute *static_rt =
        this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    const BgpPath *static_path = static_rt->BestPath();
    BgpAttrPtr attr = static_path->GetAttr();
    EXPECT_EQ(0, this->GetRTargetFromPath(static_path).size());
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "unresolved");

    autogen::VirtualNetwork::NtProperty *blue_vni_1 =
        new autogen::VirtualNetwork::NtProperty;
    blue_vni_1->data = 1;
    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_,
        "virtual-network", "blue", "virtual-network-network-id", blue_vni_1);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup("blue",
                             this->BuildPrefix("192.168.1.0", 24)),
                             NULL, 1000, 10000,
                             "Wait for Static route in blue..");

    static_rt = this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24));
    static_path = static_rt->BestPath();
    attr = static_path->GetAttr();
    EXPECT_EQ(0, this->GetRTargetFromPath(static_path).size());
    EXPECT_EQ(this->GetOriginVnFromRoute(static_path), "blue");

    // Delete nexthop route
    this->DeleteRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");
}

// Sandesh introspect test
// Verify http introspect output
//   1. After creating the config and before nexthop route is published
//   2. After creating the config and after nexthop route is published
//   3. After updating the config(nexthop) and before new nexthop route is
//      published
//   4. After updating the config(nexthop) and after new nexthop route is
//      published
TYPED_TEST(StaticRouteTest, SandeshTest) {
    vector<string> instance_names = list_of("blue")("nat")("red")("green");
    multimap<string, string> connections;
    this->NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_1.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->VerifyStaticRouteSandesh("nat");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32),
                       100, this->BuildNextHopAddress("2.3.4.5"));
    task_util::WaitForIdle();

     // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("nat", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in nat instance..");

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->VerifyStaticRouteSandesh("nat");
    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    task_util::WaitForIdle();


    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_4.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_, "routing-instance",
                         "nat", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_EQ_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->VerifyStaticRouteSandesh("nat");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32), 100,
                       this->BuildNextHopAddress("5.4.3.2"));
    task_util::WaitForIdle();

    // Check for Static route
    TASK_UTIL_WAIT_NE_NO_MSG(
            this->RouteLookup("blue", this->BuildPrefix("192.168.1.0", 24)),
            NULL, 1000, 10000, "Wait for Static route in blue..");

    this->VerifyStaticRouteSandesh("nat");
    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32));
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
