/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <pugixml/pugixml.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/inet/inet_route.h"
#include "bgp/mvpn/mvpn_route.h"
#include "bgp/security_group/security_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using std::cout;
using std::endl;
using std::string;
using std::vector;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;

static const char *config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>90000</autonomous-system>\
        <address>127.0.0.1</address>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:90000:100</vrf-target>\
    </routing-instance>\
</config>\
";

class XmppTestPeer : public IPeerUpdate {
public:
    XmppTestPeer(const string &name) : name_(name) { }
    const string &ToString() const { return name_; }
    bool SendUpdate(const uint8_t *msg, size_t msgsize, const string *msg_str) {
        string str;
        if (!msg_str) {
            str.append(reinterpret_cast<const char *>(msg), msgsize);
            msg_str = &str;
        }
        EXPECT_TRUE(msg_str->size() >= msgsize);
        return true;
    }
    bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return SendUpdate(msg, msgsize, NULL);
    }

private:
    string name_;
};

class XmppMessageBuilderTest : public ::testing::Test {
protected:
    static const int kRepeatCount = 1024;
    static const int kRouteCount = 32;
    static const int kPeerCount = 1024;

    XmppMessageBuilderTest()
        : thread_(&evm_), message_(NULL), table_(NULL), ribout_(NULL) {
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
        ribout_ = table_->RibOutLocate(bs_x_->update_sender(),
            RibExportPolicy(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0));
        message_ = ribout_->updates(0)->GetMessage();

        BgpAttrNextHop nexthop(0x0a0a0a0a);
        BgpAttrSpec spec;
        spec.push_back(&nexthop);

        SecurityGroup sg(0, 0x123);
        SecurityGroup4ByteAs sg4(90000, 0);
        ExtCommunitySpec extcomm;
        extcomm.communities.push_back(
                get_value(sg.GetExtCommunity().begin(), 8));
        extcomm.communities.push_back(
                get_value(sg4.GetExtCommunity().begin(), 8));
        spec.push_back(&extcomm);
        attr_ = bs_x_->attr_db()->Locate(spec);

        for (int idx = 0;  idx < kRouteCount; ++idx) {
            string prefix_str =
                string("192.168.1.") + integerToString(idx) + "/32";
            InetRoute *route = new InetRoute(Ip4Prefix::FromString(prefix_str));
            routes_.push_back(route);
            RibOutAttr *roattr = new RibOutAttr(table_, attr_.get(),
                100 + idx, 0, true);
            roattrs_.push_back(roattr);
        }
    }

    bool VerifySG(const uint8_t* msg, size_t msgsize, uint32_t exp_val) {
        xml_document xdoc;
        xml_parse_result result = xdoc.load_buffer(msg, msgsize);
        if (!result) {
            BGP_WARN_UT("Unable to load XML document. (status="
                << result.status << ", offset=" << result.offset << ")");
            assert(0);
        }
        xml_node parent = xdoc.first_child();
        // This is the hierarchy of sg:
        // nlri->security-group-list->security-group
        while (parent && strcmp(parent.name(), "nlri"))
            parent = parent.first_child();
        while (parent && strcmp(parent.name(), "security-group-list"))
            parent = parent.next_sibling();
        if (parent)
            parent = parent.first_child();
        string sgid = integerToString(exp_val);
        for (; parent; parent = parent.next_sibling()) {
            xml_node sg = parent.first_child();
            if (sg && !strcmp(sg.value(), sgid.c_str()))
                return true;
        }
        return false;
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
    Message *message_;
    BgpTable *table_;
    RibOut *ribout_;
    BgpAttrPtr attr_;
    vector<InetRoute *> routes_;
    vector<RibOutAttr *> roattrs_;
};

class XmppMvpnMessageBuilderTest : public ::testing::Test {
protected:
    static const int kRepeatCount = 1024;
    static const int kRouteCount = 32;
    static const int kPeerCount = 1024;

    XmppMvpnMessageBuilderTest()
        : thread_(&evm_), message_(NULL), table_(NULL), ribout_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        thread_.Start();
        bs_x_->Configure(config);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_TRUE(
            bs_x_->database()->FindTable("blue.mvpn.0") != NULL);
        table_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("blue.mvpn.0"));
        ribout_ = table_->RibOutLocate(bs_x_->update_sender(),
            RibExportPolicy(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0));
        message_ = ribout_->updates(0)->GetMessage();

        BgpAttrNextHop nexthop(0x0a0a0a0a);
        BgpOListSpec olist_spec(BgpAttribute::OList);
        BgpOListElem elem(Ip4Address(), 100, vector<string>());
        olist_spec.elements.push_back(elem);
        BgpAttrSpec spec;
        spec.push_back(&nexthop);
        spec.push_back(&olist_spec);
        attr_ = bs_x_->attr_db()->Locate(spec);

        for (int idx = 0;  idx < kRouteCount; ++idx) {
            string prefix_str =
                string("5-10.1.1.1:65535,9.8.7.6,224.1.2.") + integerToString(idx);
            MvpnRoute *route = new MvpnRoute(MvpnPrefix::FromString(prefix_str));
            routes_.push_back(route);
            RibOutAttr *roattr = new RibOutAttr(table_, attr_.get(),
                100 + idx, 0, true);
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
    Message *message_;
    BgpTable *table_;
    RibOut *ribout_;
    BgpAttrPtr attr_;
    vector<MvpnRoute *> routes_;
    vector<RibOutAttr *> roattrs_;
};

// Parameterize the following:
// 1. Long vs. short peer names.
// 2. Reuse message string for tracing by passing it to SendUpdate
// 3. Caching of RibOutAttr string representation
typedef std::tr1::tuple<bool, bool, bool> TestParams;

class XmppMessageBuilderParamTest:
    public XmppMessageBuilderTest,
    public ::testing::WithParamInterface<TestParams> {
};

TEST_P(XmppMessageBuilderParamTest, Basic) {
    bool long_name = std::tr1::get<0>(GetParam());
    bool reuse_msg_str = std::tr1::get<1>(GetParam());
    bool cache = std::tr1::get<2>(GetParam());
    cout << "Long Name = " << long_name << " ";
    cout << "Reuse Message String = " << reuse_msg_str << " ";
    cout << "Cache = " << cache << endl;

    string peer_name = "agent.juniper.net";
    if (long_name) {
        for (int idx = 0; idx < 16; ++idx) {
            peer_name += ".agent.juniper.net";
        }
    }
    for (int idx = 0; idx < kRepeatCount; ++idx) {
        message_->Start(ribout_, cache, roattrs_[0], routes_[0]);
        for (int ridx = 1; ridx < kRouteCount; ++ridx) {
            message_->AddRoute(routes_[ridx], roattrs_[ridx]);
        }
        message_->Finish();
        for (int pidx = 0; pidx < kPeerCount; ++pidx) {
            XmppTestPeer peer(peer_name);
            size_t msgsize;
            const string *msg_str = NULL;
            string temp;
            const uint8_t *msg = message_->GetData(&peer, &msgsize, &msg_str,
                                                   &temp);
            if (idx == 0 && pidx == 0)
                EXPECT_TRUE(VerifySG(msg, msgsize, 0x123));
            peer.SendUpdate(msg, msgsize, reuse_msg_str ? msg_str : NULL);
        }
    }
}

class XmppMvpnMessageBuilderParamTest:
    public XmppMvpnMessageBuilderTest,
    public ::testing::WithParamInterface<TestParams> {
};

TEST_P(XmppMvpnMessageBuilderParamTest, Basic) {
    bool long_name = std::tr1::get<0>(GetParam());
    bool reuse_msg_str = std::tr1::get<1>(GetParam());
    bool cache = std::tr1::get<2>(GetParam());
    cout << "Long Name = " << long_name << " ";
    cout << "Reuse Message String = " << reuse_msg_str << " ";
    cout << "Cache = " << cache << endl;

    string peer_name = "agent.juniper.net";
    if (long_name) {
        for (int idx = 0; idx < 16; ++idx) {
            peer_name += ".agent.juniper.net";
        }
    }
    for (int idx = 0; idx < kRepeatCount; ++idx) {
        message_->Start(ribout_, cache, roattrs_[0], routes_[0]);
        for (int ridx = 1; ridx < kRouteCount; ++ridx) {
            message_->AddRoute(routes_[ridx], roattrs_[ridx]);
        }
        message_->Finish();
        for (int pidx = 0; pidx < kPeerCount; ++pidx) {
            XmppTestPeer peer(peer_name);
            size_t msgsize;
            const string *msg_str = NULL;
            string temp;
            const uint8_t *msg = message_->GetData(&peer, &msgsize, &msg_str,
                                                   &temp);
            peer.SendUpdate(msg, msgsize, reuse_msg_str ? msg_str : NULL);
        }
    }
}

INSTANTIATE_TEST_CASE_P(ShortPeerName, XmppMvpnMessageBuilderParamTest,
    ::testing::Combine(
        ::testing::Values(false), ::testing::Bool(), ::testing::Bool()));

INSTANTIATE_TEST_CASE_P(LongPeerName, XmppMvpnMessageBuilderParamTest,
    ::testing::Combine(
        ::testing::Values(true), ::testing::Values(false), ::testing::Bool()));

INSTANTIATE_TEST_CASE_P(ShortPeerName, XmppMessageBuilderParamTest,
    ::testing::Combine(
        ::testing::Values(false), ::testing::Bool(), ::testing::Bool()));

INSTANTIATE_TEST_CASE_P(LongPeerName, XmppMessageBuilderParamTest,
    ::testing::Combine(
        ::testing::Values(true), ::testing::Values(false), ::testing::Bool()));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}

static void TearDown() {
    BgpServer::Terminate();
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
