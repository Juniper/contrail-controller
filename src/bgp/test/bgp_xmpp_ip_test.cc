/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include <sstream>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "xmpp/xmpp_factory.h"

using std::string;
using std::auto_ptr;
using boost::assign::list_of;

static const char *config_2_control_nodes = "\
<config>\
    <bgp-router name=\'X\'>\
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

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix> Inet6Definition;

//
// Fixture class template - instantiated later for each TypeDefinition.
// Control Nodes X and Y.
// Agents A and B.
//
template <typename T>
class BgpXmppIpTest : public ::testing::Test {
protected:
    static const int kRouteCount = 512;
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;

    BgpXmppIpTest()
        : thread_(&evm_), xs_x_(NULL), xs_y_(NULL), family_(GetFamily()),
          ipv6_prefix_("::"), master_(BgpConfigManager::kMasterInstance) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_x_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_x_->GetPort());
        cm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_y_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_y_->GetPort());
        cm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        thread_.Start();

        Configure();
        task_util::WaitForIdle();

        // Create XMPP Agent A connected to XMPP server X.
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                "127.0.0.1", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

        // Create XMPP Agent B connected to XMPP server Y.
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
                "127.0.0.2", "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        cm_x_.reset();

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        cm_y_.reset();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(const char *cfg_template = config_2_control_nodes) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
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

    string BuildNextHop(int index) const {
        assert(index <= 255);
        string ipv4_prefix("192.168.1.");
        if (family_ == Address::INET) {
            return ipv4_prefix + integerToString(index);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + integerToString(index);
        }
        assert(false);
        return "";
    }

    void Subscribe(test::NetworkAgentMockPtr agent, int id) {
        if (family_ == Address::INET) {
            agent->Subscribe(master_, id);
        } else {
            agent->Inet6Subscribe(master_, id);
        }
    }

    void Unsubscribe(test::NetworkAgentMockPtr agent) {
        if (family_ == Address::INET) {
            agent->Unsubscribe(master_);
        } else {
            agent->Inet6Unsubscribe(master_);
        }
    }

    void SessionDown(test::NetworkAgentMockPtr agent) {
        agent->SessionDown();
    }

    void AddRoute(test::NetworkAgentMockPtr agent, const string &prefix,
        const test::NextHop &nexthop, const test::RouteAttributes &attr) {
        if (family_ == Address::INET) {
            agent->AddRoute(master_, prefix, nexthop, attr);
        } else {
            agent->AddInet6Route(master_, prefix, nexthop, attr);
        }
    }

    void DeleteRoute(test::NetworkAgentMockPtr agent, const string &prefix) {
        if (family_ == Address::INET) {
            agent->DeleteRoute(master_, prefix);
        } else {
            agent->DeleteInet6Route(master_, prefix);
        }
    }

    const autogen::ItemType *FindRoute(test::NetworkAgentMockPtr agent,
        const string &prefix) const {
        if (family_ == Address::INET) {
            return agent->RouteLookup(master_, prefix);
        } else {
            return agent->Inet6RouteLookup(master_, prefix);
        }
    }

    bool CheckRouteActual(test::NetworkAgentMockPtr agent, string prefix,
        string nexthop, int local_pref, int med,
        const vector<string> communities, bool *result) const {
        *result = false;
        const autogen::ItemType *rt = FindRoute(agent, prefix);
        if (!rt)
            return false;
        if (local_pref && rt->entry.local_preference != local_pref)
            return false;
        if (med && rt->entry.med != med)
            return false;
        if (rt->entry.next_hops.next_hop.size() != 1)
            return false;
        const autogen::NextHopType &nh = rt->entry.next_hops.next_hop[0];
        if (nh.address != nexthop)
            return false;
        if (!nh.tunnel_encapsulation_list.tunnel_encapsulation.empty())
            return false;
        if (!communities.empty() &&
            rt->entry.community_tag_list.community_tag != communities)
            return false;
        *result = true;
        return true;
    }

    bool CheckRoute(test::NetworkAgentMockPtr agent, string prefix,
                    string nexthop, int local_pref, int med,
                    const vector<string> communities) const {
        bool result;
        task_util::TaskFire(boost::bind(&BgpXmppIpTest::CheckRouteActual, this,
            agent, prefix, nexthop, local_pref, med, communities, &result),
                "bgp::Config");
        return result;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string prefix,
        string nexthop, int local_pref = 0, int med = 0) const {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, prefix, nexthop, local_pref, med, vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string prefix,
        string nexthop, const vector<string> &communities) const {
        TASK_UTIL_EXPECT_TRUE(
            CheckRoute(agent, prefix, nexthop, 0, 0, communities));
    }

    void VerifyRouteNoExists(test::NetworkAgentMockPtr agent,
                             string prefix) const {
        TASK_UTIL_EXPECT_TRUE(FindRoute(agent, prefix) == NULL);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    Address::Family family_;
    string ipv6_prefix_;
    string master_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_y_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family BgpXmppIpTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family BgpXmppIpTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types<InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(BgpXmppIpTest, TypeDefinitionList);

//
// Basic route exchange.
//
TYPED_TEST(BgpXmppIpTest, Basic) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, this->BuildNextHop(1));
    test::RouteAttributes attr;
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 100);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 100);

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Change local preference.
//
TYPED_TEST(BgpXmppIpTest, RouteChangeLocalPref) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, this->BuildNextHop(1));
    test::RouteAttributes attr(200, 0, 0);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 200);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 200);

    // Change route from agent A.
    attr = test::RouteAttributes(300, 0, 0);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route changed on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 300);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 300);

    // Add route from agent A without local pref.
    attr = test::RouteAttributes();
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route changed on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 100);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 100);

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes are deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Route added with explicit med has expected med.
//
TYPED_TEST(BgpXmppIpTest, RouteExplicitMed) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A with local preference 100 and med 500.
    test::NextHop nexthop(true, this->BuildNextHop(1));
    test::RouteAttributes attr(100, 500, 0);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 500);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 500);

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Route added with local preference and no med has auto calculated med.
//
TYPED_TEST(BgpXmppIpTest, RouteLocalPrefToMed) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A with local preference 100.
    test::NextHop nexthop(true, this->BuildNextHop(1));
    test::RouteAttributes attr(100, 0, 0);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 200);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 200);

    // Change route from agent A to have local preference 200.
    attr = test::RouteAttributes(200, 0, 0);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 100);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), 0, 100);

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Route added with community list.
//
TYPED_TEST(BgpXmppIpTest, RouteWithCommunity) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, this->BuildNextHop(1));
    vector<string> comm = {"64512:101", "64512:102"};
    test::RouteAttributes attr(comm);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), comm);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), comm);

    // Change route from agent A.
    comm = list_of("64512:201")("64512:202")
        .convert_to_container<vector<string> >();
    attr = test::RouteAttributes(comm);
    this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(1),
        this->BuildNextHop(1), comm);
    this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(1),
        this->BuildNextHop(1), comm);

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Multiple routes are added/updated.
//
TYPED_TEST(BgpXmppIpTest, MultipleRoutes) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add multiple routes from agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        test::NextHop nexthop(true, this->BuildNextHop(1));
        test::RouteAttributes attr;
        this->AddRoute(this->agent_a_, this->BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
        this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
    }

    // Updates routes from agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        test::NextHop nexthop(true, this->BuildNextHop(2));
        test::RouteAttributes attr;
        this->AddRoute(this->agent_a_, this->BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes are updated up on agents A and B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(idx),
            this->BuildNextHop(2));
        this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(idx),
            this->BuildNextHop(2));
    }

    // Delete route from agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->DeleteRoute(this->agent_a_, this->BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(idx));
    }

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
//
TYPED_TEST(BgpXmppIpTest, JoinLeave) {
    // Subscribe agent A to master instance.
    this->Subscribe(this->agent_a_, 0);

    // Add routes from agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        test::NextHop nexthop(true, this->BuildNextHop(1));
        test::RouteAttributes attr;
        this->AddRoute(this->agent_a_, this->BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes showed up on agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
    }

    // Register agent B to master instance.
    this->Subscribe(this->agent_b_, 0);
    task_util::WaitForIdle();

    // Verify that routes are present at agent A and showed up on agent B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
        this->VerifyRouteExists(this->agent_b_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
    }

    // Unregister agent B from master instance.
    this->Unsubscribe(this->agent_b_);
    task_util::WaitForIdle();

    // Verify that routes are present at agent A and deleted at agent B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteExists(this->agent_a_, this->BuildPrefix(idx),
            this->BuildNextHop(1));
        this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->DeleteRoute(this->agent_a_, this->BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < this->kRouteCount; ++idx) {
        this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(idx));
    }

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

//
// Agent flaps a route by changing it repeatedly.
//
TYPED_TEST(BgpXmppIpTest, RouteFlap) {
    // Subscribe to master instance
    this->Subscribe(this->agent_a_, 0);
    this->Subscribe(this->agent_b_, 0);

    // Add route from agent A and change it repeatedly.
    for (int idx = 0; idx < 128; ++idx) {
        test::NextHop nexthop1(true, this->BuildNextHop(1));
        test::NextHop nexthop2(true, this->BuildNextHop(2));
        test::RouteAttributes attr;
        this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop1, attr);
        this->AddRoute(this->agent_a_, this->BuildPrefix(1), nexthop2, attr);
    }

    // Delete route from agent A.
    this->DeleteRoute(this->agent_a_, this->BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    this->VerifyRouteNoExists(this->agent_a_, this->BuildPrefix(1));
    this->VerifyRouteNoExists(this->agent_b_, this->BuildPrefix(1));

    // Close the sessions.
    this->SessionDown(this->agent_a_);
    this->SessionDown(this->agent_b_);
}

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
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
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
    DB::SetPartitionCount(1);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
