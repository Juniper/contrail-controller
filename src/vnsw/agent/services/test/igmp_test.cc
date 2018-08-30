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
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV1Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V1_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    usleep(10*USECS_PER_SEC);

    local_g_del_count++;
    ret = WaitForGCount(false, local_g_del_count);

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }
    IgmpGlobalClear();

    TestEnvDeinit();
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
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

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

    usleep(5*USECS_PER_SEC);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string("239.1.1.10", ec);
    Ip4Address source = Ip4Address::from_string("0.0.0.0", ec);

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
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    idx = 0;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    usleep(5*USECS_PER_SEC);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }
    IgmpGlobalClear();

    TestEnvDeinit();
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

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    usleep(10*USECS_PER_SEC);

    local_g_del_count++;
    ret = WaitForGCount(false, local_g_del_count);

    IgmpRxCountReset();
    while (IgmpRxCountGet() < 7) {
        usleep(10);
    }
    EXPECT_EQ(IgmpRxCountGet(), 7);

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }
    IgmpGlobalClear();

    TestEnvDeinit();
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

    Ip4Address group = Ip4Address::from_string("239.1.1.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.1.10", ec);

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

TEST_F(IgmpTest, SendV3ReportsAndFabricOlist) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;

    Set_Up();

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;
    const TunnelNH *tnh;
    const InterfaceNH *inh;

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    Agent *agent = Agent::GetInstance();

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 100,
                    Ip4Address::from_string("1.1.1.1"),
                    TunnelType::MplsType()));

    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    1000, olist, 1);
    client->WaitForIdle();

    agent->oper_db()->multicast()->ModifyMvpnVrfRegistration(bgp_peer(1),
                            vm_itf->vrf()->GetName(), group, source, 0);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    tnh = dynamic_cast<const TunnelNH *>(nh);
    EXPECT_EQ((tnh != NULL), true);

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    cnh1 = cnh->Get(1);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    inh = dynamic_cast<const InterfaceNH *>(nh);
    EXPECT_EQ((inh != NULL), true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    TunnelOlist olist1;
    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    0, olist1, ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3ReportsAndNoFabricOlist) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;

    Set_Up();

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    Agent *agent = Agent::GetInstance();

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 100,
                    Ip4Address::from_string("1.1.1.1"),
                    TunnelType::MplsType()));

    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    1000, olist, 1);
    client->WaitForIdle();

    agent->oper_db()->multicast()->ModifyMvpnVrfRegistration(bgp_peer(1),
                            vm_itf->vrf()->GetName(), group, source,
                            ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    TunnelOlist olist1;
    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    0, olist1, ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
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

    Ip4Address group = Ip4Address::from_string("239.1.1.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.1.10", ec);

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
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

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
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    IgmpVnEnable("vn1", 1, true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

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
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

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
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQuerySend) {

    bool ret = false;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, true, vms*(sleep+1));
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQueryDisabled) {

    bool ret = false;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(false);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, false, vms*(sleep+1));
    EXPECT_EQ(ret, true);

    usleep(lmqt);

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

    uint32_t group = inet_addr("239.1.1.10");
    uint32_t source = inet_addr("100.1.1.10");

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

    group = inet_addr("239.1.1.10");
    source = inet_addr("100.1.1.10");

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

