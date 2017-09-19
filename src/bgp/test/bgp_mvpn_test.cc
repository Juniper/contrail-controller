/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using boost::scoped_ptr;
using boost::system::error_code;
using std::make_pair;
using std::string;

struct PMSIParams {
public:
    PMSIParams(bool result, uint32_t label, const string &address,
               string encap_s, ErmVpnRoute **rt) :
            result(result), label(label), address(address), ermvpn_rt(rt) {
        encaps.push_back(encap_s);
    }

    bool result;
    uint32_t label;
    string address;
    std::vector<std::string> encaps;
    ErmVpnRoute **ermvpn_rt;
};
static std::map<MvpnState::SG, PMSIParams> pmsi_params;

class McastTreeManagerMock : public McastTreeManager {
public:
    McastTreeManagerMock(ErmVpnTable *table) : McastTreeManager(table) {
    }
    ~McastTreeManagerMock() { }
    virtual UpdateInfo *GetUpdateInfo(ErmVpnRoute *route) { return NULL; }

    virtual ErmVpnRoute *GetGlobalTreeRootRoute(const Ip4Address &source,
            const Ip4Address &group) const {
        std::map<MvpnState::SG, PMSIParams>::iterator iter =
            pmsi_params.find(MvpnState::SG(source, group));
        if (iter == pmsi_params.end() || !iter->second.result)
            return NULL;
        TASK_UTIL_EXPECT_NE(static_cast<ErmVpnRoute *>(NULL),
                            *(iter->second.ermvpn_rt));
        return *(iter->second.ermvpn_rt);
    }

    virtual bool GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
            Ip4Address *address, std::vector<std::string> *encap) const {
        if (!rt)
            return false;

        std::map<MvpnState::SG, PMSIParams>::iterator iter =
            pmsi_params.find(MvpnState::SG(rt->GetPrefix().source(),
                                           rt->GetPrefix().group()));
        if (iter == pmsi_params.end() || !iter->second.result)
            return false;

        *label = iter->second.label;
        error_code e;
        *address = IpAddress::from_string(iter->second.address, e).to_v4();
        BOOST_FOREACH(string encap_str, iter->second.encaps)
            encap->push_back(encap_str);
        return true;
    }


private:
};

class BgpMvpnTest : public ::testing::Test {
protected:
    BgpMvpnTest() {
    }

    const string GetConfig() const {
        return ""
"<?xml version='1.0' encoding='utf-8'?>"
"<config>"
"   <bgp-router name=\"local\">"
"       <address>127.0.0.1</address>"
"       <autonomous-system>1</autonomous-system>"
"   </bgp-router>"
"   <routing-instance name='red'>"
"       <vrf-target>target:127.0.0.1:1001</vrf-target>"
"   </routing-instance>"
"   <routing-instance name='blue'>"
"       <vrf-target>target:127.0.0.1:1002</vrf-target>"
"   </routing-instance>"
"   <routing-instance name='green'>"
"       <vrf-target>target:127.0.0.1:1003</vrf-target>"
"       <vrf-target>"
"           target:127.0.0.1:1001"
"           <import-export>import</import-export>"
"       </vrf-target>"
"       <vrf-target>"
"           target:127.0.0.1:1002"
"           <import-export>import</import-export>"
"       </vrf-target>"
"   </routing-instance>"
"</config>"
        ;
    }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "local"));
        thread_.reset(new ServerThread(evm_.get()));
        thread_->Start();
        server_->Configure(GetConfig());
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.mvpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.ermvpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("red.mvpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("blue.mvpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("green.mvpn.0"));

        master_ = static_cast<BgpTable *>(
            server_->database()->FindTable("bgp.mvpn.0"));
        red_ = static_cast<MvpnTable *>(
            server_->database()->FindTable("red.mvpn.0"));
        blue_ = static_cast<MvpnTable *>(
            server_->database()->FindTable("blue.mvpn.0"));
        green_ = static_cast<MvpnTable *>(
            server_->database()->FindTable("green.mvpn.0"));
        fabric_ermvpn_ = static_cast<ErmVpnTable *>(
            server_->database()->FindTable("bgp.ermvpn.0"));
    }

    void UpdateBgpIdentifier(const string &address) {
        error_code err;
        task_util::TaskFire(boost::bind(&BgpServer::UpdateBgpIdentifier,
            server_.get(), Ip4Address::from_string(address, err)),
            "bgp::Config");
    }

    void TearDown() {
        server_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, TcpServerManager::GetServerCount());
        evm_->Shutdown();
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    ErmVpnRoute *AddErmVpnRoute(ErmVpnTable *table, const string &prefix_str,
                                const string &target) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new ErmVpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        return FindErmVpnRoute(table, prefix_str);
    }

    void DeleteErmVpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
    }

    ErmVpnRoute *FindErmVpnRoute(ErmVpnTable *table, const string &prefix_str) {
        while (!table->FindRoute(ErmVpnPrefix::FromString(prefix_str)))
            usleep(10);
        return table->FindRoute(ErmVpnPrefix::FromString(prefix_str));
    }

    void AddMvpnRoute(BgpTable *table, const string &prefix_str,
                      const string &target) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));

        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        task_util::WaitForIdle();
    }

    void DeleteMvpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
    }

    MvpnRoute *VerifyLeafADMvpnRoute(MvpnTable *table, const string &prefix,
            const PMSIParams &pmsi_params) {
        MvpnPrefix type4_prefix =
            MvpnPrefix::FromString("4-" + prefix + ",127.0.0.1");
        MvpnRoute *leaf_ad_rt = table->FindRoute(type4_prefix);
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL), leaf_ad_rt);
        EXPECT_EQ(type4_prefix, leaf_ad_rt->GetPrefix());
        TASK_UTIL_EXPECT_EQ(type4_prefix.ToString(),
                            leaf_ad_rt->GetPrefix().ToString());
        TASK_UTIL_EXPECT_TRUE(leaf_ad_rt->IsUsable());

        // Verify path attributes.
        const BgpAttr *attr = leaf_ad_rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_NE(static_cast<const BgpAttr *>(NULL), attr);
        TASK_UTIL_EXPECT_NE(static_cast<const ExtCommunity *>(NULL),
                  attr->ext_community());

        int tunnel_encap = 0;
        ExtCommunity::ExtCommunityValue tunnel_encap_val;
        BOOST_FOREACH(ExtCommunity::ExtCommunityValue v,
                      attr->ext_community()->communities()) {
            if (ExtCommunity::is_tunnel_encap(v)) {
                tunnel_encap_val = v;
                tunnel_encap++;
            }
        }
        TASK_UTIL_EXPECT_EQ(1, tunnel_encap);
        TASK_UTIL_EXPECT_EQ("encapsulation:" + pmsi_params.encaps.front(),
                  TunnelEncap(tunnel_encap_val).ToString());
        TASK_UTIL_EXPECT_NE(static_cast<PmsiTunnel *>(NULL),
                            attr->pmsi_tunnel());
        TASK_UTIL_EXPECT_EQ(Ip4Address::from_string(pmsi_params.address),
                  attr->pmsi_tunnel()->identifier());
        TASK_UTIL_EXPECT_EQ(pmsi_params.label, attr->pmsi_tunnel()->label());
        TASK_UTIL_EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
                  attr->pmsi_tunnel()->tunnel_type());
        int rtargets = 0;
        ExtCommunity::ExtCommunityValue rtarget_val;
        BOOST_FOREACH(ExtCommunity::ExtCommunityValue v,
                      attr->ext_community()->communities()) {
            if (ExtCommunity::is_route_target(v)) {
                rtarget_val = v;
                rtargets++;
            }
        }

        // Route target must be solely S-PMSI Sender Originator:0, 192.168.1.1:0
        TASK_UTIL_EXPECT_EQ(1, rtargets);
        TASK_UTIL_EXPECT_EQ("target:192.168.1.1:0",
                            RouteTarget(rtarget_val).ToString());

        return leaf_ad_rt;
    }

    scoped_ptr<EventManager> evm_;
    scoped_ptr<ServerThread> thread_;
    scoped_ptr<BgpServerTest> server_;
    DB db_;
    BgpTable *master_;
    ErmVpnTable *fabric_ermvpn_;
    MvpnTable *red_;
    MvpnTable *blue_;
    MvpnTable *green_;
};

// Ensure that Type1 AD routes are created inside the mvpn table.
TEST_F(BgpMvpnTest, Type1ADLocal) {
    TASK_UTIL_EXPECT_EQ(1, red_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());
    TASK_UTIL_EXPECT_EQ(3, master_->Size());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    error_code err;
    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(red_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(red_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.1", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(blue_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(blue_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.1", err), neighbor.originator());
}

// Change Identifier and ensure that routes have updated originator id.
TEST_F(BgpMvpnTest, Type1ADLocalWithIdentifierChanged) {
    error_code err;
    UpdateBgpIdentifier("127.0.0.2");
    TASK_UTIL_EXPECT_EQ(3, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, red_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(red_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(red_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.2", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(blue_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(blue_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.2", err), neighbor.originator());
}

// Reset BGP Identifier and ensure that Type1 route is no longer generated.
TEST_F(BgpMvpnTest, Type1ADLocalWithIdentifierRemoved) {
    error_code err;
    UpdateBgpIdentifier("0.0.0.0");
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    TASK_UTIL_EXPECT_EQ(0, red_->Size());
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(0, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, green_->manager()->neighbors().size());
}

// Add Type1AD route from a mock bgp peer into bgp.mvpn.0 table.
TEST_F(BgpMvpnTest, Type1AD_Remote) {
    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    // Inject a Type1 route from a mock peer into bgp.mvpn.0 table with red
    // route-target.
    string prefix = "1-10.1.1.1:65535,9.8.7.6";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");

    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(4, green_->Size()); // 1 local + 1 remote(red)

    // Verify that neighbor is detected.
    TASK_UTIL_EXPECT_EQ(1, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(3, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    error_code err;

    EXPECT_TRUE(red_->manager()->FindNeighbor(
                    RouteDistinguisher::FromString("10.1.1.1:65535", &err),
                    &neighbor));
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    RouteDistinguisher::FromString("10.1.1.1:65535", &err),
                    &neighbor));
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), neighbor.originator());

    DeleteMvpnRoute(master_, prefix);

    // Verify that neighbor is deleted.
    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 local + 1 red + 1 blue
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD is not originated if
// PMSI information is not available for forwarding.
TEST_F(BgpMvpnTest, Type3_SPMSI_Without_ErmVpnRoute) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    DeleteMvpnRoute(master_, prefix);

    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes.
TEST_F(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";

    // Setup ermvpn route before type 3 spmsi route is added.
    string ermvpn_prefix = "2-10.1.1.1:65535-192.168.1.1,224.1.2.3,9.8.7.6";

    error_code e;
    ErmVpnRoute *ermvpn_rt = NULL;
    MvpnState::SG sg(IpAddress::from_string("9.8.7.6", e),
                     IpAddress::from_string("224.1.2.3", e));
    PMSIParams pmsi(PMSIParams(true, 10, "1.2.3.4", "gre", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi));
    ermvpn_rt = AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix,
                               "target:127.0.0.1:1100");

    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(5, master_->Size()); // 3 local + 1 remote + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(3, red_->Size()); // 1 local + 1 remote(red) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    // Lookup the actual leaf-ad route and verify its attributes.
    VerifyLeafADMvpnRoute(red_, prefix, pmsi);
    VerifyLeafADMvpnRoute(green_, prefix, pmsi);

    DeleteMvpnRoute(master_, prefix);
    pmsi_params.clear();
    DeleteErmVpnRoute(fabric_ermvpn_, ermvpn_prefix);

    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes, but only after ermvpn route becomes available.
TEST_F(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_2) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    // Make ermvpn route available now and verifiy that leaf-ad is originated.
    // Add a ermvpn route into the table.
    ErmVpnRoute *ermvpn_rt = NULL;
    error_code e;
    MvpnState::SG sg(IpAddress::from_string("9.8.7.6", e),
                     IpAddress::from_string("224.1.2.3", e));
    PMSIParams pmsi(PMSIParams(true, 10, "1.2.3.4", "gre", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi));
    string ermvpn_prefix = "2-10.1.1.1:65535-192.168.1.1,224.1.2.3,9.8.7.6";
    ermvpn_rt =
        AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix, "target:127.0.0.1:1100");

    TASK_UTIL_EXPECT_EQ(5, master_->Size()); // 3 local + 1 remote + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(3, red_->Size()); // 1 local + 1 remote(red) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    // Lookup the actual leaf-ad route and verify its attributes.
    VerifyLeafADMvpnRoute(red_, prefix, pmsi);
    VerifyLeafADMvpnRoute(green_, prefix, pmsi);

    DeleteMvpnRoute(master_, prefix);
    pmsi_params.clear();
    DeleteErmVpnRoute(fabric_ermvpn_, ermvpn_prefix);

    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Verify that if ermvpn route is deleted, then any type 4 route if originated
// already is withdrawn.
TEST_F(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_3) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    // Make ermvpn route available now and verifiy that leaf-ad is originated.
    // Add a ermvpn route into the table.
    error_code e;
    ErmVpnRoute *ermvpn_rt = NULL;
    MvpnState::SG sg(IpAddress::from_string("9.8.7.6", e),
                     IpAddress::from_string("224.1.2.3", e));
    PMSIParams pmsi(PMSIParams(true, 10, "1.2.3.4", "gre", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi));
    string ermvpn_prefix = "2-10.1.1.1:65535-192.168.1.1,224.1.2.3,9.8.7.6";
    ermvpn_rt =
        AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix, "target:127.0.0.1:1100");

    TASK_UTIL_EXPECT_EQ(5, master_->Size()); // 3 local + 1 remote + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(3, red_->Size()); // 1 local + 1 remote(red) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    // Lookup the actual leaf-ad route and verify its attributes.
    VerifyLeafADMvpnRoute(red_, prefix, pmsi);
    VerifyLeafADMvpnRoute(green_, prefix, pmsi);

    // Delete the ermvpn route and verify that leaf-ad route is also deleted.
    pmsi_params.erase(sg);
    DeleteErmVpnRoute(fabric_ermvpn_, ermvpn_prefix);
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    DeleteMvpnRoute(master_, prefix);
    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes, but only after ermvpn route becomes available.
// Add spurious notify on ermvpn route and ensure that type-4 leafad path and
// its attributes do not change.
TEST_F(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_4) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    // Make ermvpn route available now and verifiy that leaf-ad is originated.
    // Add a ermvpn route into the table.
    ErmVpnRoute *ermvpn_rt = NULL;
    error_code e;
    MvpnState::SG sg(IpAddress::from_string("9.8.7.6", e),
                     IpAddress::from_string("224.1.2.3", e));
    PMSIParams pmsi(PMSIParams(true, 10, "1.2.3.4", "gre", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi));
    string ermvpn_prefix = "2-10.1.1.1:65535-192.168.1.1,224.1.2.3,9.8.7.6";
    ermvpn_rt =
        AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix, "target:127.0.0.1:1100");

    TASK_UTIL_EXPECT_EQ(5, master_->Size()); // 3 local + 1 remote + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(3, red_->Size()); // 1 local + 1 remote(red) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    // Lookup the actual leaf-ad route and verify its attributes.
    MvpnRoute *leafad_red_rt = VerifyLeafADMvpnRoute(red_, prefix, pmsi);
    MvpnRoute *leafad_green_rt = VerifyLeafADMvpnRoute(green_, prefix, pmsi);
    const BgpPath *red_path = leafad_red_rt->BestPath();
    const BgpAttr *red_attr = red_path->GetAttr();
    const BgpPath *green_path = leafad_green_rt->BestPath();
    const BgpAttr *green_attr = green_path->GetAttr();

    // Notify ermvpn route without any change.
    ermvpn_rt->Notify();

    // Verify that leafad path or its attributes did not change.
    TASK_UTIL_EXPECT_EQ(leafad_red_rt, VerifyLeafADMvpnRoute(red_, prefix,
                                                             pmsi));
    TASK_UTIL_EXPECT_EQ(leafad_green_rt, VerifyLeafADMvpnRoute(green_, prefix,
                                                               pmsi));
    TASK_UTIL_EXPECT_EQ(red_path, leafad_red_rt->BestPath());
    TASK_UTIL_EXPECT_EQ(green_path, leafad_green_rt->BestPath());
    TASK_UTIL_EXPECT_EQ(red_attr, leafad_red_rt->BestPath()->GetAttr());
    TASK_UTIL_EXPECT_EQ(green_attr, leafad_green_rt->BestPath()->GetAttr());

    DeleteMvpnRoute(master_, prefix);
    pmsi_params.clear();
    DeleteErmVpnRoute(fabric_ermvpn_, ermvpn_prefix);

    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Similar to previous test, but this time, do change some of the PMSI attrs.
// Leaf4 ad path already generated must be updated with the PMSI information.
TEST_F(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_5) {
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red route
    // target. This route should go into red and green table.
    string prefix = "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    // Make ermvpn route available now and verifiy that leaf-ad is originated.
    // Add a ermvpn route into the table.
    ErmVpnRoute *ermvpn_rt = NULL;
    error_code e;
    MvpnState::SG sg(IpAddress::from_string("9.8.7.6", e),
                     IpAddress::from_string("224.1.2.3", e));
    PMSIParams pmsi(PMSIParams(true, 10, "1.2.3.4", "gre", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi));
    string ermvpn_prefix = "2-10.1.1.1:65535-192.168.1.1,224.1.2.3,9.8.7.6";
    ermvpn_rt =
        AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix, "target:127.0.0.1:1100");

    TASK_UTIL_EXPECT_EQ(5, master_->Size()); // 3 local + 1 remote + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(3, red_->Size()); // 1 local + 1 remote(red) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(green) + 1 leaf-ad
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    // Lookup the actual leaf-ad route and verify its attributes.
    MvpnRoute *leafad_red_rt = VerifyLeafADMvpnRoute(red_, prefix, pmsi);
    MvpnRoute *leafad_green_rt = VerifyLeafADMvpnRoute(green_, prefix, pmsi);
    const BgpPath *red_path = leafad_red_rt->BestPath();
    const BgpAttr *red_attr = red_path->GetAttr();
    const BgpPath *green_path = leafad_green_rt->BestPath();
    const BgpAttr *green_attr = green_path->GetAttr();

    // Update PMSI.
    pmsi_params.erase(sg);
    PMSIParams pmsi2(PMSIParams(true, 20, "1.2.3.5", "udp", &ermvpn_rt));
    pmsi_params.insert(make_pair(sg, pmsi2));

    TASK_UTIL_EXPECT_EQ(ermvpn_rt,
        AddErmVpnRoute(fabric_ermvpn_, ermvpn_prefix, "target:127.0.0.1:1101"));

    // Verify that leafad path and its attributes did change.
    TASK_UTIL_EXPECT_NE(red_attr, leafad_red_rt->BestPath()->GetAttr());
    TASK_UTIL_EXPECT_NE(green_attr, leafad_green_rt->BestPath()->GetAttr());
    TASK_UTIL_EXPECT_NE(red_path, leafad_red_rt->BestPath());
    TASK_UTIL_EXPECT_NE(green_path, leafad_green_rt->BestPath());
    TASK_UTIL_EXPECT_EQ(leafad_red_rt, VerifyLeafADMvpnRoute(red_, prefix,
                                                             pmsi2));
    TASK_UTIL_EXPECT_EQ(leafad_green_rt, VerifyLeafADMvpnRoute(green_, prefix,
                                                               pmsi2));

    DeleteMvpnRoute(master_, prefix);
    pmsi_params.clear();
    DeleteErmVpnRoute(fabric_ermvpn_, ermvpn_prefix);

    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

// Receive Type-4 leaf ad route and ensure that Type-5 source-active route is
// updated with olist correctly.
TEST_F(BgpMvpnTest, Type4_LeafAD_Receive_1) {
    const string t5_prefix = "5-10.1.1.1:65535,224.1.2.3,9.8.7.6";
    AddMvpnRoute(red_, t5_prefix, "target:127.0.0.1:1001");
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(4, green_->Size());

    // Inject type-7 receiver route with red RI vit.
    const string t7_prefix = "7-10.1.1.1:65535,1,224.1.2.3,9.8.7.6";
    AddMvpnRoute(master_, t7_prefix, "target:127.0.0.1:" +
        integerToString(red_->routing_instance()->index()));

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green but no type-4 will get generated as there is no
    // active receiver joined yet.
    TASK_UTIL_EXPECT_EQ(6, master_->Size()); // 3 local + 1 remote + 1 join +
                                             // 1 spmsi
    TASK_UTIL_EXPECT_EQ(4, red_->Size()); // 1 local + 1 remote(red) + 1 join +
                                          // 1 spmsi
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 2 remote(red) + 1 remote(blue) + 1 spmsi(red)
    TASK_UTIL_EXPECT_EQ(5, green_->Size());

    DeleteMvpnRoute(red_, t5_prefix);
    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 join
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local+ 1 join
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());

    // Remove join route.
    DeleteMvpnRoute(master_, t7_prefix);
    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local + 1 join
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local+ 1 join
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local

    // 1 local + 1 remote(red) + 1 remote(blue)
    TASK_UTIL_EXPECT_EQ(3, green_->Size());
}

static void SetUp() {
    bgp_log_test::init();
    MvpnManager::set_enable(true);
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<McastTreeManager>(
        boost::factory<McastTreeManagerMock *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
