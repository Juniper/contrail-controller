/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/path_resolver.h"

#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using std::string;

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
        int label = 0, const string &nexthop_str2 = string()) {
        assert(!nexthop_str1.empty());
        assert(peer->IsXmppPeer() || nexthop_str2.empty());
        assert(peer->IsXmppPeer() != resolve);

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
        task_util::WaitForIdle();
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
        task_util::WaitForIdle();
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
    task_util::WaitForIdle();
    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverPathUpdateListSize("blue"));

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, ResolverPathUpdateListSize("blue"));

    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, ResolverPathUpdateListSize("blue"));

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, ResolverPathUpdateListSize("blue"));

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));

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
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10000);
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10002);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10003);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));

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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));
    DisableResolverNexthopUpdateProcessing("blue");

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, ResolverNexthopUpdateListSize("blue"));

    EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverNexthopUpdateListSize("blue"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10000);
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000,
        this->BuildHostAddress("172.16.2.1"));
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000);
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10000,
        this->BuildHostAddress("172.16.2.1"));
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
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
    task_util::WaitForIdle();

    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();

    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    task_util::WaitForIdle();

    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));
    task_util::WaitForIdle();

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
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.2.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.2.2"), 10002);
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001,
        this->BuildHostAddress("172.16.2.1"));
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002,
        this->BuildHostAddress("172.16.2.2"));
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002);
    task_util::WaitForIdle();

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001,
        this->BuildHostAddress("172.16.2.1"));
    AddPath(xmpp_peer2_, false, "blue",
        this->BuildPrefix(peer2_->ToString(), 32),
        this->BuildHostAddress("172.16.1.2"), 10002,
        this->BuildHostAddress("172.16.2.2"));
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));
    task_util::WaitForIdle();

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
    task_util::WaitForIdle();

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();

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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10002);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();

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
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));
    DisableResolverPathUpdateProcessing("blue");

    AddPath(xmpp_peer1_, false, "blue",
        this->BuildPrefix(peer1_->ToString(), 32),
        this->BuildHostAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        ResolverPathUpdateListSize("blue"));

    EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, ResolverPathUpdateListSize("blue"));

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

    DeletePath(xmpp_peer1_, "blue", this->BuildPrefix(peer1_->ToString(), 32));
    DeletePath(xmpp_peer2_, "blue", this->BuildPrefix(peer2_->ToString(), 32));
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
        DeletePath(peer2_, "blue", this->BuildPrefix(idx));
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
