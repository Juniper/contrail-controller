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

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

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
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
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

TEST_F(IgmpTest, IgmpV2_2Groups) {

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
    report_count += 1;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, igmp_gs, 2);
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

    idx = 0;
    local_g_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    BridgeRouteEntry *b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route != NULL), true);

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

    group = Ip4Address::from_string(MGROUP_ADDR_2, ec);
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route != NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

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
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route == NULL), true);

    group = Ip4Address::from_string(MGROUP_ADDR_2, ec);
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

TEST_F(IgmpTest, IgmpV2_Fabric) {

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

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;

    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    BridgeRouteEntry *b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route != NULL), true);

    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 100,
                    Ip4Address::from_string("1.1.1.1"),
                    TunnelType::MplsType()));

    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    vrf_name, group, source, 1000, olist, 1);
    client->WaitForIdle();

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(1);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

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
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    TunnelOlist olist1;
    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    vrf_name, group, source, 0, olist1,
                    ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

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

TEST_F(IgmpTest, IgmpV2_IgmpVnEnable) {

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

    IgmpVnEnable("vn1", 1, true);

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

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

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
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route == NULL), true);

    usleep(lmqt);

    IgmpVnEnable("vn1", 1, false);

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpV2_IntfDelete) {

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

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

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

    TestEnvDeinit();
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    b_route = L2RouteGet(vrf_name, mac);
    EXPECT_EQ((b_route == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    return;
}

TEST_F(IgmpTest, IgmpV2_TwoGroups) {

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

    local_g_add_count += 1;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

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
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    group = Ip4Address::from_string(MGROUP_ADDR_2, ec);
    source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    local_g_del_count += 1;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, &igmp_gs[1], 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
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

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_11, ec);

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

TEST_F(IgmpTest, MulticastPolicyWithEvpn_1) {

    bool ret = false;
    uint32_t idx = 0;
    boost::system::error_code ec;
    struct IgmpGroupSource igmp_gs_1[2];

    memset(&igmp_gs_1[0], 0x00, sizeof(igmp_gs_1[2]));

    TestEnvInit(UT_IGMP_VERSION_2, true);

    AddMulticastPolicy("policy-01", 1, policy_01, 2);
    AddLink("multicast-policy", "policy-01", "virtual-network", "vn1");

    AddMulticastPolicy("policy-02", 2, policy_02, 2);
    AddLink("multicast-policy", "policy-02", "virtual-network", "vn1");

    AddMulticastPolicy("policy-11", 3, policy_11, 2);
    AddLink("multicast-policy", "policy-11", "virtual-network", "vn2");

    AddMulticastPolicy("policy-12", 4, policy_12, 2);
    AddLink("multicast-policy", "policy-12", "virtual-network", "vn2");

    IgmpGlobalEnable(true);

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t report_count = 0;
    uint32_t leave_count = 0;
    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;

    igmp_gs_1[0].group = ntohl(inet_addr(policy_01[0].grp));
    idx = 0;
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Report, &igmp_gs_1[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group;
    Ip4Address source;

    Agent *agent = Agent::GetInstance();
    const NextHop *nh;

    group = Ip4Address::from_string(policy_01[0].grp, ec);
    source = Ip4Address::from_string(policy_01[0].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(), "vrf1", group, source);
    if (policy_01[0].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    group = Ip4Address::from_string(policy_01[1].grp, ec);
    source = Ip4Address::from_string(policy_01[1].src, ec);
    nh = MCRouteToNextHop(agent->local_vm_peer(), "vrf1", group, source);
    if (policy_01[1].action)
        EXPECT_TRUE((nh != NULL));
    else
        EXPECT_TRUE((nh == NULL));

    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, &igmp_gs_1[0], 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);
    EXPECT_EQ(ret, true);

    DelLink("multicast-policy", "policy-12", "virtual-network", "vn2");
    DelMulticastPolicy("policy-12");

    DelLink("multicast-policy", "policy-11", "virtual-network", "vn2");
    DelMulticastPolicy("policy-11");

    DelLink("multicast-policy", "policy-02", "virtual-network", "vn1");
    DelMulticastPolicy("policy-02");

    DelLink("multicast-policy", "policy-01", "virtual-network", "vn1");
    DelMulticastPolicy("policy-01");

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
