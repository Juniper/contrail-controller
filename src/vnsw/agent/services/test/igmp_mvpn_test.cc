/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

TEST_F(IgmpTest, SendV3ReportsAndFabricOlist) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

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

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(true, local_sg_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;
    const TunnelNH *tnh;
    const InterfaceNH *inh;

    Ip4Address group = Ip4Address::from_string(MGROUP_ADDR_1, ec);
    Ip4Address source = Ip4Address::from_string(MSOURCE_ADDR_11, ec);

    Agent *agent = Agent::GetInstance();

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3U, cnh->ActiveComponentNHCount());

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
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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
    EXPECT_EQ(3U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    inh = dynamic_cast<const InterfaceNH *>(nh);
    EXPECT_EQ((inh != NULL), true);

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

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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

    uint32_t local_sg_add_count = 0;
    uint32_t local_sg_del_count = 0;
    uint32_t report_count = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

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

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sg_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
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

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3U, cnh->ActiveComponentNHCount());

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
    EXPECT_EQ(2U, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sg_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSgCount(false, local_sg_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1U, cnh->ActiveComponentNHCount());

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
