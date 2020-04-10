/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cmn/agent_cmn.h>

#include "test/test_cmn_util.h"
#define VR_FABRIC 1
#define VR_BOND_SLAVES 2
void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.100"},
};

class TestVnswIf : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        vnswif_ = agent_->ksync()->vnsw_interface_listner();

        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        vnet1_ = static_cast<VmInterface *>(VmPortGet(1));
        vnet2_ = static_cast<VmInterface *>(VmPortGet(2));
        AddNode("virtual-machine-interface", "vhost0", 100);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        InterfaceEvent(false, "vnet1", 0);
        InterfaceEvent(false, "vnet2", 0);
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        EXPECT_EQ(0U, vnswif_->GetHostInterfaceCount());
        WAIT_FOR(1000, 100, (VmPortFindRetDel(1) == false));
        WAIT_FOR(1000, 100, (VmPortFindRetDel(2) == false));
        DelNode("virtual-machine-interface", "vhost0");
        client->WaitForIdle();
    }

    void SetSeen(const string &ifname, bool oper, uint32_t id) {
        vnswif_->SetSeen(agent_->vhost_interface_name(), true, id);
    }

    void ResetSeen(const string &ifname) {
        vnswif_->ResetSeen(agent_->vhost_interface_name(), true);
    }

    void InterfaceEvent(bool add, const string &ifname, uint32_t flags, unsigned short int type_ = 0) {
        VnswInterfaceListener::Event::Type type;
        if (add) {
            type = VnswInterfaceListener::Event::ADD_INTERFACE;
        } else {
            type = VnswInterfaceListener::Event::DEL_INTERFACE;
        }
        vnswif_->Enqueue(new VnswInterfaceListener::Event(type, ifname, flags, type_));
        client->WaitForIdle();
    }

    void InterfaceAddressEvent(bool add, const string &ifname, const char *ip) {
        VnswInterfaceListener::Event::Type type;
        if (add) {
            type = VnswInterfaceListener::Event::ADD_ADDR;
        } else {
            type = VnswInterfaceListener::Event::DEL_ADDR;
        }

        Ip4Address addr = Ip4Address::from_string(ip);
        vnswif_->Enqueue(new VnswInterfaceListener::Event(type, ifname,
                                                          addr, 24, 0,
                                                          false));
        client->WaitForIdle();
    }

    void RouteEvent(bool add, const string &ifname, Ip4Address addr,
                    uint8_t plen, uint32_t protocol) {

        VnswInterfaceListener::Event::Type type;
        if (add) {
            type = VnswInterfaceListener::Event::ADD_ROUTE;
        } else {
            type = VnswInterfaceListener::Event::DEL_ROUTE;
        }

        vnswif_->Enqueue(new VnswInterfaceListener::Event(type, addr, plen,
                                                          ifname, Ip4Address(0),
                                                          protocol, 0));
        client->WaitForIdle();
    }

    Agent *agent_;
    VnswInterfaceListener *vnswif_;
    VmInterface *vnet1_;
    VmInterface *vnet2_;
};

// Ensure LL routes added for active ports
TEST_F(TestVnswIf, basic_1) {
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));
}

// Validate that link-local address is deleted for inactive interface
TEST_F(TestVnswIf, intf_inactive) {
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_FALSE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));
}

// Validate that link-local address is deleted when interface is deleted
TEST_F(TestVnswIf, intf_delete) {
    Ip4Address vnet1_address = vnet1_->mdata_ip_addr();
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    EXPECT_FALSE(vnswif_->IsValidLinkLocalAddress(vnet1_address));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));
}

// Validate that link-local address is added back if route deleted from kernel
TEST_F(TestVnswIf, host_route_del) {
    uint32_t count = vnswif_->ll_add_count();
    VnswInterfaceListener::Event *event =
        new VnswInterfaceListener::Event(VnswInterfaceListener::Event::DEL_ROUTE,
                                         vnet1_->mdata_ip_addr(), 32, "",
                                         Ip4Address(0),
                                         VnswInterfaceListener::kVnswRtmProto,
                                         0);
    vnswif_->Enqueue(event);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->ll_add_count() > count));
}

// On link flap of vhost, all link-local must be written again
TEST_F(TestVnswIf, vhost_link_flap) {
    uint32_t id = Interface::kInvalidIndex;
    const Interface *intf = agent_->vhost_interface();
    if (intf) {
        id = intf->id();
    }
    SetSeen(agent_->vhost_interface_name(), true, id);
    client->WaitForIdle();

    // Set link-state down
    InterfaceEvent(true, agent_->vhost_interface_name(), 0);

    // bring up link of vhost0
    uint32_t count = vnswif_->ll_add_count();

    // Set link-state up
    InterfaceEvent(true, agent_->vhost_interface_name(),
                   (IFF_UP|IFF_RUNNING));

    // Ensure routes are synced to host
    WAIT_FOR(1000, 100, (vnswif_->ll_add_count() >= (count + 2)));

    InterfaceEvent(false, agent_->vhost_interface_name(), 0);
    ResetSeen(agent_->vhost_interface_name());
    client->WaitForIdle();
}

// Interface inactive when seen by oper only
TEST_F(TestVnswIf, inactive_1) {
    VnswInterfaceListener::HostInterfaceEntry *entry =
        vnswif_->GetHostInterfaceEntry("vnet1");
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL)
        return;

    EXPECT_TRUE(entry->oper_seen_);
    EXPECT_FALSE(entry->host_seen_);
    EXPECT_TRUE(entry->link_up_);

    InterfaceEvent(true, "vnet1", 0);
    EXPECT_TRUE(entry->host_seen_);
    EXPECT_FALSE(entry->link_up_);

    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    EXPECT_TRUE(entry->link_up_);
}

// vhost-ip address update
TEST_F(TestVnswIf, vhost_addr_1) {
    uint32_t id = Interface::kInvalidIndex;
    const Interface *intf = agent_->vhost_interface();
    if (intf) {
        id = intf->id();
    }
    SetSeen(agent_->vhost_interface_name(), true, id);
    client->WaitForIdle();

    VnswInterfaceListener::HostInterfaceEntry *entry =
        vnswif_->GetHostInterfaceEntry(agent_->vhost_interface_name());
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL)
        return;

    const VmInterface *vhost =
        static_cast<const VmInterface *>(agent_->vhost_interface());
    string vhost_ip = "1.1.1.1";

    // vnswif module does not know IP address yet
    EXPECT_EQ(entry->addr_.to_ulong(), 0U);

    InterfaceAddressEvent(true, agent_->vhost_interface_name(), "1.1.1.1");

    // Validate address in entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "1.1.1.1");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 1U);

    InterfaceAddressEvent(true, agent_->vhost_interface_name(), "2.2.2.2");

    // Validate address in local entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "2.2.2.2");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 1U);

    // Ensure there is no change vhost-ip
    EXPECT_STREQ(vhost->primary_ip_addr().to_string().c_str(), vhost_ip.c_str());

    InterfaceAddressEvent(false, agent_->vhost_interface_name(), "1.1.1.1");

    // Validate address in local entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "0.0.0.0");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 1U);

    // Ensure there is no change vhost-ip
    EXPECT_STREQ(vhost->primary_ip_addr().to_string().c_str(), vhost_ip.c_str());

    InterfaceEvent(false, agent_->vhost_interface_name(), 0);
    ResetSeen(agent_->vhost_interface_name());
    client->WaitForIdle();
}

// vhost-ip address update
TEST_F(TestVnswIf, oper_state_1) {
    VnswInterfaceListener::HostInterfaceEntry *entry =
        vnswif_->GetHostInterfaceEntry("vnet1");
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL)
        return;

    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    EXPECT_TRUE(entry->link_up_);
    EXPECT_TRUE(entry->host_seen_);
    EXPECT_TRUE(entry->oper_seen_);

    InterfaceEvent(true, "vnet1", 0);
    EXPECT_FALSE(entry->link_up_);
    EXPECT_FALSE(vnet1_->ipv4_active());
    EXPECT_FALSE(vnet1_->l2_active());
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet(vnet1_->vrf()->GetName(), vnet1_->primary_ip_addr(), 32)
                == NULL);

    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();
    EXPECT_TRUE(entry->link_up_);
    EXPECT_TRUE(vnet1_->ipv4_active());
    EXPECT_TRUE(vnet1_->l2_active());

    EXPECT_TRUE(RouteGet(vnet1_->vrf()->GetName(), vnet1_->primary_ip_addr(), 32)
                != NULL);
}

// Delete the config node for interface, and verify interface NH are deleted
// Add the config node and verify the interface NH are readded
TEST_F(TestVnswIf, EcmpActivateDeactivate_1) {
    struct PortInfo input[] = {
        {"vnet3", 3, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
        {"vnet4", 4, "1.1.1.10", "00:00:00:01:01:02", 1, 2},
        {"vnet5", 5, "1.1.1.10", "00:00:00:01:01:03", 1, 3},
    };

    client->Reset();
    client->WaitForIdle();
    // Create ports with ECMP
    CreateVmportWithEcmp(input, 3);
    client->WaitForIdle();
    client->WaitForIdle();
    // Ensure all interface are active
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortActive(input, 2));
    InterfaceEvent(true, "vnet3", 0);
    InterfaceEvent(true, "vnet4", 0);
    InterfaceEvent(true, "vnet5", 0);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 1));
    EXPECT_TRUE(VmPortInactive(input, 2));

    InterfaceEvent(true, "vnet3", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet4", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet5", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();

    // Ensure ECMP is created
    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);

    const CompositeNH *nh;
    WAIT_FOR(100, 1000, ((nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop())) != NULL));
    EXPECT_EQ(nh->ActiveComponentNHCount(), 3U);

    // Set oper-state of vnet3 down. We should have ECMP with 2 NH
    InterfaceEvent(true, "vnet3", 0);
    WAIT_FOR(100, 100, (VmPortActive(input, 0) == false));
    client->WaitForIdle();
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_EQ(nh->ActiveComponentNHCount(), 2U);

    // Set oper-state of vnet4 down. We should have non-ECMP route
    InterfaceEvent(true, "vnet4", 0);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 1) == false));
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh == NULL);

    // Set oper-state of vnet5 down. Route should be deleted
    InterfaceEvent(true, "vnet5", 0);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 2) == false));
    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt == NULL);

    // Set oper-state of vnet3 and vnet4 up. We should have ECMP os 2 NH
    InterfaceEvent(true, "vnet3", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet4", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 0)));
    WAIT_FOR(100, 100, (VmPortActive(input, 1)));

    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_EQ(nh->ActiveComponentNHCount(), 2U);

    //Clean up
    DeleteVmportEnv(input, 3, true);
    InterfaceEvent(false, "vnet5", 0);
    InterfaceEvent(false, "vnet4", 0);
    InterfaceEvent(false, "vnet3", 0);
    client->WaitForIdle();
}

// Agent adds a link-local route if its deleted in kernel
TEST_F(TestVnswIf, linklocal_1) {
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));

    uint32_t count = vnswif_->ll_add_count();
    // Delete mdata-ip. agent should add it again
    RouteEvent(false, "vnet1", vnet1_->mdata_ip_addr(), 32,
               VnswInterfaceListener::kVnswRtmProto);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->ll_add_count() >= (count + 1)));
}

// Agent deletes a link-local route from kernel if its not valid address
TEST_F(TestVnswIf, linklocal_2) {
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));

    uint32_t count = vnswif_->ll_del_count();
    // Add a dummy route with kVnswRtmProto as protocol. Agent should delete it
    RouteEvent(true, "vnet1", Ip4Address::from_string("169.254.1.1"), 32,
               VnswInterfaceListener::kVnswRtmProto);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->ll_del_count() >= (count + 1)));
}

TEST_F(TestVnswIf, FabricIpam) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
        {"2.2.2.0", 24, "2.2.2.10"},
        {"10.1.1.0", 24, "10.1.1.10"}
    };

    uint32_t id = Interface::kInvalidIndex;
    const Interface *intf = agent_->vhost_interface();
    if (intf) {
        id = intf->id();
    }
    SetSeen(agent_->vhost_interface_name(), true, id);
    client->WaitForIdle();

    uint32_t del_count = vnswif_->ll_del_count();
    uint32_t count = vnswif_->ll_add_count();

    AddVn(agent_->fabric_vn_name().c_str(), 100);
    AddVn("test_vn1", 101);
    AddIPAM("test_vn1", ipam_info, 3);
    client->WaitForIdle();

    EXPECT_TRUE(vnswif_->ll_add_count() == count);
    //VN enabled for fabric forwarding
    //all the IPAM routes should be added
    AddLink("virtual-network", "test_vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    //Only 2 routes get added because 10.1.1.0/24 overlaps
    //with vhost resolve route and shouldnt get added
    EXPECT_TRUE(vnswif_->ll_add_count() == count + 2);


    //Add another VN with same IPAM, no changes as routes
    //are already present
    AddVn("test_vn2", 102);
    AddIPAM("test_vn2", ipam_info, 3);
    AddLink("virtual-network", "test_vn2", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    //No change
    EXPECT_TRUE(vnswif_->ll_add_count() == count + 2);

    //Disabled test_vn2 from forwarding no change as
    //test_vn1 is enabled for forwarding still
    DelLink("virtual-network", "test_vn2", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(vnswif_->ll_add_count() == count + 2);
    EXPECT_TRUE(vnswif_->ll_del_count() == del_count);

    //Delete the IPAM
    DelIPAM("test_vn1");
    DelIPAM("test_vn2");
    client->WaitForIdle();

    EXPECT_TRUE(vnswif_->ll_del_count() == del_count + 2);

    DelLink("virtual-network", "test_vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());

    DelVn("test_vn1");
    DelVn("test_vn2");
    DelVn(agent_->fabric_vn_name().c_str());
    client->WaitForIdle();
    InterfaceEvent(false, agent_->vhost_interface_name(), 0);
    ResetSeen(agent_->vhost_interface_name());
    client->WaitForIdle();
}

TEST_F(TestVnswIf, FabricIpamLinkFlap) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"}
    };

    uint32_t count = vnswif_->ll_add_count();
    uint32_t del_count = vnswif_->ll_del_count();

    AddVn(agent_->fabric_vn_name().c_str(), 100);
    AddVn("test_vn1", 101);
    AddIPAM("test_vn1", ipam_info, 1);
    client->WaitForIdle();

    EXPECT_TRUE(vnswif_->ll_add_count() == count);
    //VN enabled for fabric forwarding
    //all the IPAM routes should be added
    AddLink("virtual-network", "test_vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(vnswif_->ll_add_count() == count + 1);

    InterfaceEvent(true, agent_->vhost_interface_name(), 0);
    client->WaitForIdle();

    InterfaceEvent(true, agent_->vhost_interface_name(), (IFF_UP|IFF_RUNNING));
    EXPECT_TRUE(vnswif_->ll_add_count() >= count + 3);

    //Delete the IPAM
    DelIPAM("test_vn1");
    DelLink("virtual-network", "test_vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());
    DelVn("test_vn1");
    DelVn(agent_->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vnswif_->ll_del_count() == del_count + 1);

    InterfaceEvent(false, agent_->vhost_interface_name(), 0);
    ResetSeen(agent_->vhost_interface_name());
    client->WaitForIdle();
}

TEST_F(TestVnswIf, InterfaceStatus) {
    InterfaceEvent(true, "test_phy test_phy_drv 0", 4163, VR_FABRIC);
    client->WaitForIdle();
    PhysicalInterface *pintf =
        EthInterfaceGet(agent_->fabric_interface_name().c_str());
    EXPECT_TRUE(pintf != NULL);
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");
    EXPECT_TRUE(pintf->get_os_params().os_oper_state_);

    InterfaceEvent(false, "test_phy test_phy_drv 0", 4163, VR_FABRIC);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");

    InterfaceEvent(true, "test_phy test_phy_drv 0", 3, VR_FABRIC);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");
    EXPECT_FALSE(pintf->get_os_params().os_oper_state_);

    InterfaceEvent(false, "test_phy test_phy_drv 0", 3, VR_FABRIC);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");

    InterfaceEvent(true, "test_phy_slave test_phy_slave_drv 0", 4163,
            VR_BOND_SLAVES);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");

    EXPECT_TRUE(getIntfStatus(pintf, "test_phy_slave"));

    InterfaceEvent(false, "test_phy_slave test_phy_slave_drv 0", 4163,
            VR_BOND_SLAVES);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");

    InterfaceEvent(true, "test_phy_slave test_phy_slave_drv 0", 3,
            VR_BOND_SLAVES);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");
    EXPECT_FALSE(getIntfStatus(pintf, "test_phy_slave"));

    InterfaceEvent(false, "test_phy_slave test_phy_slave_drv 0", 3,
            VR_BOND_SLAVES);
    client->WaitForIdle();
    EXPECT_TRUE(pintf->vrf() != NULL);
    EXPECT_TRUE(pintf->vrf()->GetName() ==
            "default-domain:default-project:ip-fabric:__default__");

    delTestPhysicalIntfFromMap(pintf, "test_phy");
    delTestPhysicalIntfFromMap(pintf, "test_phy_slave");
}

class SetupTask : public Task {
    public:
        SetupTask() :
            Task((TaskScheduler::GetInstance()->
                  GetTaskId("db::DBTable")), 0) {
        }

        virtual bool Run() {
            Agent::GetInstance()->ksync()->vnsw_interface_listner()->Shutdown();
            return true;
        }
    std::string Description() const { return "SetupTask"; }
};


int main(int argc, char *argv[]) {
    GETUSERARGS();

    strcpy(init_file, "controller/src/vnsw/agent/test/vnswa_no_ip_cfg.ini");
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->ksync()->VnswInterfaceListenerInit();
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    SetupTask * task = new SetupTask();
    TaskScheduler::GetInstance()->Enqueue(task);
    client->WaitForIdle();
    delete client;
    return ret;
}
