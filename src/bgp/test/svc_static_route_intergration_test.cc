/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "svc_static_route_intergration_test.h"


//
// Each Agent has multiple l3 interfaces and instance for static routes is
// created on both compute nodes.
//
TYPED_TEST(ServiceChainIntegrationTest, StaticRouteMultipleL3Intf) {
    unique_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_8.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(this->cn1_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);

    params = this->GetStaticRouteConfig(
        "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn2_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);
    task_util::WaitForIdle();

    // Add Connected route
    NextHops nexthops;
    nexthops.push_back(NextHop("88.88.88.88", 6, "udp"));
    nexthops.push_back(NextHop("99.99.99.99", 7, "udp"));

    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_a_2_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_a_2_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else {
        assert(false);
    }

    nexthops.clear();
    nexthops.push_back(NextHop("66.66.66.66", 6, "udp"));
    nexthops.push_back(NextHop("77.77.77.77", 7, "udp"));
    if (this->GetFamily() == Address::INET) {
        this->agent_b_1_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_b_2_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_b_1_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_b_2_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Check for aggregated route
    vector<PathVerify> path_list;
    PathVerify verify_1("66.66.66.66", "StaticRoute", "66.66.66.66",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_2("77.77.77.77", "StaticRoute", "77.77.77.77",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_3("88.88.88.88", "StaticRoute", "88.88.88.88",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_4("99.99.99.99", "StaticRoute", "99.99.99.99",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_5("66.66.66.66", "BGP_XMPP", "66.66.66.66",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_6("77.77.77.77", "BGP_XMPP", "77.77.77.77",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_7("88.88.88.88", "BGP_XMPP", "88.88.88.88",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_8("99.99.99.99", "BGP_XMPP", "99.99.99.99",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    path_list.push_back(verify_1);
    path_list.push_back(verify_2);
    path_list.push_back(verify_3);
    path_list.push_back(verify_4);
    path_list.push_back(verify_5);
    path_list.push_back(verify_6);
    path_list.push_back(verify_7);
    path_list.push_back(verify_8);

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->DeleteConnectedRoute(true);
}

//
// Each Agent has multiple l3 interfaces and service instance is created on
// both compute nodes.
// Tests both static route and service chain functionality.
//
TYPED_TEST(ServiceChainIntegrationTest, DISABLED_StaticRouteMultipleL3Intf) {
    unique_ptr<autogen::StaticRouteEntriesType> params =
        this->GetStaticRouteConfig(
                "controller/src/bgp/testdata/static_route_8.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(this->cn1_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);

    params = this->GetStaticRouteConfig(
        "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn2_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);
    task_util::WaitForIdle();


    // Add external route from MX to a VN which is connected to dest
    // routing instance
    this->AddRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24), 100);
    task_util::WaitForIdle();

    // Add Connected route
    NextHops nexthops;
    nexthops.push_back(NextHop("88.88.88.88", 6, "udp"));
    nexthops.push_back(NextHop("99.99.99.99", 7, "udp"));

    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_a_2_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_a_2_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else {
        assert(false);
    }

    nexthops.clear();
    nexthops.push_back(NextHop("66.66.66.66", 6, "udp"));
    nexthops.push_back(NextHop("77.77.77.77", 7, "udp"));
    if (this->GetFamily() == Address::INET) {
        this->agent_b_1_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_b_2_->AddRoute(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_b_1_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
        this->agent_b_2_->AddInet6Route(
            ServiceChainIntegrationTestGlobals::connected_table_,
            this->BuildPrefix("1.1.2.3", 32), nexthops);
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Check for aggregated route
    vector<PathVerify> path_list;
    PathVerify verify_1("66.66.66.66", "StaticRoute", "66.66.66.66",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_2("77.77.77.77", "StaticRoute", "77.77.77.77",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_3("88.88.88.88", "StaticRoute", "88.88.88.88",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_4("99.99.99.99", "StaticRoute", "99.99.99.99",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_5("66.66.66.66", "ServiceChain", "66.66.66.66",
        list_of  ("udp"), "red");
    PathVerify verify_6("77.77.77.77", "ServiceChain", "77.77.77.77",
        list_of  ("udp"), "red");
    PathVerify verify_7("88.88.88.88", "ServiceChain", "88.88.88.88",
        list_of  ("udp"), "red");
    PathVerify verify_8("99.99.99.99", "ServiceChain", "99.99.99.99",
        list_of  ("udp"), "red");
    PathVerify verify_9("66.66.66.66", "BGP_XMPP", "66.66.66.66",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_10("77.77.77.77", "BGP_XMPP", "77.77.77.77",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_11("88.88.88.88", "BGP_XMPP", "88.88.88.88",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    PathVerify verify_12("99.99.99.99", "BGP_XMPP", "99.99.99.99",
        list_of  ("udp"), ServiceChainIntegrationTestGlobals::connected_table_);
    path_list.push_back(verify_1);
    path_list.push_back(verify_2);
    path_list.push_back(verify_3);
    path_list.push_back(verify_4);
    path_list.push_back(verify_5);
    path_list.push_back(verify_6);
    path_list.push_back(verify_7);
    path_list.push_back(verify_8);
    path_list.push_back(verify_9);
    path_list.push_back(verify_10);
    path_list.push_back(verify_11);
    path_list.push_back(verify_12);

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);

    this->DeleteRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(true);
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());

    ServiceChainIntegrationTestGlobals::connected_table_ = "blue";
    ServiceChainIntegrationTestGlobals::mx_push_connected_ = true;
    ServiceChainIntegrationTestGlobals::single_si_ = true;
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
