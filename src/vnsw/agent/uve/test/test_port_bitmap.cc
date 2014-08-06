/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>
#include <uve/test/vn_uve_table_test.h>
#include <uve/test/vm_uve_table_test.h>
#include <uve/test/vrouter_uve_entry_test.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include "xmpp/test/xmpp_test_util.h"

using namespace std;

const std::string dest_vn_name("VN2");
void RouterIdDepInit(Agent *agent) {
}

class UvePortBitmapTest : public ::testing::Test {
public:
    UvePortBitmapTest() {};
    virtual ~UvePortBitmapTest() {};

    virtual void SetUp() {
        struct PortInfo input[] = {
            {"vnet11", 1, "1.1.1.1", "00:00:00:00:01:01", 1, 1},
            {"vnet12", 2, "1.1.1.2", "00:00:00:00:01:02", 1, 2},
            {"vnet13", 3, "1.1.1.3", "00:00:00:00:01:01", 1, 3},
            {"vnet21", 4, "2.2.2.1", "00:00:00:00:02:01", 1, 4},
            {"vnet22", 5, "2.2.2.2", "00:00:00:00:02:01", 1, 5},
        };

        uve = static_cast<VrouterUveEntryTest *>
            (Agent::GetInstance()->uve()->vrouter_uve_entry());
        CreateVmportEnv(input, 5);
        client->WaitForIdle();
        //Don't expect bitmaps to be reset on start of each test
        //Previous tests might have set some value.
        //EXPECT_TRUE(ValidateVrouter(0xFF, 0xFFFF, 0xFFFF));
    }

    virtual void TearDown() {
        struct PortInfo input[] = {
            {"vnet11", 1, "1.1.1.1", "00:00:00:00:01:01", 1, 1},
            {"vnet12", 2, "1.1.1.2", "00:00:00:00:01:02", 1, 2},
            {"vnet13", 3, "1.1.1.3", "00:00:00:00:01:01", 1, 3},
            {"vnet21", 4, "2.2.2.1", "00:00:00:00:02:01", 1, 4},
            {"vnet22", 5, "2.2.2.2", "00:00:00:00:02:01", 1, 5},
        };
        DeleteVmportEnv(input, 5, true);
        client->WaitForIdle();
        //We don't reset bitmaps on removal of flows
        //EXPECT_TRUE(ValidateVrouter(0xFF, 0xFFFF, 0xFFFF));
        LOG(DEBUG, "Vrf table size " << Agent::GetInstance()->vrf_table()->Size());
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->vrf_table()->Size() == 1));
    }

    bool ValidateBmap(const PortBucketBitmap &port, uint8_t proto,
                      uint16_t sport, uint16_t dport) {
        std::vector<uint32_t> bmap;
        bool ret = true;

        int idx = sport / L4PortBitmap::kBucketCount;
        int sport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int sport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        idx = dport / L4PortBitmap::kBucketCount;
        int dport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int dport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        if (proto == IPPROTO_TCP) {
            bmap = port.get_tcp_sport_bitmap();
            uint32_t val = bmap[sport_idx];
            if ((val & sport_bit) == 0) {
                EXPECT_STREQ("TCP Sport bit not set", "");
                ret = false;
            }

            bmap = port.get_tcp_dport_bitmap();
            val = bmap[dport_idx];
            if ((val & dport_bit) == 0) {
                EXPECT_STREQ("TCP Dport bit not set", "");
                ret = false;
            }
        } else if (proto == IPPROTO_UDP) {
            bmap = port.get_udp_sport_bitmap();
            uint32_t val = bmap[sport_idx];
            if ((val & sport_bit) == 0) {
                EXPECT_STREQ("UDP Sport bit not set", "");
                ret = false;
            }

            bmap = port.get_udp_dport_bitmap();
            val = bmap[dport_idx];
            if ((val & dport_bit) == 0) {
                EXPECT_STREQ("UDP Dport bit not set", "");
                ret = false;
            }
        } else {
            std::vector<uint32_t> null_bmap;
            null_bmap.push_back(0); null_bmap.push_back(0);
            null_bmap.push_back(0); null_bmap.push_back(0);

            EXPECT_TRUE(null_bmap == port.get_tcp_sport_bitmap());
            if (null_bmap != port.get_tcp_sport_bitmap()) {
                EXPECT_STREQ("TCP Sport not NULL", "");
                ret = false;
            }

            EXPECT_TRUE(null_bmap == port.get_tcp_dport_bitmap());
            if (null_bmap != port.get_tcp_dport_bitmap()) {
                EXPECT_STREQ("TCP Dport not NULL", "");
                ret = false;
            }

            EXPECT_TRUE(null_bmap == port.get_udp_sport_bitmap());
            if (null_bmap != port.get_udp_sport_bitmap()) {
                EXPECT_STREQ("UDP Sport not NULL", "");
                ret = false;
            }

            EXPECT_TRUE(null_bmap == port.get_udp_dport_bitmap());
            if (null_bmap != port.get_udp_dport_bitmap()) {
                EXPECT_STREQ("UDP Dport not NULL", "");
                ret = false;
            }
        }
        return ret;
    }

    bool ValidateVrouter(uint8_t proto, uint16_t sport, uint16_t dport) {
        PortBucketBitmap port_uve;
        uve->port_bitmap().Encode(port_uve);
        return ValidateBmap(port_uve, proto, sport, dport);
    }

    bool ValidateVn(FlowEntry *flow, uint8_t proto, uint16_t sport,
                    uint16_t dport) {
        bool ret = true;
        PortBucketBitmap port_uve;
        VnUveTableTest *vut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());

        L4PortBitmap *bmap = vut->GetVnUvePortBitmap(flow->data().source_vn);
        if (bmap) {
            bmap->Encode(port_uve);
            if (ValidateBmap(port_uve, proto, sport, dport) == false) {
                ret = false;
            }
        }

        bmap = vut->GetVnUvePortBitmap(flow->data().dest_vn);
        if (bmap) {
            bmap->Encode(port_uve);
            if (ValidateBmap(port_uve, proto, sport, dport) == false) {
                ret = false;
            }
        }

        return ret;
    }

    bool ValidateVm(FlowEntry *flow, uint8_t proto, uint16_t sport,
                    uint16_t dport) {
        PortBucketBitmap port_uve;
        bool ret = true;
        const VmInterface *intf = static_cast<const VmInterface *>
            (flow->data().intf_entry.get());
        const VmEntry *vm = intf->vm();
        VmUveTableTest *vut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

        L4PortBitmap *bmap = vut->GetVmUvePortBitmap(vm);
        if (bmap) {
            bmap->Encode(port_uve);
            if (ValidateBmap(port_uve, proto, sport, dport) == false) {
                ret = false;
            }
        }

        return ret;
    }

    bool ValidateIntf(FlowEntry *flow, uint8_t proto, uint16_t sport,
                      uint16_t dport) {
        PortBucketBitmap port_uve;
        bool ret = true;
        VmUveTableTest *vut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

        const VmInterface *intf = static_cast<const VmInterface *>
            (flow->data().intf_entry.get());
        const VmEntry *vm = intf->vm();

        L4PortBitmap *bmap = vut->GetVmIntfPortBitmap(vm, intf);
        if (bmap) {
            bmap->Encode(port_uve);
            if (ValidateBmap(port_uve, proto, sport, dport) == false) {
                ret = false;
            }
        }
        return ret;
    }

    bool ValidateFlow(FlowEntry *flow) {
        uint8_t proto = flow->key().protocol;
        uint16_t sport = flow->key().src_port;
        uint16_t dport = flow->key().dst_port;
        bool ret = true;

        // Validate vrouter port bitmap
        EXPECT_TRUE(ValidateVrouter(proto, sport, dport));

        if (ValidateVn(flow, proto, sport, dport) != true) {
            ret = false;
        }

        if (ValidateVm(flow, proto, sport, dport) != true) {
            ret = false;
        }

        if (ValidateIntf(flow, proto, sport, dport) != true) {
            ret = false;
        }

        return ret;
    }

    void MakeFlow(FlowEntry *flow, uint16_t port, const std::string *dest_vn) {
        VmInterface *intf = static_cast<VmInterface *>(VmPortGet(port));
        const VnEntry *vn = intf->vn();
        SecurityGroupList empty_sg_id_l;

        boost::shared_ptr<PktInfo> pkt_info(new PktInfo(NULL, 0, 0));
        PktFlowInfo info(pkt_info, Agent::GetInstance()->pkt()->flow_table());
        PktInfo *pkt = pkt_info.get();

        PktControlInfo ctrl;
        ctrl.vn_ = vn;
        ctrl.intf_ = intf;

        flow->InitFwdFlow(&info, pkt, &ctrl, &ctrl);
    }

protected:
    VrouterUveEntryTest *uve;
};

TEST_F(UvePortBitmapTest, PortBitmap_1) {
    FlowKey key(0, 0, 0, IPPROTO_TCP, 1, 1);
    FlowEntry flow(key);
    MakeFlow(&flow, 1, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow);
    EXPECT_TRUE(ValidateFlow(&flow));
    Agent::GetInstance()->uve()->DeleteFlow(&flow);
    EXPECT_TRUE(ValidateFlow(&flow));
    client->WaitForIdle();
}

TEST_F(UvePortBitmapTest, PortBitmap_2) {
    FlowKey key(0, 0, 0, IPPROTO_TCP, 1, 1);
    FlowEntry flow(key);
    MakeFlow(&flow, 1, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow);
    Agent::GetInstance()->uve()->NewFlow(&flow);
    EXPECT_TRUE(ValidateFlow(&flow));
    Agent::GetInstance()->uve()->DeleteFlow(&flow);
    EXPECT_TRUE(ValidateFlow(&flow));
    Agent::GetInstance()->uve()->DeleteFlow(&flow);
    EXPECT_TRUE(ValidateFlow(&flow));
    client->WaitForIdle();
}

TEST_F(UvePortBitmapTest, PortBitmap_3) {
    FlowKey key1(0, 0, 0, IPPROTO_TCP, 1, 1);
    FlowEntry flow1(key1);
    MakeFlow(&flow1, 1, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow1);
    EXPECT_TRUE(ValidateFlow(&flow1));

    FlowKey key2(0, 0, 0, IPPROTO_TCP, 2, 2);
    FlowEntry flow2(key2);
    MakeFlow(&flow2, 2, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow2);
    EXPECT_TRUE(ValidateFlow(&flow2));

    Agent::GetInstance()->uve()->DeleteFlow(&flow1);
    EXPECT_TRUE(ValidateFlow(&flow1));
    EXPECT_TRUE(ValidateFlow(&flow2));
    Agent::GetInstance()->uve()->DeleteFlow(&flow2);
    EXPECT_TRUE(ValidateFlow(&flow1));
    EXPECT_TRUE(ValidateFlow(&flow2));
    client->WaitForIdle();
}

TEST_F(UvePortBitmapTest, PortBitmap_4) {
    FlowKey key1(0, 0, 0, IPPROTO_TCP, 1, 1);
    FlowEntry flow1(key1);
    MakeFlow(&flow1, 1, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow1);
    EXPECT_TRUE(ValidateFlow(&flow1));

    FlowKey key2(0, 0, 0, IPPROTO_TCP, 257, 257);
    FlowEntry flow2(key2);
    MakeFlow(&flow2, 2, &dest_vn_name);
    Agent::GetInstance()->uve()->NewFlow(&flow2);
    EXPECT_TRUE(ValidateFlow(&flow2));

    Agent::GetInstance()->uve()->DeleteFlow(&flow1);
    EXPECT_TRUE(ValidateFlow(&flow1));
    EXPECT_TRUE(ValidateFlow(&flow2));
    Agent::GetInstance()->uve()->DeleteFlow(&flow2);
    EXPECT_TRUE(ValidateFlow(&flow1));
    EXPECT_TRUE(ValidateFlow(&flow2));
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->vrf_table()->Size() == 1));
    TestShutdown();
    delete client;
    return ret;
}
