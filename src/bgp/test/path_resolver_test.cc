/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/path_resolver.h"

#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using std::string;

class BgpPeerMock : public IPeer {
public:
    explicit BgpPeerMock(const string &address_str) {
        boost::system::error_code ec;
        address_ = Ip4Address::from_string(address_str, ec);
        assert(ec.value() == 0);
    }
    virtual ~BgpPeerMock() { }
    virtual std::string ToString() const { return address_.to_string(); }
    virtual std::string ToUVEKey() const { return address_.to_string(); }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() {
    }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::EBGP; }
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
          peer1_(new BgpPeerMock("192.168.1.1")),
          peer2_(new BgpPeerMock("192.168.1.2")) {
        bgp_server_->session_manager()->Initialize(0);
    }
    ~PathResolverTest() {
        delete peer1_;
        delete peer2_;
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
        const string &prefix_str, const string &nexthop_str) {
        boost::system::error_code ec;
        Ip4Prefix prefix = Ip4Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(prefix, peer));

        BgpAttrSpec attr_spec;
        Ip4Address nexthop = Ip4Address::from_string(nexthop_str, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nexthop.to_ulong());
        attr_spec.push_back(&nh_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        uint32_t flags = resolve ? BgpPath::ResolveNexthop : 0;
        request.data.reset(new BgpTable::RequestData(attr, flags, 0));
        BgpTable *table = GetTable(instance);
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

    void DisableResolverRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableRegUnregProcessing();
    }

    void EnableResolverRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableRegUnregProcessing();
    }

    EventManager evm_;
    BgpServerTestPtr bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    BgpPeerMock *peer1_;
    BgpPeerMock *peer2_;
};

TEST_F(PathResolverTest, SinglePrefix) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
}

TEST_F(PathResolverTest, MultiplePrefix) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
    }
}

TEST_F(PathResolverTest, MultiplePeersSinglePrefix) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
}

TEST_F(PathResolverTest, MultiplePeersMultiplePrefix) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddPath(peer1_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
        AddPath(peer2_, true, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(peer2_->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeletePath(peer1_, "blue", this->BuildPrefix(idx));
        DeletePath(peer2_, "blue", this->BuildPrefix(idx));
    }
}

TEST_F(PathResolverTest, StopResolutionBeforeRegister) {
    DisableResolverRegUnregProcessing("blue");
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    EnableResolverRegUnregProcessing("blue");
}

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

TEST_F(PathResolverTest, Shutdown2) {
    AddPath(peer1_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    AddPath(peer2_, true, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));

    bgp_server_->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());

    DisableResolverRegUnregProcessing("blue");
    DeletePath(peer1_, "blue", this->BuildPrefix(1));
    DeletePath(peer2_, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server_->routing_instance_mgr()->count());

    EnableResolverRegUnregProcessing("blue");
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
