/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_table.h"

#include "base/task.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_update.h"
#include "bgp/scheduling_group.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet/inet_route.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

//
// OVERVIEW
//
// This is the test suite for BgpTable::Export method for inet and inetvpn
// tables. Since the method is called for one RibOut and one BgpRoute at a
// time, the test also uses a single RibOut and BgpRoute.
//
// The tests are parameterized by the table_name and the type (internal or
// external) of peer that added the best path for the BgpRoute.
//
// TEST STEPS
//
// 1. The Setup method for the parameterized tests extracts the table_name
//    and the peer type and then invokes Setup for BgpTableExportTest.
// 2. The Setup method for BgpTableExportTest then creates a BgpTestPeer, a
//    suitable BgpAttr based on the peer type and finds the right BgpTable.
// 3. Each test creates a RibOut with the right peer type and AS number.
// 4. The test updates the default BgpAttr created in step 2 by calling the
//    SetAttr* methods.
// 5. The Export method is invoked.
// 6. The export result (accept or reject) is verified.
// 7. In cases where the export result is accept, the RibOutAttr is verified
//    by calling the VerifyAttr* methods.
//
// CONVENTIONS
//
// 1. If source of the path is eBGP we use an AS Path of 100.
// 2. Our AS number is 200.
// 3. The RibOut peer type is eBGP and the AS number for it is 300.
//

class BgpTestPeer : public IPeer {
public:
    BgpTestPeer(bool internal) : internal_(internal) { }
    virtual ~BgpTestPeer() { }

    virtual std::string ToString() const {
        return internal_ ? "TestPeerInt" : "TestPeerExt";
    }
    virtual std::string ToUVEKey() const {
        return internal_ ? "TestPeerInt" : "TestPeerExt";
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() { }
    BgpProto::BgpPeerType PeerType() const {
         return internal_ ? BgpProto::IBGP : BgpProto::EBGP;
    }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const std::string GetStateName() const { return ""; }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }

private:
    bool internal_;
};

class BgpRouteMock : public BgpRoute {
public:
    virtual std::string ToString() const { return ""; }
    virtual int CompareTo(const Route &rhs) const { return 0; }
    virtual void SetKey(const DBRequestKey *key) { }
    virtual KeyPtr GetDBRequestKey() const { return KeyPtr(NULL); }
    virtual bool IsLess(const DBEntry &rhs) const { return true; }
    virtual u_int16_t Afi() const { return 0; }
    virtual u_int8_t Safi() const { return 0; }
};


class RTargetGroupMgrTest : public RTargetGroupMgr {
public:
    RTargetGroupMgrTest(BgpServer *server) : RTargetGroupMgr(server) {
    }
    virtual void GetRibOutInterestedPeers(RibOut *ribout, 
             const ExtCommunity *ext_community, 
             const RibPeerSet &peerset, RibPeerSet &new_peerset) {
        new_peerset = peerset;
    }
};


class BgpTableExportTest : public ::testing::Test {
protected:

    BgpTableExportTest()
        : server_(&evm_, "Local") {
        server_.set_autonomous_system(200);
        active_peerset_.set(1);
        active_peerset_.set(3);
        active_peerset_.set(5);
    }

    virtual void SetUp() {
        std::cout << "Table: " << table_name_
                << " Source: " << (internal_ ? "IBGP" : "EBGP")
                << std::endl;

        CreatePeer();
        CreateAttr();
        FindTable();
    }

    virtual void TearDown() {
        rt_.RemovePath(peer_.get());
        table_->RibOutDelete(ribout_->ExportPolicy());
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    as_t AsNumber() {
        return server_.autonomous_system();
    }

    bool PeerIsInternal() {
        return internal_;
    }

    void CreatePeer() {
        peer_.reset(new BgpTestPeer(internal_));
    }

    void CreateAttr() {
        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attr_spec.push_back(&origin);

        BgpAttrNextHop nexthop(0x01010101);
        attr_spec.push_back(&nexthop);

        AsPathSpec path_spec;
        BgpAttrLocalPref local_pref(100);
        BgpAttrMultiExitDisc med(100);

        if (internal_) {
            attr_spec.push_back(&path_spec);
            attr_spec.push_back(&local_pref);
        } else {
            AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
            path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
            path_seg->path_segment.push_back(100);
            path_spec.path_segments.push_back(path_seg);
            attr_spec.push_back(&path_spec);
            attr_spec.push_back(&med);
        }

        attr_ptr_ = server_.attr_db()->Locate(attr_spec);
    }

    void FindTable() {
        ASSERT_TRUE(table_name_ != NULL);
        DB *db = server_.database();
        table_ = static_cast<BgpTable *>(db->FindTable(table_name_));
        ASSERT_TRUE(table_ != NULL);
    }

    void CreateRibOut(BgpProto::BgpPeerType type,
            RibExportPolicy::Encoding encoding, as_t as_number = 0) {
        RibExportPolicy policy(type, encoding, as_number, -1, 0);
        ribout_ = table_->RibOutLocate(&mgr_, policy);
    }

    void SetAttrAsPath(as_t as_number) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        const AsPathSpec &path_spec = attr_ptr_->as_path()->path();
        AsPathSpec *path_spec_ptr = path_spec.Add(as_number);
        attr->set_as_path(path_spec_ptr);
        delete path_spec_ptr;
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void ResetAttrAsPath() {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_as_path(NULL);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrCommunity(uint32_t comm_value) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        CommunitySpec community;
        community.communities.push_back(comm_value);
        attr->set_community(&community);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrExtCommunity(uint32_t identifier) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        ostringstream oss;
        oss << "target:" << AsNumber() << ":" << identifier;
        RouteTarget rt(RouteTarget::FromString(oss.str()));
        ExtCommunitySpec extcomm;
        extcomm.communities.push_back(
                get_value(rt.GetExtCommunity().begin(), 8));
        attr->set_ext_community(&extcomm);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrLocalPref(uint32_t local_pref) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_local_pref(local_pref);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrMed(uint32_t med) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_med(med);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void AddPath() {
        BgpPath *path = 
            new BgpPath(peer_.get(), BgpPath::BGP_XMPP, attr_ptr_, 0, 0);
        rt_.InsertPath(path);
    }

    void AddInfeasiblePath() {
        BgpPath *path =
            new BgpPath(peer_.get(), BgpPath::BGP_XMPP, attr_ptr_, 
                        BgpPath::AsPathLooped, 0);
        rt_.InsertPath(path);
    }

    void RunExport() {
        result_ = table_->Export(ribout_, &rt_, active_peerset_, uinfo_slist_);
    }

    void VerifyExportReject() {
        EXPECT_TRUE(false == result_);
        EXPECT_EQ(0, uinfo_slist_->size());
    }

    void VerifyExportAccept() {
        EXPECT_TRUE(true == result_);
        EXPECT_EQ(1, uinfo_slist_->size());
        const UpdateInfo &uinfo = uinfo_slist_->front();
        EXPECT_EQ(active_peerset_, uinfo.target);
    }

    void VerifyAttrNoChange() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(attr_ptr_.get(), attr);
    }

    void VerifyAttrLocalPref(uint32_t local_pref) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(local_pref, attr->local_pref());
    }

    void VerifyAttrMed(uint32_t med) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(med, attr->med());
    }

    void VerifyAttrAsPrepend() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        as_t my_as = server_.autonomous_system();
        EXPECT_TRUE(as_path->path().AsLeftMostMatch(my_as));
    }

    void VerifyAttrNoAsPrepend() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        as_t my_as = server_.autonomous_system();
        EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
    }

    void VerifyAttrExtCommunity(bool is_null) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(is_null, attr->ext_community() == NULL);
    }

    EventManager evm_;
    BgpServerTest server_;
    SchedulingGroupManager mgr_;

    bool internal_;
    const char *table_name_;

    BgpTable *table_;
    RibOut *ribout_;
    std::auto_ptr<BgpTestPeer> peer_;
    RibPeerSet active_peerset_;
    BgpRouteMock rt_;
    BgpAttrPtr attr_ptr_;

    UpdateInfoSList uinfo_slist_;
    bool result_;

};

// Parameterize table name and peer type.

typedef std::tr1::tuple<const char *, bool> TestParams1;

class BgpTableExportParamTest1 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams1> {

    virtual void SetUp() {
        table_name_ = std::tr1::get<0>(GetParam());
        internal_ = std::tr1::get<1>(GetParam());
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Route without a best path is rejected.
//
TEST_P(BgpTableExportParamTest1, NoBestPath) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Route with an infeasible best path is rejected.
//
TEST_P(BgpTableExportParamTest1, NoFeasiblePath) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    AddInfeasiblePath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Route with NoAdvertise community is rejected for eBGP.
//
TEST_P(BgpTableExportParamTest1, CommunityNoAdvertise1) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrCommunity(Community::NoAdvertise);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Route with NoAdvertise community is rejected for iBGP.
//
TEST_P(BgpTableExportParamTest1, CommunityNoAdvertise2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrCommunity(Community::NoAdvertise);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Route with NoExport community is rejected for eBGP.
//
TEST_P(BgpTableExportParamTest1, CommunityNoExport) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrCommunity(Community::NoExport);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Route with NoExportSubconfed community is rejected for eBGP.
//
TEST_P(BgpTableExportParamTest1, CommunityNoExportSubconfed) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrCommunity(Community::NoExportSubconfed);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: LocalPref and Med are cleared for eBGP.
//
TEST_P(BgpTableExportParamTest1, EBgpNoLocalPrefMed) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(0);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Our AS is prepended to AsPath for eBGP.
// Note:   Current AsPath is non-NULL.
//
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(0);
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Our AS is prepended to AsPath for eBGP.
// Note:   Current AsPath is NULL.
//
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend2) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    ResetAttrAsPath();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(0);
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Route with AsPath loop is rejected.
//
TEST_P(BgpTableExportParamTest1, AsPathLoop) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrAsPath(300);
    AddPath();
    RunExport();
    VerifyExportReject();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest1,
        ::testing::Combine(
            ::testing::Values("inet.0", "bgp.l3vpn.0"),
            ::testing::Bool()));

// Parameterize table name and fix peer type to internal.

class BgpTableExportParamTest2 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<const char *> {

    virtual void SetUp() {
        table_name_ = GetParam();
        internal_ = true;
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: iBGP
// Intent: Split Horizon check rejects export.
//
TEST_P(BgpTableExportParamTest2, IBgpSplitHorizon) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    AddPath();
    RunExport();
    VerifyExportReject();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest2,
        ::testing::Values("inet.0", "bgp.l3vpn.0"));

// Parameterize table name and fix peer type to external.

class BgpTableExportParamTest3 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<const char *> {

    virtual void SetUp() {
        table_name_ = GetParam();
        internal_ = false;
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Route with NoExport community is accepted for iBGP.
//
TEST_P(BgpTableExportParamTest3, CommunityNoExport) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrCommunity(Community::NoExport);
    AddPath();
    RunExport();
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Route with NoExportSubconfed community is accepted for iBGP.
//
TEST_P(BgpTableExportParamTest3, CommunityNoExportSubconfed) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrCommunity(Community::NoExportSubconfed);
    AddPath();
    RunExport();
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: LocalPref should be set to default value.
//
TEST_P(BgpTableExportParamTest3, IBgpLocalPref) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    AddPath();
    RunExport();
    VerifyAttrLocalPref(100);
    VerifyAttrMed(100);
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Our AS is not prepended when sending to iBGP.
// Note:   Current AsPath is non-NULL.
//
TEST_P(BgpTableExportParamTest3, IBgpNoAsPrepend1) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(100);
    VerifyAttrMed(100);
    VerifyAttrNoAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Our AS is not prepended when sending to iBGP.
// Note:   Current AsPath is NULL.
//
TEST_P(BgpTableExportParamTest3, IBgpNoAsPrepend2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    ResetAttrAsPath();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(100);
    VerifyAttrMed(100);
    VerifyAttrNoAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: LocalPref should not be overwritten if it's already set.
//
TEST_P(BgpTableExportParamTest3, IBgpNoOverwriteLocalPref) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrLocalPref(50);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(50);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Med should not be overwritten.
//
TEST_P(BgpTableExportParamTest3, IBgpNoOverwriteMed) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrMed(100);
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest3,
        ::testing::Values("inet.0", "bgp.l3vpn.0"));

// Parameterize peer type and fix table name to inet.0.

class BgpTableExportParamTest4a :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<bool> {

    virtual void SetUp() {
        table_name_ = "inet.0";
        internal_ = GetParam();
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Extended communities are stripped on export.
//
TEST_P(BgpTableExportParamTest4a, StripExtendedCommunity1) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrExtCommunity(true);
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Extended communities are stripped on export.
//
TEST_P(BgpTableExportParamTest4a, StripExtendedCommunity2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    if (PeerIsInternal()) {
        VerifyExportReject();
    } else {
        VerifyExportAccept();
        VerifyAttrExtCommunity(true);
    }
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest4a,
            ::testing::Bool());

// Parameterize peer type and fix table name to inet.0.
// RibOut encoding is XMPP.

class BgpTableExportParamTest4b :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<bool> {

    virtual void SetUp() {
        table_name_ = "inet.0";
        internal_ = GetParam();
        BgpTableExportTest::SetUp();
        CreateRibOut(BgpProto::XMPP, RibExportPolicy::XMPP);
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: XMPP
// Intent: Attribute manipulations are skipped if RibOut is XMPP.
//
TEST_P(BgpTableExportParamTest4b, AttrNoChange) {
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoChange();
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: XMPP
// Intent: NoAdvertise community is ignored if RibOut is XMPP.
//
TEST_P(BgpTableExportParamTest4b, CommunityNoAdvertise) {
    SetAttrCommunity(Community::NoAdvertise);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoChange();
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: XMPP
// Intent: NoExport community is ignored if RibOut is XMPP.
//
TEST_P(BgpTableExportParamTest4b, CommunityNoExport) {
    SetAttrCommunity(Community::NoExport);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoChange();
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: XMPP
// Intent: NoExportSubconfed community is ignored if RibOut is XMPP.
//
TEST_P(BgpTableExportParamTest4b, CommunityNoExportSubconfed) {
    SetAttrCommunity(Community::NoExportSubconfed);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoChange();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest4b,
            ::testing::Bool());

// Parameterize peer type and fix table name to bgp.l3vpn.0.

class BgpTableExportParamTest5 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<bool> {

    virtual void SetUp() {
        table_name_ = "bgp.l3vpn.0";
        internal_ = GetParam();
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }
};

//
// Table : bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Extended communities are not stripped on export.
//
TEST_P(BgpTableExportParamTest5, NoStripExtendedCommunity1) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrExtCommunity(false);
}

//
// Table : bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Extended communities are not stripped on export.
//
TEST_P(BgpTableExportParamTest5, NoStripExtendedCommunity2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, AsNumber());
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    if (PeerIsInternal()) {
        VerifyExportReject();
    } else {
        VerifyExportAccept();
        VerifyAttrExtCommunity(false);
    }
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest5,
            ::testing::Bool());

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();

    BgpObjectFactory::Register<RTargetGroupMgr>(
        boost::factory<RTargetGroupMgrTest *>());
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
