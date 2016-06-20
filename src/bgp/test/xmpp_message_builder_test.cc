/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"
#include "bgp/bgp_ribout.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/inet/inet_route.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using std::string;
using std::vector;

static const char *config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:64512:100</vrf-target>\
    </routing-instance>\
</config>\
";

class XmppTestPeer : public IPeerUpdate {
public:
    XmppTestPeer(const string &name) : name_(name) { }
    string ToString() const { return name_; }
    bool SendUpdate(const uint8_t *msg, size_t msgsize)  { return true; }

private:
    string name_;
};

class XmppMessageBuilderTest : public ::testing::Test {
protected:
    static const int kRepeatCount = 1024;
    static const int kRouteCount = 32;
    static const int kPeerCount = 1024;

    XmppMessageBuilderTest()
        : thread_(&evm_), builder_(NULL), table_(NULL), ribout_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        thread_.Start();
        bs_x_->Configure(config);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_TRUE(
            bs_x_->database()->FindTable("blue.inet.0") != NULL);
        table_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("blue.inet.0"));
        ribout_ = table_->RibOutLocate(bs_x_->scheduling_group_manager(),
            RibExportPolicy(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0));
        builder_ = MessageBuilder::GetInstance(RibExportPolicy::XMPP);

        BgpAttrNextHop nexthop(0x0a0a0a0a);
        BgpAttrSpec spec;
        spec.push_back(&nexthop);
        attr_ = bs_x_->attr_db()->Locate(spec);

        for (int idx = 0;  idx < kRouteCount; ++idx) {
            string prefix_str =
                string("192.168.1.") + integerToString(idx) + "/32";
            InetRoute *route = new InetRoute(Ip4Prefix::FromString(prefix_str));
            routes_.push_back(route);
            RibOutAttr *roattr = new RibOutAttr(table_, attr_.get(), 100 + idx);
            roattrs_.push_back(roattr);
        }
    }

    virtual void TearDown() {
        STLDeleteValues(&roattrs_);
        STLDeleteValues(&routes_);
        table_->RibOutDelete(
            RibExportPolicy(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0));
        bs_x_->Shutdown();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    MessageBuilder *builder_;
    BgpTable *table_;
    RibOut *ribout_;
    BgpAttrPtr attr_;
    vector<InetRoute *> routes_;
    vector<RibOutAttr *> roattrs_;
};

// Parameterize caching of RibOutAttr string representation and short vs. long
// peer names.
typedef std::tr1::tuple<bool, bool> TestParams;

class XmppMessageBuilderParamTest:
    public XmppMessageBuilderTest,
    public ::testing::WithParamInterface<TestParams> {
};

TEST_P(XmppMessageBuilderParamTest, Basic) {
    bool cache_routes = std::tr1::get<0>(GetParam());
    string peer_name = "agent.juniper.net";
    if (!std::tr1::get<1>(GetParam())) {
        for (int idx = 0; idx < 16; ++idx) {
            peer_name += ".agent.juniper.net";
        }
    }
    for (int idx = 0; idx < kRepeatCount; ++idx) {
        boost::scoped_ptr<Message> message(
            builder_->Create(ribout_, cache_routes, roattrs_[0], routes_[0]));
        for (int ridx = 1; ridx < kRouteCount; ++ridx) {
            message->AddRoute(routes_[ridx], roattrs_[ridx]);
        }
        message->Finish();
        for (int pidx = 0; pidx < kPeerCount; ++pidx) {
            XmppTestPeer peer(peer_name);
            size_t msgsize;
            const uint8_t *msg = message->GetData(&peer, &msgsize);
            peer.SendUpdate(msg, msgsize);
        }
    }
}

INSTANTIATE_TEST_CASE_P(Test, XmppMessageBuilderParamTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

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
