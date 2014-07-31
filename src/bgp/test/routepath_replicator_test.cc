/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routepath_replicator.h"

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_log.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "net/bgp_af.h"
#include "schema/bgp_schema_types.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;

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

class ReplicationTest : public ::testing::Test {
protected:
    ReplicationTest() : bgp_server_(new BgpServer(&evm_)) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~ReplicationTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                                  "localhost");
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

    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref, string rd = "") {
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
        BgpAttrSourceRd rd_spec(RouteDistinguisher::FromString(rd));
        if (!rd.empty()) {
            attr_spec.push_back(&rd_spec);
        }
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void AddVPNRouteCommon(IPeer *peer, const string &prefix,
                           const BgpAttrSpec &attr_spec) {
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void AddVPNRoute(IPeer *peer, const string &prefix, int localpref,
                     const vector<string> &instance_names) {
        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        boost::scoped_ptr<ExtCommunitySpec> commspec;
        commspec.reset(BuildInstanceListTargets(instance_names, &attr_spec));
        AddVPNRouteCommon(peer, prefix, attr_spec);
        task_util::WaitForIdle();
    }

    void AddVPNRouteWithTarget(IPeer *peer, const string &prefix, int localpref,
                               const string &target,
                               string origin_vn_str = string()) {
        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        boost::scoped_ptr<ExtCommunitySpec> commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        const ExtCommunity::ExtCommunityValue &extcomm =
            tgt.GetExtCommunity();
        uint64_t value = get_value(extcomm.data(), extcomm.size());
        commspec->communities.push_back(value);

        if (!origin_vn_str.empty()) {
            OriginVn origin_vn = OriginVn::FromString(origin_vn_str);
            commspec->communities.push_back(origin_vn.GetExtCommunityValue());
        }
        attr_spec.push_back(commspec.get());
        AddVPNRouteCommon(peer, prefix, attr_spec);
        task_util::WaitForIdle();
    }

    void DeleteVPNRoute(IPeer *peer, const string &prefix) {
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
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


    ExtCommunitySpec *BuildInstanceListTargets(
        const vector<string> &instance_names, BgpAttrSpec *attr_spec) {
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            RoutingInstance *rti =
                bgp_server_->routing_instance_mgr()->GetRoutingInstance(*iter);
            BOOST_FOREACH(RouteTarget tgt, rti->GetExportList()) {
                const ExtCommunity::ExtCommunityValue &extcomm =
                        tgt.GetExtCommunity();
                uint64_t value = get_value(extcomm.data(), extcomm.size());
                commspec->communities.push_back(value);
            }
        }
        if (!commspec->communities.empty()) {
            attr_spec->push_back(commspec);
        }

        return commspec;
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

    BgpRoute *VPNRouteLookup(const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetVpnTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyVPNPathRouteTargets(const BgpPath *path,
        const vector<string> &expected_targets) {
        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();
        set<string> actual_targets;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (!ExtCommunity::is_route_target(comm))
                continue;
            RouteTarget rtarget(comm);
            actual_targets.insert(rtarget.ToString());
        }

        EXPECT_EQ(actual_targets.size(), expected_targets.size());
        BOOST_FOREACH(const string &target, expected_targets) {
            EXPECT_TRUE(actual_targets.find(target) != actual_targets.end());
        }
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

    const RtReplicated *InetRouteReplicationState(const string &instance_name,
                                                  BgpRoute *rt) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        RoutePathReplicator *replicator =
            bgp_server_->replicator(Address::INETVPN);
        return replicator->GetReplicationState(table, rt);
    }

    const BgpRoute *GetVPNSecondary(const RtReplicated *rts) {
        BOOST_FOREACH(const RtReplicated::SecondaryRouteInfo rinfo,
                      rts->GetList()) {
            if (rinfo.rt_->Safi() == BgpAf::Vpn) {
                return rinfo.rt_;
            }
        }
        return NULL;
    }

    vector<string> GetInstanceRouteTargetList(const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        vector<string> target_list;
        BOOST_FOREACH(RouteTarget tgt, rti->GetExportList()) {
            target_list.push_back(tgt.ToString());
        }
        sort(target_list.begin(), target_list.end());
        return target_list;
    }

    int GetInstanceOriginVnIndex(const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance);
        TASK_UTIL_EXPECT_NE(0, rti->virtual_network_index());
        return rti->virtual_network_index();
    }

    void AddInstanceRouteTarget(const string &instance, const string &target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance));
        ifmap_test_util::IFMapMsgLink(&config_db_,
            "routing-instance", instance,
            "route-target", target, "instance-target");
        task_util::WaitForIdle();
    }

    void RemoveInstanceRouteTarget(const string instance, const string &target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(instance));
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
            "routing-instance", instance,
            "route-target", target, "instance-target");
        task_util::WaitForIdle();
    }

    int GetOriginVnIndexFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            if (origin_vn.as_number() != 64496)
                continue;
            return origin_vn.vn_index();
        }
        return 0;
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    vector<BgpPeerMock *> peers_;
};

TEST_F(ReplicationTest, PathImport) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    // generate a notification for the route.
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    BgpTable *table = static_cast<BgpTable *>(
        bgp_server_->database()->FindTable("bgp.l3vpn.0"));
    table->Change(rt);
    task_util::WaitForIdle();

    // make sure that there are no changes.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("red"));
    task_util::WaitForIdle();

    // make sure that there are no changes.
    VERIFY_EQ(1, rt->count());
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
                list_of("green"));
    task_util::WaitForIdle();
    VERIFY_EQ(1, rt->count());
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(1, RouteCount("green"));

    // Uninteresting route target.
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
                          "target:1:1");
    task_util::WaitForIdle();
    VERIFY_EQ(1, rt->count());
    VERIFY_EQ(0, RouteCount("green"));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
    VERIFY_EQ(0, RouteCount("blue"));

}

TEST_F(ReplicationTest, NoExtCommunities) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("blue"));

    // No extended communities
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, vector<string>());
    task_util::WaitForIdle();
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VERIFY_EQ(1, rt->count());
    VERIFY_EQ(0, RouteCount("blue"));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, Delete) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();
    VERIFY_EQ(1, RouteCount("blue"));

    // No extended communities
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, MultiplePaths)  {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.2.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    AddVPNRoute(peers_[1], "192.2.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();
    AddVPNRoute(peers_[2], "192.2.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));
    BgpRoute *rt = InetRouteLookup("blue", "10.0.1.1/32");
    // Verify that all 3 paths are replicated
    VERIFY_EQ(3, rt->count());

    DeleteVPNRoute(peers_[0], "192.2.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[1], "192.2.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[2], "192.2.0.1:1:10.0.1.1/32");

    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, IdentifySecondary)  {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red")
            ("blue", "green");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));

    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    // Replicated to both green and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));
    VERIFY_EQ(1, RouteCount("green"));

    BgpRoute *rt_blue = InetRouteLookup("blue", "10.0.1.1/32");
    BgpRoute *rt_red = InetRouteLookup("red", "10.0.1.1/32");
    BgpRoute *rt_green = InetRouteLookup("green", "10.0.1.1/32");

    VERIFY_EQ(1, rt_blue->count());
    VERIFY_EQ(1, rt_red->count());
    VERIFY_EQ(1, rt_green->count());

    //
    // Add 2 more paths in blue
    //
    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 100);
    AddInetRoute(peers_[2], "blue", "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    VERIFY_EQ(3, rt_blue->count());
    // Verify that all three paths are replicated
    VERIFY_EQ(3, rt_red->count());
    // Verify that all three paths are replicated
    VERIFY_EQ(3, rt_green->count());

    AddInetRoute(peers_[0], "red", "10.0.1.1/32", 100);

#if 0
    //
    // Red path is better and is imported in blue. Now, Blue's best is the one 
    // imported from red. Hence its exported path into green should be gone by
    // now
    VERIFY_EQ(0, InetRouteLookup("green", "10.0.1.1/32"));
#endif

    AddInetRoute(peers_[0], "green", "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    rt_green = InetRouteLookup("green", "10.0.1.1/32");
    // 2 replicated path and 3 paths sourced from peer
    VERIFY_EQ(5, rt_blue->count());
    // 3 replicated path and 1 path from the peer_[0]
    VERIFY_EQ(4, rt_red->count());
    // 3 replicated path and 1 path from the peer_[0]
    VERIFY_EQ(4, rt_green->count());

    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();
    // 2 replicatd  and 2 paths from peer
    VERIFY_EQ(4, rt_blue->count());
    // 2 replicatd  and 1 path from peer[0]
    VERIFY_EQ(3, rt_red->count());
    // 2 replicatd  and 1 path from peer[0]
    VERIFY_EQ(3, rt_green->count());

    DeleteInetRoute(peers_[1], "blue", "10.0.1.1/32");
    DeleteInetRoute(peers_[2], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    // 2 replicated paths
    VERIFY_EQ(2, rt_blue->count());
    // path from peer[0]
    VERIFY_EQ(1, rt_red->count());
    // path from peer[0]
    VERIFY_EQ(1, rt_green->count());

    DeleteInetRoute(peers_[0], "red", "10.0.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, InetRouteLookup("red", "10.0.1.1/32"));

    VERIFY_EQ(1, rt_green->count()); // peers_[0]
    VERIFY_EQ(1, rt_blue->count());  // green(peers_[0])

    DeleteInetRoute(peers_[0], "green", "10.0.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("green"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("blue"));

}

TEST_F(ReplicationTest, MultiplePathsDiffRD)  {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    AddVPNRoute(peers_[1], "192.168.0.2:1:10.0.1.1/32", 100, list_of("red"));
    task_util::WaitForIdle();

    // Multiple paths should get imported.
    VERIFY_EQ(1, RouteCount("blue"));
    BgpRoute *rt = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(2, rt->count());
    VERIFY_EQ(1, RouteCount("red"));

    DeleteVPNRoute(peers_[1], "192.168.0.2:1:10.0.1.1/32");
    task_util::WaitForIdle();
    rt = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(1, rt->count());

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, PathChange)  {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 90, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    //
    // Better path from peer1 which goes only to green
    //
    AddVPNRoute(peers_[1], "192.168.0.1:1:10.0.1.1/32", 100, list_of("green"));
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("green"));
    VERIFY_EQ(0, RouteCount("red"));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[1], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, WithLocalRoute) {
    // Add a route to "red" with the same prefix.
    // Imported route becomes secondary.
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    // VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 80, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    AddInetRoute(peers_[1], "red", "10.0.1.1/32", 100);
    task_util::WaitForIdle();
    BgpRoute *rt = InetRouteLookup("red", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    // Selected path is from peer[1]
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());

    const RtReplicated *rts = InetRouteReplicationState("red", rt);
    ASSERT_TRUE(rts != NULL);
    VERIFY_EQ(2, rts->GetList().size());
    BOOST_FOREACH(const RtReplicated::SecondaryRouteInfo rinfo,
                  rts->GetList()) {
        BGP_DEBUG_UT(rinfo.table_->name() << " " << rinfo.rt_->ToString());
    }

    // change VPN route
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 90, list_of("blue"));
    task_util::WaitForIdle();
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());
    rts = InetRouteReplicationState("red", rt);
    ASSERT_TRUE(rts != NULL);
    VERIFY_EQ(2, rts->GetList().size());

    // change the best path
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 110, list_of("blue"));
    task_util::WaitForIdle();
    // Best path is from peer[0]
    VERIFY_EQ(peers_[0], rt->BestPath()->GetPeer());

    // Route from "red" is still replicated
    rts = InetRouteReplicationState("red", rt);
    ASSERT_TRUE(rts == NULL);

    DeleteInetRoute(peers_[1], "red", "10.0.1.1/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, ResurrectInetRoute) {
    // Imported VPN route becomes secondary after inet route is added.
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    // VPN route with same RD as exported inet route
    AddVPNRoute(peers_[0], "192.168.0.100:1:10.0.1.1/32", 80, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    VERIFY_EQ(1, RouteCount("red"));

    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 100);
    task_util::WaitForIdle();
    BgpRoute *rt = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt != NULL);
    VERIFY_EQ(peers_[1], rt->BestPath()->GetPeer());

    //
    // Update local-pref inorder to make the path ecmp eligible
    //
    AddVPNRoute(peers_[0], "192.168.0.100:1:10.0.1.1/32", 100, list_of("blue"));

    // Two paths.. One replicated from bgp.l3vpn.0 from peer[0]
    // other one from blue.inet.0 from peer[1]
    VERIFY_EQ(2, rt->count());

    // Delete the INET route. This causes the VPN route to be imported to the
    // VRF
    DeleteInetRoute(peers_[1], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();
    BgpRoute *rt_current = InetRouteLookup("blue", "10.0.1.1/32");
    VERIFY_EQ(peers_[0], rt_current->BestPath()->GetPeer());

    DeleteVPNRoute(peers_[0], "192.168.0.100:1:10.0.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, LocalRouteFirst) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.3", ec)));

    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 100);
    AddInetRoute(peers_[2], "green", "10.0.1.1/32", 100);
    task_util::WaitForIdle();

    BgpRoute *rt1 = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt1 != NULL);
    VERIFY_EQ(peers_[1], rt1->BestPath()->GetPeer());
    const RtReplicated *rts = InetRouteReplicationState("blue", rt1);
    ASSERT_TRUE(rts != NULL);
    const BgpRoute *rt1_vpn = GetVPNSecondary(rts);
    string rt1_vpn_prefix(rt1_vpn->ToString());

    AddVPNRoute(peers_[0], rt1_vpn_prefix, 80, list_of("blue"));
    task_util::WaitForIdle();
    VERIFY_EQ(peers_[1], rt1->BestPath()->GetPeer());
    VERIFY_EQ(2, rt1_vpn->count());

    BgpRoute *rt2 = InetRouteLookup("green", "10.0.1.1/32");
    rts = InetRouteReplicationState("green", rt2);
    ASSERT_TRUE(rts != NULL);
    const BgpRoute *rt2_vpn = GetVPNSecondary(rts);
    string rt2_vpn_prefix(rt2_vpn->ToString());
    AddVPNRoute(peers_[0], rt2_vpn_prefix, 110, list_of("green"));
    task_util::WaitForIdle();
    VERIFY_EQ(peers_[0], rt2->BestPath()->GetPeer());

    //
    // VPN route is a better path. Hence green local route should not be
    // replicated any more
    //
    rts = InetRouteReplicationState("green", rt2);
    ASSERT_TRUE(rts == NULL);

    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 120);
    AddInetRoute(peers_[2], "green", "10.0.1.1/32", 120);
    task_util::WaitForIdle();
    VERIFY_EQ(peers_[1], rt1->BestPath()->GetPeer());
    VERIFY_EQ(peers_[2], rt2->BestPath()->GetPeer());

    DeleteVPNRoute(peers_[0], rt1_vpn_prefix);
    DeleteVPNRoute(peers_[0], rt2_vpn_prefix);
    DeleteInetRoute(peers_[1], "blue", "10.0.1.1/32");
    DeleteInetRoute(peers_[2], "green", "10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, ResurrectVPNRoute) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.2", ec)));

    // VPN route with same RD as exported inet route
    AddVPNRoute(peers_[0], "192.168.0.100:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();

    // The inet route is secondary and thus should not be exported.
    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 80);
    task_util::WaitForIdle();

    BgpRoute *rt_vpn = VPNRouteLookup("192.168.0.100:1:10.0.1.1/32");
    VERIFY_EQ(1, rt_vpn->count());
    VERIFY_EQ(peers_[0], rt_vpn->BestPath()->GetPeer());

    //
    // Update local-pref so that the path becomes ecmp eligible
    //
    AddInetRoute(peers_[1], "blue", "10.0.1.1/32", 100);
    VERIFY_EQ(2, rt_vpn->count());

    // Delete the VPN route. This causes the inet route to be exported to
    // the l3vpn table.
    DeleteVPNRoute(peers_[0], "192.168.0.100:1:10.0.1.1/32");
    task_util::WaitForIdle();
    rt_vpn = VPNRouteLookup("192.168.0.100:1:10.0.1.1/32");
    VERIFY_EQ(peers_[1], rt_vpn->BestPath()->GetPeer());

    // Delete the INET route
    DeleteInetRoute(peers_[1], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

// Unconfigure connection
TEST_F(ReplicationTest, DisconnectNetwork) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN routes with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32", 100, list_of("blue"));

    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(3, RouteCount("blue"));
    VERIFY_EQ(3, RouteCount("red"));

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "blue",
                                    "routing-instance", "red",
                                    "connection");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

// Add connection
TEST_F(ReplicationTest, ConnectNetwork) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN routes with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32", 100, list_of("blue"));

    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(3, RouteCount("blue"));
    VERIFY_EQ(3, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                    "routing-instance", "blue",
                                    "routing-instance", "green",
                                    "connection");
    task_util::WaitForIdle();
    VERIFY_EQ(3, RouteCount("blue"));
    VERIFY_EQ(3, RouteCount("red"));
    VERIFY_EQ(3, RouteCount("green"));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, DeleteNetwork) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // VPN routes with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32", 100, list_of("blue"));
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32", 100, list_of("blue"));

    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "blue",
                                    "routing-instance", "red",
                                    "connection");
    RoutingInstance *rti =
        bgp_server_->routing_instance_mgr()->GetRoutingInstance("blue");
    ASSERT_TRUE(rti != NULL);

    //
    // Cache a copy of the export route-targets before the instance is deleted
    //
    const RoutingInstance::RouteTargetList target_list(rti->GetExportList());
    BOOST_FOREACH(RouteTarget tgt, target_list) {
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                        "routing-instance", "blue",
                                        "route-target", tgt.ToString(),
                                        "instance-target");
    }
    task_util::WaitForIdle();

#if 0
    VERIFY_EQ(0,
              bgp_server_->routing_instance_mgr()->GetRoutingInstance("blue"));
#endif
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));

    TASK_UTIL_ASSERT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    BgpTable *table = static_cast<BgpTable *>(
        bgp_server_->database()->FindTable("bgp.l3vpn.0"));
    RoutePathReplicator *replicator = bgp_server_->replicator(Address::INETVPN);
    VERIFY_EQ(0, replicator->GetReplicationState(table, rt));

    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.2/32");
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.3/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, AnotherPathWithDifferentRD) {
    vector<string> instance_names = list_of("blue")("red")("green");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "1.1.1.1:2");
    task_util::WaitForIdle();

    BgpRoute *rt1 = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt1 != NULL);
    VERIFY_EQ(peers_[0], rt1->BestPath()->GetPeer());
    const RtReplicated *rts = InetRouteReplicationState("blue", rt1);
    ASSERT_TRUE(rts != NULL);
    ASSERT_TRUE(rts->GetList().size() == 2);
    const InetVpnRoute *rt1_vpn = (const InetVpnRoute *)GetVPNSecondary(rts);
    VERIFY_EQ("1.1.1.1:2", rt1_vpn->GetPrefix().route_distinguisher().ToString());

    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "2.2.2.2:3");
    task_util::WaitForIdle();

    rt1 = InetRouteLookup("blue", "10.0.1.1/32");
    ASSERT_TRUE(rt1 != NULL);
    rts = InetRouteReplicationState("blue", rt1);
    ASSERT_TRUE(rts != NULL);
    ASSERT_TRUE(rts->GetList().size() == 2);
    rt1_vpn = (const InetVpnRoute *)GetVPNSecondary(rts);
    VERIFY_EQ("2.2.2.2:3", rt1_vpn->GetPrefix().route_distinguisher().ToString());

    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
    VERIFY_EQ(0, RouteCount("green"));
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets1) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Verify targets for blue instance.
    vector<string> instance_targets = GetInstanceRouteTargetList("blue");
    TASK_UTIL_EXPECT_EQ(1, instance_targets.size());
    TASK_UTIL_EXPECT_EQ("target:64496:1", instance_targets[0]);

    // Add route to blue table.
    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "192.168.0.1:1");
    task_util::WaitForIdle();

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Add another target to blue instance and verify targets.
    AddInstanceRouteTarget("blue", "target:64496:101");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("blue").size());
    instance_targets = GetInstanceRouteTargetList("blue");
    TASK_UTIL_EXPECT_EQ("target:64496:1", instance_targets[0]);
    TASK_UTIL_EXPECT_EQ("target:64496:101", instance_targets[1]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Remove original target from blue instance and verify targets.
    RemoveInstanceRouteTarget("blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("blue").size());
    instance_targets = GetInstanceRouteTargetList("blue");
    TASK_UTIL_EXPECT_EQ("target:64496:101", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Delete route from blue table.
    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets2) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Verify targets for blue instance.
    vector<string> instance_targets = GetInstanceRouteTargetList("blue");
    TASK_UTIL_EXPECT_EQ(1, instance_targets.size());
    TASK_UTIL_EXPECT_EQ("target:64496:1", instance_targets[0]);

    // Add route to blue table.
    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "192.168.0.1:1");
    task_util::WaitForIdle();

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Remove original target from blue instance and verify targets.
    RemoveInstanceRouteTarget("blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(0, GetInstanceRouteTargetList("blue").size());

    // Make sure the route is in the blue table but not in the red table.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);

    // Verify that the VPN route is gone.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);

    // Add a new target to blue instance and verify targets.
    AddInstanceRouteTarget("blue", "target:64496:101");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("blue").size());
    instance_targets = GetInstanceRouteTargetList("blue");
    TASK_UTIL_EXPECT_EQ("target:64496:101", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Delete route from blue table.
    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets3) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Verify targets for red instance.
    vector<string> instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ(1, instance_targets.size());
    TASK_UTIL_EXPECT_EQ("target:64496:2", instance_targets[0]);

    // Add route to blue table.
    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "192.168.0.1:1");
    task_util::WaitForIdle();

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Add another target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:2", instance_targets[0]);
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[1]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Delete route from blue table.
    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets4) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Verify targets for red instance.
    vector<string> instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ(1, instance_targets.size());
    TASK_UTIL_EXPECT_EQ("target:64496:2", instance_targets[0]);

    // Add route to blue table.
    AddInetRoute(peers_[0], "blue", "10.0.1.1/32", 100, "192.168.0.1:1");
    task_util::WaitForIdle();

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(0, GetInstanceRouteTargetList("red").size());

    // Make sure the route is in the blue and red tables.
    // Even though red has no export targets, it's still importing blue.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Add a new target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Delete route from blue table.
    DeleteInetRoute(peers_[0], "blue", "10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets5) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add route to VPN table with target for blue table.
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
        "target:64496:1");
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    vector<string> instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Add another target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:2", instance_targets[0]);
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[1]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Delete route from VPN table.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets6) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add route to VPN table with target for blue table.
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
        "target:64496:1");
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    vector<string> instance_targets = GetInstanceRouteTargetList("blue");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(0, GetInstanceRouteTargetList("red").size());

    // Make sure the route is in the blue and red tables.
    // Even though red has no export targets, it's still importing blue.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Add a new target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Delete route from VPN table.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets7) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add route to VPN table.
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, instance_names);
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    vector<string> instance_targets;
    instance_targets.push_back("target:64496:1");
    instance_targets.push_back("target:64496:2");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Add another target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:2", instance_targets[0]);
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[1]);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue table but not the red table.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);

    // Update the route in VPN table.
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, instance_names);
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets.clear();
    instance_targets.push_back("target:64496:1");
    instance_targets.push_back("target:64496:202");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Delete route from VPN table.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, UpdateInstanceRouteTargets8) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections;
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add route to VPN table.
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, instance_names);
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    BgpRoute *rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    vector<string> instance_targets;
    instance_targets.push_back("target:64496:1");
    instance_targets.push_back("target:64496:2");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Remove original target from red instance and verify targets.
    RemoveInstanceRouteTarget("red", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(0, GetInstanceRouteTargetList("red").size());

    // Make sure the route is in the blue table but not the red table.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);

    // Add a new target to red instance and verify targets.
    AddInstanceRouteTarget("red", "target:64496:202");
    TASK_UTIL_EXPECT_EQ(1, GetInstanceRouteTargetList("red").size());
    instance_targets = GetInstanceRouteTargetList("red");
    TASK_UTIL_EXPECT_EQ("target:64496:202", instance_targets[0]);

    // Make sure the route is in the blue table but not the red table.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);

    // Update the route in VPN table.
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, instance_names);
    task_util::WaitForIdle();

    // Verify the VPN route.
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") != NULL);
    rt = VPNRouteLookup("192.168.0.1:1:10.0.1.1/32");
    instance_targets.clear();
    instance_targets.push_back("target:64496:1");
    instance_targets.push_back("target:64496:202");
    VerifyVPNPathRouteTargets(rt->BestPath(), instance_targets);

    // Make sure the route is in the blue and red tables.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Delete route from VPN table.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();

    // Make sure the route is gone from the blue red tables and VPN table.
    VERIFY_EQ(0, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") == NULL);
    VERIFY_EQ(0, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(VPNRouteLookup("192.168.0.1:1:10.0.1.1/32") == NULL);
}

TEST_F(ReplicationTest, OriginVn1) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, GetInstanceOriginVnIndex("blue"));
    int blue_vn_idx = GetInstanceOriginVnIndex("blue");

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add VPN route with target "blue".
    AddVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100, list_of("blue"));
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify OriginVn for routes in blue and red.
    BgpRoute *rt;
    rt = InetRouteLookup("blue", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));
    rt = InetRouteLookup("red", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));

    // Delete VPN route.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, OriginVn2) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, GetInstanceOriginVnIndex("blue"));
    int blue_vn_idx = GetInstanceOriginVnIndex("blue");

    // Add another target to blue instance.
    AddInstanceRouteTarget("blue", "target:64496:101");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("blue").size());

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add VPN route with target 101.
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
        "target:64496:101");
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify OriginVn for routes in blue and red.
    BgpRoute *rt;
    rt = InetRouteLookup("blue", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));
    rt = InetRouteLookup("red", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));

    // Delete VPN route.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, OriginVn3) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, GetInstanceOriginVnIndex("blue"));
    int blue_vn_idx = GetInstanceOriginVnIndex("blue");

    // Add another target to blue instance.
    AddInstanceRouteTarget("blue", "target:64496:101");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("blue").size());

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add VPN route with target "blue".
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
        "target:64496:101", "originvn:64496:1001");
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify OriginVn for routes in blue and red.
    // OriginVn from the VPN route is used since it has our ASN.
    BgpRoute *rt;
    rt = InetRouteLookup("blue", "10.0.1.1/32");
    VERIFY_EQ(1001, GetOriginVnIndexFromRoute(rt->BestPath()));
    rt = InetRouteLookup("red", "10.0.1.1/32");
    VERIFY_EQ(1001, GetOriginVnIndexFromRoute(rt->BestPath()));

    // Delete VPN route.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
}

TEST_F(ReplicationTest, OriginVn4) {
    vector<string> instance_names = list_of("blue")("red");
    multimap<string, string> connections = map_list_of("blue", "red");
    NetworkConfig(instance_names, connections);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, GetInstanceOriginVnIndex("blue"));
    int blue_vn_idx = GetInstanceOriginVnIndex("blue");

    // Add another target to blue instance.
    AddInstanceRouteTarget("blue", "target:64496:101");
    TASK_UTIL_EXPECT_EQ(2, GetInstanceRouteTargetList("blue").size());

    boost::system::error_code ec;
    peers_.push_back(
        new BgpPeerMock(Ip4Address::from_string("192.168.0.1", ec)));

    // Add VPN route with target "blue".
    AddVPNRouteWithTarget(peers_[0], "192.168.0.1:1:10.0.1.1/32", 100,
        "target:64496:101", "originvn:65596:1001");
    task_util::WaitForIdle();

    // Imported in both blue and red.
    VERIFY_EQ(1, RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("blue", "10.0.1.1/32") != NULL);
    VERIFY_EQ(1, RouteCount("red"));
    TASK_UTIL_EXPECT_TRUE(InetRouteLookup("red", "10.0.1.1/32") != NULL);

    // Verify OriginVn for routes in blue and red.
    // OriginVn from the VPN route is ignored since it doesn't have our ASN.
    BgpRoute *rt;
    rt = InetRouteLookup("blue", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));
    rt = InetRouteLookup("red", "10.0.1.1/32");
    VERIFY_EQ(blue_vn_idx, GetOriginVnIndexFromRoute(rt->BestPath()));

    // Delete VPN route.
    DeleteVPNRoute(peers_[0], "192.168.0.1:1:10.0.1.1/32");
    task_util::WaitForIdle();
    VERIFY_EQ(0, RouteCount("blue"));
    VERIFY_EQ(0, RouteCount("red"));
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
