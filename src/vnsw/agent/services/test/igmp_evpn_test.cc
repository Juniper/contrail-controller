/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

TEST_F(IgmpTest, IgmpV2) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    Agent *agent = Agent::GetInstance();

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 0;
    local_g_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string("239.1.1.10", ec);
    Ip4Address source = Ip4Address::from_string("0.0.0.0", ec);

    MacAddress mac;

    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    BridgeRouteEntry *b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route != NULL), true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    idx = 0;
    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpV3) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;

    Agent *agent = Agent::GetInstance();

    TestEnvInit(UT_IGMP_VERSION_3, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string("239.1.1.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.1.10", ec);

    MacAddress mac;

    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    BridgeRouteEntry *b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route != NULL), true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route == NULL), true);

    usleep(lmqt);

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

    // Disable MVPN mode for normal IGMP testing.
    agent->params()->set_mvpn_ipv4_enable(false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();

    StopControlNodeMock();

    TestShutdown();
    delete client;

    return ret;
}
