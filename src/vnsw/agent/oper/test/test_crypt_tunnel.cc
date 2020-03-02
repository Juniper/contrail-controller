/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include "oper/crypt_tunnel.h"

#define VNSW_CRYPT_CONFIG_FILE "controller/src/vnsw/agent/test/vnswa_crypt_cfg.ini"

void RouterIdDepInit(Agent *agent) {
}

class CryptTunnelConfigTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        PhysicalInterface::CreateReq(agent->interface_table(),
                                     "ipsec0", agent->fabric_vrf_name(),
                                     PhysicalInterface::FABRIC,
                                     PhysicalInterface::ETHERNET, false,
                                     boost::uuids::nil_uuid(), Ip4Address(0),
                                     Interface::TRANSPORT_ETHERNET);
    }

    bool WaitForAWhile(time_t target) {
        time_t now = time(NULL);
        return now >= target;
    }

    virtual void TearDown() {
    }

    CryptTunnelEntry *FindCryptTunnelEntry(const std::string &remote_ip) {
        CryptTunnelTable *table = agent->crypt_tunnel_table();
        return table->Find(remote_ip);
    }

protected:
    static const int kTimeoutSeconds = 15;
    Agent *agent;
};

struct CryptTunnel {
    std::string remote_ip;
    bool crypt;
};

TEST_F(CryptTunnelConfigTest, Basic) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("2.2.2.10"));
    // Create two tunnels
    struct EncryptTunnelEndpoint endpoints[] = {
        {"2.2.2.10"},
        {"2.2.2.11"}
    };
    unsigned int num_endpoints = sizeof(endpoints)/sizeof(EncryptTunnelEndpoint);
    EXPECT_TRUE(agent->crypt_tunnel_table()->Size() == 0);

    // Create VR to VR encryption, add the global vrouter configration
    AddEncryptRemoteTunnelConfig(endpoints, num_endpoints, "all");
    client->WaitForIdle();
    EXPECT_EQ(agent->crypt_tunnel_table()->Size(), num_endpoints-1);
    CryptTunnelEntry *entry;
    entry = agent->crypt_tunnel_table()->Find(endpoints[1].ip);
    EXPECT_TRUE(entry != NULL);

    // Check for encryption
    EXPECT_TRUE(entry->GetVRToVRCrypt() == true);
    // Tunnel should be available
    client->WaitForIdle();
    EXPECT_EQ(entry->GetTunnelAvailable(), true);

    // Change and check for encryption false
    AddEncryptRemoteTunnelConfig(endpoints, num_endpoints, "none");
    client->WaitForIdle();
    EXPECT_EQ(entry->GetTunnelAvailable(), true);
    EXPECT_TRUE((entry = agent->crypt_tunnel_table()->Find(endpoints[1].ip)) != NULL);
    EXPECT_TRUE((entry->GetVRToVRCrypt() == false));

    // Change and check for encryption true
    AddEncryptRemoteTunnelConfig(endpoints, num_endpoints, "all");
    client->WaitForIdle();
    EXPECT_TRUE((entry = agent->crypt_tunnel_table()->Find(endpoints[1].ip)) != NULL);
    EXPECT_TRUE((entry->GetVRToVRCrypt() == true));

    // Delete configuration
    DeleteGlobalVrouterConfig();

}

int main(int argc, char **argv) {

    GETUSERARGS();
    client = TestInit(VNSW_CRYPT_CONFIG_FILE, ksync_init, false, false, false);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    usleep(10000);

    TestShutdown();
    delete client;
    return ret;
}
