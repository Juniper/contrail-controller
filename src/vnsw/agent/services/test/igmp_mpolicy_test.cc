/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

TEST_F(IgmpTest, MulticastPolicy) {

    bool ret = false;
    uint32_t idx = 0;
    boost::system::error_code ec;
    struct IgmpGroupSource igmp_gs;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    AddMulticastPolicy("policy-1", 1, policy, 4);
    AddLink("multicast-policy", "policy-1", "virtual-network", "vn1");

    IgmpGlobalEnable(true);

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t report_count = 0;
    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;

    memset(&igmp_gs, 0x00, sizeof(igmp_gs));

    igmp_gs.record_type = 1;
    igmp_gs.source_count = 4;
    igmp_gs.group = ntohl(inet_addr(policy[0].grp));
    igmp_gs.sources[0] = ntohl(inet_addr(policy[0].src));
    igmp_gs.sources[1] = ntohl(inet_addr(policy[1].src));
    igmp_gs.sources[2] = ntohl(inet_addr(policy[2].src));
    igmp_gs.sources[3] = ntohl(inet_addr(policy[3].src));
    local_sg_add_count += 2;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group;
    Ip4Address source;

    Agent *agent = Agent::GetInstance();
    const NextHop *nh;

    group = Ip4Address::from_string(policy[0].grp, ec);
    source = Ip4Address::from_string(policy[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy[0].grp, ec);
    source = Ip4Address::from_string(policy[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy[1].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy[0].grp, ec);
    source = Ip4Address::from_string(policy[2].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy[2].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy[0].grp, ec);
    source = Ip4Address::from_string(policy[3].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy[3].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    igmp_gs.record_type = 6;
    igmp_gs.source_count = 4;
    local_sg_del_count += 2;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    DelLink("multicast-policy", "policy-1", "virtual-network", "vn1");
    DelMulticastPolicy("policy-1");

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, MulticastPolicy_2) {

    bool ret = false;
    uint32_t idx = 0;
    boost::system::error_code ec;
    struct IgmpGroupSource igmp_gs[2];

    TestEnvInit(UT_IGMP_VERSION_3, true);

    AddMulticastPolicy("policy-11", 1, policy_11, 2);
    AddLink("multicast-policy", "policy-11", "virtual-network", "vn1");

    AddMulticastPolicy("policy-12", 2, policy_12, 2);
    AddLink("multicast-policy", "policy-12", "virtual-network", "vn1");

    IgmpGlobalEnable(true);

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t report_count = 0;
    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;

    memset(&igmp_gs[0], 0x00, sizeof(igmp_gs[2]));

    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 2;
    igmp_gs[0].group = ntohl(inet_addr(policy_11[0].grp));
    igmp_gs[0].sources[0] = ntohl(inet_addr(policy_11[0].src));
    igmp_gs[0].sources[1] = ntohl(inet_addr(policy_11[1].src));
    igmp_gs[1].record_type = 1;
    igmp_gs[1].source_count = 2;
    igmp_gs[1].group = ntohl(inet_addr(policy_12[0].grp));
    igmp_gs[1].sources[0] = ntohl(inet_addr(policy_12[0].src));
    igmp_gs[1].sources[1] = ntohl(inet_addr(policy_12[1].src));
    local_sg_add_count += 2;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 2);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group;
    Ip4Address source;

    Agent *agent = Agent::GetInstance();
    const NextHop *nh;

    group = Ip4Address::from_string(policy_11[0].grp, ec);
    source = Ip4Address::from_string(policy_11[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_11[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_11[0].grp, ec);
    source = Ip4Address::from_string(policy_11[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_11[1].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_12[0].grp, ec);
    source = Ip4Address::from_string(policy_12[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    if (policy_12[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_12[0].grp, ec);
    source = Ip4Address::from_string(policy_12[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_12[1].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 2;
    igmp_gs[1].record_type = 6;
    igmp_gs[1].source_count = 2;
    local_sg_del_count += 2;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 2);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    DelLink("multicast-policy", "policy-12", "virtual-network", "vn1");
    DelMulticastPolicy("policy-12");

    DelLink("multicast-policy", "policy-11", "virtual-network", "vn1");
    DelMulticastPolicy("policy-11");

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, MulticastPolicy_3) {

    bool ret = false;
    uint32_t idx = 0;
    boost::system::error_code ec;
    struct IgmpGroupSource igmp_gs[2];

    TestEnvInit(UT_IGMP_VERSION_3, true);

    AddMulticastPolicy("policy-11", 1, policy_11, 2);
    AddLink("multicast-policy", "policy-11", "virtual-network", "vn1");

    AddMulticastPolicy("policy-12", 2, policy_12, 2);
    AddLink("multicast-policy", "policy-12", "virtual-network", "vn1");

    AddMulticastPolicy("policy-21", 3, policy_21, 2);
    AddLink("multicast-policy", "policy-21", "virtual-network", "vn2");

    AddMulticastPolicy("policy-22", 4, policy_22, 2);
    AddLink("multicast-policy", "policy-22", "virtual-network", "vn2");

    IgmpGlobalEnable(true);

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t report_count = 0;
    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;

    memset(&igmp_gs[0], 0x00, sizeof(igmp_gs[2]));

    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 2;
    igmp_gs[0].group = ntohl(inet_addr(policy_11[0].grp));
    igmp_gs[0].sources[0] = ntohl(inet_addr(policy_11[0].src));
    igmp_gs[0].sources[1] = ntohl(inet_addr(policy_11[1].src));
    local_sg_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group;
    Ip4Address source;

    Agent *agent = Agent::GetInstance();
    const NextHop *nh;

    group = Ip4Address::from_string(policy_11[0].grp, ec);
    source = Ip4Address::from_string(policy_11[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_11[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_11[0].grp, ec);
    source = Ip4Address::from_string(policy_11[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_11[1].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 2;
    local_sg_del_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    memset(&igmp_gs[0], 0x00, sizeof(igmp_gs[2]));
    report_count = 0;

    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 2;
    igmp_gs[0].group = ntohl(inet_addr(policy_21[0].grp));
    igmp_gs[0].sources[0] = ntohl(inet_addr(policy_21[0].src));
    igmp_gs[0].sources[1] = ntohl(inet_addr(policy_21[1].src));
    local_sg_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx+7), input_2[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx+7, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    group = Ip4Address::from_string(policy_21[0].grp, ec);
    source = Ip4Address::from_string(policy_21[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_21[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_21[0].grp, ec);
    source = Ip4Address::from_string(policy_21[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    if (policy_21[1].action)
        EXPECT_TRUE((nh != NULL));
    else
       EXPECT_TRUE((nh == NULL));

    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 2;
    local_sg_del_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx+7), input_2[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx+7, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    DelLink("multicast-policy", "policy-22", "virtual-network", "vn2");
    DelMulticastPolicy("policy-22");

    DelLink("multicast-policy", "policy-21", "virtual-network", "vn2");
    DelMulticastPolicy("policy-21");

    DelLink("multicast-policy", "policy-12", "virtual-network", "vn1");
    DelMulticastPolicy("policy-12");

    DelLink("multicast-policy", "policy-11", "virtual-network", "vn1");
    DelMulticastPolicy("policy-11");

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(DEFAULT_VNSW_CONFIG_FILE, ksync_init, true, true);
    client->WaitForIdle();

    StartControlNodeMock();

    Agent *agent = Agent::GetInstance();
    RouterIdDepInit(agent);

    // Enable MVPN mode.
    agent->params()->set_mvpn_ipv4_enable(true);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();

    StopControlNodeMock();

    TestShutdown();
    delete client;

    return ret;
}
