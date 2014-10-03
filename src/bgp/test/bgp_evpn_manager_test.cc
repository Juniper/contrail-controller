/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

class PeerMock : public IPeer {
public:
    PeerMock(int index, const Ip4Address address, bool is_xmpp,
        uint32_t label, vector<string> encap = vector<string>())
        : index_(index), address_(address), is_xmpp_(is_xmpp),
          label_(label), encap_(encap) {
        sort(encap_.begin(), encap_.end());
        edge_replication_supported_ = is_xmpp_;
    }
    virtual ~PeerMock() { }

    virtual void UpdateRefCount(int count) const { }
    int index() {
        return index_;
    }
    Ip4Address address() {
        return address_;
    }
    void set_address(Ip4Address address) {
        address_ = address;
    }
    uint32_t label() {
        return label_;
    }
    void set_label(uint32_t label) {
        label_ = label;
    }
    vector<string> encap() {
        return encap_;
    }
    void set_encap(const vector<string> encap) {
        encap_ = encap;
        sort(encap_.begin(), encap_.end());
    }
    bool edge_replication_supported() {
        return edge_replication_supported_;
    }
    void set_edge_replication_supported(bool value) {
        edge_replication_supported_ = value;
    }
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
    virtual bool IsXmppPeer() const {
        return is_xmpp_;
    }
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
    int index_;
    Ip4Address address_;
    bool is_xmpp_;
    uint32_t label_;
    vector<string> encap_;
    bool edge_replication_supported_;
};

static const char *config_template = "\
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

class BgpEvpnManagerTest : public ::testing::TestWithParam<uint32_t> {
protected:
    typedef boost::shared_ptr<UpdateInfo> UpdateInfoPtr;

    static const int kVrfId = 1;
    static const int kVnIndex = 1;

    BgpEvpnManagerTest() : thread_(&evm_) {
    }

    virtual void SetUp() {
        server_.reset(new BgpServerTest(&evm_, "local"));
        thread_.Start();
        server_->Configure(config_template);
        task_util::WaitForIdle();

        DB *db = server_->database();
        TASK_UTIL_EXPECT_TRUE(db->FindTable("bgp.evpn.0") != NULL);
        master_ = static_cast<EvpnTable *>(db->FindTable("bgp.evpn.0"));

        TASK_UTIL_EXPECT_TRUE(db->FindTable("blue.evpn.0") != NULL);
        blue_ = static_cast<EvpnTable *>(db->FindTable("blue.evpn.0"));
        blue_manager_ = blue_->GetEvpnManager();
        RibExportPolicy policy(BgpProto::XMPP, RibExportPolicy::XMPP, 0, 0);
        blue_ribout_.reset(
            new RibOut(blue_, server_->scheduling_group_manager(), policy));

        CreateAllBgpPeers();
        CreateAllXmppPeers();
    }

    virtual void TearDown() {
        server_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();

        STLDeleteValues(&bgp_peers_);
        STLDeleteValues(&xmpp_peers_);
    }

    void RibOutRegister(RibOut *ribout, PeerMock *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueJoin(RibOutUpdates::QBULK, bit);
    }

    bool VerifyPeerInUpdateInfo(PeerMock *peer, UpdateInfoPtr uinfo) {
        const BgpAttr *attr = uinfo->roattr.attr();
        BgpOListPtr olist = attr->olist();
        if (olist == NULL)
            return false;
        bool found = false;
        BOOST_FOREACH(BgpOListElem &elem, olist->elements) {
            if (peer->address() == elem.address) {
                EXPECT_FALSE(found);
                found = true;
                if (peer->label() != elem.label)
                    return false;
                vector<string> encap = elem.encap;
                sort(encap.begin(), encap.end());
                if (peer->encap() != encap)
                    return false;
            }
        }
        return found;
    }

    bool VerifyPeerNotInUpdateInfo(PeerMock *peer, UpdateInfoPtr uinfo) {
        const BgpAttr *attr = uinfo->roattr.attr();
        BgpOListPtr olist = attr->olist();
        if (olist == NULL)
            return false;
        bool found = false;
        BOOST_FOREACH(BgpOListElem &elem, olist->elements) {
            if (peer->address() == elem.address)
                return false;
        }
        return true;
    }

    bool VerifyPeerUpdateInfoCommon(PeerMock *peer, bool odd, bool even,
        bool include_xmpp) {
        ConcurrencyScope scope("db::DBTable");
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());
        EvpnTable::RequestKey key(prefix, peer);
        EvpnRoute *rt = dynamic_cast<EvpnRoute *>(blue_->Find(&key));
        if (rt == NULL)
            return false;
        UpdateInfoPtr uinfo(blue_manager_->GetUpdateInfo(rt));
        if (uinfo == NULL)
            return false;

        size_t count = 0;
        BOOST_FOREACH(PeerMock *bgp_peer, bgp_peers_) {
            if (peer->address() == bgp_peer->address())
                continue;
            if ((odd && bgp_peer->index() % 2 != 0) ||
                (even && bgp_peer->index() % 2 == 0)) {
                if (!VerifyPeerInUpdateInfo(bgp_peer, uinfo))
                    return false;
                count++;
            } else {
                if (!VerifyPeerNotInUpdateInfo(bgp_peer, uinfo))
                    return false;
            }
        }

        if (include_xmpp && !peer->edge_replication_supported()) {
            BOOST_FOREACH(PeerMock *xmpp_peer, xmpp_peers_) {
                if (!VerifyPeerInUpdateInfo(xmpp_peer, uinfo))
                    return false;
                count++;
            }
        }

        const BgpAttr *attr = uinfo->roattr.attr();
        if (attr->olist()->elements.size() != count)
            return false;
        return true;
    }

    bool VerifyPeerNoUpdateInfo(PeerMock *peer) {
        ConcurrencyScope scope("db::DBTable");
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());
        EvpnTable::RequestKey key(prefix, peer);
        EvpnRoute *rt = dynamic_cast<EvpnRoute *>(blue_->Find(&key));
        if (rt == NULL)
            return true;
        UpdateInfoPtr uinfo(blue_manager_->GetUpdateInfo(rt));
        return (uinfo == NULL);
    }

    void CreateAllXmppPeers() {
        for (int idx = 1; idx <= 16; ++idx) {
            boost::system::error_code ec;
            string address_str = string("10.1.1.") + integerToString(idx);
            Ip4Address address = Ip4Address::from_string(address_str, ec);
            assert(ec.value() == 0);
            PeerMock *peer = new PeerMock(idx, address, true, 100 + idx);
            xmpp_peers_.push_back(peer);
            RibOutRegister(blue_ribout_.get(), peer);
        }
    }

    void ChangeXmppPeersLabelCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                peer->set_label(peer->label() + 1000);
            }
        }
    }

    void ChangeOddXmppPeersLabel() {
        ChangeXmppPeersLabelCommon(true, false);
    }

    void ChangeEvenXmppPeersLabel() {
        ChangeXmppPeersLabelCommon(false, true);
    }

    void ChangeAllXmppPeersLabel() {
        ChangeXmppPeersLabelCommon(true, true);
    }

    void ChangeXmppPeersEncapCommon(bool odd, bool even,
        const vector<string> encap) {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                peer->set_encap(encap);
            }
        }
    }

    void ChangeOddXmppPeersEncap(const vector<string> encap) {
        ChangeXmppPeersEncapCommon(true, false, encap);
    }

    void ChangeEvenXmppPeersEncap(const vector<string> encap) {
        ChangeXmppPeersEncapCommon(false, true, encap);
    }

    void ChangeAllXmppPeersEncap(const vector<string> encap) {
        ChangeXmppPeersEncapCommon(true, true, encap);
    }

    void AddXmppPeerBroadcastMacRoute(PeerMock *peer, string nexthop_str = "",
        uint32_t label = 0) {
        EXPECT_TRUE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());

        BgpAttrSpec attr_spec;
        ExtCommunitySpec ext_comm;
        OriginVn origin_vn(server_->autonomous_system(), kVnIndex);
        ext_comm.communities.push_back(origin_vn.GetExtCommunityValue());
        BOOST_FOREACH(string encap, peer->encap()) {
            TunnelEncap tun_encap(encap);
            ext_comm.communities.push_back(tun_encap.GetExtCommunityValue());
        }
        attr_spec.push_back(&ext_comm);

        Ip4Address nexthop_address;
        if (!nexthop_str.empty()) {
            boost::system::error_code ec;
            nexthop_address = Ip4Address::from_string(nexthop_str, ec);
            assert(ec.value() == 0);
        } else {
            nexthop_address = peer->address();
        }
        BgpAttrNextHop nexthop(nexthop_address.to_ulong());
        attr_spec.push_back(&nexthop);
        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        addReq.data.reset(
            new EvpnTable::RequestData(attr, 0, label ? label : peer->label()));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
        task_util::WaitForIdle();
    }

    void AddXmppPeersBroadcastMacRouteCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                AddXmppPeerBroadcastMacRoute(peer);
            }
        }
    }

    void AddOddXmppPeersBroadcastMacRoute() {
        AddXmppPeersBroadcastMacRouteCommon(true, false);
    }

    void AddEvenXmppPeersBroadcastMacRoute() {
        AddXmppPeersBroadcastMacRouteCommon(false, true);
    }

    void AddAllXmppPeersBroadcastMacRoute() {
        AddXmppPeersBroadcastMacRouteCommon(true, true);
    }

    void DelXmppPeerBroadcastMacRoute(PeerMock *peer) {
        EXPECT_TRUE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
        task_util::WaitForIdle();
    }

    void DelAllXmppPeersBroadcastMacRoute() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            DelXmppPeerBroadcastMacRoute(peer);
        }
    }

    void VerifyXmppPeerInclusiveMulticastRoute(PeerMock *peer) {
        EXPECT_TRUE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, peer->address());
        EvpnTable::RequestKey key(prefix, peer);
        TASK_UTIL_EXPECT_TRUE(master_->Find(&key) != NULL);
        EvpnRoute *rt = dynamic_cast<EvpnRoute *>(master_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        TASK_UTIL_EXPECT_TRUE(rt->BestPath()->IsReplicated());
        TASK_UTIL_EXPECT_TRUE(rt->BestPath()->GetAttr() != NULL);
        const BgpAttr *attr = rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_TRUE(attr->pmsi_tunnel() != NULL);
        const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
        TASK_UTIL_EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported,
            pmsi_tunnel->tunnel_flags);
        TASK_UTIL_EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
            pmsi_tunnel->tunnel_type);
        TASK_UTIL_EXPECT_EQ(peer->label(), pmsi_tunnel->label);
        TASK_UTIL_EXPECT_EQ(peer->address(), pmsi_tunnel->identifier);
        TASK_UTIL_EXPECT_EQ(peer->address(), attr->nexthop().to_v4());
        TASK_UTIL_EXPECT_EQ(peer->address(), attr->originator_id());
        TASK_UTIL_EXPECT_TRUE(attr->ext_community() != NULL);
        vector<string> encap = attr->ext_community()->GetTunnelEncap();
        sort(encap.begin(), encap.end());
        TASK_UTIL_EXPECT_TRUE(peer->encap() == encap);
    }

    void VerifyAllXmppPeersInclusiveMulticastRoute() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            VerifyXmppPeerInclusiveMulticastRoute(peer);
        }
    }

    void VerifyXmppPeerNoInclusiveMulticastRoute(PeerMock *peer) {
        EXPECT_TRUE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, peer->address());
        EvpnTable::RequestKey key(prefix, peer);
        TASK_UTIL_EXPECT_TRUE(blue_->Find(&key) == NULL);
    }

    void VerifyAllXmppPeersNoInclusiveMulticastRoute() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            VerifyXmppPeerNoInclusiveMulticastRoute(peer);
        }
    }

    void VerifyAllXmppPeersOddUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyPeerUpdateInfoCommon(peer, true, false, false));
        }
    }

    void VerifyAllXmppPeersEvenUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyPeerUpdateInfoCommon(peer, false, true, false));
        }
    }

    void VerifyAllXmppPeersAllUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyPeerUpdateInfoCommon(peer, true, true, false));
        }
    }

    void VerifyAllXmppPeersNoUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, xmpp_peers_) {
            TASK_UTIL_EXPECT_TRUE(VerifyPeerNoUpdateInfo(peer));
        }
    }

    void CreateAllBgpPeers() {
        for (int idx = 1; idx <= 4; ++idx) {
            boost::system::error_code ec;
            string address_str = string("20.1.1.") + integerToString(idx);
            Ip4Address address = Ip4Address::from_string(address_str, ec);
            assert(ec.value() == 0);
            PeerMock *peer = new PeerMock(idx, address, false, 200 + idx);
            bgp_peers_.push_back(peer);
        }
    }

    void ChangeBgpPeersLabelCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                peer->set_label(peer->label() + 2000);
            }
        }
    }

    void ChangeOddBgpPeersLabel() {
        ChangeBgpPeersLabelCommon(true, false);
    }

    void ChangeEvenBgpPeersLabel() {
        ChangeBgpPeersLabelCommon(false, true);
    }

    void ChangeAllBgpPeersLabel() {
        ChangeBgpPeersLabelCommon(true, true);
    }

    void ChangeBgpPeersEncapCommon(bool odd, bool even,
        const vector<string> encap) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                peer->set_encap(encap);
            }
        }
    }

    void ChangeOddBgpPeersEncap(const vector<string> encap) {
        ChangeBgpPeersEncapCommon(true, false, encap);
    }

    void ChangeEvenBgpPeersEncap(const vector<string> encap) {
        ChangeBgpPeersEncapCommon(false, true, encap);
    }

    void ChangeAllBgpPeersEncap(const vector<string> encap) {
        ChangeBgpPeersEncapCommon(true, true, encap);
    }

    void ChangeBgpPeersEdgeReplicationSupportedCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                bool value = peer->edge_replication_supported();
                peer->set_edge_replication_supported(!value);
            }
        }
    }

    void ChangeOddBgpPeersEdgeReplicationSupported() {
        ChangeBgpPeersEdgeReplicationSupportedCommon(true, false);
    }

    void ChangeEvenBgpPeersEdgeReplicationSupported() {
        ChangeBgpPeersEdgeReplicationSupportedCommon(false, true);
    }

    void ChangeAllBgpPeersEdgeReplicationSupported() {
        ChangeBgpPeersEdgeReplicationSupportedCommon(true, true);
    }

    void ChangeBgpPeersAddressCommon(bool odd, bool even,
        const string address_prefix) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                boost::system::error_code ec;
                string address_str =
                    address_prefix + "." + integerToString(peer->index());
                Ip4Address address = Ip4Address::from_string(address_str, ec);
                assert(ec.value() == 0);
                peer->set_address(address);
            }
        }
    }

    void ChangeOddBgpPeersAddress(const string address_prefix) {
        ChangeBgpPeersAddressCommon(true, false, address_prefix);
    }

    void ChangeEvenBgpPeersAddress(const string address_prefix) {
        ChangeBgpPeersAddressCommon(false, true, address_prefix);
    }

    void ChangeAllBgpPeersAddress(const string address_prefix) {
        ChangeBgpPeersAddressCommon(true, true, address_prefix);
    }

    void AddBgpPeerInclusiveMulticastRoute(PeerMock *peer,
        string rtarget_str = "target:64512:1") {
        EXPECT_FALSE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, peer->address());

        BgpAttrSpec attr_spec;
        ExtCommunitySpec ext_comm;
        RouteTarget rtarget = RouteTarget::FromString(rtarget_str);
        ext_comm.communities.push_back(rtarget.GetExtCommunityValue());
        BOOST_FOREACH(string encap, peer->encap()) {
            TunnelEncap tun_encap(encap);
            ext_comm.communities.push_back(tun_encap.GetExtCommunityValue());
        }
        attr_spec.push_back(&ext_comm);
        BgpAttrNextHop nexthop(peer->address().to_ulong());
        attr_spec.push_back(&nexthop);
        PmsiTunnelSpec pmsi_spec;
        if (peer->edge_replication_supported()) {
            pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
        } else {
            pmsi_spec.tunnel_flags = 0;
        }
        pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
        pmsi_spec.SetLabel(peer->label());
        pmsi_spec.SetIdentifier(peer->address());
        attr_spec.push_back(&pmsi_spec);
        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        addReq.data.reset(
            new EvpnTable::RequestData(attr, 0, peer->label()));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        master_->Enqueue(&addReq);
        task_util::WaitForIdle();
    }

    void AddBgpPeerInclusiveMulticastRouteCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                AddBgpPeerInclusiveMulticastRoute(peer);
            }
        }
    }

    void AddOddBgpPeersInclusiveMulticastRoute() {
        AddBgpPeerInclusiveMulticastRouteCommon(true, false);
    }

    void AddEvenBgpPeersInclusiveMulticastRoute() {
        AddBgpPeerInclusiveMulticastRouteCommon(false, true);
    }

    void AddAllBgpPeersInclusiveMulticastRoute() {
        AddBgpPeerInclusiveMulticastRouteCommon(true, true);
    }

    void DelBgpPeerInclusiveMulticastRoute(PeerMock *peer) {
        EXPECT_FALSE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, peer->address());

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        master_->Enqueue(&delReq);
    }

    void DelBgpPeerInclusiveMulticastRouteCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                DelBgpPeerInclusiveMulticastRoute(peer);
            }
        }
        task_util::WaitForIdle();
    }

    void DelOddBgpPeersInclusiveMulticastRoute() {
        DelBgpPeerInclusiveMulticastRouteCommon(true, false);
    }

    void DelEvenBgpPeersInclusiveMulticastRoute() {
        DelBgpPeerInclusiveMulticastRouteCommon(false, true);
    }

    void DelAllBgpPeersInclusiveMulticastRoute() {
        DelBgpPeerInclusiveMulticastRouteCommon(true, true);
    }

    void AddBgpPeerBroadcastMacRoute(PeerMock *peer) {
        EXPECT_FALSE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());

        BgpAttrSpec attr_spec;
        ExtCommunitySpec ext_comm;
        OriginVn origin_vn(server_->autonomous_system(), kVnIndex);
        ext_comm.communities.push_back(origin_vn.GetExtCommunityValue());
        BOOST_FOREACH(string encap, peer->encap()) {
            TunnelEncap tun_encap(encap);
            ext_comm.communities.push_back(tun_encap.GetExtCommunityValue());
        }
        attr_spec.push_back(&ext_comm);

        BgpAttrNextHop nexthop(peer->address().to_ulong());
        attr_spec.push_back(&nexthop);

        uint64_t params = 0;
        if (!peer->edge_replication_supported())
            params = BgpAttrParams::EdgeReplicationNotSupported;
        BgpAttrParams params_spec(params);
        attr_spec.push_back(&params_spec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        addReq.data.reset(new EvpnTable::RequestData(attr, 0, peer->label()));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
        task_util::WaitForIdle();
    }

    void AddBgpPeersBroadcastMacRouteCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                AddBgpPeerBroadcastMacRoute(peer);
            }
        }
    }

    void AddOddBgpPeersBroadcastMacRoute() {
        AddBgpPeersBroadcastMacRouteCommon(true, false);
    }

    void AddEvenBgpPeersBroadcastMacRoute() {
        AddBgpPeersBroadcastMacRouteCommon(false, true);
    }

    void AddAllBgpPeersBroadcastMacRoute() {
        AddBgpPeersBroadcastMacRouteCommon(true, true);
    }

    void DelBgpPeerBroadcastMacRoute(PeerMock *peer) {
        EXPECT_FALSE(peer->IsXmppPeer());
        RouteDistinguisher rd(peer->address().to_ulong(), kVrfId);
        EvpnPrefix prefix(rd, tag_, MacAddress::BroadcastMac(), IpAddress());

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
        task_util::WaitForIdle();
    }

    void DelBgpPeerBroadcastMacRouteCommon(bool odd, bool even) {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            if ((odd && peer->index() % 2 != 0) ||
                (even && peer->index() % 2 == 0)) {
                DelBgpPeerBroadcastMacRoute(peer);
            }
        }
        task_util::WaitForIdle();
    }

    void DelOddBgpPeersBroadcastMacRoute() {
        DelBgpPeerBroadcastMacRouteCommon(true, false);
    }

    void DelEvenBgpPeersBroadcastMacRoute() {
        DelBgpPeerBroadcastMacRouteCommon(false, true);
    }

    void DelAllBgpPeersBroadcastMacRoute() {
        DelBgpPeerBroadcastMacRouteCommon(true, true);
    }

    void VerifyAllBgpPeersBgpUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyPeerUpdateInfoCommon(peer, true, true, false));
        }
    }

    void VerifyAllBgpPeersAllUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyPeerUpdateInfoCommon(peer, true, true, true));
        }
    }

    void VerifyAllBgpPeersNoUpdateInfo() {
        BOOST_FOREACH(PeerMock *peer, bgp_peers_) {
            TASK_UTIL_EXPECT_TRUE(VerifyPeerNoUpdateInfo(peer));
        }
    }

    size_t GetPartitionLocalSize(uint32_t tag) {
        int part_id = 0;
        EvpnManagerPartition *partition = blue_manager_->partitions_[part_id];
        return partition->local_mcast_node_list_.size();
    }

    size_t GetPartitionRemoteSize(uint32_t tag) {
        int part_id = 0;
        EvpnManagerPartition *partition = blue_manager_->partitions_[part_id];
        return partition->remote_mcast_node_list_.size();
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr server_;
    EvpnTable *blue_;
    EvpnTable *master_;
    EvpnManager *blue_manager_;
    boost::scoped_ptr<RibOut> blue_ribout_;
    vector<PeerMock *> bgp_peers_;
    vector<PeerMock *> xmpp_peers_;
    uint32_t tag_;
};

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, Basic1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Inclusive Multicast route from all BGP peers.
// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, Basic2) {
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC routes from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Verify UpdateInfo for Broadcast MAC routes from all BGP peers.
TEST_P(BgpEvpnManagerTest, Basic3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    VerifyAllBgpPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();
    VerifyAllBgpPeersAllUpdateInfo();

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    VerifyAllBgpPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all BGP peers.
// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, Basic4) {
    VerifyAllBgpPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllBgpPeersBgpUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    VerifyAllBgpPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Add Inclusive Multicast route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, AddBgpPeers1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddOddBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() / 2 + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersOddUpdateInfo();

    AddEvenBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Add Broadcast MAC route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, AddBgpPeers2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddOddBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() / 2 + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersOddUpdateInfo();

    AddEvenBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Delete Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, DelBgpPeers1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelOddBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() / 2 + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersEvenUpdateInfo();

    DelEvenBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Delete Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, DelBgpPeers2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelOddBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() / 2 + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersEvenUpdateInfo();

    DelEvenBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncap1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.clear();
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Inclusive Multicast route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncap2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");
    ChangeOddBgpPeersEncap(encap);
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.clear();
    encap.push_back("udp");
    ChangeEvenBgpPeersEncap(encap);
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncap3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.clear();
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncap4) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");
    ChangeOddBgpPeersEncap(encap);
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.clear();
    encap.push_back("udp");
    ChangeEvenBgpPeersEncap(encap);
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersLabel1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeAllBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeAllBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Inclusive Multicast route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersLabel2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeEvenBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeOddBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC routes from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersLabel3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeAllBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeAllBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersLabel4) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeEvenBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeOddBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncapAndLabel1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    ChangeAllBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.push_back("gre");
    ChangeAllBgpPeersEncap(encap);
    ChangeAllBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Inclusive Multicast route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncapAndLabel2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");
    ChangeOddBgpPeersEncap(encap);
    ChangeOddBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.push_back("gre");
    ChangeEvenBgpPeersEncap(encap);
    ChangeEvenBgpPeersLabel();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncapAndLabel3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");
    ChangeAllBgpPeersEncap(encap);
    ChangeAllBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.push_back("gre");
    ChangeAllBgpPeersEncap(encap);
    ChangeAllBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEncapAndLabel4) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");
    ChangeOddBgpPeersEncap(encap);
    ChangeOddBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    encap.push_back("gre");
    ChangeEvenBgpPeersEncap(encap);
    ChangeEvenBgpPeersLabel();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size() + bgp_peers_.size(),
        GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Inclusive Multicast route from all BGP peers.
// Verify no UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEdgeReplicationSupported1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeAllBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeAllBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Inclusive Multicast route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Inclusive Multicast route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEdgeReplicationSupported2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeOddBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersEvenUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeOddBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeEvenBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersOddUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Broadcast MAC route from all BGP peers.
// Verify no UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEdgeReplicationSupported3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeAllBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeAllBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Broadcast MAC route from odd BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Toggle edge rep support in Broadcast MAC route from even BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersEdgeReplicationSupported4) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    ChangeOddBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersEvenUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeOddBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    ChangeEvenBgpPeersEdgeReplicationSupported();
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersOddUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Inclusive Multicast route from all BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Inclusive Multicast route from all BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersAddress1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersInclusiveMulticastRoute();
    ChangeAllBgpPeersAddress("30.1.1");
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    ChangeAllBgpPeersAddress("40.1.1");
    AddAllBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Inclusive Multicast route from odd BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Inclusive Multicast route from even BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersAddress2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelOddBgpPeersInclusiveMulticastRoute();
    ChangeOddBgpPeersAddress("30.1.1");
    AddOddBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelEvenBgpPeersInclusiveMulticastRoute();
    ChangeEvenBgpPeersAddress("40.1.1");
    AddEvenBgpPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersInclusiveMulticastRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Broadcast MAC route from all BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Broadcast MAC route from all BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersAddress3) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelAllBgpPeersBroadcastMacRoute();
    ChangeAllBgpPeersAddress("30.1.1");
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    ChangeAllBgpPeersAddress("40.1.1");
    AddAllBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Broadcast MAC route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Broadcast MAC route from odd BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change address in Broadcast MAC route from even BGP peers - del + add.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
TEST_P(BgpEvpnManagerTest, ChangeBgpPeersAddress4) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();
    AddAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersAllUpdateInfo();

    DelOddBgpPeersBroadcastMacRoute();
    ChangeOddBgpPeersAddress("30.1.1");
    AddOddBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelEvenBgpPeersBroadcastMacRoute();
    ChangeEvenBgpPeersAddress("40.1.1");
    AddEvenBgpPeersBroadcastMacRoute();
    VerifyAllXmppPeersAllUpdateInfo();
    TASK_UTIL_EXPECT_EQ(bgp_peers_.size() + xmpp_peers_.size(),
        GetPartitionRemoteSize(tag_));

    DelAllBgpPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoUpdateInfo();
    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    TASK_UTIL_EXPECT_EQ(0, GetPartitionRemoteSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Change encap in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersEncap1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");
    ChangeAllXmppPeersEncap(encap);
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    encap.clear();
    encap.push_back("udp");
    ChangeAllXmppPeersEncap(encap);
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap in Broadcast MAC route from odd XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Change encap in Broadcast MAC route from even XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersEncap2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    vector<string> encap;
    encap.push_back("gre");
    encap.push_back("udp");

    ChangeOddXmppPeersEncap(encap);
    AddOddXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    ChangeEvenXmppPeersEncap(encap);
    AddEvenXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Change label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersLabel1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    ChangeAllXmppPeersLabel();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    ChangeAllXmppPeersLabel();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change label in Broadcast MAC route from even XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.

// Change label in Broadcast MAC route from odd XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersLabel2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    ChangeEvenXmppPeersLabel();
    AddEvenXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    ChangeOddXmppPeersLabel();
    AddOddXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Change encap and label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersEncapAndLabel1) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");
    ChangeAllXmppPeersEncap(encap);
    ChangeAllXmppPeersLabel();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    encap.push_back("gre");
    ChangeAllXmppPeersEncap(encap);
    ChangeAllXmppPeersLabel();
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

// Add Broadcast MAC routes from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Add Inclusive Multicast route from all BGP peers.
// Verify UpdateInfo for Broadcast MAC routes from all XMPP peers.
// Change encap and label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
// Change encap and label in Broadcast MAC route from all XMPP peers.
// Verify generated Inclusive Multicast routes in bgp.evpn.0.
TEST_P(BgpEvpnManagerTest, ChangeXmppPeersEncapAndLabel2) {
    AddAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();
    VerifyAllXmppPeersNoUpdateInfo();

    vector<string> encap;
    encap.push_back("udp");

    ChangeOddXmppPeersEncap(encap);
    ChangeOddXmppPeersLabel();
    AddOddXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    ChangeEvenXmppPeersEncap(encap);
    ChangeEvenXmppPeersLabel();
    AddEvenXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(xmpp_peers_.size(), GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersInclusiveMulticastRoute();

    DelAllXmppPeersBroadcastMacRoute();
    TASK_UTIL_EXPECT_EQ(0, GetPartitionLocalSize(tag_));
    VerifyAllXmppPeersNoInclusiveMulticastRoute();
}

INSTANTIATE_TEST_CASE_P(Default, BgpEvpnManagerTest, ::testing::Values(0, 4094));

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
