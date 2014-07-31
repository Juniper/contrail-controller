/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#if defined(__FreeBSD__)
#include <sys/types.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <boost/uuid/string_generator.hpp>

#include <io/event_manager.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"

#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "pkt/pkt_handler.h"

#include "vr_interface.h"
#include "vr_types.h"

#include "test/test_cmn_util.h"
#include "test/pkt_gen.h"
#include <controller/controller_vrf_export.h>

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

class TestVnswIf : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        vnswif_ = agent_->ksync()->vnsw_interface_listner();

        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        vnet1_ = static_cast<VmInterface *>(VmPortGet(1));
        vnet2_ = static_cast<VmInterface *>(VmPortGet(2));
    }

    virtual void TearDown() {
        InterfaceEvent(false, "vnet1", 0);
        InterfaceEvent(false, "vnet2", 0);
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();
        EXPECT_EQ(0, vnswif_->GetHostInterfaceCount());
    }

    void SetSeen(const string &ifname, bool oper) {
        vnswif_->SetSeen(agent_->vhost_interface_name(), true);
    }

    void ResetSeen(const string &ifname) {
        vnswif_->ResetSeen(agent_->vhost_interface_name(), true);
    }

    void InterfaceEvent(bool add, const string &ifname, uint32_t flags) {
        VnswInterfaceListener::Event::Type type;
        if (add) {
            type = VnswInterfaceListener::Event::ADD_INTERFACE;
        } else {
            type = VnswInterfaceListener::Event::DEL_INTERFACE;
        }
        vnswif_->Enqueue(new VnswInterfaceListener::Event(type, ifname, flags));
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
                                                          addr, 24, 0));
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
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    EXPECT_FALSE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));
}

// Validate that link-local address is added back if route deleted from kernel
TEST_F(TestVnswIf, host_route_del) {
    uint32_t count = vnswif_->netlink_ll_add_count();
    VnswInterfaceListener::Event *event = 
        new VnswInterfaceListener::Event(VnswInterfaceListener::Event::DEL_ROUTE,
                                         vnet1_->mdata_ip_addr(), 32, "",
                                         Ip4Address(0),
                                         VnswInterfaceListener::kVnswRtmProto,
                                         0);
    vnswif_->Enqueue(event);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->netlink_ll_add_count() > count));
}

// On link flap of vhost, all link-local must be written again
TEST_F(TestVnswIf, vhost_link_flap) {
    SetSeen(agent_->vhost_interface_name(), true);
    client->WaitForIdle();

    // Set link-state down
    InterfaceEvent(true, agent_->vhost_interface_name(), 0);

    // bring up link of vhost0
    uint32_t count = vnswif_->netlink_ll_add_count();

    // Set link-state up
    InterfaceEvent(true, agent_->vhost_interface_name(),
                   (IFF_UP|IFF_RUNNING));

    // Ensure routes are synced to host
    WAIT_FOR(1000, 100, (vnswif_->netlink_ll_add_count() >= (count + 2)));

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
    EXPECT_FALSE(entry->link_up_);

    InterfaceEvent(true, "vnet1", 0);
    EXPECT_TRUE(entry->host_seen_);

    InterfaceEvent(true, "vnet1", IFF_UP);
    EXPECT_FALSE(entry->link_up_);

    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    EXPECT_TRUE(entry->link_up_);
}

// vhost-ip address update
TEST_F(TestVnswIf, vhost_addr_1) {
    SetSeen(agent_->vhost_interface_name(), true);
    client->WaitForIdle();

    VnswInterfaceListener::HostInterfaceEntry *entry =
        vnswif_->GetHostInterfaceEntry(agent_->vhost_interface_name());
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL)
        return;

    const InetInterface *vhost = 
        static_cast<const InetInterface *>(agent_->vhost_interface());
    string vhost_ip = vhost->ip_addr().to_string();

    // vnswif module does not know IP address yet
    EXPECT_EQ(entry->addr_.to_ulong(), 0);

    InterfaceAddressEvent(true, agent_->vhost_interface_name(), "1.1.1.1");

    // Validate address in entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "1.1.1.1");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 0);

    InterfaceAddressEvent(true, agent_->vhost_interface_name(), "2.2.2.2");

    // Validate address in local entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "2.2.2.2");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 0);

    // Ensure there is no change vhost-ip
    EXPECT_STREQ(vhost->ip_addr().to_string().c_str(), vhost_ip.c_str());

    InterfaceAddressEvent(false, agent_->vhost_interface_name(), "1.1.1.1");

    // Validate address in local entry
    EXPECT_STREQ(entry->addr_.to_string().c_str(), "0.0.0.0");

    // Message not sent to kernel since vhost already has IP
    EXPECT_EQ(vnswif_->vhost_update_count(), 0);

    // Ensure there is no change vhost-ip
    EXPECT_STREQ(vhost->ip_addr().to_string().c_str(), vhost_ip.c_str());

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
    EXPECT_TRUE(RouteGet(vnet1_->vrf()->GetName(), vnet1_->ip_addr(), 32) 
                == NULL);

    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();
    EXPECT_TRUE(entry->link_up_);
    EXPECT_TRUE(vnet1_->ipv4_active());
    EXPECT_TRUE(vnet1_->l2_active());

    EXPECT_TRUE(RouteGet(vnet1_->vrf()->GetName(), vnet1_->ip_addr(), 32) 
                != NULL);
}

// Delete the config node for interface, and verify interface NH are deleted
// Add the config node and verify the interface NH are readded
TEST_F(TestVnswIf, EcmpActivateDeactivate_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.10", "00:00:00:01:01:02", 1, 2},
        {"vnet3", 3, "1.1.1.10", "00:00:00:01:01:03", 1, 3},
    };

    client->Reset();
    // Create ports with ECMP
    CreateVmportWithEcmp(input, 3);
    client->WaitForIdle();
    client->WaitForIdle();
    // Ensure all interface are active
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortActive(input, 2));
    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet2", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet3", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();

    // Ensure ECMP is created
    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);

    const CompositeNH *nh;
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_EQ(nh->ActiveComponentNHCount(), 3);

    // Set oper-state of vnet1 down. We should have ECMP with 2 NH
    InterfaceEvent(true, "vnet1", 0);
    WAIT_FOR(100, 100, (VmPortActive(input, 0) == false));
    client->WaitForIdle();
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_EQ(nh->ActiveComponentNHCount(), 2);

    // Set oper-state of vnet2 down. We should have non-ECMP route
    InterfaceEvent(true, "vnet2", 0);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 1) == false));
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh == NULL);

    // Set oper-state of vnet3 down. Route should be deleted
    InterfaceEvent(true, "vnet3", 0);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 2) == false));
    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt == NULL);

    // Set oper-state of vnet1 and vnet2 up. We should have ECMP os 2 NH
    InterfaceEvent(true, "vnet1", (IFF_UP|IFF_RUNNING));
    InterfaceEvent(true, "vnet2", (IFF_UP|IFF_RUNNING));
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmPortActive(input, 0)));
    WAIT_FOR(100, 100, (VmPortActive(input, 1)));

    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_EQ(nh->ActiveComponentNHCount(), 2);

    //Clean up
    DeleteVmportEnv(input, 3, true);
    InterfaceEvent(false, "vnet3", 0);
    client->WaitForIdle();
}

// Agent adds a link-local route if its deleted in kernel
TEST_F(TestVnswIf, linklocal_1) {
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));

    uint32_t count = vnswif_->netlink_ll_add_count();
    // Delete mdata-ip. agent should add it again
    RouteEvent(false, "vnet1", vnet1_->mdata_ip_addr(), 32, 
               VnswInterfaceListener::kVnswRtmProto);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->netlink_ll_add_count() >= (count + 1)));
}

// Agent deletes a link-local route from kernel if its not valid address
TEST_F(TestVnswIf, linklocal_2) {
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet1_->mdata_ip_addr()));
    EXPECT_TRUE(vnswif_->IsValidLinkLocalAddress(vnet2_->mdata_ip_addr()));

    uint32_t count = vnswif_->netlink_ll_del_count();
    // Add a dummy route with kVnswRtmProto as protocol. Agent should delete it
    RouteEvent(true, "vnet1", Ip4Address::from_string("169.254.1.1"), 32, 
               VnswInterfaceListener::kVnswRtmProto);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->netlink_ll_del_count() >= (count + 1)));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->ksync()->VnswInterfaceListenerInit();
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
