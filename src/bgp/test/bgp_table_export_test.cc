/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_update.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "net/community_type.h"

using boost::assign::list_of;
using boost::scoped_ptr;
using std::auto_ptr;
using std::cout;
using std::endl;
using std::ostringstream;
using std::string;
using std::vector;

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
    explicit BgpTestPeer(int index, bool internal)
        : index_(index),
          internal_(internal),
          as4_supported_(false),
          cluster_id_(100),
          to_str_("Peer " + integerToString(index_)) {
    }
    virtual ~BgpTestPeer() { }

    virtual const std::string &ToString() const { return to_str_; }
    virtual const std::string &ToUVEKey() const { return to_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close(bool graceful) { }
    BgpProto::BgpPeerType PeerType() const {
         return internal_ ? BgpProto::IBGP : BgpProto::EBGP;
    }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const string GetStateName() const { return ""; }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return as4_supported_; }
    void SetAs4Supported() { as4_supported_ = true; }
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual void ProcessPathTunnelEncapsulation(const BgpPath *path,
        BgpAttr *attr, ExtCommunityDB *extcomm_db, const BgpTable *table)
        const {
    }
    virtual const std::vector<std::string> GetDefaultTunnelEncap(
        Address::Family family) const {
        return std::vector<std::string>();
    }
    virtual bool IsRegistrationRequired() const { return true; }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }
    virtual bool ProcessSession() const { return true; }
    virtual bool CheckSplitHorizon(uint32_t cluster_id = 0,
            uint32_t ribout_cid = 0) const {
        if (PeerType() != BgpProto::IBGP)
            return false;
        if (!cluster_id) return true;
        // check if received from client or non-client by comparing the
        // clusterid of router with first value in cluster_list
        if (cluster_id != cluster_id_) {
            // If received from non-client, reflect to all the clients only
            if (ribout_cid && ribout_cid != cluster_id)
                return true;
        }
        return false;
    }

private:
    int index_;
    bool internal_;
    bool as4_supported_;
    uint32_t cluster_id_;
    std::string to_str_;
};

class BgpRouteMock : public BgpRoute {
public:
    virtual string ToString() const { return ""; }
    virtual int CompareTo(const Route &rhs) const { return 0; }
    virtual void SetKey(const DBRequestKey *key) { }
    virtual KeyPtr GetDBRequestKey() const { return KeyPtr(NULL); }
    virtual bool IsLess(const DBEntry &rhs) const { return true; }
    virtual u_int16_t Afi() const { return 0; }
    virtual u_int8_t Safi() const { return 0; }
};


class RTargetGroupMgrTest : public RTargetGroupMgr {
public:
    explicit RTargetGroupMgrTest(BgpServer *server) : RTargetGroupMgr(server) {
    }
    virtual void GetRibOutInterestedPeers(RibOut *ribout,
             const ExtCommunity *ext_community,
             const RibPeerSet &peerset, RibPeerSet *new_peerset) {
        *new_peerset = peerset;
    }
};


class BgpTableExportTest : public ::testing::Test {
protected:
    BgpTableExportTest()
        : server_(&evm_, "Local"),
          sender_(server_.update_sender()),
          internal_(false),
          local_as_is_different_(false),
          server_as4_supported_(false),
          table_name_(NULL),
          table_(NULL),
          ribout_(NULL),
          result_(false) {
        server_.set_autonomous_system(200);
    }

    virtual void SetUp() {
        int peer_as = 201;
        if (server_as4_supported_) {
            server_.set_enable_4byte_as(true);
            peer_as = 70201;
        }
        cout << "Table: " << table_name_
                << " Source: " << (internal_ ? "IBGP" : "EBGP")
                << " Local AS: " << (local_as_is_different_ ? peer_as : 200)
                << " 4 Byte ASN Capable: " << server_as4_supported_
                << endl;

        if (local_as_is_different_) {
            server_.set_local_autonomous_system(peer_as);
        } else {
            server_.set_local_autonomous_system(200);
        }

        CreatePeer();
        CreateAttr();
        FindTable();
    }

    virtual void TearDown() {
        rt_.RemovePath(peer_.get());
        UnregisterRibOutPeers();
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    as_t AsNumber() {
        return server_.autonomous_system();
    }

    as_t LocalAsNumber() {
        return server_.local_autonomous_system();
    }

    as_t PeerAsNumber() const {
        assert(!internal_);
        return 100;
    }

    bool PeerIsInternal() {
        return internal_;
    }

    bool TableIsVpn() {
        return table_->IsVpnTable();
    }

    void CreatePeer() {
        peer_.reset(new BgpTestPeer(0, internal_));
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
            attr_spec.push_back(&med);
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

    void CreateRibOut(RibExportPolicy &policy) {
        ribout_ = table_->RibOutLocate(sender_, policy);
        RegisterRibOutPeers();
    }

    void CreateRibOut(BgpProto::BgpPeerType type,
            RibExportPolicy::Encoding encoding, as_t as_number = 0,
            as_t local_as = 0, uint32_t cluster_id = 0,
            bool as4_supported = false) {
        RibExportPolicy policy(type, encoding, as_number, false, false,
                               as4_supported, -1, cluster_id, local_as);
        ribout_ = table_->RibOutLocate(sender_, policy);
        RegisterRibOutPeers();
    }

    void CreateRibOut(BgpProto::BgpPeerType type,
            RibExportPolicy::Encoding encoding, as_t as_number,
            bool as_override, IpAddress nexthop, uint32_t cluster_id = 0,
            bool as4_supported = false) {
        vector<string> default_tunnel_encap_list;
        RibExportPolicy policy(
            type, encoding, as_number, as_override, false, as4_supported,
            nexthop, -1, cluster_id, default_tunnel_encap_list);
        ribout_ = table_->RibOutLocate(sender_, policy);
        RegisterRibOutPeers();
    }

    void CreateRibOut(BgpProto::BgpPeerType type,
            RibExportPolicy::Encoding encoding, as_t as_number,
            as_t local_as, bool llgr) {
        RibExportPolicy policy(
            type, encoding, as_number, false, llgr, false, -1, local_as);
        ribout_ = table_->RibOutLocate(sender_, policy);
        RegisterRibOutPeers();
    }

    void RegisterRibOut(BgpTestPeer *peer = NULL) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout_->Register(peer ? peer : peer_.get());
    }

    void UnregisterRibOut(BgpTestPeer *peer = NULL) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout_->Deactivate(peer ? peer : peer_.get());
        ribout_->Unregister(peer ? peer : peer_.get());
    }

    void RegisterRibOutPeers() {
        for (int idx = 0; idx < 3; ++idx) {
            bool internal = (ribout_->peer_type() == BgpProto::IBGP);
            BgpTestPeer *peer = new BgpTestPeer(16 + idx, internal);
            ribout_peers_.push_back(peer);
            RegisterRibOut(ribout_peers_[idx]);
        }
    }

    void UnregisterRibOutPeers() {
        for (size_t idx = 0; idx < ribout_peers_.size(); ++idx) {
            UnregisterRibOut(ribout_peers_[idx]);
        }
        STLDeleteValues(&ribout_peers_);
    }

    void SetAttrAggregator(as_t as_number) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_aggregator(as_number, Ip4Address());
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrAs4Aggregator(as_t as_number) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_as4_aggregator(as_number, Ip4Address());
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrAsPath(as_t as_number) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        const AsPathSpec &path_spec = attr_ptr_->as_path()->path();
        AsPathSpec *path_spec_ptr = path_spec.Add(as_number);
        attr->set_as_path(path_spec_ptr);
        delete path_spec_ptr;
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrAsPath4(as_t as_number, bool as_set = false) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        if (attr->aspath_4byte()) {
            const AsPath4ByteSpec &path_spec = attr_ptr_->aspath_4byte()->path();
            AsPath4ByteSpec *path_spec_ptr = path_spec.Add(as_number);
            attr->set_aspath_4byte(path_spec_ptr);
            delete path_spec_ptr;
        } else if (!as_set) {
            scoped_ptr<AsPath4ByteSpec> aspath_4byte(new AsPath4ByteSpec);
            AsPath4ByteSpec::PathSegment *ps4 =
                                      new AsPath4ByteSpec::PathSegment;
            ps4->path_segment_type = As4PathSpec::PathSegment::AS_SEQUENCE;
            ps4->path_segment.push_back(as_number);
            aspath_4byte->path_segments.push_back(ps4);
            attr->set_aspath_4byte(aspath_4byte.get());
        } else {
            scoped_ptr<AsPath4ByteSpec> aspath_4byte(new AsPath4ByteSpec);
            AsPath4ByteSpec::PathSegment *ps4 =
                                      new AsPath4ByteSpec::PathSegment;
            ps4->path_segment_type = As4PathSpec::PathSegment::AS_SET;
            ps4->path_segment.push_back(as_number);
            aspath_4byte->path_segments.push_back(ps4);
            attr->set_aspath_4byte(aspath_4byte.get());
        }
        if (attr->as_path())
            attr->set_as_path(NULL);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrAs4Path(as_t as_number) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        if (attr->as4_path()) {
            const As4PathSpec &path_spec = attr->as4_path()->path();
            As4PathSpec *path_spec_ptr = path_spec.Add(as_number);
            attr->set_as4_path(path_spec_ptr);
            delete path_spec_ptr;
        } else {
            scoped_ptr<As4PathSpec> aspath(new As4PathSpec);
            As4PathSpec::PathSegment *ps4 = new As4PathSpec::PathSegment;
            ps4->path_segment_type = As4PathSpec::PathSegment::AS_SEQUENCE;
            ps4->path_segment.push_back(as_number);
            aspath->path_segments.push_back(ps4);
            attr->set_as4_path(aspath.get());
        }
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void ResetAttrAsPath() {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_as_path(NULL);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void ResetAttrAsPath4() {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_aspath_4byte(NULL);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    void SetAttrClusterList(const vector<uint32_t> cluster_list) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        ClusterListSpec clist_spec;
        clist_spec.cluster_list = cluster_list;
        attr->set_cluster_list(&clist_spec);
        EXPECT_EQ(cluster_list.size(), attr->cluster_list_length());
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

    void SetAttrOriginatorId(Ip4Address originator_id) {
        BgpAttr *attr = new BgpAttr(*attr_ptr_);
        attr->set_originator_id(originator_id);
        attr_ptr_ = server_.attr_db()->Locate(attr);
    }

    BgpPath *AddPath(bool as4_supported = false) {
        if (as4_supported)
            peer_->SetAs4Supported();
        BgpPath *path =
            new BgpPath(peer_.get(), BgpPath::BGP_XMPP, attr_ptr_, 0, 0);
        rt_.InsertPath(path);
        return path;
    }

    void AddInfeasiblePath() {
        BgpPath *path =
            new BgpPath(peer_.get(), BgpPath::BGP_XMPP, attr_ptr_,
                        BgpPath::AsPathLooped, 0);
        rt_.InsertPath(path);
    }

    void RunExport() {
        RibPeerSet peerset = ribout_->PeerSet();
        result_ = table_->Export(ribout_, &rt_, peerset, uinfo_slist_);
    }

    void VerifyExportReject() {
        EXPECT_TRUE(false == result_);
        EXPECT_EQ(0, uinfo_slist_->size());
    }

    void VerifyExportAccept() {
        EXPECT_TRUE(true == result_);
        EXPECT_EQ(1, uinfo_slist_->size());
        const UpdateInfo &uinfo = uinfo_slist_->front();
        RibPeerSet peerset = ribout_->PeerSet();
        int index = ribout_->GetPeerIndex(peer_.get());
        if (index >= 0)
            peerset.reset(index);
        EXPECT_EQ(peerset, uinfo.target);
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

    void VerifyAttrAsPathCount(uint32_t count) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(count, attr->as_path_count());
    }

    void VerifyAttrAs4BytePathCount(uint32_t count) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(count, attr->aspath_4byte_count());
    }

    void VerifyAttrAs4PathCount(uint32_t count) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(count, attr->as4_path_count());
    }

    void VerifyAttrAggregator(as_t local_as = 0) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        as_t my_asn = attr->aggregator_as_num();
        EXPECT_TRUE(my_asn == local_as);
    }

    void VerifyAttrAs4Aggregator(as_t local_as = 0) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        as_t my_asn = attr->aggregator_as4_num();
        EXPECT_TRUE(my_asn == local_as);
    }

    void VerifyAttrAsPrepend(as_t local_as = 0) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        as_t my_as = server_.autonomous_system();
        as_t my_local_as = local_as ?: server_.local_autonomous_system();
        if (my_local_as > 0xFFFF)
            my_local_as = 23456;
        EXPECT_TRUE(as_path->path().AsLeftMostMatch(my_local_as));
        if (my_as != my_local_as)
            EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
    }

    void VerifyAttrAsValue(size_t index, as_t asn) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        EXPECT_TRUE(as_path->path().path_segments[0][0].path_segment[index]
                == asn);
    }

    void VerifyAttrAs4Value(size_t index, as_t asn) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const As4Path *as_path = attr->as4_path();
        EXPECT_TRUE(as_path->path().path_segments[0][0].path_segment[index]
                == asn);
    }

    void VerifyAttrAs4ByteValue(size_t index, as_t asn) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath4Byte *as_path = attr->aspath_4byte();
        EXPECT_TRUE(as_path->path().path_segments[0][0].path_segment[index]
                == asn);
    }

    void VerifyAttrAs4Prepend(as_t local_as = 0) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const As4Path *as_path = attr->as4_path();
        as_t my_as = server_.autonomous_system();
        as_t my_local_as = local_as ?: server_.local_autonomous_system();
        EXPECT_TRUE(as_path->path().AsLeftMostMatch(my_local_as));
        if (my_as != my_local_as)
            EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
    }

    void VerifyAttrAs4BytePrepend(as_t local_as = 0) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath4Byte *as_path = attr->aspath_4byte();
        as_t my_as = server_.autonomous_system();
        as_t my_local_as = local_as ?: server_.local_autonomous_system();
        EXPECT_TRUE(as_path->path().AsLeftMostMatch(my_local_as));
        if (my_as != my_local_as)
            EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
    }

    void VerifyAttrNoAs4BytePrepend() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath4Byte *as_path = attr->aspath_4byte();
        as_t my_as = server_.autonomous_system();
        as_t my_local_as = server_.local_autonomous_system();
        EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
        EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_local_as));
    }

    void VerifyAttrNoAsPrepend() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        as_t my_as = server_.autonomous_system();
        as_t my_local_as = server_.local_autonomous_system();
        EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_as));
        EXPECT_FALSE(as_path->path().AsLeftMostMatch(my_local_as));
    }

    void VerifyAttrAsPathLoop(as_t as_number) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_TRUE(attr->IsAsPathLoop(as_number, 0));
    }

    void VerifyAttrNoAsPathLoop(as_t as_number) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_FALSE(attr->IsAsPathLoop(as_number, 0));
    }

    void VerifyAttrAsPathAsCount(as_t as_number, uint8_t count) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath *as_path = attr->as_path();
        EXPECT_FALSE(as_path->path().AsPathLoop(as_number, count));
        if (count)
            EXPECT_TRUE(as_path->path().AsPathLoop(as_number, count - 1));
    }

    void VerifyAttrAs4BytePathAsCount(as_t as_number, uint8_t count) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        const AsPath4Byte *as_path = attr->aspath_4byte();
        EXPECT_FALSE(as_path->path().AsPathLoop(as_number, count));
        if (count)
            EXPECT_TRUE(as_path->path().AsPathLoop(as_number, count - 1));
    }

    void VerifyAttrNoClusterList() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(NULL, attr->cluster_list());
        EXPECT_EQ(0, attr->cluster_list_length());
    }

    void VerifyAttrExtCommunity(bool is_null) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(is_null, attr->ext_community() == NULL);
    }

    void VerifyAttrNexthop(const string &nexthop_str) {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_EQ(nexthop_str, attr->nexthop().to_string());
    }

    void VerifyAttrNoOriginatorId() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();
        EXPECT_TRUE(attr->originator_id().is_unspecified());
    }

    EventManager evm_;
    BgpServerTest server_;
    BgpUpdateSender *sender_;

    bool internal_;
    bool local_as_is_different_;
    bool server_as4_supported_;
    const char *table_name_;

    BgpTable *table_;
    RibOut *ribout_;
    auto_ptr<BgpTestPeer> peer_;
    vector<BgpTestPeer *> ribout_peers_;
    BgpRouteMock rt_;
    BgpAttrPtr attr_ptr_;

    UpdateInfoSList uinfo_slist_;
    bool result_;
};

// Parameterize table name, peer type and local AS.

typedef std::tr1::tuple<const char *, bool, bool, bool> TestParams1;

class BgpTableExportParamTest1 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams1> {
    virtual void SetUp() {
        table_name_ = std::tr1::get<0>(GetParam());
        internal_ = std::tr1::get<1>(GetParam());
        local_as_is_different_ = std::tr1::get<2>(GetParam());
        server_as4_supported_ = std::tr1::get<3>(GetParam());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
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
    SetAttrCommunity(CommunityType::NoAdvertise);
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    SetAttrCommunity(CommunityType::NoAdvertise);
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
    SetAttrCommunity(CommunityType::NoExport);
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
    SetAttrCommunity(CommunityType::NoExportSubconfed);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: LocalPref is cleared for eBGP.
//
TEST_P(BgpTableExportParamTest1, EBgpNoLocalPref) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
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
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: (peer) local-as AS is prepended to AsPath for eBGP.
// Note:   Current AsPath is non-NULL.
//
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend3) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, 400);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrAsPrepend(400);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: (peer) local-as AS is prepended to AsPath for eBGP.
// Note:   Current AsPath is non-NULL.
//
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend4) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, 400);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrAsPrepend(400);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Our AS is prepended to AsPath for eBGP.
// Note:   Current AsPath is non-NULL.
// Inbout AS_PATH: [100], Outbound AS_PATH: [local_as, 100]
//
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs2) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
}

// Inbound AS_PATH: [23456, 100], AS4_PATH: [65000, 100]
// Outbound AS_PATH: [local_as, 23456, 100] AS4_PATH: [local_as, 65000, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs2WithAs4Path) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    if (internal_)
        SetAttrAsPath(100);
    SetAttrAsPath(23456);
    SetAttrAs4Path(100);
    SetAttrAs4Path(65000);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4PathCount(3);
        VerifyAttrAs4Prepend();
        VerifyAttrAs4Value(1, 65000);
        VerifyAttrAs4Value(2, 100);
    } else {
        VerifyAttrAs4PathCount(2);
        VerifyAttrAs4Value(0, 65000);
        VerifyAttrAs4Value(1, 100);
    }
    VerifyAttrAsPathCount(3);
}

// Inbound AS_PATH: [23456, 100], AS4_PATH: [65000, 100]
// Outbound AS_PATH: [local_as, 23456, 100] AS4_PATH: [local_as, 65000, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs2WithAs4Path2) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAs4Path(65000);
    SetAttrAs4Path(75000);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4Prepend();
        VerifyAttrAs4Value(1, 75000);
        VerifyAttrAs4Value(2, 65000);
    } else {
        VerifyAttrAs4Value(0, 75000);
        VerifyAttrAs4Value(1, 65000);
    }
}

TEST_P(BgpTableExportParamTest1, EBgpRiboutAs2PeerAs4Aggregator1) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAggregator(100);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAggregator(100);
}

TEST_P(BgpTableExportParamTest1, EBgpRiboutAs2PeerAs4Aggregator2) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAs4Aggregator(100);
    SetAttrAggregator(23456);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAggregator(100);
}

TEST_P(BgpTableExportParamTest1, EBgpRiboutAs2PeerAs4Aggregator3) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAs4Aggregator(100);
    SetAttrAggregator(200);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAggregator(200);
}

// Inbound AS_PATH4: [100]
// Outbound AS_PATH: [local_as, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs4) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    if (!internal_ && server_.enable_4byte_as()) {
        SetAttrAsPath4(100);
    }
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(internal_ ? 1 : 2);
}

// Inbound AS_PATH4: [80000, 70000, 100]
// Outbound AS_PATH: [local_as, 23456, 23456, 100]
//         AS4_PATH: [local_as, 80000, 70000, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs4WithAs4Path) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    if (server_.enable_4byte_as()) {
        SetAttrAsPath4(100);
        SetAttrAsPath4(70000);
        SetAttrAsPath4(80000);
    } else {
        SetAttrAs4Path(100);
        SetAttrAs4Path(70000);
        SetAttrAs4Path(80000);
        SetAttrAsPath(23456);
        SetAttrAsPath(23456);
    }
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4Prepend();
        VerifyAttrAs4PathCount(4);
        VerifyAttrAs4Value(1, 80000);
        VerifyAttrAs4Value(2, 70000);
    } else {
        VerifyAttrAs4PathCount(3);
        VerifyAttrAsPathCount(internal_ ? 3 : 4);
    }
}

// Inbound AS_PATH4: [80000, 70000, 100]
// Outbound AS_PATH: [local_as, 23456, 23456, 100]
//         AS4_PATH: [local_as, 80000, 70000, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs2PeerAs4WithAs4PathSet) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAsPath4(100, true);
    SetAttrAsPath4(70000, true);
    SetAttrAsPath4(80000, true);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAs4Prepend();
    VerifyAttrAs4Value(1, 80000);
    VerifyAttrAs4Value(2, 70000);
}

TEST_P(BgpTableExportParamTest1, EBgpAsPrepend2RiboutAs2PeerAs4WithAs4PathSet) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    CreateRibOut(policy);
    SetAttrAsPath4(100, true);
    SetAttrAsPath4(70000);
    SetAttrAsPath4(80000);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAs4Prepend();
    as_t local_as = LocalAsNumber();
    if (local_as > 0xFFFF)
       local_as = 23456;
    VerifyAttrAsValue(0, local_as);
    VerifyAttrAs4Value(0, LocalAsNumber());
    VerifyAttrAs4Value(1, 80000);
    VerifyAttrAs4Value(2, 70000);
}

// Add local as in the as_path or as4_path of the route, we should detect a loop
// and reject the route
TEST_P(BgpTableExportParamTest1, EBgpAsLoopAsPath) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    if (LocalAsNumber() > AS2_MAX) {
        SetAttrAsPath(AS_TRANS);
        SetAttrAs4Path(LocalAsNumber());
    } else
        SetAttrAsPath(LocalAsNumber());
    AddPath();
    RunExport();
    VerifyExportReject();
}

// Inbound AS_PATH: [600, 500, 100]
// Outbound AS_PATH4: [local_as, 600, 500, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs4PeerAs2) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    SetAttrAsPath(500);
    SetAttrAsPath(600);
    AddPath();
    RunExport();
    VerifyExportAccept();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4BytePrepend();
        VerifyAttrAs4BytePathCount(internal_ ? 3 : 4);
        VerifyAttrAs4ByteValue(1, 600);
        VerifyAttrAs4ByteValue(2, 500);
    } else {
        VerifyAttrAsPrepend();
        VerifyAttrAsPathCount(internal_ ? 3 : 4);
        VerifyAttrAsValue(1, 600);
        VerifyAttrAsValue(2, 500);
    }
}

// Inbound AS_PATH: [650, 23456, 400, 23456, 100]
//         AS4_PATH:[80000, 400, 70000, 100]
// Outbound AS_PATH4: [local_as, 650, 80000, 400, 70000, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs4PeerAs2WithAs4Path) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    SetAttrAsPath(23456);
    SetAttrAsPath(400);
    SetAttrAsPath(23456);
    SetAttrAsPath(650);
    if (!internal_)
        SetAttrAs4Path(100);
    SetAttrAs4Path(70000);
    SetAttrAs4Path(400);
    SetAttrAs4Path(80000);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4BytePrepend();
        VerifyAttrAs4BytePathCount(internal_ ? 5 : 6);
        VerifyAttrAs4ByteValue(0, LocalAsNumber());
        VerifyAttrAs4ByteValue(1, 650);
        VerifyAttrAs4ByteValue(2, 80000);
        VerifyAttrAs4ByteValue(3, 400);
        VerifyAttrAs4ByteValue(4, 70000);
        if (!internal_)
            VerifyAttrAs4ByteValue(5, 100);
    } else {
        VerifyAttrAsPrepend();
        VerifyAttrAs4PathCount(internal_ ? 3 : 4);
    }
}

// Inbound AS_PATH: [650, 100]
//         AS4_PATH:[80000, 400, 70000, 100]
// Outbound AS_PATH4: [local_as, 650, 100]
TEST_P(BgpTableExportParamTest1, EBgpAsPrepend2RiboutAs4PeerAs2WithAs4Path) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    SetAttrAsPath(650);
    if (!internal_)
        SetAttrAs4Path(100);
    SetAttrAs4Path(70000);
    SetAttrAs4Path(400);
    SetAttrAs4Path(80000);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4BytePrepend();
        VerifyAttrAs4BytePathCount(internal_ ? 2 : 3);
        VerifyAttrAs4ByteValue(0, LocalAsNumber());
        VerifyAttrAs4ByteValue(1, 650);
        if (!internal_)
            VerifyAttrAs4ByteValue(2, 100);
    } else {
        VerifyAttrAsPrepend();
        VerifyAttrAs4PathCount(internal_ ? 3 : 4);
    }
}

TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs4PeerAs4) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    if (server_.enable_4byte_as())
        SetAttrAsPath4(100);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4BytePrepend();
        VerifyAttrAs4BytePathCount(2);
    } else {
        VerifyAttrAsPrepend();
        VerifyAttrAsPathCount(internal_ ? 1 : 2);
    }
}

TEST_P(BgpTableExportParamTest1, EBgpAsPrepend1RiboutAs4PeerAs4Type2) {
    RibExportPolicy policy(BgpProto::EBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, true, -1, 0);
    CreateRibOut(policy);
    if (server_.enable_4byte_as())
        SetAttrAsPath4(70000);
    else {
        SetAttrAs4Path(100);
        SetAttrAs4Path(70000);
        if (internal_)
            SetAttrAsPath(100);
        SetAttrAsPath(23456);
    }
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    if (server_.enable_4byte_as()) {
        VerifyAttrAs4BytePrepend();
        VerifyAttrAs4BytePathCount(2);
    } else {
        VerifyAttrAsPrepend();
        VerifyAttrAsPathCount(3);
    }
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

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP with AS override.
// Intent: RibOut AS override does not affect impact learnt from IBGP source
//         or EBGP source in different AS.
// Note:   Assumes that RibOut AS is not in the AS Path.
//
//
TEST_P(BgpTableExportParamTest1, AsOverride) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, true, IpAddress());
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 1 : 2);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Remove private all (w/o replace) removes all private ASes.
//
TEST_P(BgpTableExportParamTest1, RemovePrivateAll) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 1 : 2);
    as_t local_as = LocalAsNumber();
    if (local_as > 0xFFFF)
       local_as = 23456;
    VerifyAttrAsPathAsCount(local_as, 1);
    if (!PeerIsInternal())
        VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest1, RemovePrivateAllAs4) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    if (!internal_)
        SetAttrAsPath4(100);
    SetAttrAsPath4(4294967294);
    SetAttrAsPath4(65534);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(PeerIsInternal() ? 1 : 2);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 1);
    if (!PeerIsInternal())
        VerifyAttrAs4BytePathAsCount(PeerAsNumber(), 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Remove private all (w/ replace) replaces all private ASes with
//         the local as.
//
TEST_P(BgpTableExportParamTest1, RemovePrivateAllReplace1) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 3 : 4);
    as_t local_as = LocalAsNumber();
    if (local_as > 0xFFFF)
       local_as = 23456;
    VerifyAttrAsPathAsCount(local_as, 3);
    if (!PeerIsInternal())
        VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest1, RemovePrivateAllAs4Replace1) {
    if (!server_.enable_4byte_as())
        return;
    bool as4_supported = true;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    if (!internal_)
        SetAttrAsPath4(100);
    SetAttrAsPath4(4294967294);
    SetAttrAsPath4(4200000000);
    AddPath(as4_supported);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(PeerIsInternal() ? 3 : 4);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 3);
    if (!PeerIsInternal())
        VerifyAttrAs4BytePathAsCount(PeerAsNumber(), 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Remove private all (w/ replace) replaces all private ASes. All
//         private ASes up to the first public AS are replaced with the local
//         as. Remaining private ASes are replaced with the nearest public AS.
//
TEST_P(BgpTableExportParamTest1, RemovePrivateAllReplace2) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);  // replaced by nearest public as (500)
    SetAttrAsPath(500);
    SetAttrAsPath(64512);  // replaced by local as
    SetAttrAsPath(64513);  // replaced by local as
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 5 : 6);
    as_t local_as = LocalAsNumber();
    if (local_as > 0xFFFF)
       local_as = 23456;
    VerifyAttrAsPathAsCount(local_as, 3);
    VerifyAttrAsPathAsCount(500, 2);
    if (!PeerIsInternal())
        VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest1, RemovePrivateAllAs4Replace2) {
    if (!server_.enable_4byte_as())
        return;
    bool as4_supported = true;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    if (!internal_)
        SetAttrAsPath4(100);
    SetAttrAsPath4(4294967294);  // replaced by nearest public as (500)
    SetAttrAsPath4(500);
    SetAttrAsPath4(4200000000);  // replaced by local as
    SetAttrAsPath4(4225000000);  // replaced by local as
    AddPath(as4_supported);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(PeerIsInternal() ? 5 : 6);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 3);
    VerifyAttrAs4BytePathAsCount(500, 2);
    if (!PeerIsInternal())
        VerifyAttrAs4BytePathAsCount(PeerAsNumber(), 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP, iBGP
// RibOut: eBGP
// Intent: Private ASes are not removed if there's no remove private config.
//
TEST_P(BgpTableExportParamTest1, NoRemovePrivate) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, true, IpAddress());
    SetAttrAsPath(65535);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 3 : 4);
    as_t local_as = LocalAsNumber();
    if (local_as > 0xFFFF)
       local_as = 23456;
    VerifyAttrAsPathAsCount(local_as, 1);
    VerifyAttrAsPathAsCount(64512, 1);
    VerifyAttrAsPathAsCount(65535, 1);
    if (!PeerIsInternal())
        VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest1, NoRemovePrivateAs4) {
    if (!server_.enable_4byte_as())
        return;
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, true, IpAddress(),
                 0, true);
    if (!internal_)
        SetAttrAsPath4(100);
    SetAttrAsPath4(4294967294);
    SetAttrAsPath4(4200000000);
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(PeerIsInternal() ? 3 : 4);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 1);
    VerifyAttrAs4BytePathAsCount(4200000000, 1);
    VerifyAttrAs4BytePathAsCount(4294967294, 1);
    if (!PeerIsInternal())
        VerifyAttrAs4BytePathAsCount(PeerAsNumber(), 1);
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest1,
    ::testing::Combine(
        ::testing::Values("inet.0", "bgp.l3vpn.0"),
        ::testing::Bool(),
        ::testing::Bool(),
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: iBGP
// Intent: Split Horizon check with Route Reflector and route from non client
//         should be accepted for client Ribout
//
TEST_P(BgpTableExportParamTest2, IBgpSplitHorizonRRNonClientToClient) {
    uint32_t cluster_id = 200;
    server_.set_cluster_id(cluster_id);
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber(), 0,
                 cluster_id);
    AddPath();
    RunExport();
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: iBGP
// Intent: Split Horizon check with Route Reflector and route from non client
//         should be rejected for non-client Ribout
//
TEST_P(BgpTableExportParamTest2, IBgpSplitHorizonRRNonClientToNonClient) {
    server_.set_cluster_id(200);
    uint32_t ribout_cluster_id = 100;
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber(), 0,
                 ribout_cluster_id);
    AddPath();
    RunExport();
    VerifyExportReject();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: iBGP
// Intent: Split Horizon check with Route Reflector and route from client
//         should be accepted for client Ribout
//
TEST_P(BgpTableExportParamTest2, IBgpSplitHorizonRRClientToClient) {
    uint32_t cluster_id = 100;
    server_.set_cluster_id(cluster_id);
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber(), 0,
                 cluster_id);
    AddPath();
    RunExport();
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: iBGP
// Intent: Split Horizon check with Route Reflector and route from client
//         should be accepted for non-client Ribout
//
TEST_P(BgpTableExportParamTest2, IBgpSplitHorizonRRClientToNonClient) {
    server_.set_cluster_id(100);
    uint32_t ribout_cluster_id = 200;
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber(), 0,
                 ribout_cluster_id);
    AddPath();
    RunExport();
    VerifyExportAccept();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP (effectively XMPP since AsPath is NULL)
// RibOut: eBGP
// Intent: Med is retained if AsPath is NULL.
//
TEST_P(BgpTableExportParamTest2, EBgpRetainMed1) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    ResetAttrAsPath();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(100);
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: eBGP
// Intent: Med is retained if AsPath is non-NULL but empty.
//
TEST_P(BgpTableExportParamTest2, EBgpRetainMed2) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(100);
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: eBGP
// Intent: Med is not retained since AsPath is non-empty.
//
TEST_P(BgpTableExportParamTest2, EBgpNoRetainMed) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrAsPath4(100);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(0);
    VerifyAttrMed(0);
    VerifyAttrAsPrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: eBGP
// Intent: Non transitive attributes are stripped.
//
TEST_P(BgpTableExportParamTest2, EBgpStripNonTransitive) {
    boost::system::error_code ec;
    Ip4Address originator_id = Ip4Address::from_string("10.1.1.1", ec);
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300);
    SetAttrOriginatorId(originator_id);
    SetAttrClusterList({1, 2, 3});
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoOriginatorId();
    VerifyAttrNoClusterList();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP (effectively XMPP since AsPath is NULL)
// RibOut: eBGP
// Intent: Remove private all is handled gracefully when AsPath is empty.
//
TEST_P(BgpTableExportParamTest2, RemovePrivateAll1) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(1);
    VerifyAttrAsPathAsCount(LocalAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest2, RemovePrivateAll1As4) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath4();
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(1);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: iBGP
// RibOut: eBGP
// Intent: Remove private all is handled gracefully when AsPath has no
//         segments.
// policy and peer do not understand as4
TEST_P(BgpTableExportParamTest2, RemovePrivateAll2) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath4();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(1);
    VerifyAttrAsPathAsCount(LocalAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest2, RemovePrivateAll2As4) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath();
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(1);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest2, RemovePrivateAll3) {
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, false, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath();
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(1);
    VerifyAttrAsPathAsCount(LocalAsNumber(), 1);
}

TEST_P(BgpTableExportParamTest2, RemovePrivateAll3As4) {
    if (!server_.enable_4byte_as())
        return;
    RibExportPolicy policy(
        BgpProto::EBGP, RibExportPolicy::BGP, 300, false, false, true, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);
    ResetAttrAsPath4();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(1);
    VerifyAttrAs4BytePathAsCount(LocalAsNumber(), 1);
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest2,
    ::testing::Values("inet.0", "bgp.l3vpn.0"));

// Fix peer type to external and parameterize table name and local AS.

typedef std::tr1::tuple<const char *, bool, bool> TestParams3;

class BgpTableExportParamTest3 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams3> {
    virtual void SetUp() {
        table_name_ = std::tr1::get<0>(GetParam());
        internal_ = false;
        local_as_is_different_ = std::tr1::get<1>(GetParam());
        server_as4_supported_ = std::tr1::get<2>(GetParam());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    SetAttrCommunity(CommunityType::NoExport);
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    SetAttrCommunity(CommunityType::NoExportSubconfed);
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(100);
    VerifyAttrMed(100);
    VerifyAttrNoAsPrepend();
}

TEST_P(BgpTableExportParamTest3, IBgpNoAsPrepend1As4) {
    if (!server_.enable_4byte_as())
        return;
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber(), 0, 0,
                 true);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrLocalPref(100);
    VerifyAttrMed(100);
    VerifyAttrNoAs4BytePrepend();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Our AS is not prepended when sending to iBGP.
// Note:   Current AsPath is NULL.
//
TEST_P(BgpTableExportParamTest3, IBgpNoAsPrepend2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    ResetAttrAsPath4();
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrMed(100);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: eBGP
// Intent: Med is not retained since AsPath is non-empty.
//
TEST_P(BgpTableExportParamTest3, EBgpNoRetainMed) {
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
// Source: eBGP
// RibOut: eBGP with AS override.
// Intent: RibOut AS is overridden to local AS.
//         Split horizon within RibOut.
// Note:   The source peer is registered to the RibOutbut and AS Override is
//         enabled in the RibOut, but the route shouldn't be advertised back
//         to the source.
//
TEST_P(BgpTableExportParamTest3, AsOverride) {
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 100, true, IpAddress());
    RegisterRibOut();
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 1 : 2);
    VerifyAttrNoAsPathLoop(100);
    UnregisterRibOut();
}

TEST_P(BgpTableExportParamTest3, As4Override) {
    if (!server_.enable_4byte_as())
        return;
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 100, true, IpAddress(),
                 0, true);
    RegisterRibOut();
    AddPath(true);
    RunExport();
    VerifyExportAccept();
    VerifyAttrAs4BytePrepend();
    VerifyAttrAs4BytePathCount(PeerIsInternal() ? 1 : 2);
    VerifyAttrNoAsPathLoop(100);
    UnregisterRibOut();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: eBGP with AS override and nexthop rewrite.
// Intent: Nexthop rewrite and AS override work together.
//         Split horizon within RibOut.
// Note:   The source peer is registered to the RibOutbut and AS Override is
//         enabled in the RibOut, but the route shouldn't be advertised back
//         to the source.
//
TEST_P(BgpTableExportParamTest3, AsOverrideAndRewriteNexthop) {
    boost::system::error_code ec;
    IpAddress nexthop = IpAddress::from_string("2.2.2.2", ec);
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 100, true, nexthop);
    RegisterRibOut();
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrExtCommunity(TableIsVpn() ? false : true);
    VerifyAttrNexthop("2.2.2.2");
    VerifyAttrAsPrepend();
    VerifyAttrAsPathCount(PeerIsInternal() ? 1 : 2);
    VerifyAttrNoAsPathLoop(100);
    UnregisterRibOut();
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Remove private all (w/o replace) removes all private ASes.
//
TEST_P(BgpTableExportParamTest3, RemovePrivateAll) {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    bool all = true; bool replace = false; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPathCount(1);
    VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Remove private all (w/ replace) replaces all private ASes with
//         leftmost public AS.
//
TEST_P(BgpTableExportParamTest3, RemovePrivateAllReplace1) {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPathCount(3);
    VerifyAttrAsPathAsCount(100, 3);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Remove private all (w/ replace) replaces all private ASes. All
//         private ASes up to the first public AS are replaced with the
//         leftmost public as. Remaining private ASes are replaced with the
//         nearest public AS, which will be the same as leftmost public AS.
//
TEST_P(BgpTableExportParamTest3, RemovePrivateAllReplace2) {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);  // replaced by nearest public as (500)
    SetAttrAsPath(500);
    SetAttrAsPath(64512);  // replaced by leftmost public as (500)
    SetAttrAsPath(64513);  // replaced by leftmost public as (500)
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPathCount(5);
    VerifyAttrAsPathAsCount(500, 4);
    VerifyAttrAsPathAsCount(100, 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Remove private all (w/ replace) replaces all private ASes. All
//         private ASes up to the first public AS are replaced with the
//         leftmost public as. Remaining private ASes are replaced with the
//         nearest public AS, which will not always be the same as leftmost
//         public AS.
//
TEST_P(BgpTableExportParamTest3, RemovePrivateAllReplace3) {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP,
        LocalAsNumber(), false, false, false, -1, 0);
    bool all = true; bool replace = true; bool peer_loop_check = true;
    policy.SetRemovePrivatePolicy(all, replace, peer_loop_check);
    CreateRibOut(policy);

    SetAttrAsPath(65534);  // replaced by nearest public as (500)
    SetAttrAsPath(500);
    SetAttrAsPath(64512);  // replaced by nearest public as (600)
    SetAttrAsPath(64513);  // replaced by nearest public as (600)
    SetAttrAsPath(600);
    SetAttrAsPath(64514);  // replaced by leftmost public as (600)
    SetAttrAsPath(64515);  // replaced by leftmost public as (600)
    SetAttrAsPath(64516);  // replaced by leftmost public as (600)
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPathCount(9);
    VerifyAttrAsPathAsCount(600, 6);
    VerifyAttrAsPathAsCount(500, 2);
    VerifyAttrAsPathAsCount(100, 1);
}

//
// Table : inet.0, bgp.l3vpn.0
// Source: eBGP
// RibOut: iBGP
// Intent: Private ASes are not removed if there's no remove private config.
//
TEST_P(BgpTableExportParamTest3, NoRemovePrivate) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    SetAttrAsPath(65535);
    SetAttrAsPath(64512);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrAsPathCount(3);
    VerifyAttrAsPathAsCount(64512, 1);
    VerifyAttrAsPathAsCount(65535, 1);
    VerifyAttrAsPathAsCount(PeerAsNumber(), 1);
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest3,
    ::testing::Combine(
        ::testing::Values("inet.0", "bgp.l3vpn.0"),
        ::testing::Bool(),
        ::testing::Bool()));

// Fix table name to inet.0 and parameterize peer type and local AS.

typedef std::tr1::tuple<bool, bool, bool> TestParams4a;

class BgpTableExportParamTest4a :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams4a> {
    virtual void SetUp() {
        table_name_ = "inet.0";
        internal_ = std::tr1::get<0>(GetParam());
        local_as_is_different_ = std::tr1::get<1>(GetParam());
        server_as4_supported_ = std::tr1::get<2>(GetParam());
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
    VerifyAttrNexthop("1.1.1.1");
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: iBGP
// Intent: Extended communities are stripped on export.
//
TEST_P(BgpTableExportParamTest4a, StripExtendedCommunity2) {
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
    SetAttrExtCommunity(12345);
    AddPath();
    RunExport();
    if (PeerIsInternal()) {
        VerifyExportReject();
    } else {
        VerifyExportAccept();
        VerifyAttrExtCommunity(true);
        VerifyAttrNexthop("1.1.1.1");
    }
}

//
// Table : inet.0
// Source: eBGP, iBGP
// RibOut: eBGP with nexthop rewrite.
// Intent: Nexthop is rewritten on export.
//
TEST_P(BgpTableExportParamTest4a, RewriteNexthop) {
    boost::system::error_code ec;
    IpAddress nexthop = IpAddress::from_string("2.2.2.2", ec);
    CreateRibOut(BgpProto::EBGP, RibExportPolicy::BGP, 300, false, nexthop);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNexthop("2.2.2.2");
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest4a,
    ::testing::Combine(
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool()));

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
    SetAttrCommunity(CommunityType::NoAdvertise);
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
    SetAttrCommunity(CommunityType::NoExport);
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
    SetAttrCommunity(CommunityType::NoExportSubconfed);
    AddPath();
    RunExport();
    VerifyExportAccept();
    VerifyAttrNoChange();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest4b,
            ::testing::Bool());

// Fix table name to bgp.l3vpn.0 and parameterize peer type and local AS.

typedef std::tr1::tuple<bool, bool, bool> TestParams5;

class BgpTableExportParamTest5 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams5> {
    virtual void SetUp() {
        table_name_ = "bgp.l3vpn.0";
        internal_ = std::tr1::get<0>(GetParam());
        local_as_is_different_ = std::tr1::get<1>(GetParam());
        server_as4_supported_ = std::tr1::get<2>(GetParam());
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
    CreateRibOut(BgpProto::IBGP, RibExportPolicy::BGP, LocalAsNumber());
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
    ::testing::Combine(
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool()));

//
// Long Lived Graceful Restart related tests.
//
typedef std::tr1::tuple<const char *, bool, bool, bool, bool> TestParams6;
class BgpTableExportParamTest6 :
    public BgpTableExportTest,
    public ::testing::WithParamInterface<TestParams6> {
    virtual void SetUp() {
        table_name_ = std::tr1::get<0>(GetParam());
        peer_llgr_ = std::tr1::get<1>(GetParam());
        path_llgr_ = std::tr1::get<2>(GetParam());
        comm_llgr_ = std::tr1::get<3>(GetParam());
        internal_ = std::tr1::get<4>(GetParam());
        BgpTableExportTest::SetUp();
    }

    virtual void TearDown() {
        BgpTableExportTest::TearDown();
    }

public:
    bool VerifyLlgrState() {
        const UpdateInfo &uinfo = uinfo_slist_->front();
        const BgpAttr *attr = uinfo.roattr.attr();

        // If path is not llgr_stale or if path has no llgr_stale community,
        // then do not expect any change.
        if (!path_llgr_ && !comm_llgr_) {
            EXPECT_EQ(static_cast<Community *>(NULL), attr->community());
            EXPECT_EQ(internal_ ? 100 : 0, attr->local_pref());
            return true;
        }

        // If peer supports LLGR, then expect attribute with LLGR_STALE
        // bgp community. Local preference should remain intact. (for ibgp)
        if (peer_llgr_) {
            EXPECT_TRUE(
                    attr->community()->ContainsValue(CommunityType::LlgrStale));
            EXPECT_EQ(internal_ ? 100 : 0, attr->local_pref());
            return true;
        }

        // Since peer does not support LLGR, expect NoExport community and
        // local preference as 0.
        EXPECT_TRUE(attr->community()->ContainsValue(CommunityType::NoExport));
        EXPECT_EQ(0, attr->local_pref());
        return true;
    }

    bool peer_llgr_;
    bool path_llgr_;
    bool comm_llgr_;
    bool internal_;
};

TEST_P(BgpTableExportParamTest6, Llgr) {

    // Create RibOut internal/external with peer support for LLGR (or not)
    CreateRibOut(internal_ ? BgpProto::IBGP : BgpProto::EBGP,
                 RibExportPolicy::BGP, 300, 0, peer_llgr_);

    // If the attribute needs to be tagged with LLGR_STALE, do so.
    if (comm_llgr_) {
        CommunityPtr comm = server_.comm_db()->AppendAndLocate(
                attr_ptr_->community(), CommunityType::LlgrStale);
        attr_ptr_ = server_.attr_db()->ReplaceCommunityAndLocate(
                attr_ptr_.get(), comm);
    }

    // Create path with desired community in the attribute.
    BgpPath *path = AddPath();

    // Set the llgr stale flag in the path.
    path_llgr_ ? path->SetLlgrStale() : path->ResetLlgrStale();

    // Run through the export routine and verify generated LLGR attributes.
    RunExport();
    if (VerifyLlgrState()) {
        VerifyExportAccept();
    } else {
        VerifyExportReject();
    }
}

INSTANTIATE_TEST_CASE_P(Instance, BgpTableExportParamTest6,
    ::testing::Combine(
        ::testing::Values("inet.0", "bgp.l3vpn.0"),
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool()));

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();

    BgpObjectFactory::Register<RTargetGroupMgr>(
        boost::factory<RTargetGroupMgrTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
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
