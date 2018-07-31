/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_update.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/extended-community/esi_label.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using namespace std;
using boost::assign::list_of;

class PeerMock : public IPeer {
public:
    PeerMock(const Ip4Address address, uint32_t label)
        : address_(address), label_(label) {
        address_str_ = address.to_string();
    }
    virtual ~PeerMock() { }

    virtual void UpdateTotalPathCount(int count) const { }
    const Ip4Address &address() const {
        return address_;
    }
    void set_address(Ip4Address address) {
        address_ = address;
        address_str_ = address.to_string();
    }
    uint32_t label() {
        return label_;
    }
    void set_label(uint32_t label) {
        label_ = label;
    }
    virtual const std::string &ToString() const {
        return address_str_;
    }
    virtual const std::string &ToUVEKey() const {
        return address_str_;
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() { return NULL; }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
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
    virtual bool IsXmppPeer() const {
        return false;
    }
    virtual bool IsRegistrationRequired() const {
        return false;
    }
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
    virtual void UpdateTotalPathCount(int count) { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return false; }
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
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    Ip4Address address_;
    uint32_t label_;
    std::string address_str_;
};

static const char *config = "\
<config>\
    <bgp-router name=\'local\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:64512:1</vrf-target>\
    </routing-instance>\
</config>\
";

class BgpEvpnAliasingTest : public ::testing::Test {
protected:

    BgpEvpnAliasingTest()
        : thread_(&evm_),
          blue_(NULL),
          master_(NULL),
          blue_manager_(NULL),
          blue_ribout_(NULL) {
    }

    virtual ~BgpEvpnAliasingTest() { }

    virtual void SetUp() {
        server_.reset(new BgpServerTest(&evm_, "local"));
        thread_.Start();
        server_->Configure(config);
        task_util::WaitForIdle();

        DB *db = server_->database();
        TASK_UTIL_EXPECT_TRUE(db->FindTable("bgp.evpn.0") != NULL);
        master_ = static_cast<EvpnTable *>(db->FindTable("bgp.evpn.0"));

        TASK_UTIL_EXPECT_TRUE(db->FindTable("blue.evpn.0") != NULL);
        blue_ = static_cast<EvpnTable *>(db->FindTable("blue.evpn.0"));
        blue_manager_ = blue_->GetEvpnManager();
        RibExportPolicy policy(BgpProto::XMPP, RibExportPolicy::XMPP, 0, 0);
        blue_ribout_.reset(
            blue_->RibOutLocate(server_->update_sender(), policy));

        CreateAllPeers();

        boost::system::error_code ec;
        esi1_ = EthernetSegmentId::FromString("11:22:33:44:55:66:77:88:99:01", &ec);
        assert(ec.value() == 0);
        esi2_ = EthernetSegmentId::FromString("11:22:33:44:55:66:77:88:99:02", &ec);
        assert(ec.value() == 0);
        esi3_ = EthernetSegmentId::FromString("11:22:33:44:55:66:77:88:99:03", &ec);
        assert(ec.value() == 0);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
        DeleteAllPeers();
    }

    void RibOutRegister(RibOut *ribout, PeerMock *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
    }

    void RibOutUnregister(RibOut *ribout, PeerMock *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Deactivate(peer);
        ribout->Unregister(peer);
    }

    void AddMacRoute(PeerMock *peer, string prefix_str, const EthernetSegmentId &esi,
        string encap = "vxlan") {
        boost::system::error_code ec;
        MacAddress mac_addr = MacAddress::FromString(prefix_str, &ec);
        assert(ec.value() == 0);
        EvpnPrefix prefix(RouteDistinguisher::kZeroRd, 0, mac_addr, IpAddress());

        BgpAttrSpec attr_spec;
        ExtCommunitySpec ext_comm;
        TunnelEncap tun_encap(encap);
        ext_comm.communities.push_back(tun_encap.GetExtCommunityValue());
        attr_spec.push_back(&ext_comm);
        BgpAttrNextHop nexthop(peer->address().to_ulong());
        attr_spec.push_back(&nexthop);
        BgpAttrEsi esi_spec(esi);
        if (!esi.IsZero()) {
            attr_spec.push_back(&esi_spec);
        }
        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        addReq.data.reset(
            new EvpnTable::RequestData(attr, 0, peer->label()));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
    }

    void DelMacRoute(PeerMock *peer, string prefix_str) {
        boost::system::error_code ec;
        MacAddress mac_addr = MacAddress::FromString(prefix_str, &ec);
        assert(ec.value() == 0);
        EvpnPrefix prefix(RouteDistinguisher::kZeroRd, 0, mac_addr, IpAddress());

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
    }

    void AddAutoDiscoveryRoute(PeerMock *peer,
        const EthernetSegmentId &esi, bool single_active = false,
        string encap = "vxlan") {

        EvpnPrefix prefix(RouteDistinguisher::kZeroRd, esi, EvpnPrefix::kMaxTag);

        BgpAttrSpec attr_spec;
        ExtCommunitySpec ext_comm;
        TunnelEncap tun_encap(encap);
        ext_comm.communities.push_back(tun_encap.GetExtCommunityValue());
        EsiLabel esi_label(single_active);
        ext_comm.communities.push_back(esi_label.GetExtCommunityValue());
        attr_spec.push_back(&ext_comm);
        BgpAttrNextHop nexthop(peer->address().to_ulong());
        attr_spec.push_back(&nexthop);
        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        addReq.data.reset(new EvpnTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
    }

    void DelAutoDiscoveryRoute(PeerMock *peer,
        const EthernetSegmentId &esi) {
        EvpnPrefix prefix(RouteDistinguisher::kZeroRd, esi, EvpnPrefix::kMaxTag);

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
    }

    void CreateAllPeers() {
        bgp_peers_.push_back(NULL);
        for (int idx = 1; idx <= 3; ++idx) {
            boost::system::error_code ec;
            string address_str = string("20.1.1.") + integerToString(idx);
            Ip4Address address = Ip4Address::from_string(address_str, ec);
            assert(ec.value() == 0);
            PeerMock *peer = new PeerMock(address, 1000);
            bgp_peers_.push_back(peer);
        }
    }

    void DeleteAllPeers() {
        STLDeleteValues(&bgp_peers_);
    }

    EvpnRoute *RouteLookup(const string &prefix_str) {
        boost::system::error_code ec;
        MacAddress mac_addr = MacAddress::FromString(prefix_str, &ec);
        assert(ec.value() == 0);
        EvpnPrefix prefix(RouteDistinguisher::kZeroRd, 0, mac_addr, IpAddress());
        EvpnTable::RequestKey key(prefix, NULL);
        DBEntry *db_entry = blue_->Find(&key);
        if (db_entry == NULL)
            return NULL;
        return dynamic_cast<EvpnRoute *>(db_entry);
    }

    bool CheckRouteExists(const string &prefix_str) {
        task_util::TaskSchedulerLock lock;
        EvpnRoute *rt = RouteLookup(prefix_str);
        return (rt && rt->BestPath() != NULL);
    }

    void VerifyRouteExists(const string &prefix_str) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteExists(prefix_str));
    }

    bool CheckRouteNoExists(const string &prefix_str) {
        task_util::TaskSchedulerLock lock;
        EvpnRoute *rt = RouteLookup(prefix_str);
        return !rt;
    }

    void VerifyRouteNoExists(const string &prefix_str) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteNoExists(prefix_str));
    }

    void VerifyRouteIsDeleted(const string &prefix_str) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(prefix_str) != NULL);
        EvpnRoute *rt = RouteLookup(prefix_str);
        TASK_UTIL_EXPECT_TRUE(rt->IsDeleted());
    }

    bool CheckRoutePathExists(const string &prefix_str, const IPeer *peer,
        bool aliased) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(prefix_str);
        if (!route)
            return false;
        for (Route::PathList::const_iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() != peer)
                continue;
            if (path->GetAttr()->nexthop().to_string() != peer->ToString())
                continue;
            if (path->IsAliased() == aliased)
                return true;
            return false;
        }

        return false;
    }

    void VerifyRoutePathExists(const string &prefix_str, const PeerMock *peer,
        bool aliased = false) {
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(
            CheckRoutePathExists(prefix_str, peer, aliased));
    }

    bool CheckRoutePathNoExists(const string &prefix_str, const IPeer *peer) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(prefix_str);
        if (!route)
            return true;
        for (Route::PathList::const_iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() == peer)
                return false;
            if (path->GetAttr()->nexthop().to_string() == peer->ToString())
                return false;
        }

        return true;
    }

    void VerifyRoutePathNoExists(const string &prefix_str,
        const PeerMock *peer) {
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(CheckRoutePathNoExists(prefix_str, peer));
    }

    bool CheckSegmentExists(const EthernetSegmentId &esi) {
        task_util::TaskSchedulerLock lock;
        EvpnSegment *segment = blue_manager_->FindSegment(esi);
        return (segment != NULL);
    }

    void VerifySegmentExists(const EthernetSegmentId &esi) {
        TASK_UTIL_EXPECT_TRUE(CheckSegmentExists(esi));
    }

    bool CheckSegmentNoExists(const EthernetSegmentId &esi) {
        task_util::TaskSchedulerLock lock;
        EvpnSegment *segment = blue_manager_->FindSegment(esi);
        return (segment == NULL);
    }

    void VerifySegmentNoExists(const EthernetSegmentId &esi) {
        TASK_UTIL_EXPECT_TRUE(CheckSegmentNoExists(esi));
    }

    bool CheckSegmentPeExists(const EthernetSegmentId &esi,
        const PeerMock *peer, bool single_active) {
        task_util::TaskSchedulerLock lock;
        EvpnSegment *segment = blue_manager_->FindSegment(esi);
        if (segment == NULL)
            return false;
        for (EvpnSegment::const_iterator it = segment->begin();
             it != segment->end(); ++it) {
            if (it->attr->nexthop() == peer->address() &&
                it->single_active == single_active) {
                return true;
            }
        }
        return false;
    }

    void VerifySegmentPeExists(const EthernetSegmentId &esi,
        const PeerMock *peer, bool single_active = false) {
        TASK_UTIL_EXPECT_TRUE(CheckSegmentPeExists(esi, peer, single_active));
    }

    bool CheckSegmentPeNoExists(const EthernetSegmentId &esi,
        const PeerMock *peer) {
        task_util::TaskSchedulerLock lock;
        EvpnSegment *segment = blue_manager_->FindSegment(esi);
        if (segment == NULL)
            return true;
        for (EvpnSegment::const_iterator it = segment->begin();
             it != segment->end(); ++it) {
            if (it->attr->nexthop() == peer->address())
                return false;
        }
        return true;
    }

    void VerifySegmentPeNoExists(const EthernetSegmentId &esi,
        const PeerMock *peer) {
        TASK_UTIL_EXPECT_TRUE(CheckSegmentPeNoExists(esi, peer));
    }

    void DisableSegmentUpdateProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::DisableSegmentUpdateProcessing,
                blue_manager_), "bgp::Config");
    }

    void EnableSegmentUpdateProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::EnableSegmentUpdateProcessing,
                blue_manager_), "bgp::Config");
    }

    void DisableSegmentDeleteProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::DisableSegmentDeleteProcessing,
                blue_manager_), "bgp::Config");
    }

    void EnableSegmentDeleteProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::EnableSegmentDeleteProcessing,
                blue_manager_), "bgp::Config");
    }

    void DisableMacUpdateProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::DisableMacUpdateProcessing,
                blue_manager_), "bgp::Config");
    }

    void EnableMacUpdateProcessing() {
        task_util::TaskFire(
            boost::bind(&EvpnManager::EnableMacUpdateProcessing,
                blue_manager_), "bgp::Config");
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr server_;
    EvpnTable *blue_;
    EvpnTable *master_;
    EvpnManager *blue_manager_;
    boost::scoped_ptr<RibOut> blue_ribout_;
    vector<PeerMock *> bgp_peers_;
    EthernetSegmentId esi_null_, esi1_, esi2_, esi3_;
};

//
// Verify add and delete of MAC route with null ESI.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac1) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi_null_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
}

//
// Verify add and delete of MAC route with non-null ESI.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac2) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentExists(esi1_);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
}

//
// Verify change from non-NULL to NULL ESI for a MAC route.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac3) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentExists(esi1_);
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi_null_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
}

//
// Verify change from NULL to non-NULL ESI for a MAC route.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac4) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi_null_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentExists(esi1_);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
}

//
// Verify change from non-NULL to non-NULL ESI for a MAC route.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac5) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentExists(esi1_);
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi2_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifySegmentExists(esi2_);
    VerifySegmentNoExists(esi1_);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi2_);
}

//
// Verify change from NULL to non-NULL ESI for a MAC route that has
// EvpnMacState pointing to NULL EvpnSegment.
// Force this scenario by disabling MAC update processing.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac6) {
    DisableMacUpdateProcessing();
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi_null_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi2_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteIsDeleted("aa:bb:cc:dd:ee:01");
    EnableMacUpdateProcessing();

    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
    VerifySegmentNoExists(esi2_);
}

//
// Verify deletion of MAC route when it has EvpnMacState that points to
// a NULL EvpnSegment.
// Force this scenario by disabling MAC update processing.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac7) {
    DisableMacUpdateProcessing();
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi_null_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteIsDeleted("aa:bb:cc:dd:ee:01");
    EnableMacUpdateProcessing();

    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    VerifySegmentNoExists(esi1_);
}

//
// Verify add and delete of multiple MAC routes with non-NULl ESI.
// Exercises EvpnSegment concurrency logic during MAC route delete.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac8) {
    string mac_prefix = "aa:bb:cc:dd:ee:0";
    for (int idx = 1; idx <= 8; ++idx) {
        AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi1_);
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentExists(esi1_);
    for (int idx = 1; idx <= 8; ++idx) {
        DelMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx));
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteNoExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentNoExists(esi1_);
}

//
// Verify change of multiple MAC routes from non-NULL to NULL ESI.
// Exercises EvpnSegment concurrency logic during MAC route change.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac9) {
    string mac_prefix = "aa:bb:cc:dd:ee:0";
    for (int idx = 1; idx <= 8; ++idx) {
        AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi1_);
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentExists(esi1_);
    for (int idx = 1; idx <= 8; ++idx) {
        AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi_null_);
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentNoExists(esi1_);
    for (int idx = 1; idx <= 8; ++idx) {
        DelMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx));
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteNoExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentNoExists(esi1_);
}

//
// Verify add and delete of multiple MAC routes with different non-NULl ESIs.
// Exercises EvpnSegment concurrency logic during MAC route and and delete.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteMac10) {
    string mac_prefix = "aa:bb:cc:dd:ee:0";
    for (int idx = 1; idx <= 8; ++idx) {
        if (idx % 2 == 0) {
            AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi1_);
        } else {
            AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi2_);
        }
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentExists(esi1_);
    VerifySegmentExists(esi2_);
    for (int idx = 1; idx <= 8; ++idx) {
        DelMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx));
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteNoExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentNoExists(esi1_);
    VerifySegmentNoExists(esi2_);
}

//
// Verify add and delete of AD route.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery1) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1]);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify a remote PE entry for AD path is not created with non-vxlan encap.
// The remote PE entry should get created when the encap is changed to vxlan.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery2a) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false, "gre");
    VerifySegmentExists(esi1_);
    VerifySegmentPeNoExists(esi1_, bgp_peers_[1]);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false, "vxlan");
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1]);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify a remote PE entry for AD path is not created with non-vxlan encap.
// The remote PE entry gets deleted when the encap is changed to non-vxlan.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery2b) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false, "vxlan");
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1]);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false, "gre");
    VerifySegmentExists(esi1_);
    VerifySegmentPeNoExists(esi1_, bgp_peers_[1]);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify the single active property in each remote PE entry for a AD route.
// Change AD route from single active to all active.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery3a) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], false);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, true);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], true);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify the single active property in remote PE entry for a AD route.
// Change AD route from all active to single active.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery3b) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, true);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], true);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], false);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify that the PE list for a AD route is built correctly when there are
// multiple paths in the AD route. The PE list should be updated properly as
// paths get added and deleted.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery4) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1]);
    VerifySegmentPeExists(esi1_, bgp_peers_[2]);
    VerifySegmentExists(esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentExists(esi1_);
    VerifySegmentPeNoExists(esi1_, bgp_peers_[1]);
    VerifySegmentPeExists(esi1_, bgp_peers_[2]);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify single active property of remote PEs for AD route with multiple
// paths.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery5) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_, true);
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], false);
    VerifySegmentPeExists(esi1_, bgp_peers_[2], true);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify that an EvpnSegment is not deleted prematurely while it's on the
// update list.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery6a) {
    DisableSegmentUpdateProcessing();
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifySegmentExists(esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    EnableSegmentUpdateProcessing();
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], false);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify that an EvpnSegment is not deleted prematurely while it's on the
// update list.
//
TEST_F(BgpEvpnAliasingTest, AddDeleteAutoDiscovery6b) {
    DisableSegmentUpdateProcessing();
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifySegmentExists(esi1_);
    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    EnableSegmentUpdateProcessing();
    VerifySegmentExists(esi1_);
    VerifySegmentPeExists(esi1_, bgp_peers_[1], false);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifySegmentNoExists(esi1_);
}

//
// Verify aliased path is added when MAC route is added before all the
// AD routes.
//
TEST_F(BgpEvpnAliasingTest, Aliasing1) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is added when MAC route is added after all the
// AD routes.
//
TEST_F(BgpEvpnAliasingTest, Aliasing2) {
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is added when AD route from alias peer is added.
//
TEST_F(BgpEvpnAliasingTest, Aliasing3) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is deleted when AD route from alias peer is deleted.
//
TEST_F(BgpEvpnAliasingTest, Aliasing4) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is not added if the AD path from alias peer is
// single-active.
//
TEST_F(BgpEvpnAliasingTest, Aliasing5) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, false);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_, true);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is deleted when the AD path from alias peer gets
// modified to be single-active.
//
TEST_F(BgpEvpnAliasingTest, Aliasing6) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_, true);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is not added if the AD path from primary peer is
// single-active.
//
TEST_F(BgpEvpnAliasingTest, Aliasing7) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, true);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_, false);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is deleted when the AD path from primary peer gets
// modified to be single-active.
//
TEST_F(BgpEvpnAliasingTest, Aliasing8) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_, true);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased paths are added/deleted when AD paths get added/deleted
// at the same time.
//
TEST_F(BgpEvpnAliasingTest, Aliasing9) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DisableMacUpdateProcessing();
    AddAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    EnableMacUpdateProcessing();

    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[3], true);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is not added if there's no AD route from primary
// peer. The aliased path gets added when the AD route from primary peer
// gets added.
//
TEST_F(BgpEvpnAliasingTest, Aliasing10) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased path is deleted when AD route from primary peer is
// deleted.
//
TEST_F(BgpEvpnAliasingTest, Aliasing11) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);

    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathNoExists("aa:bb:cc:dd:ee:01", bgp_peers_[2]);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
}

//
// Verify aliased path is not added if the alias peer has advertised
// a MAC route. An aliased path is added when the alias peer withdraws
// the MAC route.
//
TEST_F(BgpEvpnAliasingTest, Aliasing12) {
    AddMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01", esi1_);
    AddMacRoute(bgp_peers_[2], "aa:bb:cc:dd:ee:01", esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[3], true);

    DelMacRoute(bgp_peers_[2], "aa:bb:cc:dd:ee:01");
    VerifyRouteExists("aa:bb:cc:dd:ee:01");
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[1], false);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[2], true);
    VerifyRoutePathExists("aa:bb:cc:dd:ee:01", bgp_peers_[3], true);

    DelMacRoute(bgp_peers_[1], "aa:bb:cc:dd:ee:01");
    VerifyRouteNoExists("aa:bb:cc:dd:ee:01");
    DelAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
}

//
// Verify aliased paths are added and deleted properly for multiple MAC
// routes when AD routes from multiple remote PEs are added and deleted.
//
TEST_F(BgpEvpnAliasingTest, Aliasing13) {
    string mac_prefix = "aa:bb:cc:dd:ee:0";
    for (int idx = 1; idx <= 8; ++idx) {
        AddMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx), esi1_);
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentExists(esi1_);

    AddAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    AddAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
        VerifyRoutePathExists(
            mac_prefix + integerToString(idx), bgp_peers_[1], false);
        VerifyRoutePathExists(
            mac_prefix + integerToString(idx), bgp_peers_[2], true);
        VerifyRoutePathExists(
            mac_prefix + integerToString(idx), bgp_peers_[3], true);
    }

    DelAutoDiscoveryRoute(bgp_peers_[3], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[2], esi1_);
    DelAutoDiscoveryRoute(bgp_peers_[1], esi1_);
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteExists(mac_prefix + integerToString(idx));
        VerifyRoutePathExists(
            mac_prefix + integerToString(idx), bgp_peers_[1], false);
        VerifyRoutePathNoExists(
            mac_prefix + integerToString(idx), bgp_peers_[2]);
        VerifyRoutePathNoExists(
            mac_prefix + integerToString(idx), bgp_peers_[3]);
    }

    for (int idx = 1; idx <= 8; ++idx) {
        DelMacRoute(bgp_peers_[1], mac_prefix + integerToString(idx));
    }
    for (int idx = 1; idx <= 8; ++idx) {
        VerifyRouteNoExists(mac_prefix + integerToString(idx));
    }
    VerifySegmentNoExists(esi1_);
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}

static void TearDown() {
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
