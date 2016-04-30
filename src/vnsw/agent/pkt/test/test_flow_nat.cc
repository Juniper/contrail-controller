/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#include "test_flow_base.cc"

TEST_F(FlowTest, Nat_FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = flow_stats_collector_->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    flow_stats_collector_->UpdateFlowAgeTime(tmp_age_time);

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { 
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    // Update stats so that flow is not aged
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 0, 0, 
                        false, -1, -1, GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                        vm1_fip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input3[0].intf_id)));

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));

    //Restore flow aging time
    flow_stats_collector_->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, NonNatFlowAdd_1) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                    flow1->id()),
            {}
        }   
    };

    CreateFlow(flow, 1);

    // Add duplicate flow
    CreateFlow(flow, 1);

    // Add reverse flow
    CreateFlow(flow + 1, 1);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[1].intf_id)));

    // Add forward and reverse flow
    CreateFlow(flow, 2);
    // Delete reverse flow. Should delete both flows
    DeleteFlow(flow + 1, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[1].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatDupFlowAdd_1) {
    TestFlow flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                                flow0->id()),
            {}
        },
        {
             TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                                flow1->id()),
            {}
        }   
    };

    // Add forward and reverse flow
    CreateFlow(flow, 2); 

    // Add duplicate forward and reverse flow
    CreateFlow(flow, 2);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            { }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
        
    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    //Make sure NAT reverse flow is also deleted
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(),
                          vm5_ip, vm1_fip, 1,
                          0, 0, GetFlowKeyNH(input[0].intf_id)));

    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm5_ip, vm1_ip, 1,
                         0, 0, GetFlowKeyNH(input[0].intf_id)));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NonNatAddOldNat_2) {
#if 0
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id()),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, "vn4:vn4", 
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id()),
            {
                new VerifyVn(unknown_vn_, unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    //Make sure NAT reverse flow is deleted
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 
                         0, 0));
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                         vm1_fip, 1, 0, 0));

    // Add Non-NAT reverse flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 
                         0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
#endif
}

TEST_F(FlowTest, NonNatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    //Make sure NAT reverse flow is also deleted
    WAIT_FOR(1000, 1000, FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(),
                                 vm5_ip, vm1_fip, 1, 0, 0,
                                 GetFlowKeyNH(input3[0].intf_id)));
    // wait for deleted entries to go
    EXPECT_TRUE(FlowTableWait(0));

    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm5_ip, vm1_ip, 1, 
                0, 0, GetFlowKeyNH(input3[0].intf_id)));
    //Make sure NAT reverse flow is not present
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(),
                         vm5_ip, vm1_fip, 1, 0, 0,
                         GetFlowKeyNH(input3[0].intf_id)));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NatFlowAdd_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
            TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, "default-project:vn4:vn4",
                    flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000,
             (FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                       vm1_fip, 1, 0, 0, GetFlowKeyNH(input3[0].intf_id))));;
    CreateFlow(nat_flow, 1); 
    DeleteFlow(nat_rev_flow, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000,
             (FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 0, 0,
                       GetFlowKeyNH(input[0].intf_id))));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_2) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    WAIT_FOR(1000, 1000,
             (FlowFail(VrfGet("vrf3")->vrf_id(), vm5_ip, vm1_fip, 1, 0, 0,
                       GetFlowKeyNH(input3[0].intf_id))));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_1) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    WAIT_FOR(1000, 1000, FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(),
                                      vm5_ip, vm1_fip, 1, 0, 0,
                                      GetFlowKeyNH(input3[0].intf_id)) == NULL);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NatAddOldNonNat_2) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                    "default-project:vn4:vn4", flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, 
                vm5_ip, 1, 0, 0, GetFlowKeyNH(input3[0].intf_id)) == NULL);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip2"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip2, 1, 0, 0) 
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);
 
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_2) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm2_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 2),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                    "default-project:vn4:vn4", flow4->id(), 2),
            {
                new VerifyNat(vm2_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    DeleteFlow(nat_flow, 1);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

//Create same Nat flow with different flow handles
TEST_F(FlowTest, TwoNatFlow) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    DeleteFlow(nat_flow, 1);    
    EXPECT_TRUE(FlowTableWait(0));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
