/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

// Test case to check L2 only mode has bridge route
// and nexthops proper.
//
// - Check the route and nexthop when change is from
//   L2_L3 mode to L2 only mode.
// - Test case checks routes and nexthop with Default
//   forwarding mode.
// - Change forwarding mode to L2 only mode and check
//   bridge route and nexthop are proper.
// - Inet table doesnt have multicast route.
TEST_F(IgmpTest, V2_ToL2OnlyFwdMode) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    Agent *agent = Agent::GetInstance();
    AgentPath *path = NULL;
    Inet4MulticastRouteEntry *mc_route = NULL;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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

    idx = 1;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    ModifyForwardingModeVn("vn1", 1, "l2");
    client->WaitForIdle(5);

    report_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(0U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    leave_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

// Test case to check L2 only mode has bridge route
// and nexthops proper.
//
// - Check the same as above except change is from
//   L2 only mode to L2_L3 mode.
// - Test case checks routes and nexthop with Default
//   forwarding mode.
// - Change forwarding mode to L2 only mode and check
//   bridge route and nexthop are proper.
// - Inet table doesnt have multicast route.
TEST_F(IgmpTest, V2_FromL2OnlyFwdMode) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    Agent *agent = Agent::GetInstance();
    AgentPath *path = NULL;
    Inet4MulticastRouteEntry *mc_route = NULL;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);

    ModifyForwardingModeVn("vn1", 1, "l2");
    client->WaitForIdle(5);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(0U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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

    idx = 1;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    ModifyForwardingModeVn("vn1", 1, "l2_l3");
    client->WaitForIdle(5);

    report_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    leave_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

// Test case to check L3 only mode has route and
// nexthops proper.
//
// - Check the route and nexthop when change is from
//   default mode to L3 only mode.
// - Test case checks routes and nexthop with Default
//   forwarding mode.
// - Change forwarding mode to L3 only mode and check
//   multicast route and nexthop are proper.
TEST_F(IgmpTest, V2_ToL3OnlyFwdMode) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t local_g_add_count = 0;
    uint32_t local_g_del_count = 0;
    uint32_t report_count = 0;
    uint32_t leave_count = 0;

    Agent *agent = Agent::GetInstance();
    AgentPath *path = NULL;
    Inet4MulticastRouteEntry *mc_route = NULL;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(local_g_add_count, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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

    idx = 1;
    local_g_del_count++;
    leave_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV2Leave, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_GROUP_LEAVE, leave_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(false, local_g_del_count);

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    ModifyForwardingModeVn("vn1", 1, "l3");
    client->WaitForIdle(5);

    report_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    leave_count = 0;
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

    mc_route = MCRouteGet(agent->local_peer(), vrf_name, group, source);
    EXPECT_EQ((mc_route == NULL), true);

    nh = L2RouteToNextHop(vrf_name, mac);
    EXPECT_EQ((nh == NULL), true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

// Test case to check two VNs and routes in them
//
// - Check the route and nexthop when two VNs are
//   configured. Check in both VNs.
// - Precussor to the next test that tests
//   Vxlan routing w.r.t evpn multicast.
TEST_F(IgmpTest, V2_TwoVns) {

    bool ret = false;
    boost::system::error_code ec;

    struct PortInfo *port;
    uint32_t local_g_add_count = 0;
    uint32_t report_count = 0;

    Agent *agent = Agent::GetInstance();
    AgentPath *path = NULL;
    Inet4MulticastRouteEntry *mc_route = NULL;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vn1_vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name_1 = vn1_vmi_0->vrf()->GetName();

    VmInterface *vn2_vmi_0 = VmInterfaceGet(input_2[0].intf_id);
    std::string vrf_name_2 = vn2_vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);

    Ip4Address uc_addr;
    InetUnicastRouteEntry *uc_route = NULL;

    GetVnPortInfo(0, &port, NULL, NULL);
    uc_addr = Ip4Address::from_string(port[0].addr, ec);
    uc_route = RouteGet(vrf_name_1, uc_addr, 32);
    EXPECT_EQ((uc_route != NULL), true);

    GetVnPortInfo(1, &port, NULL, NULL);
    uc_addr = Ip4Address::from_string(port[0].addr, ec);
    uc_route = RouteGet(vrf_name_1, uc_addr, 32);
    EXPECT_EQ((uc_route == NULL), true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    uint32_t base = 0;
    uint32_t idx = 0;

    GetVnPortInfo(0, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    GetVnPortInfo(0, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    report_count = 0;

    base = 7; // second VN
    idx = 0;
    GetVnPortInfo(1, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    GetVnPortInfo(1, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    mc_route = MCRouteGet(agent->local_peer(), vrf_name_1, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name_1, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name_1, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    mc_route = MCRouteGet(agent->local_peer(), vrf_name_2, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name_2, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name_2, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

// Test case to check two VNs and routes in them
// when Vxlan routing is enabled.
//
// - Check the route and nexthop when two VNs are
//   configured. routes should have VRF Translate NH
// - Check vxlan routing VRF for the routes.
TEST_F(IgmpTest, V2_VxlanRoutingEnabled) {

    bool ret = false;
    boost::system::error_code ec;

    struct PortInfo *port;
    uint32_t local_g_add_count = 0;
    uint32_t report_count = 0;

    Agent *agent = Agent::GetInstance();
    AgentPath *path = NULL;
    Inet4MulticastRouteEntry *mc_route = NULL;

    const NextHop *nh;
    const InterfaceNH *interface_nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    TestEnvInit(UT_IGMP_VERSION_2, true);

    VmInterface *vn1_vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name_1 = vn1_vmi_0->vrf()->GetName();

    VmInterface *vn2_vmi_0 = VmInterfaceGet(input_2[0].intf_id);
    std::string vrf_name_2 = vn2_vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);

    AddLrVmiPort("lr-vmi-vn1", 91, "10.2.1.250", "vrf1", "vn1",
            "instance_ip_1", 1);
    AddLrVmiPort("lr-vmi-vn2", 92, "10.2.2.250", "vrf2", "vn2",
            "instance_ip_2", 2);
    AddLrRoutingVrf(1);
    AddLrBridgeVrf("vn1", 1);
    AddLrBridgeVrf("vn2", 1);
    client->WaitForIdle(5);

    using boost::uuids::nil_uuid;

    EXPECT_TRUE(VmInterfaceGet(1)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(2)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(92)->logical_router_uuid() != nil_uuid());

    Ip4Address uc_addr;
    InetUnicastRouteEntry *uc_route = NULL;

    GetVnPortInfo(0, &port, NULL, NULL);
    uc_addr = Ip4Address::from_string(port[0].addr, ec);
    uc_route = RouteGet(vrf_name_1, uc_addr, 32);
    EXPECT_EQ((uc_route != NULL), true);

    nh = LPMRouteToNextHop(vrf_name_1, uc_addr);
    EXPECT_EQ((nh != NULL), true);

    interface_nh = dynamic_cast<const InterfaceNH *>(nh);
    EXPECT_EQ((interface_nh != NULL), true);

    GetVnPortInfo(1, &port, NULL, NULL);
    uc_addr = Ip4Address::from_string(port[0].addr, ec);
    uc_route = RouteGet("l3evpn_1", uc_addr, 32);
    EXPECT_EQ((uc_route != NULL), true);

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_0, ec);

    MacAddress mac;
    agent->oper_db()->multicast()->GetMulticastMacFromIp(group, mac);

    uint32_t base = 0;
    uint32_t idx = 0;

    GetVnPortInfo(0, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    GetVnPortInfo(0, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    report_count = 0;

    base = 7; // second VN
    idx = 0;
    GetVnPortInfo(1, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    GetVnPortInfo(1, &port, NULL, NULL);
    local_g_add_count++;
    report_count++;
    SendIgmp(GetItfId(base+idx), port[idx].addr, IgmpTypeV2Report, igmp_gs, 1);
    ret = WaitForRxOkCount(base+idx, IGMP_V2_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForGCount(true, local_g_add_count);
    EXPECT_EQ(ret, true);

    mc_route = MCRouteGet(agent->local_peer(), vrf_name_1, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name_1, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name_1, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    mc_route = MCRouteGet(agent->local_peer(), vrf_name_2, group, source);
    EXPECT_EQ((mc_route != NULL), true);

    path = mc_route->FindPath(agent->local_peer());
    EXPECT_EQ((path != NULL), true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name_2, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    nh = L2RouteToNextHop(vrf_name_2, mac);
    EXPECT_EQ((nh != NULL), true);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    DelLrBridgeVrf("vn1", 1);
    DelLrBridgeVrf("vn2", 1);
    DelLrRoutingVrf(1);
    DeleteVxlanRouting();

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

