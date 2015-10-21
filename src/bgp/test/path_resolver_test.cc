/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/path_resolver.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using boost::assign::list_of;
using std::set;
using std::string;
using std::vector;

class PeerMock : public IPeer {
public:
    PeerMock(bool is_xmpp, const string &address_str)
        : is_xmpp_(is_xmpp) {
        boost::system::error_code ec;
        address_ = Ip4Address::from_string(address_str, ec);
        assert(ec.value() == 0);
    }
    virtual ~PeerMock() { }
    virtual std::string ToString() const { return address_.to_string(); }
    virtual std::string ToUVEKey() const { return address_.to_string(); }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return is_xmpp_; }
    virtual void Close() {
    }
    BgpProto::BgpPeerType PeerType() const {
        return is_xmpp_ ? BgpProto::XMPP : BgpProto::EBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const { return ""; }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }

private:
    bool is_xmpp_;
    Ip4Address address_;
};

static const char *config = "\
<config>\
    <bgp-router name=\'localhost\'>\
        <identifier>192.168.0.100</identifier>\
        <address>192.168.0.100</address>\
        <autonomous-system>64512</autonomous-system>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:64512:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <vrf-target>target:64512:2</vrf-target>\
    </routing-instance>\
</config>\
";

class PathResolverTest : public ::testing::Test {
protected:
    PathResolverTest()
        : bgp_server_(new BgpServerTest(&evm_, "localhost")),
          family_(Address::INET),
          ipv6_prefix_("::ffff:"),
          peer1_(new PeerMock(false, "192.168.1.1")),
          peer2_(new PeerMock(false, "192.168.1.2")),
          xmpp_peer1_(new PeerMock(true, "172.16.1.1")),
          xmpp_peer2_(new PeerMock(true, "172.16.1.2")) {
        bgp_server_->session_manager()->Initialize(0);
    }
    ~PathResolverTest() {
        delete peer1_;
        delete peer2_;
        delete xmpp_peer1_;
        delete xmpp_peer2_;
    }

    virtual void SetUp() {
        bgp_server_->Configure(config);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
    }

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr;
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;
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

    string BuildPrefix(int index) const {
        assert(index <= 65535);
        string ipv4_prefix("10.1.");
        uint8_t ipv4_plen = Address::kMaxV4PrefixLen;
        string byte3 = integerToString(index / 256);
        string byte4 = integerToString(index % 256);
        if (family_ == Address::INET) {
            return ipv4_prefix + byte3 + "." + byte4 + "/" +
                integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + byte3 + "." + byte4 + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    string BuildNextHopAddress(const string &ipv4_addr) const {
        return ipv4_addr;
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

    BgpTable *GetTable(const string &instance) {
        return static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetTableName(instance)));
    }

    void AddPath(IPeer *peer, bool resolve, const string &instance,
        const string &prefix_str, const string &nexthop_str1,
        int label = 0, const string &nexthop_str2 = string(),
        vector<uint32_t> sgid_list = vector<uint32_t>(),
        set<string> encap_list = set<string>()) {
        assert(!nexthop_str1.empty());
        assert(peer->IsXmppPeer() || nexthop_str2.empty());
        assert(peer->IsXmppPeer() != resolve);
        assert(peer->IsXmppPeer() || sgid_list.empty());
        assert(peer->IsXmppPeer() || encap_list.empty());

        boost::system::error_code ec;
        Ip4Prefix prefix = Ip4Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(prefix, peer));

        BgpTable *table = GetTable(instance);
        int index = table->routing_instance()->index();
        BgpAttrSpec attr_spec;

        Ip4Address nh_addr1 = Ip4Address::from_string(nexthop_str1, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr1.to_ulong());
        attr_spec.push_back(&nh_spec);

        BgpAttrSourceRd source_rd_spec(
            RouteDistinguisher(nh_addr1.to_ulong(), index));
        if (peer->IsXmppPeer())
            attr_spec.push_back(&source_rd_spec);

        ExtCommunitySpec extcomm_spec;
        for (vector<uint32_t>::iterator it = sgid_list.begin();
             it != sgid_list.end(); ++it) {
            SecurityGroup sgid(64512, *it);
            uint64_t value = sgid.GetExtCommunityValue();
            extcomm_spec.communities.push_back(value);
        }
        for (set<string>::iterator it = encap_list.begin();
             it != encap_list.end(); ++it) {
            TunnelEncap encap(*it);
            uint64_t value = encap.GetExtCommunityValue();
            extcomm_spec.communities.push_back(value);
        }
        if (!extcomm_spec.communities.empty())
            attr_spec.push_back(&extcomm_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        InetTable::RequestData::NextHops nexthops;
        InetTable::RequestData::NextHop nexthop1;
        nexthop1.flags_ = resolve ? BgpPath::ResolveNexthop : 0;
        nexthop1.address_ = nh_addr1;
        nexthop1.label_ = label;
        if (peer->IsXmppPeer()) {
            nexthop1.source_rd_ =
                RouteDistinguisher(nh_addr1.to_ulong(), index);
        }
        nexthops.push_back(nexthop1);

        InetTable::RequestData::NextHop nexthop2;
        if (!nexthop_str2.empty()) {
            Ip4Address nh_addr2 = Ip4Address::from_string(nexthop_str2, ec);
            EXPECT_FALSE(ec);
            nexthop2.flags_ = 0;
            nexthop2.address_ = nh_addr2;
            nexthop2.label_ = label;
            if (peer->IsXmppPeer()) {
                nexthop2.source_rd_ =
                    RouteDistinguisher(nh_addr2.to_ulong(), index);
            }
            nexthops.push_back(nexthop2);
        }

        request.data.reset(new BgpTable::RequestData(attr, nexthops));
        table->Enqueue(&request);
    }

    void AddPathWithSecurityGroups(IPeer *peer, bool resolve,
        const string &instance, const string &prefix_str,
        const string &nexthop_str, int label, vector<uint32_t> sgid_list) {
        AddPath(peer, resolve, instance, prefix_str, nexthop_str, label,
            string(), sgid_list, set<string>());
    }

    void AddPathWithTunnelEncapsulations(IPeer *peer, bool resolve,
        const string &instance, const string &prefix_str,
        const string &nexthop_str, int label, set<string> encap_list) {
        AddPath(peer, resolve, instance, prefix_str, nexthop_str, label,
            string(), vector<uint32_t>(), encap_list);
    }

    void DeletePath(IPeer *peer, const string &instance,
        const string &prefix_str) {
        boost::system::error_code ec;
        Ip4Prefix prefix = Ip4Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(prefix, peer));

        BgpTable *table = GetTable(instance);
        table->Enqueue(&request);
    }

    void DisableResolverNexthopRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverNexthopRegUnregProcessing();
    }

    void EnableResolverNexthopRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverNexthopRegUnregProcessing();
    }

    size_t ResolverNexthopRegUnregListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopRegUnregListSize();
    }

    void DisableResolverNexthopUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverNexthopUpdateProcessing();
    }

    void EnableResolverNexthopUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverNexthopUpdateProcessing();
    }

    size_t ResolverNexthopUpdateListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopUpdateListSize();
    }

    void DisableResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverPathUpdateProcessing();
    }

    void EnableResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverPathUpdateProcessing();
    }

    size_t ResolverPathUpdateListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverPathUpdateListSize();
    }

    BgpRoute *RouteLookup(const string &instance_name, const string &prefix) {
        BgpTable *bgp_table = GetTable(instance_name);
        InetTable *table = dynamic_cast<InetTable *>(bgp_table);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = dynamic_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    BgpRoute *VerifyRouteExists(const string &instance, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(instance, prefix) != NULL);
        BgpRoute *rt = RouteLookup(instance, prefix);
        if (rt == NULL) {
            return NULL;
        }
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        return rt;
    }

    void VerifyRouteNoExists(const string &instance, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(instance, prefix) == NULL);
    }

    vector<uint32_t> GetSGIDListFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        vector<uint32_t> list;
        if (!ext_comm)
            return list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_security_group(comm))
                continue;
            SecurityGroup security_group(comm);
            list.push_back(security_group.security_group_id());
        }
        sort(list.begin(), list.end());
        return list;
    }

    set<string> GetTunnelEncapListFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        set<string> list;
        if (!ext_comm)
            return list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_tunnel_encap(comm))
                continue;
            TunnelEncap encap(comm);
            list.insert(encap.ToXmppString());
        }
        return list;
    }

    bool MatchPathAttributes(const BgpPath *path, const string &path_id,
        uint32_t label, const vector<uint32_t> sgid_list,
        const set<string> encap_list) {
        const BgpAttr *attr = path->GetAttr();
        if (attr->nexthop().to_v4().to_string() != path_id)
            return false;
        if (label && path->GetLabel() != label)
            return false;
        if (sgid_list.size()) {
            vector<uint32_t> path_sgid_list = GetSGIDListFromPath(path);
            if (path_sgid_list.size() != sgid_list.size())
                return false;
            for (vector<uint32_t>::const_iterator
                it1 = path_sgid_list.begin(), it2 = sgid_list.begin();
                it1 != path_sgid_list.end() && it2 != sgid_list.end();
                ++it1, ++it2) {
                if (*it1 != *it2)
                    return false;
            }
        }
        if (encap_list.size()) {
            set<string> path_encap_list = GetTunnelEncapListFromPath(path);
            if (path_encap_list != encap_list)
                return false;
        }

        return true;
    }

    bool CheckPathAttributes(const string &instance, const string &prefix,
        const string &path_id, int label, const vector<uint32_t> sgid_list,
        const set<string> encap_list) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetSource() != BgpPath::ResolvedRoute)
                continue;
            if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                continue;
            if (MatchPathAttributes(path, path_id, label, sgid_list,
                encap_list)) {
                return true;
            }
            return false;
        }

        return false;
    }

    bool CheckPathNoExists(const string &instance, const string &prefix,
        const string &path_id) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return true;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetSource() != BgpPath::ResolvedRoute)
                continue;
            if (BgpPath::PathIdString(path->GetPathId()) == path_id)
                return false;
        }

        return true;
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label, const set<string> encap_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), encap_list));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label,
        const vector<uint32_t> sgid_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, sgid_list, set<string>()));
    }

    void VerifyPathNoExists(const string &instance, const string &prefix,
        const string &path_id) {
        TASK_UTIL_EXPECT_TRUE(CheckPathNoExists(instance, prefix, path_id));
    }

    EventManager evm_;
    BgpServerTestPtr bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    PeerMock *peer1_;
    PeerMock *peer2_;
    PeerMock *xmpp_peer1_;
    PeerMock *xmpp_peer2_;
};

//
// Add BGP path before XMPP path.
//
TEST_F(PathResolverTest, SinglePrefix1) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Add BGP path after XMPP path.
//
TEST_F(PathResolverTest, SinglePrefix2) {
    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
}

//
// Add BGP path after XMPP path.
// Delete and add BGP path multiple times when path update list processing is
// disabled.
//
TEST_F(PathResolverTest, SinglePrefixAddDelete) {
    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(1, ResolverPathUpdateListSize("blue"));

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    TASK_UTIL_EXPECT_EQ(3, ResolverPathUpdateListSize("blue"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, ResolverPathUpdateListSize("blue"));

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
}

//
// Change XMPP path label after BGP path has been resolved.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath1) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);

    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path nexthop after BGP path has been resolved.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath2) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path multiple times when nh update list processing is disabled.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath3) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10002);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10003);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10003);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Change and delete XMPP path when nh update list processing is disabled.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath4) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Delete and resurrect XMPP path when nh update list processing is disabled.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath5) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path sgid list.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath6) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint32_t> sgid_list1 = list_of(1)(2)(3)(4);
    AddPathWithSecurityGroups(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000, sgid_list1);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list1);

    vector<uint32_t> sgid_list2 = list_of(3)(4)(5)(6);
    AddPathWithSecurityGroups(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000, sgid_list2);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list2);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path encap list.
//
TEST_F(PathResolverTest, SinglePrefixChangeXmppPath7) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    set<string> encap_list1 = list_of("gre")("udp");
    AddPathWithTunnelEncapsulations(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000, encap_list1);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list1);

    set<string> encap_list2 = list_of("udp");
    AddPathWithTunnelEncapsulations(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000, encap_list2);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list2);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// XMPP path has multiple ECMP nexthops.
// Change XMPP route from ECMP to non-ECMP and back.
//
TEST_F(PathResolverTest, SinglePrefixWithEcmp) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000,
        this->BuildHostAddress("172.16.2.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10000);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000,
        this->BuildHostAddress("172.16.2.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000,
        this->BuildHostAddress("172.16.2.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple paths for same prefix, each with a different nexthop.
// Add and remove paths for the prefix.
//
TEST_F(PathResolverTest, SinglePrefixWithMultipath) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));

    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple paths for same prefix, each with a different nexthop.
// XMPP route for the nexthop is ECMP.
//
TEST_F(PathResolverTest, SinglePrefixWithMultipathAndEcmp) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001,
        this->BuildHostAddress("172.16.2.1"));
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002,
        this->BuildHostAddress("172.16.2.2"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.2.2"), 10002);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001,
        this->BuildHostAddress("172.16.2.1"));
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002,
        this->BuildHostAddress("172.16.2.2"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10003,
        this->BuildHostAddress("172.16.2.1"));
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10004,
        this->BuildHostAddress("172.16.2.2"));
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10003);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10003);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10004);
    VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10004);

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));
    VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple prefixes, each with the same nexthop.
//
TEST_F(PathResolverTest, MultiplePrefix) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes, each with the same nexthop.
// Change XMPP path multiple times when path update list processing is disabled.
//
TEST_F(PathResolverTest, MultiplePrefixChangeXmppPath1) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10002);
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10002);
    }

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes, each with the same nexthop.
// Change XMPP path and delete it path update list processing is disabled.
//
TEST_F(PathResolverTest, MultiplePrefixChangeXmppPath2) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has 2 paths for all prefixes.
//
TEST_F(PathResolverTest, MultiplePrefixWithMultipath) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
        AddPath(peer2_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer2_->ToString()));
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10001);
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.2"), 10002);
    }

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.2"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
        DeletePath(peer2_, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes in blue and pink tables, each with same nexthop.
//
TEST_F(PathResolverTest, MultipleTableMultiplePrefix) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
        AddPath(peer1_, true, "pink", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    AddPath(xmpp_peer1_, false, "pink",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
        VerifyPathAttributes("pink", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer1_, "pink", this->BuildPrefix(peer1_->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
        VerifyPathNoExists("pink", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
        DeletePath(peer1_, "pink", this->BuildPrefix(idx));
    }
}

//
// Delete BGP path before it's nexthop is registered to condition listener.
//
TEST_F(PathResolverTest, StopResolutionBeforeRegister) {
    DisableResolverNexthopRegUnregProcessing("blue");
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    EnableResolverNexthopRegUnregProcessing("blue");
}

//
// Shutdown server and resolver before all BGP paths are deleted.
// Deletion does not finish till BGP paths are deleted.
//
TEST_F(PathResolverTest, Shutdown1) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    bgp_server_->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(0, bgp_server_->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before all BGP paths are deleted.
// Deletion does not finish till BGP paths are deleted and nexthop gets
// unregistered from condition listener.
//
TEST_F(PathResolverTest, Shutdown2) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    bgp_server_->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());

    DisableResolverNexthopRegUnregProcessing("blue");
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());

    EnableResolverNexthopRegUnregProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, bgp_server_->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before all resolver paths are deleted.
// Deletion does not finish till resolver paths are deleted and nexthop
// gets unregistered from condition listener.
//
TEST_F(PathResolverTest, Shutdown3) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    DisableResolverPathUpdateProcessing("blue");

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    bgp_server_->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, bgp_server_->routing_instance_mgr()->count());
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
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
