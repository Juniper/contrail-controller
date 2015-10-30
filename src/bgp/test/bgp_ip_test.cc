/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using std::string;

class PeerMock : public IPeer {
public:
    PeerMock(const string &address_str) {
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

static const char *cfg_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet6</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet6</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

class BgpIpTest : public ::testing::Test {
protected:
    BgpIpTest()
        : thread_(&evm_),
          peer1_(new PeerMock("192.168.1.1")),
          peer2_(new PeerMock("192.168.1.2")),
          peer_xy_(NULL),
          peer_yx_(NULL),
          family_(Address::INET6),
          master_(BgpConfigManager::kMasterInstance),
          ipv6_prefix_("::ffff:") {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
    }

    ~BgpIpTest() {
        delete peer1_;
        delete peer2_;
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    virtual void SetUp() {
        thread_.Start();
        Configure();
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_TRUE(bs_x_->FindMatchingPeer(master_, "Y") != NULL);
        peer_xy_ = bs_x_->FindMatchingPeer(master_, "Y");
        TASK_UTIL_EXPECT_TRUE(bs_y_->FindMatchingPeer(master_, "X") != NULL);
        peer_yx_ = bs_y_->FindMatchingPeer(master_, "X");

    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
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
        if (family_ == Address::INET) {
            return ipv4_addr;
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;
        }
        assert(false);
        return "";
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            if (instance == master_) {
                return "inet.0";
            } else {
                return instance + ".inet.0";
            }
        }
        if (family_ == Address::INET6) {
            if (instance == master_) {
                return "inet6.0";
            } else {
                return instance + ".inet6.0";
            }
        }
        assert(false);
        return "";
    }

    BgpTable *GetTable(BgpServerTestPtr server, const string &instance) {
        return static_cast<BgpTable *>(
            server->database()->FindTable(GetTableName(instance)));
    }

    void AddRoute(BgpServerTestPtr server, IPeer *peer, const string &instance,
        const string &prefix_str, const string &nexthop_str) {

        boost::system::error_code ec;
        Inet6Prefix prefix = Inet6Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new Inet6Table::RequestKey(prefix, peer));

        BgpTable *table = GetTable(server, instance);
        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin_spec(BgpAttrOrigin::INCOMPLETE);
        attr_spec.push_back(&origin_spec);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(64513);
        path_seg->path_segment.push_back(64514);
        path_seg->path_segment.push_back(64515);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        Ip6Address nh_addr = Ip6Address::from_string(nexthop_str, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr);
        attr_spec.push_back(&nh_spec);

        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        table->Enqueue(&request);
    }

    void DeleteRoute(BgpServerTestPtr server, IPeer *peer,
        const string &instance, const string &prefix_str) {
        boost::system::error_code ec;
        Inet6Prefix prefix = Inet6Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new Inet6Table::RequestKey(prefix, peer));

        BgpTable *table = GetTable(server, instance);
        table->Enqueue(&request);
    }

    BgpRoute *RouteLookup(BgpServerTestPtr server, const string &instance_name,
        const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpTable *bgp_table = GetTable(server, instance_name);
        Inet6Table *table = dynamic_cast<Inet6Table *>(bgp_table);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        Inet6Prefix nlri = Inet6Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename Inet6Table::RequestKey key(nlri, NULL);
        BgpRoute *rt = dynamic_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    bool CheckPathExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(server, instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() != peer)
                continue;
            if (path->GetAttr()->nexthop().to_string() != nexthop)
                continue;
            return true;
        }
        return false;
    }

    bool CheckPathNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(server, instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() != peer)
                continue;
            if (path->GetAttr()->nexthop().to_string() != nexthop)
                continue;
            return false;
        }
        return true;
    }

    void VerifyPathExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        TASK_UTIL_EXPECT_TRUE(
            CheckPathExists(server, instance, prefix, peer, nexthop));
    }

    void VerifyPathNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        TASK_UTIL_EXPECT_TRUE(
            CheckPathNoExists(server, instance, prefix, peer, nexthop));
    }

    BgpRoute *VerifyRouteExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const string &nexthop) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(server, instance, prefix) != NULL);
        BgpRoute *rt = RouteLookup(server, instance, prefix);
        if (rt == NULL) {
            return NULL;
        }
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        return rt;
    }

    void VerifyRouteNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(server, instance, prefix) == NULL);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    PeerMock *peer1_;
    PeerMock *peer2_;
    BgpPeer *peer_xy_;
    BgpPeer *peer_yx_;
    Address::Family family_;
    string master_;
    string ipv6_prefix_;
};

//
// Add a single prefix on X and verify it shows up on X and Y.
//
TEST_F(BgpIpTest, SinglePrefix) {
    AddRoute(bs_x_, peer1_, master_, this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    VerifyPathExists(bs_x_, master_, this->BuildPrefix(1),
        peer1_, this->BuildNextHopAddress(peer1_->ToString()));
    VerifyPathExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));

    DeleteRoute(bs_x_, peer1_, master_, this->BuildPrefix(1));
    VerifyRouteNoExists(bs_x_, master_, this->BuildPrefix(1));
    VerifyRouteNoExists(bs_y_, master_, this->BuildPrefix(1));
}

//
// Add a single prefix with 2 paths on X and verify it on X and Y.
// X should have both paths while Y will have only 1 path.
//
TEST_F(BgpIpTest, SinglePrefixMultipath) {
    AddRoute(bs_x_, peer2_, master_, this->BuildPrefix(1),
        this->BuildHostAddress(peer2_->ToString()));
    VerifyPathExists(bs_x_, master_, this->BuildPrefix(1),
        peer2_, this->BuildNextHopAddress(peer2_->ToString()));
    VerifyPathNoExists(bs_x_, master_, this->BuildPrefix(1),
        peer1_, this->BuildNextHopAddress(peer1_->ToString()));
    VerifyPathExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer2_->ToString()));
    VerifyPathNoExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));

    AddRoute(bs_x_, peer1_, master_, this->BuildPrefix(1),
        this->BuildHostAddress(peer1_->ToString()));
    VerifyPathExists(bs_x_, master_, this->BuildPrefix(1),
        peer1_, this->BuildNextHopAddress(peer1_->ToString()));
    VerifyPathExists(bs_x_, master_, this->BuildPrefix(1),
        peer2_, this->BuildNextHopAddress(peer2_->ToString()));
    VerifyPathExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));
    VerifyPathNoExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer2_->ToString()));

    DeleteRoute(bs_x_, peer1_, master_, this->BuildPrefix(1));
    VerifyPathNoExists(bs_x_, master_, this->BuildPrefix(1),
        peer1_, this->BuildNextHopAddress(peer1_->ToString()));
    VerifyPathExists(bs_x_, master_, this->BuildPrefix(1),
        peer2_, this->BuildNextHopAddress(peer2_->ToString()));
    VerifyPathExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer2_->ToString()));
    VerifyPathNoExists(bs_y_, master_, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));

    DeleteRoute(bs_x_, peer2_, master_, this->BuildPrefix(1));

    VerifyRouteNoExists(bs_x_, master_, this->BuildPrefix(1));
    VerifyRouteNoExists(bs_y_, master_, this->BuildPrefix(1));
}

//
// Add multiple prefixes on X and verify they show up on X and Y.
//
TEST_F(BgpIpTest, MultiplePrefix) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddRoute(bs_x_, peer1_, master_, this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathExists(bs_x_, master_, this->BuildPrefix(idx),
            peer1_, this->BuildNextHopAddress(peer1_->ToString()));
        VerifyPathExists(bs_y_, master_, this->BuildPrefix(idx),
            peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeleteRoute(bs_x_, peer1_, master_, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyRouteNoExists(bs_x_, master_, this->BuildPrefix(idx));
        VerifyRouteNoExists(bs_y_, master_, this->BuildPrefix(idx));
    }
}

//
// Add multiple prefixes with 2 paths each on X and verify them on X and Y.
// X should have both paths while Y will have only 1 path for each prefix.
//
TEST_F(BgpIpTest, MultiplePrefixMultipath) {
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        AddRoute(bs_x_, peer2_, master_, this->BuildPrefix(idx),
            this->BuildHostAddress(peer2_->ToString()));
        AddRoute(bs_x_, peer1_, master_, this->BuildPrefix(idx),
            this->BuildHostAddress(peer1_->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyPathExists(bs_x_, master_, this->BuildPrefix(idx),
            peer2_, this->BuildNextHopAddress(peer2_->ToString()));
        VerifyPathExists(bs_x_, master_, this->BuildPrefix(idx),
            peer1_, this->BuildNextHopAddress(peer1_->ToString()));
        VerifyPathExists(bs_y_, master_, this->BuildPrefix(idx),
            peer_yx_, this->BuildNextHopAddress(peer1_->ToString()));
        VerifyPathNoExists(bs_y_, master_, this->BuildPrefix(idx),
            peer_yx_, this->BuildNextHopAddress(peer2_->ToString()));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        DeleteRoute(bs_x_, peer1_, master_, this->BuildPrefix(idx));
        DeleteRoute(bs_x_, peer2_, master_, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        VerifyRouteNoExists(bs_x_, master_, this->BuildPrefix(idx));
        VerifyRouteNoExists(bs_y_, master_, this->BuildPrefix(idx));
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
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
