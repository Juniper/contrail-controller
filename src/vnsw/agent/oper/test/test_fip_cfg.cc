/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cmn/agent_cmn.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"

using namespace std;

class FipCfg : public ::testing::Test {
public:
    FipCfg() :
        agent_(Agent::GetInstance()), vmi1_(NULL) {
        }

    virtual ~FipCfg() { }

    virtual void SetUp() {
        struct PortInfo input[] = {
            {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        };
        CreateVmportEnv(input, 1);

        AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1");
        AddFloatingIp("fip2", 2, "2.2.2.2", "1.1.1.1");
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIpPool("fip-pool2", 2);
        AddVn("fip-vn1", 21);
        AddVn("fip-vn2", 22);
        AddVrf("fip-vrf1");
        AddVrf("fip-vrf2");
        AddInstanceIp("fip-iip1", 21, "10.1.1.1");
        AddInstanceIp("fip-iip2", 22, "10.1.1.2");
        client->WaitForIdle();
        WAIT_FOR(100, 100, VmPortFind(1));
        vmi1_ = static_cast<VmInterface *>(VmPortGet(1));
    }

    virtual void TearDown() {
        client->WaitForIdle();
        struct PortInfo input[] = {
            {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        };
        DelFloatingIp("fip1");
        DelFloatingIp("fip2");
        DelFloatingIpPool("fip-pool1");
        DelFloatingIpPool("fip-pool2");
        DelVn("fip-vn1");
        DelVn("fip-vn2");
        DelVrf("fip-vrf1");
        DelVrf("fip-vrf2");
        DelInstanceIp("fip-iip1");
        DelInstanceIp("fip-iip2");

        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VmPortFindRetDel(1) == false));
    }

    void AddFipFipPool(const char *fip, const char *pool) {
        AddLink("floating-ip", fip, "floating-ip-pool", pool);
        client->WaitForIdle();
    }

    void DelFipFipPool(const char *fip, const char *pool) {
        DelLink("floating-ip", fip, "floating-ip-pool", pool);
        client->WaitForIdle();
    }
    void AddVmiFip(const char *vmi, const char *fip) {
        AddLink("virtual-machine-interface", vmi, "floating-ip", fip);
        client->WaitForIdle();
    }

    void DelVmiFip(const char *vmi, const char *fip) {
        DelLink("virtual-machine-interface", vmi, "floating-ip", fip);
        client->WaitForIdle();
    }

    void AddFipPoolVn(const char *pool, const char *vn) {
        AddLink("floating-ip-pool", pool, "virtual-network", vn);
        client->WaitForIdle();
    }

    void DelFipPoolVn(const char *pool, const char *vn) {
        DelLink("floating-ip-pool", pool, "virtual-network", vn);
        client->WaitForIdle();
    }

    void AddVnVrf(const char *vn, const char *vrf) {
        AddLink("virtual-network", vn, "routing-instance", vrf);
        client->WaitForIdle();
    }

    void DelVnVrf(const char *vn, const char *vrf) {
        DelLink("virtual-network", vn, "routing-instance", vrf);
        client->WaitForIdle();
    }

    void AddFipIip(const char *fip, const char *iip) {
        AddLink("floating-ip", fip, "instance-ip", iip);
        client->WaitForIdle();
    }

    void DelFipIip(const char *fip, const char *iip) {
        DelLink("floating-ip", fip, "instance-ip", iip);
        client->WaitForIdle();
    }

    void AddIipVn(const char *iip, const char *vn) {
        AddLink("instance-ip", iip, "virtual-network", vn);
        client->WaitForIdle();
    }

    void DelIipVn(const char *iip, const char *vn) {
        DelLink("instance-ip",  iip, "virtual-network", vn);
        client->WaitForIdle();
    }

    void AddFip(uint8_t sequence[4]) {
        for (int i = 0; i < 4; i++) {
            if (sequence[i] == 0) {
                AddVmiFip("vmi1", "fip1");
            }
            if (sequence[i] == 1) {
                AddFipFipPool("fip1", "fip-pool1");
            }
            if (sequence[i] == 2) {
                AddFipPoolVn("fip-pool1", "fip-vn1");
            }
            if (sequence[i] == 3) {
                AddVnVrf("fip-vn1", "fip-vrf1");
            }
        }
        WAIT_FOR(100, 100, (vmi1_->GetFloatingIpCount() == 1));
    }

    void DelFip(uint8_t sequence[4]) {
        for (int i = 0; i < 4; i++) {
            if (sequence[i] == 0) {
                DelVmiFip("vmi1", "fip1");
            }
            if (sequence[i] == 1) {
                DelFipFipPool("fip1", "fip-pool1");
            }
            if (sequence[i] == 2) {
                DelFipPoolVn("fip-pool1", "fip-vn1");
            }
            if (sequence[i] == 3) {
                DelVnVrf("fip-vn1", "fip-vrf1");
            }
            WAIT_FOR(100, 100, (vmi1_->GetFloatingIpCount() == 0));
        }
    }

    void AddFipIip(uint8_t sequence[4]) {
        for (int i = 0; i < 4; i++) {
            if (sequence[i] == 0) {
                AddVmiFip("vmi1", "fip1");
            }
            if (sequence[i] == 1) {
                AddFipIip("fip1", "fip-iip1");
            }
            if (sequence[i] == 2) {
                AddIipVn("fip-iip1", "fip-vn1");
            }
            if (sequence[i] == 3) {
                AddVnVrf("fip-vn1", "fip-vrf1");
            }
        }
        WAIT_FOR(100, 100, (vmi1_->GetFloatingIpCount() == 1));
    }

    void DelFipIip(uint8_t sequence[4]) {
        for (int i = 0; i < 4; i++) {
            if (sequence[i] == 0) {
                DelVmiFip("vmi1", "fip1");
            }
            if (sequence[i] == 1) {
                DelFipIip("fip1", "fip-iip1");
            }
            if (sequence[i] == 2) {
                DelIipVn("fip-iip1", "fip-vn1");
            }
            if (sequence[i] == 3) {
                DelVnVrf("fip-vn1", "fip-vrf1");
            }
        }
        WAIT_FOR(100, 100, (vmi1_->GetFloatingIpCount() == 0));
    }

protected:
    Agent *agent_;
    VmInterface *vmi1_;
};

// Validate basic floating-ip configuration
TEST_F(FipCfg, Fip_Cfg_Seq_0) {
    uint8_t sequence[6][4] = {
        {0, 1, 2, 3},
        {0, 1, 3, 2},
        {0, 2, 1, 3},
        {0, 2, 3, 1},
        {0, 3, 1, 2},
        {0, 3, 2, 1}
    };

    for (int i = 0; i < 6; i++) {
        AddFip(sequence[i]);
        DelFip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_Cfg_Seq_1) {
    uint8_t sequence[6][4] = {
        {1, 0, 2, 3},
        {1, 0, 3, 2},
        {1, 2, 0, 3},
        {1, 2, 3, 0},
        {1, 3, 0, 2},
        {1, 3, 2, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFip(sequence[i]);
        DelFip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_Cfg_Seq_2) {
    uint8_t sequence[6][4] = {
        {2, 0, 1, 3},
        {2, 0, 3, 1},
        {2, 1, 0, 3},
        {2, 1, 3, 0},
        {2, 3, 0, 1},
        {2, 3, 1, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFip(sequence[i]);
        DelFip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_Cfg_Seq_3) {
    uint8_t sequence[6][4] = {
        {3, 0, 1, 2},
        {3, 0, 2, 1},
        {3, 1, 0, 2},
        {3, 1, 2, 0},
        {3, 2, 0, 1},
        {3, 2, 1, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFip(sequence[i]);
        DelFip(sequence[i]);
    }
}

// Validate basic floating-ip configuration
TEST_F(FipCfg, Fip_Iip_Cfg_Seq_0) {
    uint8_t sequence[6][4] = {
        {0, 1, 2, 3},
        {0, 1, 3, 2},
        {0, 2, 1, 3},
        {0, 2, 3, 1},
        {0, 3, 1, 2},
        {0, 3, 2, 1}
    };

    for (int i = 0; i < 6; i++) {
        AddFipIip(sequence[i]);
        DelFipIip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_Iip_Cfg_Seq_1) {
    uint8_t sequence[6][4] = {
        {1, 0, 2, 3},
        {1, 0, 3, 2},
        {1, 2, 0, 3},
        {1, 2, 3, 0},
        {1, 3, 0, 2},
        {1, 3, 2, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFipIip(sequence[i]);
        DelFipIip(sequence[i]);
    }
 }

TEST_F(FipCfg, Fip_Iip_Cfg_Seq_2) {
    uint8_t sequence[6][4] = {
        {2, 0, 1, 3},
        {2, 0, 3, 1},
        {2, 1, 0, 3},
        {2, 1, 3, 0},
        {2, 3, 0, 1},
        {2, 3, 1, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFipIip(sequence[i]);
        DelFipIip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_Iip_Cfg_Seq_3) {
    uint8_t sequence[6][4] = {
        {3, 0, 1, 2},
        {3, 0, 2, 1},
        {3, 1, 0, 2},
        {3, 1, 2, 0},
        {3, 2, 0, 1},
        {3, 2, 1, 0}
    };

    for (int i = 0; i < 6; i++) {
        AddFipIip(sequence[i]);
        DelFipIip(sequence[i]);
    }
}

TEST_F(FipCfg, Fip_PortMap_1) {
    uint8_t sequence[]  = {1, 0, 2, 3};
    AddFipIip(sequence);
    WAIT_FOR(100, 100, (vmi1_->FloatingIpCount() == 1));
    DelFipIip(sequence);
}

TEST_F(FipCfg, Fip_PortMap_Add) {
    uint8_t sequence[]  = {1, 0, 2, 3};
    AddFipIip(sequence);
    DelFloatingIp("fip1");
    client->WaitForIdle();
    WAIT_FOR(100, 100, (vmi1_->FloatingIpCount() == 0));

    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", NULL, true, 100, 200);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (vmi1_->FloatingIpCount() == 1));

    const VmInterface::FloatingIpSet &fip_list =
        vmi1_->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    EXPECT_TRUE(it != fip_list.end());

    EXPECT_TRUE(it->port_map_enabled());
    EXPECT_EQ(4U, it->PortMappingSize());
    EXPECT_EQ(1100, it->GetDstPortMap(IPPROTO_TCP, 100));
    EXPECT_EQ(1200, it->GetDstPortMap(IPPROTO_TCP, 200));
    EXPECT_EQ(100, it->GetSrcPortMap(IPPROTO_TCP, 1100));
    EXPECT_EQ(200, it->GetSrcPortMap(IPPROTO_TCP, 1200));
    EXPECT_EQ(1100, it->GetDstPortMap(IPPROTO_UDP, 100));
    EXPECT_EQ(1200, it->GetDstPortMap(IPPROTO_UDP, 200));
    EXPECT_EQ(-1, it->GetDstPortMap(IPPROTO_TCP, 101));
    EXPECT_EQ(-1, it->GetDstPortMap(IPPROTO_TCP, 201));
    EXPECT_EQ(-1, it->GetDstPortMap(IPPROTO_UDP, 101));
    EXPECT_EQ(-1, it->GetDstPortMap(IPPROTO_UDP, 201));
    DelFipIip(sequence);
}

TEST_F(FipCfg, Fip_PortMap_Change_1) {
    uint8_t sequence[]  = {1, 0, 2, 3};
    AddFipIip(sequence);
    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", NULL, true, 100, 200);
    client->WaitForIdle();

    const VmInterface::FloatingIpSet &fip_list =
        vmi1_->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    EXPECT_TRUE(it != fip_list.end());

    EXPECT_TRUE(it->port_map_enabled());
    DelFipIip(sequence);
}

TEST_F(FipCfg, Fip_PortMap_Change_2) {
    uint8_t sequence[]  = {1, 0, 2, 3};
    AddFipIip(sequence);
    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", NULL, true, 100, 200);
    client->WaitForIdle();
    const VmInterface::FloatingIpSet &fip_list =
        vmi1_->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    EXPECT_TRUE(it != fip_list.end());
    EXPECT_TRUE(it->port_map_enabled());

    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", NULL, true, 300, 400);
    client->WaitForIdle();

    it = fip_list.begin();
    EXPECT_TRUE(it != fip_list.end());

    EXPECT_EQ(-1, it->GetSrcPortMap(IPPROTO_TCP, 100));
    EXPECT_EQ(-1, it->GetSrcPortMap(IPPROTO_TCP, 200));
    EXPECT_EQ(1300, it->GetDstPortMap(IPPROTO_TCP, 300));
    EXPECT_EQ(1400, it->GetDstPortMap(IPPROTO_TCP, 400));

    EXPECT_EQ(300, it->GetSrcPortMap(IPPROTO_TCP, 1300));
    EXPECT_EQ(400, it->GetSrcPortMap(IPPROTO_TCP, 1400));
    DelFipIip(sequence);
}

TEST_F(FipCfg, Fip_Direction_1) {
    uint8_t sequence[]  = {0, 1, 2, 3};
    AddFipIip(sequence);
    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1");
    client->WaitForIdle();
    const VmInterface::FloatingIpSet &fip_list =
        vmi1_->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    EXPECT_TRUE(it != fip_list.end());
    EXPECT_TRUE(it->port_map_enabled() == false);
    EXPECT_EQ(it->direction(), VmInterface::FloatingIp::DIRECTION_BOTH);
    EXPECT_TRUE(it->AllowDNat());
    EXPECT_TRUE(it->AllowSNat());

    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", "INGRESS");
    client->WaitForIdle();
    EXPECT_EQ(it->direction(), VmInterface::FloatingIp::DIRECTION_INGRESS);
    EXPECT_TRUE(it->AllowDNat());
    EXPECT_FALSE(it->AllowSNat());

    AddFloatingIp("fip1", 1, "2.2.2.1", "1.1.1.1", "EGRESS");
    client->WaitForIdle();
    EXPECT_EQ(it->direction(), VmInterface::FloatingIp::DIRECTION_EGRESS);
    EXPECT_FALSE(it->AllowDNat());
    EXPECT_TRUE(it->AllowSNat());

    DelFipIip(sequence);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
