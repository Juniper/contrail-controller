/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
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
        EXPECT_TRUE(VmPortActive(input, 0));
        vnet1_ = static_cast<VmInterface *>(VmPortGet(1));
        vnet2_ = static_cast<VmInterface *>(VmPortGet(2));
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();
    }

    void SetSeen(const string &ifname, bool oper) {
        vnswif_->SetSeen(agent_->vhost_interface_name(), true);
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
    uint32_t count = vnswif_->netlink_msg_tx_count();
    VnswInterfaceListener::Event *event = 
        new VnswInterfaceListener::Event(VnswInterfaceListener::Event::DEL_ROUTE,
                                         vnet1_->mdata_ip_addr(), 32, "",
                                         Ip4Address(0),
                                         VnswInterfaceListener::kVnswRtmProto,
                                         0);
    vnswif_->Enqueue(event);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (vnswif_->netlink_msg_tx_count() > count));
}

// On link flap of vhost, all link-local must be written again
TEST_F(TestVnswIf, vhost_link_flap) {
    SetSeen(agent_->vhost_interface_name(), true);
    client->WaitForIdle();

    // Set link-state down
    InterfaceEvent(true, agent_->vhost_interface_name(), 0);

    // bring up link of vhost0
    uint32_t count = vnswif_->netlink_msg_tx_count();

    // Set link-state up
    InterfaceEvent(true, agent_->vhost_interface_name(),
                   (IFF_UP|IFF_RUNNING));

    // Ensure routes are synced to host
    WAIT_FOR(1000, 100, (vnswif_->netlink_msg_tx_count() >= (count + 2)));
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
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->ksync()->VnswInterfaceListenerInit();
    Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
