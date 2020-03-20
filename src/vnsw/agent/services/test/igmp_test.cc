/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

TEST_F(IgmpTest, SendV1Reports) {

    bool ret = false;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;

    TestEnvInit(UT_IGMP_VERSION_1, true);

    IgmpGlobalEnable(true);
    uint32_t idx = 0;

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV1Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V1_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    usleep(15*USECS_PER_SEC);

    local_g_del_count++;
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV2Reports) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    uint32_t idx = 0;

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    Agent *agent = Agent::GetInstance();

    Inet4MulticastRouteEntry *mc_route = MCRouteGet(agent->local_peer(),
                                        vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    AgentPath *path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count, cnh->ActiveComponentNHCount());

    idx = 0;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    // 4 second delay to trigger timer as is case with
    // evpn test-framework script.
    usleep(4*USECS_PER_SEC);

    // Because of above 4 second delay, IGMP timeout happens for the
    // <S,G> and all the joins are timed out.
    // In this case, code path is different where igmp_g_del_count
    // is not increment but just having a check for local_g_del_count
    // for completeness sake.
    idx = 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    // 4 second delay to trigger timer as is case with
    // evpn test-framework script.
    usleep(4*USECS_PER_SEC);

    idx = 2;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    agent = Agent::GetInstance();

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count/2, cnh->ActiveComponentNHCount());

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV2ReportsWithDelVnFirst) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    uint32_t idx = 0;

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    Agent *agent = Agent::GetInstance();

    Inet4MulticastRouteEntry *mc_route = MCRouteGet(agent->local_peer(),
                                        vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    AgentPath *path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count, cnh->ActiveComponentNHCount());

    idx = 0;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    // 4 second delay to trigger timer as is case with
    // evpn test-framework script.
    usleep(4*USECS_PER_SEC);

    idx = 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    // 4 second delay to trigger timer as is case with
    // evpn test-framework script.
    usleep(4*USECS_PER_SEC);

    idx = 2;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    agent = Agent::GetInstance();

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count/2, cnh->ActiveComponentNHCount());

    IgmpGlobalClear();

    TestEnvDeinit(true);
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV2ReportsWithoutLeave) {

    bool ret = false;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    IgmpGlobalEnable(true);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    usleep(15*USECS_PER_SEC);

    local_g_del_count++;
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }
    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3Reports) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 1;
    igmp_gs[0].record_type = 3;
    igmp_gs[0].source_count = 0;
    local_sg_add_count += 0;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 3;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 4;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 5;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    igmp_gs[1].record_type = 1;
    igmp_gs[1].source_count = 4;
    local_sg_add_count += igmp_gs[0].source_count;
    local_sg_add_count += igmp_gs[1].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 2);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[2].record_type = 1;
    igmp_gs[2].source_count = 5;
    local_sg_add_count += igmp_gs[2].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_11, ec);

    Agent *agent = Agent::GetInstance();

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(5, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 4;
    local_sg_del_count += 0;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 3;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 4;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 5;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    igmp_gs[1].record_type = 6;
    igmp_gs[1].source_count = 4;
    local_sg_del_count += igmp_gs[0].source_count + igmp_gs[1].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 2);
    ret = WaitForRxOkCount(idx, IgmpTypeV3Report, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[2].record_type = 6;
    igmp_gs[2].source_count = 5;
    local_sg_del_count += igmp_gs[2].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3ReportsNonSequential) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;

    uint32_t group = inet_addr(MGROUP_ADDR_1);
    uint32_t source = inet_addr(MSOURCE_ADDR_11);

    Ip4Address group_v4 = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source_v4 = Ip4Address::from_string(MSOURCE_ADDR_11, ec);

    TestEnvInit(UT_IGMP_VERSION_3, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    memset(&igmp_gs, 0x00, sizeof(igmp_gs));

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    igmp_gs[0].group = ntohl(group);

    local_sg_add_count += igmp_gs[0].source_count;
    igmp_gs[0].sources[0] = ntohl(source);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[1].record_type = 1;
    igmp_gs[1].source_count = 1;
    igmp_gs[1].group = ntohl(group);

    local_sg_add_count += igmp_gs[1].source_count;
    igmp_gs[1].sources[0] = ntohl(source+1);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[2].record_type = 1;
    igmp_gs[2].source_count = 1;
    igmp_gs[2].group = ntohl(group);

    local_sg_add_count += igmp_gs[2].source_count;
    igmp_gs[2].sources[0] = ntohl(source+2);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Agent *agent = Agent::GetInstance();

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group_v4, source_v4);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[1].record_type = 6;
    igmp_gs[1].source_count = 1;
    igmp_gs[1].group = ntohl(group);

    local_sg_del_count += igmp_gs[1].source_count;
    igmp_gs[1].sources[0] = ntohl(source+1);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[2].record_type = 6;
    igmp_gs[2].source_count = 1;
    igmp_gs[2].group = ntohl(group);

    local_sg_del_count += igmp_gs[2].source_count;
    igmp_gs[2].sources[0] = ntohl(source+2);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    igmp_gs[0].group = ntohl(group);

    local_sg_del_count += igmp_gs[0].source_count;
    igmp_gs[0].sources[0] = ntohl(source);
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group_v4, source_v4);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpIntfConfig) {

    bool ret = false;
    boost::system::error_code ec;

    VmInterface *vm_itf = NULL;

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;
    uint32_t rej_count = 0;
    uint32_t idx = 0;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Agent *agent = Agent::GetInstance();

    TestEnvInit(UT_IGMP_VERSION_3, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_11, ec);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    // Disable IGMP on idx = 0
    idx = 0;
    IgmpVmiEnable(idx, false);
    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    rej_count = 0;

    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    local_sg_add_count += igmp_gs[0].source_count;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 6;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    // Enable IGMP on idx = 0
    idx = 0;
    IgmpVmiEnable(idx, true);
    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_del_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpVnEnable) {

    bool ret = false;

    uint32_t report_count = 0;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpVnEnable("vn1", 1, true);

    idx = 0;
    VmInterface *vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpVnDisable) {

    bool ret = false;

    uint32_t idx = 0;
    uint32_t rej_count = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpVnEnable("vn1", 1, false);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpSystemEnable) {

    bool ret = false;

    uint32_t report_count = 0;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);

    idx = 0;
    VmInterface *vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpSystemDisable) {

    bool ret = false;

    uint32_t idx = 0;
    uint32_t rej_count = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(false);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_1) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(false);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(true);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(true);

    IgmpVnEnable("vn1", 1, true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(false);

    IgmpVnEnable("vn1", 1, false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_2) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_3) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVnEnable("vn1", 1, false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, DISABLED_IgmpTaskTrigger) {

    bool ret = false;
    uint32_t idx = 0;
    struct IgmpGroupSource igmp_gs;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);

    Agent *agent = Agent::GetInstance();
    IgmpProto *igmp_proto = agent->GetIgmpProto();
    GmpProto *gmp_proto = igmp_proto->GetGmpProto();
    GmpProto::GmpStats stats;

    uint32_t report_count = 0;
    uint32_t loop = 10;
    uint32_t pkt_per_loop = 50;
    uint32_t source_count = 2;
    uint32_t ex_count = loop*pkt_per_loop*source_count;
    uint32_t count = 0;

    igmp_proto->ClearStats();
    gmp_proto->ClearStats();
    client->WaitForIdle();

    uint32_t group = inet_addr(MGROUP_ADDR_1);
    uint32_t source = inet_addr(MSOURCE_ADDR_11);

    memset(&igmp_gs, 0x00, sizeof(igmp_gs));
    igmp_gs.record_type = 1;
    igmp_gs.source_count = source_count;
    igmp_gs.group = ntohl(group);

    for (uint32_t i = 0; i < loop; i++) {
        for (uint32_t j = 0; j < pkt_per_loop; j++) {
            igmp_gs.sources[0] = ntohl(source);
            igmp_gs.sources[1] = ntohl(source+1);
            report_count++;
            SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
            source += igmp_gs.source_count;
            usleep(1000);
        }
        usleep(10000);
    }
    client->WaitForIdle();

    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = gmp_proto->GetStats();
        if (++count == 10000) {
            break;
        }
    } while (stats.gmp_sg_add_count_ != ex_count);

    uint32_t actual_count = stats.gmp_sg_add_count_;
    EXPECT_EQ(actual_count, ex_count);

    group = inet_addr(MGROUP_ADDR_1);
    source = inet_addr(MSOURCE_ADDR_11);

    igmp_gs.record_type = 6;
    igmp_gs.source_count = source_count;
    igmp_gs.group = ntohl(group);

    for (uint32_t i = 0; i < loop; i++) {
        for (uint32_t j = 0; j < pkt_per_loop; j++) {
            igmp_gs.sources[0] = ntohl(source);
            igmp_gs.sources[1] = ntohl(source+1);
            report_count++;
            SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
            source += igmp_gs.source_count;
            usleep(1000);
        }
        usleep(10000);
    }
    client->WaitForIdle();

    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = gmp_proto->GetStats();
        if (++count == 10000) {
            break;
        }
    } while (stats.gmp_sg_del_count_ != ex_count);

    actual_count = stats.gmp_sg_del_count_;
    EXPECT_EQ(actual_count, ex_count);

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

