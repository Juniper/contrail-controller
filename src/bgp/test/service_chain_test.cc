/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/service_chaining.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"

#include <fstream>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include <pugixml/pugixml.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/community.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using namespace pugi;

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
    virtual void UpdateRefCount(int count) { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }

private:
    Ip4Address address_;
};

class ServiceChainTest : public ::testing::Test {
protected:
    ServiceChainTest()
        : bgp_server_(new BgpServer(&evm_)) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~ServiceChainTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        bgp_schema_ParserInit(parser);
        vnc_cfg_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                                  "localhost");
        bgp_server_->service_chain_mgr()->set_aggregate_host_route(true);
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
        task_util::WaitForIdle();
    }

    void VerifyNetworkConfig(const vector<string> &instance_names) {
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(ri_mgr_->GetRoutingInstance(*iter),
                NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(*iter);
            TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
                0, 1000, 10000, "Wait for vn index..");
        }
    }

    void DisableServiceChainQ() {
        bgp_server_->service_chain_mgr()->DisableQueue();
    }

    void EnableServiceChainQ() {
        bgp_server_->service_chain_mgr()->EnableQueue();
    }
    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref, 
                      std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                      std::set<string> encap = std::set<string>(),
                      string nexthop="7.8.9.1", 
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
        const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
        TASK_UTIL_EXPECT_NE(0, rti->virtual_network_index());
        OriginVn origin_vn(0, rti->virtual_network_index());
        ext_comm.communities.push_back(origin_vn.GetExtCommunityValue());

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

    void AddInetVpnRoute(IPeer *peer, const string &instance_name,
                         const string &prefix, int localpref,
                         std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                         std::set<string> encap = std::set<string>(),
                         string nexthop="7.8.9.1",
                         uint32_t flags=0, int label=0) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        const RoutingInstance *rtinstance = table->routing_instance();
        ASSERT_TRUE(rtinstance != NULL);

        string vpn_prefix;
        if (peer) {
            vpn_prefix = peer->ToString() + ":" +
                boost::lexical_cast<std::string>(rtinstance->index()) + ":" +
                prefix;
        } else {
            vpn_prefix = "7.7.7.7:7777:" + prefix;
        }

        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        RouteTarget target = *(rtinstance->GetExportList().begin());
        uint64_t extcomm_value = get_value(target.GetExtCommunity().begin(), 8);
        ExtCommunitySpec extcomm_spec;
	    extcomm_spec.communities.push_back(extcomm_value);
        for(std::vector<uint32_t>::iterator it = sglist.begin(); 
            it != sglist.end(); it++) {
            SecurityGroup sgid(0, *it);
            extcomm_spec.communities.push_back(sgid.GetExtCommunityValue());
        }
        for(std::set<string>::iterator it = encap.begin(); 
            it != encap.end(); it++) {
            TunnelEncap tunnel_encap(*it);
            extcomm_spec.communities.push_back(tunnel_encap.GetExtCommunityValue());
        }
        const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
        TASK_UTIL_EXPECT_NE(0, rti->virtual_network_index());
        OriginVn origin_vn(0, rti->virtual_network_index());
        extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
 
        attr_spec.push_back(&extcomm_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void AddInetVpnRoute(IPeer *peer, const vector<string> &instance_names,
                         const string &prefix, int localpref,
                         string nexthop="7.8.9.1",
                         uint32_t flags=0, int label=0) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_names[0]);
        ASSERT_TRUE(rtinstance != NULL);

        string vpn_prefix;
        if (peer) {
            vpn_prefix = peer->ToString() + ":" +
                boost::lexical_cast<std::string>(rtinstance->index()) + ":" +
                prefix;
        } else {
            vpn_prefix = "7.7.7.7:7777:" + prefix;
        }

        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        ExtCommunitySpec extcomm_spec;
        BOOST_FOREACH(const string &instance_name, instance_names) {
            RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
            ASSERT_TRUE(rti != NULL);
            ASSERT_EQ(1, rti->GetExportList().size());
            RouteTarget rtarget = *(rti->GetExportList().begin());
            extcomm_spec.communities.push_back(rtarget.GetExtCommunityValue());
        }
        attr_spec.push_back(&extcomm_spec);
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void DeleteInetVpnRoute(IPeer *peer, const string &instance_name,
                            const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        const RoutingInstance *rtinstance = table->routing_instance();
        ASSERT_TRUE(rtinstance != NULL);

        string vpn_prefix;
        if (peer) {
            vpn_prefix = peer->ToString() + ":" +
                boost::lexical_cast<std::string>(rtinstance->index()) + ":" +
                prefix;
        } else {
            vpn_prefix = "7.7.7.7:7777:" + prefix;
        }

        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void AddConnectedRoute(IPeer *peer, const string &prefix,
                   int localpref, string nexthop="7.8.9.1",
                   uint32_t flags=0, int label=0,
                   std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                   std::set<string> encap = std::set<string>()) {
        if (connected_rt_is_inetvpn_) {
            AddInetVpnRoute(peer, connected_table_, prefix,
                    localpref, sglist, encap, nexthop, flags, label);
        } else {
            AddInetRoute(peer, connected_table_, prefix,
                    localpref, sglist, encap, nexthop, flags, label);
        }
    }

    void DeleteConnectedRoute(IPeer *peer, const string &prefix) {
        if (connected_rt_is_inetvpn_) {
            DeleteInetVpnRoute(peer, connected_table_, prefix);
        } else {
            DeleteInetRoute(peer, connected_table_, prefix);
        }
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

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    void AddRoutingInstance(string name, string connection) {
        stringstream target;
        target << "target:64496:" << 100;

        BGP_DEBUG_UT("ADD routing instance " << name << " Route Target " << target.str());
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "route-target", target.str(),
                                      "instance-target");
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));

        BGP_DEBUG_UT("ADD connection " << name << "<->" << connection); 
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "routing-instance", connection,
                                      "connection");
        task_util::WaitForIdle();
    }

    void RemoveRoutingInstance(string name, string connection) {
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                        "routing-instance", name,
                                        "routing-instance", connection,
                                        "connection");
        //
        // Cache a copy of the export route-targets before the instance is
        // deleted
        //
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        const RoutingInstance::RouteTargetList
            target_list(rti->GetExportList());
        BOOST_FOREACH(RouteTarget tgt, target_list) {
            ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                            "routing-instance", name,
                                            "route-target", tgt.ToString(),
                                            "instance-target");
        }
    }

    std::auto_ptr<autogen::ServiceChainInfo> 
        GetChainConfig(std::string filename) {
        std::auto_ptr<autogen::ServiceChainInfo> 
            params (new autogen::ServiceChainInfo());
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

    std::vector<uint32_t> GetSGIDListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        std::vector<uint32_t> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_security_group(comm))
                continue;
            SecurityGroup security_group(comm);

            list.push_back(security_group.security_group_id());
        }
        std::sort(list.begin(), list.end());
        return list;
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

    static void ValidateShowServiceChainResponse(Sandesh *sandesh, 
                                                 vector<std::string> &result) {
        ShowServiceChainResp *resp = 
            dynamic_cast<ShowServiceChainResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowServiceChainResp *)NULL, resp);
        validate_done_ = 1;

        TASK_UTIL_EXPECT_EQ(result.size(), 
                              resp->get_service_chain_list().size());
        int i = 0;
        cout << "*******************************************************"<<endl;
        BOOST_FOREACH(const ShowServicechainInfo &info, 
                      resp->get_service_chain_list()) {
            TASK_UTIL_EXPECT_EQ(info.get_src_rt_instance(), result[i]);
            cout << info.log() << endl;
            i++;
        }
        cout << "*******************************************************"<<endl;
    }

    static void ValidateShowPendingServiceChainResponse(Sandesh *sandesh, 
                                                 vector<std::string> &result) {
        ShowPendingServiceChainResp *resp = 
            dynamic_cast<ShowPendingServiceChainResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowPendingServiceChainResp *)NULL, resp);

        TASK_UTIL_EXPECT_TRUE((result == resp->get_pending_chains()));

        validate_done_ = 1;
    }


    void VerifyServiceChainSandesh(std::vector<std::string> result) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateShowServiceChainResponse,
                                                   _1, result));
        ShowServiceChainReq *req = new ShowServiceChainReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
    }

    void VerifyPendingServiceChainSandesh(std::vector<std::string> pending) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateShowPendingServiceChainResponse,
                                                   _1, pending));
        ShowPendingServiceChainReq *req = new ShowPendingServiceChainReq;
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
    const char *connected_table_;
    bool connected_rt_is_inetvpn_;
    static int validate_done_;
};

int ServiceChainTest::validate_done_;
// Parameterize the service type (transparent vs. in-network).

typedef std::tr1::tuple<bool, bool> TestParams;

class ServiceChainParamTest :
    public ServiceChainTest,
    public ::testing::WithParamInterface<TestParams> {
    virtual void SetUp() {
        connected_table_ =
	        std::tr1::get<0>(GetParam()) ? "blue-i1" : "blue";
        connected_rt_is_inetvpn_ = std::tr1::get<1>(GetParam());
        ServiceChainTest::SetUp();
    }

    virtual void TearDown() {
        ServiceChainTest::TearDown();
    }
};


TEST_P(ServiceChainParamTest, Basic) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, MoreSpecificAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Add different more specific
    AddInetRoute(NULL, "red", "192.168.1.34/32", 100);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Add different more specific
    AddInetRoute(NULL, "red", "192.168.2.34/32", 100);
    task_util::WaitForIdle();
    
    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.2.34/32");
    DeleteInetRoute(NULL, "red", "192.168.1.34/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ConnectedAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}


TEST_P(ServiceChainParamTest, DeleteConnected) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    LOG(DEBUG, "XXXXXX -- Delete the connected route 1.1.2.3/32 --- XXXXXX");
    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    LOG(DEBUG, "XXXXXX -- Add more specific route 192.168.2.1/32 --- XXXXXX");
    AddInetRoute(NULL, "red", "192.168.2.1/32", 100);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();
    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.2.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, StopServiceChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");
    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance", 
                                            "blue-i1", 
                                            "service-chain-information");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ServiceChainWithExistingRouteEntries) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.2/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.3/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.4/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.1/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.2/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.3/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.4/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance", 
                                            "blue-i1", 
                                            "service-chain-information");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "192.168.1.3/32");
    DeleteInetRoute(NULL, "red", "192.168.1.4/32");
    DeleteInetRoute(NULL, "red", "192.168.2.1/32");
    DeleteInetRoute(NULL, "red", "192.168.2.2/32");
    DeleteInetRoute(NULL, "red", "192.168.2.3/32");
    DeleteInetRoute(NULL, "red", "192.168.2.4/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, UpdateNexthop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "3.4.5.6");
    task_util::WaitForIdle();

    int count = 0;
    while(1) {
        // Check for aggregated route
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                                 NULL, 1000, 10000, 
                                 "Wait for Aggregate route in blue..");

        BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
        const BgpPath *aggregate_path = aggregate_rt->BestPath();
        BgpAttrPtr attr = aggregate_path->GetAttr();
        if (attr->nexthop().to_v4().to_string() == "3.4.5.6") {
            break;
        }
        if (count++ == 100) {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "3.4.5.6");
            EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");
            break;
        }
    }

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}


TEST_P(ServiceChainParamTest, UpdateLabel) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 16);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ(aggregate_path->GetLabel(), 16);

    // Add Connected with updated label
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 32);
    task_util::WaitForIdle();

    int count = 0;
    while(1) {
        // Check for aggregated route
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                                 NULL, 1000, 10000, 
                                 "Wait for Aggregate route in blue..");

        BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
        const BgpPath *aggregate_path = aggregate_rt->BestPath();
        if (aggregate_path->GetLabel() == 32) {
            break;
        }
        if (count++ == 100) {
            EXPECT_EQ(aggregate_path->GetLabel(), 32);
            break;
        }
    }

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, DeleteRoutingInstance) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    RemoveRoutingInstance("blue-i1", "blue");
    task_util::WaitForIdle();

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}


TEST_P(ServiceChainParamTest, PendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    VerifyPendingServiceChainSandesh(list_of("blue-i1"));

    // Add "red" routing instance and create connection with "red-i2"
    instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    connections = map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    // Add MoreSpecific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, UnresolvedPendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    VerifyPendingServiceChainSandesh(list_of("blue-i1"));

    ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance", 
                                            "blue-i1", 
                                            "service-chain-information");
    task_util::WaitForIdle();

    // Delete connected
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, UpdateChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_3.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();
    AddInetRoute(NULL, "red", "192.169.2.1/32", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.169.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    VerifyServiceChainSandesh(list_of("blue-i1"));

    params = GetChainConfig("controller/src/bgp/testdata/service_chain_2.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.169.2.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    VerifyServiceChainSandesh(list_of("blue-i1"));

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.169.2.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, PeerUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 90, "2.3.0.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ(aggregate_rt->count(), 1);
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.0.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    task_util::WaitForIdle();
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.1.5", BgpPath::PathIdString(aggregate_path->GetPathId()));

    AddConnectedRoute(peers_[2], "1.1.2.3/32", 95, "2.3.2.5");
    task_util::WaitForIdle();
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.1.5", BgpPath::PathIdString(aggregate_path->GetPathId()));

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.2.5", BgpPath::PathIdString(aggregate_path->GetPathId()));

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add more specific route 192.168.1.1/32
// 3. Add MX leaked route 10.1.1.0/24
// 4. Add connected routes from 2 peers with same forwarding information
// 5. Verify aggregate route exists and has only one path
// 6. Verify ext connected route exists and has only one path
// 7. Remove one of the connected routes
// 8. Verify aggregate route exists and still has one path
// 9. Verify ext connected route exists and still has one path
//
TEST_P(ServiceChainParamTest, DuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params =
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
                                         "blue-i1",
                                         "service-chain-information",
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected with duplicate forwarding information
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.4.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    EXPECT_EQ(aggregate_rt->count(), 1);
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    EXPECT_EQ(ext_rt->count(), 1);
    attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete connected route from peers_[0]
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for Aggregate route in blue..");

    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    EXPECT_EQ(aggregate_rt->count(), 1);
    attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");

    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    EXPECT_EQ(ext_rt->count(), 1);
    attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete Ext connect route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, EcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    EXPECT_EQ(aggregate_rt->count(), 1);
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.0.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    task_util::WaitForIdle();
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");

    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 2, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 2);

    std::string path_ids[] = {"2.3.0.5", "2.3.1.5"};
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            BgpPath *path = static_cast<BgpPath *>(it.operator->());
            if (BgpPath::PathIdString(path->GetPathId()) == path_id) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_TRUE(BgpPath::PathIdString(path->GetPathId()) != "2.3.1.5");
    }

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, EcmpPathUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 2, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 2);

    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.0.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.0.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
        }
        EXPECT_EQ(GetOriginVnFromRoute(path), "red");
    }
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.8");
    task_util::WaitForIdle();

    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 2, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 2);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.0.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.0.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.8") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.8");
        }
        EXPECT_EQ(GetOriginVnFromRoute(path), "red");
    }

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, N_ECMP_PATHADD) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.2.5");
    AddConnectedRoute(peers_[3], "1.1.2.3/32", 100, "2.3.3.5");
    scheduler->Start();
    task_util::WaitForIdle();

    // Check for aggregated route count
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 3, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 3);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        BgpAttrPtr attr = path->GetAttr();
        assert(path->GetPeer() != peers_[0]);

        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.2.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.2.5");
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.3.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
        }
        EXPECT_EQ(GetOriginVnFromRoute(path), "red");
    }

    scheduler->Stop();
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
                 BgpPath::AsPathLooped);
    scheduler->Start();
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, N_ECMP_PATHDEL) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.2.5");
    AddConnectedRoute(peers_[3], "1.1.2.3/32",  90, "2.3.3.5");
    scheduler->Start();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 3, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    EXPECT_EQ(aggregate_rt->count(), 3);

    scheduler->Stop();
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 1, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");

    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.3.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    EXPECT_EQ(aggregate_rt->count(), 1);
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    // Delete connected route
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add more specific route 192.168.1.1/32
// 3. Add MX leaked route 10.1.1.0/24
// 4. Add connected routes from 2 peers with forwarding information F1
// 5. Add connected routes from 2 peers with forwarding information F2
// 6. Verify aggregate route exists and has only two paths
// 7. Verify ext connected route exists and has only two paths
//
TEST_P(ServiceChainParamTest, EcmpWithDuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.4", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params =
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
                                         "blue-i1",
                                         "service-chain-information",
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected with duplicate forwarding information F1
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.4.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Add Connected with duplicate forwarding information F2
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.4.6");
    AddConnectedRoute(peers_[3], "1.1.2.3/32", 100, "2.3.4.6");
    task_util::WaitForIdle();

    std::string path_ids[] = {"2.3.4.5", "2.3.4.6"};

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for Aggregate route in blue..");
    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 2);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin();
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *aggregate_path = static_cast<BgpPath *>(it.operator->());
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            if (BgpPath::PathIdString(aggregate_path->GetPathId()) == path_id) {
                BgpAttrPtr attr = aggregate_path->GetAttr();
                EXPECT_EQ(attr->nexthop().to_v4().to_string(), path_id);
                EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");
    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    EXPECT_EQ(ext_rt->count(), 2);
    for (Route::PathList::iterator it = ext_rt->GetPathList().begin();
         it != ext_rt->GetPathList().end(); it++) {
        BgpPath *ext_path = static_cast<BgpPath *>(it.operator->());
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            if (BgpPath::PathIdString(ext_path->GetPathId()) == path_id) {
                BgpAttrPtr attr = ext_path->GetAttr();
                EXPECT_EQ(attr->nexthop().to_v4().to_string(), path_id);
                EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    // Delete connected routes from peers_[0] and peers_[2]
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for Aggregate route in blue..");
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 2);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin();
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *aggregate_path = static_cast<BgpPath *>(it.operator->());
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            if (BgpPath::PathIdString(aggregate_path->GetPathId()) == path_id) {
                BgpAttrPtr attr = aggregate_path->GetAttr();
                EXPECT_EQ(attr->nexthop().to_v4().to_string(), path_id);
                EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");
    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    EXPECT_EQ(ext_rt->count(), 2);
    for (Route::PathList::iterator it = ext_rt->GetPathList().begin();
         it != ext_rt->GetPathList().end(); it++) {
        BgpPath *ext_path = static_cast<BgpPath *>(it.operator->());
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            if (BgpPath::PathIdString(ext_path->GetPathId()) == path_id) {
                BgpAttrPtr attr = ext_path->GetAttr();
                EXPECT_EQ(attr->nexthop().to_v4().to_string(), path_id);
                EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete Ext connect route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    task_util::WaitForIdle();

    // Delete connected routes
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 192.168.1.0/24
// 4. Verify that ext connect route 192.168.1.0/24 is not added
// 5. Add VM route(192.168.1.1/32) and verify aggregate route 192.168.1.0/24
//
TEST_P(ServiceChainParamTest, IgnoreAggregateRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Add MX leaked route
    AddInetRoute(NULL, "red", "192.168.1.0/24", 100);
    task_util::WaitForIdle();

    // Check for absence of ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Delete MX leaked, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.0/24");
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route 10.1.1.0/24
// 3. Add connected route
// 4. Verify that ext connect route 10.1.1.0/24 is added
// 5. Remove connected route
// 6. Verify that ext connect route is removed
// 7. Add connected route
// 8. Add VM route(192.168.1.1/32) and verify aggregate route 192.168.1.0/24
//
TEST_P(ServiceChainParamTest, ExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for absence Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Delete Connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();
 
    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add VM route 192.168.1.1/32
// 4. Add MX leaked route 10.1.1.0/24
// 5. Add non-OriginVn route 20.1.1.0/24
// 8. Verify that aggregate route 192.168.1.0/24 is added
// 7. Verify that ext connect route 10.1.1.0/24 is added
// 8. Verify that non-OriginVn route 20.1.1.0/24 is not added
//
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnOnly) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params =
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
                                         "blue-i1",
                                         "service-chain-information",
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add route to green VN which gets imported into red
    AddInetRoute(NULL, "green", "20.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for Aggregate connect route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");

    // Verify ExtConnect route attributes
    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Check for non-OriginVn route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "20.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for non-origin vn route in blue..");

    // Delete ExtRoute, More specific, non-OriginVn and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteInetRoute(NULL, "green", "20.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// Service chain route should be added for routes with unresolved origin
// vn if there is at least one route target matching an export target of
// the destination instance.
//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 10.1.1.0/24 with unresolved OriginVn
// 4. Verify that ext connect route 10.1.1.0/24 is added
//
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnUnresolved1) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params =
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
                                         "blue-i1",
                                         "service-chain-information",
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Add Ext connect route with targets of both red and green.
    vector<string> instances = list_of("red")("green");
    AddInetVpnRoute(NULL, instances, "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Verify that MX leaked route is present in red
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("red", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for MX leaked route in red..");

    // Verify that ExtConnect route is present in blue
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");

    // Verify ExtConnect route attributes
    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ExtRoute and connected route
    DeleteInetVpnRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// Service chain route must not be added for routes with unresolved origin
// vn if there is no route target matching an export target of destination
// instance.
//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 10.1.1.0/24 with unresolved OriginVn
// 4. Verify that ext connect route 10.1.1.0/24 is not added
//
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnUnresolved2) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green")("yellow");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params =
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
                                         "blue-i1",
                                         "service-chain-information",
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Add Ext connect route with targets of green and yellow.
    vector<string> instances = list_of("green")("yellow");
    AddInetVpnRoute(NULL, instances, "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Verify that MX leaked route is present in red
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("red", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for MX leaked route in red..");

    // Verify that ExtConnect route is not present in blue
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000,
                             "Wait for ExtConnect route in blue..");

    // Delete ExtRoute and connected route
    DeleteInetVpnRoute(NULL, "green", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route covering the VN subnet 192.168.0.0/16
// 3. Add VM route and connected route
// 4. Verify that Aggregate route 192.168.1.0/24 is added 
//    Verify that ext connect route 192.168.0.0/16 is added
//
TEST_P(ServiceChainParamTest, ExtConnectRouteCoveringSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route.. Say MX leaks /16 route
    AddInetRoute(NULL, "red", "192.168.0.0/16", 100);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "192.168.0.0/16");
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.4.5", BgpPath::PathIdString(ext_path->GetPathId()));
    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.0.0/16");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route within the VN subnet 192.168.1.252/30
// 3. Add VM route and connected route
// 4. Verify that Aggregate route is added with connected route nexthop
//    Verify that MX added ext connect route is treated as more specific itself
//
TEST_P(ServiceChainParamTest, ExtConnectRouteWithinSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route.. Say MX leaks /30 route
    AddInetRoute(NULL, "red", "192.168.1.252/30", 100);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.252/30");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

//
// 1. Add service chain with vn subnet as 192.168.1.0/24
// 2. Add ExtConnecting route 192.168.1.252/30 within the VN subnet
// 3. Add VM route 192.168.1.1/32 as more specific
// 4. Add connected route 1.1.2.3/32
// 5. Verify aggregate route(192.168.1.0/24) & ExtConnect route 192.168.1.252/30
//    is not added as it more specific of vn subnet
// 6. Update the service chain to contain only 10.1.1.0/24 as subnet prefix.
//    Removed 192.168.1.0/24 
// 7. Verify ext connect route 192.168.1.252/30 and 192.168.1.1/32 is added and
//    old aggregate(192.168.1.0/24) should be removed
// 7.1 Add 192.168.0.0/16 and verify this is added as ext connect route
// 8. Add new VM route in new subnet 10.1.1.1/32 and 
//    verify aggregate route 10.1.1/24
// 9. Update the service chain to contain only 192.168.1.0/24
// 10. Verify 10.1.1.1/32 is added as ext connect route
//     Verify 192.168.0.0/16 is added as ext connect route
//     Verify 192.168.1.0/24 is added as aggregate route
//     Verify 192.168.1.1/32 is removed as ext connecting route
//     Verify 192.168.1.250/30 is removed as ext connecting route
//
TEST_P(ServiceChainParamTest, ExtConnectRouteServiceChainUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route.. Say MX leaks /30 route
    AddInetRoute(NULL, "red", "192.168.1.252/30", 100);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    params = GetChainConfig("controller/src/bgp/testdata/service_chain_4.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");
    // Check for Previous Aggregate route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    // Add Ext connect route.. Say MX leaks /16 route
    AddInetRoute(NULL, "red", "192.168.0.0/16", 100);
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    // Add more specific for new subnet prefix 
    AddInetRoute(NULL, "red", "10.1.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");

    params = GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Check for ext connect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for Ext connect route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.0.0/16"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for new Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for removal of ExtConnect route it is now more specific
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.252/30"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for removal of ExtConnect route it is now more specific
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    DeleteInetRoute(NULL, "red", "192.168.1.252/30");
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.0.0/16");
    DeleteInetRoute(NULL, "red", "10.1.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ExtConnectedEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add MX leaked route 
    AddInetRoute(NULL, "red", "10.10.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    task_util::WaitForIdle();

    // Check for external connected route 
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.10.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for External connecting route in blue..");

    BgpRoute *ext_route = InetRouteLookup("blue", "10.10.1.0/24");
    const BgpPath *ext_path = ext_route->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(ext_path->GetPathId()));
    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.0.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    task_util::WaitForIdle();

    ext_route = InetRouteLookup("blue", "10.10.1.0/24");
    std::string path_ids[] = {"2.3.0.5", "2.3.1.5"};
    for (Route::PathList::iterator it = ext_route->GetPathList().begin(); 
         it != ext_route->GetPathList().end(); it++) {
        bool found = false;
        BOOST_FOREACH(std::string path_id, path_ids) {
            BgpPath *path = static_cast<BgpPath *>(it.operator->());
            if (BgpPath::PathIdString(path->GetPathId()) == path_id) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();

    ext_route = InetRouteLookup("blue", "10.10.1.0/24");
    for (Route::PathList::iterator it = ext_route->GetPathList().begin(); 
         it != ext_route->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        ASSERT_TRUE(BgpPath::PathIdString(path->GetPathId()) != "2.3.1.5");
    }
    
    // Delete MX route
    DeleteInetRoute(NULL, "red", "10.10.1.0/24");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();
}


TEST_P(ServiceChainParamTest, ExtConnectedMoreSpecificEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    task_util::WaitForIdle();

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    task_util::WaitForIdle();

    // Check for Aggregate route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    EXPECT_EQ(ext_rt->count(), 1);
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(ext_path->GetPathId()));

    // Connected path is infeasible
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
                 BgpPath::AsPathLooped);
    task_util::WaitForIdle();

    // Verify that Aggregate route and ExtConnect route is gone
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    // Connected path again from two peers
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    task_util::WaitForIdle();

    // Check for Aggregate & ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate connect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 2, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 2);
    TASK_UTIL_WAIT_EQ_NO_MSG(ext_rt->count(), 2, 1000, 10000, 
                             "Wait for all paths in Service Chain route ..");
    EXPECT_EQ(ext_rt->count(), 2);

    // Connected path is infeasible
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
                 BgpPath::AsPathLooped);
    task_util::WaitForIdle();

    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 1);
    EXPECT_EQ(ext_rt->count(), 1);
    aggregate_path = aggregate_rt->BestPath();
    EXPECT_EQ("2.3.1.5", BgpPath::PathIdString(aggregate_path->GetPathId()));
    ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.1.5", BgpPath::PathIdString(ext_path->GetPathId()));

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ServiceChainRouteSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    bgp_server_->service_chain_mgr()->set_aggregate_host_route(false);
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    std::vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    std::vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    std::vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    std::vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, sgid_list_ext);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_connected);
    task_util::WaitForIdle();

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, sgid_list_more_specific_1);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, sgid_list_more_specific_2);
    task_util::WaitForIdle();

    // Check for More specific routes leaked in src rtinstance
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.2/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    BgpRoute *leak_rt_1 = InetRouteLookup("blue", "192.168.1.1/32");
    BgpRoute *leak_rt_2 = InetRouteLookup("blue", "192.168.1.2/32");

    EXPECT_EQ(leak_rt_1->count(), 1);
    EXPECT_EQ(leak_rt_2->count(), 1);
    EXPECT_EQ(ext_rt->count(), 1);

    const BgpPath *leak_path = leak_rt_1->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(leak_path->GetPathId()));
    std::vector<uint32_t> list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_more_specific_1.begin();
        it1 != list.end() && it2 != sgid_list_more_specific_1.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    leak_path = leak_rt_2->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(leak_path->GetPathId()));
    list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_more_specific_2.begin();
        it1 != list.end() && it2 != sgid_list_more_specific_2.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(ext_path->GetPathId()));
    list = GetSGIDListFromRoute(ext_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_ext.begin();
        it1 != list.end() && it2 != sgid_list_ext.end(); it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ServiceChainRouteUpdateSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    bgp_server_->service_chain_mgr()->set_aggregate_host_route(false);
    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    std::vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    std::vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    std::vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    std::vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, sgid_list_ext);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_connected);
    task_util::WaitForIdle();

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, sgid_list_more_specific_1);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, sgid_list_more_specific_2);
    task_util::WaitForIdle();

    // Check for More specific routes leaked in src rtinstance
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.2/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    BgpRoute *leak_rt_1 = InetRouteLookup("blue", "192.168.1.1/32");
    BgpRoute *leak_rt_2 = InetRouteLookup("blue", "192.168.1.2/32");

    EXPECT_EQ(leak_rt_1->count(), 1);
    EXPECT_EQ(leak_rt_2->count(), 1);
    EXPECT_EQ(ext_rt->count(), 1);

    const BgpPath *leak_path = leak_rt_1->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(leak_path->GetPathId()));
    std::vector<uint32_t> list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_more_specific_1.begin();
        it1 != list.end() && it2 != sgid_list_more_specific_1.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    leak_path = leak_rt_2->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(leak_path->GetPathId()));
    list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_more_specific_2.begin();
        it1 != list.end() && it2 != sgid_list_more_specific_2.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    const BgpPath *ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(ext_path->GetPathId()));
    list = GetSGIDListFromRoute(ext_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_ext.begin();
        it1 != list.end() && it2 != sgid_list_ext.end(); it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Update Ext connect route with different SGID list
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, sgid_list_more_specific_1);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_more_specific_2);
    task_util::WaitForIdle();

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, sgid_list_ext);
    task_util::WaitForIdle();

    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, sgid_list_connected);
    task_util::WaitForIdle();

    // Check for More specific routes leaked in src rtinstance
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.2/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    leak_rt_1 = InetRouteLookup("blue", "192.168.1.1/32");
    leak_rt_2 = InetRouteLookup("blue", "192.168.1.2/32");

    EXPECT_EQ(leak_rt_1->count(), 1);
    EXPECT_EQ(leak_rt_2->count(), 1);
    EXPECT_EQ(ext_rt->count(), 1);

    leak_path = leak_rt_1->BestPath();
    list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_ext.begin();
        it1 != list.end() && it2 != sgid_list_ext.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    leak_path = leak_rt_2->BestPath();
    list = GetSGIDListFromRoute(leak_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_connected.begin();
        it1 != list.end() && it2 != sgid_list_connected.end(); 
        it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(leak_path), "red");

    ext_path = ext_rt->BestPath();
    EXPECT_EQ("2.3.0.5", BgpPath::PathIdString(ext_path->GetPathId()));
    list = GetSGIDListFromRoute(ext_path);
    EXPECT_EQ(list.size(), 4);
    for(std::vector<uint32_t>::iterator it1 = list.begin(), 
        it2=sgid_list_more_specific_1.begin();
        it1 != list.end() && it2 != sgid_list_more_specific_1.end(); it1++, it2++)
        EXPECT_EQ(*it1, *it2);
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ValidateTunnelEncapAggregate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    set<string> encap_more_specific = list_of("udp");
    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, vector<uint32_t>(), encap_more_specific);
    task_util::WaitForIdle();

    set<string> encap = list_of("vxlan");
    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    const BgpPath *aggregate_path = aggregate_rt->BestPath();
    set<string> list = GetTunnelEncapListFromRoute(aggregate_path);
    EXPECT_EQ(list, encap);

    BgpAttrPtr attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    encap = list_of("gre");
    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    aggregate_path = aggregate_rt->BestPath();
    list = GetTunnelEncapListFromRoute(aggregate_path);
    EXPECT_EQ(list, encap);

    attr = aggregate_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_path), "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, ValidateTunnelEncapExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route
    set<string> encap_ext = list_of("vxlan");
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, vector<uint32_t>(), encap_ext);
    task_util::WaitForIdle();

    // Add Connected
    set<string> encap = list_of("gre");
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);
    task_util::WaitForIdle();

    // Check for service Chain router
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Service Chain route in blue..");

    BgpRoute *ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    const BgpPath *ext_path = ext_rt->BestPath();
    set<string> list = GetTunnelEncapListFromRoute(ext_path);
    EXPECT_EQ(list, encap);

    BgpAttrPtr attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    encap = list_of("udp");
    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Service Chain route in blue..");

    ext_rt = InetRouteLookup("blue", "10.1.1.0/24");
    ext_path = ext_rt->BestPath();
    list = GetTunnelEncapListFromRoute(ext_path);
    EXPECT_EQ(list, encap);

    attr = ext_path->GetAttr();
    EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.4.5");
    EXPECT_EQ(GetOriginVnFromRoute(ext_path), "red");

    // Delete ext connected route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    task_util::WaitForIdle();

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, MultiPathTunnelEncap) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
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

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add More specific
    set<string> encap_1 = list_of("gre");
    set<string> encap_2 = list_of("udp");
    set<string> encap_3 = list_of("vxlan");
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.1.5", 0, 0, vector<uint32_t>(), encap_1);
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.2.5", 0, 0, vector<uint32_t>(), encap_2);
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.3.5", 0, 0, vector<uint32_t>(), encap_3);
    task_util::WaitForIdle();

    // Check for aggregated route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "192.168.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");

    BgpRoute *aggregate_rt = InetRouteLookup("blue", "192.168.1.0/24");
    EXPECT_EQ(aggregate_rt->count(), 3);

    // Check for aggregated route count
    TASK_UTIL_WAIT_EQ_NO_MSG(aggregate_rt->count(), 3, 1000, 10000, 
                             "Wait for all paths in Aggregate route ..");
    EXPECT_EQ(aggregate_rt->count(), 3);
    for (Route::PathList::iterator it = aggregate_rt->GetPathList().begin(); 
         it != aggregate_rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        set<string> list = GetTunnelEncapListFromRoute(path);
        BgpAttrPtr attr = path->GetAttr();
        if (BgpPath::PathIdString(path->GetPathId()) == "2.3.1.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.1.5");
            EXPECT_EQ(list, encap_1);
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.2.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.2.5");
            EXPECT_EQ(list, encap_2);
        } else if (BgpPath::PathIdString(path->GetPathId()) == "2.3.3.5") {
            EXPECT_EQ(attr->nexthop().to_v4().to_string(), "2.3.3.5");
            EXPECT_EQ(list, encap_3);
        }
        EXPECT_EQ(GetOriginVnFromRoute(path), "red");
    }

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    task_util::WaitForIdle();
}


TEST_P(ServiceChainParamTest, DeleteConnectedWithExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.1.2/32", 100);
    AddInetRoute(NULL, "red", "10.1.1.3/32", 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.2/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    DisableServiceChainQ();
    AddConnectedRoute(NULL, "1.1.2.3/32", 200, "2.3.4.5");
    DeleteInetRoute(NULL, "red", "10.1.1.1/32");

    BgpRoute *ext_rt = InetRouteLookup("red", "10.1.1.1/32");
    TASK_UTIL_WAIT_EQ_NO_MSG(ext_rt->IsDeleted(),
                             true, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    // Check for ExtConnect route
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.2/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", "10.1.1.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    EnableServiceChainQ();

    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());

    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup("blue", "10.1.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for ExtConnect route in blue..");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "10.1.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.3/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, DeleteEntryReuse) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    std::vector<string> routes_to_play = list_of("10.1.1.1/32")("10.1.1.2/32")("10.1.1.3/32");
    // Add Ext connect route
    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", *it),
                                 NULL, 1000, 10000, 
                                 "Wait for ExtConnect route in blue..");
    }
    DisableServiceChainQ();
    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        BgpRoute *ext_rt = InetRouteLookup("red", *it);
        TASK_UTIL_WAIT_EQ_NO_MSG(ext_rt->IsDeleted(),
                                 true, 1000, 10000, 
                                 "Wait for delete marking of ExtConnect route in red..");
    }

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");


    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        BgpRoute *ext_rt = InetRouteLookup("red", *it);
        TASK_UTIL_WAIT_EQ_NO_MSG(ext_rt->IsDeleted(),
                                 true, 1000, 10000, 
                                 "Wait for delete marking of ExtConnect route in red..");
    }

    EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());

    task_util::WaitForIdle();
}

TEST_P(ServiceChainParamTest, EntryAfterStop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    std::auto_ptr<autogen::ServiceChainInfo> params = 
        GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");

    // Service Chain Info
    ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance", 
                                         "blue-i1", 
                                         "service-chain-information", 
                                         params.release(),
                                         0);
    task_util::WaitForIdle();

    std::vector<string> routes_to_play;
    // Add Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.1.1." << i << "/32";
        routes_to_play.push_back(route.str());
    }

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);
    task_util::WaitForIdle();

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
    task_util::WaitForIdle();

    // Check for ExtConnect route
    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup("blue", *it),
                                 NULL, 1000, 10000, 
                                 "Wait for ExtConnect route in blue..");
    }
    DisableServiceChainQ();

    ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance", 
                                            "blue-i1", 
                                            "service-chain-information");
    // Add more Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.2.1." << i << "/32";
        routes_to_play.push_back(route.str());
    }

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 200);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());

    for (std::vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    TASK_UTIL_WAIT_EQ_NO_MSG(RouteCount("red"),
                             0, 1000, 10000, 
                             "Wait for route in red to be deleted..");
    task_util::WaitForIdle();
}

INSTANTIATE_TEST_CASE_P(Instance, ServiceChainParamTest,
        ::testing::Combine(::testing::Bool(), ::testing::Bool()));

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
