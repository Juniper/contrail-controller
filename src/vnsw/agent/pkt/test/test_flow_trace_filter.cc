/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_trace_filter.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.11", "00:00:01:01:01:02", 1, 1},
    {"vnet3", 3, "1.1.1.13", "00:00:01:01:01:03", 1, 1},
};

class FlowTraceFilterTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();

        CreateVmportEnv(input, 3);
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());

        vmi_ = VmInterfaceGet(1);
        filter_v4_ = flow_proto_->ipv4_trace_filter();
        filter_v6_ = flow_proto_->ipv6_trace_filter();
        FlowStatsTimerStartStop(agent_, true);
        agent_->flow_stats_manager()->set_delete_short_flow(false);
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        DeleteVmportEnv(input, 3, true);
        client->WaitForIdle();
    }

    Ip4Address StringToIp4(const char *str) {
        boost::system::error_code ec;
        Ip4Address ip = Ip4Address::from_string(str);
        assert(!ec);
        return ip;
    }

    Ip6Address StringToIp6(const char *str) {
        boost::system::error_code ec;
        Ip6Address ip = Ip6Address::from_string(str);
        assert(!ec);
        return ip;
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    VmInterface *vmi_;
    FlowTraceFilter *filter_v4_;
    FlowTraceFilter *filter_v6_;
};

TEST_F(FlowTraceFilterTest, Enable_Default_1) {
    EXPECT_TRUE(filter_v4_->enabled_);
    EXPECT_TRUE(filter_v4_->src_addr_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->src_mask_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->dst_addr_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->dst_mask_ == StringToIp4("0.0.0.0"));
    EXPECT_EQ(filter_v4_->proto_start_, 0);
    EXPECT_EQ(filter_v4_->proto_end_, 0xFF);
    EXPECT_EQ(filter_v4_->src_port_start_, 0);
    EXPECT_EQ(filter_v4_->src_port_end_, 0xFFFF);
    EXPECT_EQ(filter_v4_->dst_port_start_, 0);
    EXPECT_EQ(filter_v4_->dst_port_end_, 0xFFFF);

    EXPECT_TRUE(filter_v6_->enabled_);
    EXPECT_TRUE(filter_v6_->src_addr_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->src_mask_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->dst_addr_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->dst_mask_ == StringToIp6("::0"));
    EXPECT_EQ(filter_v6_->proto_start_, 0);
    EXPECT_EQ(filter_v6_->proto_end_, 0xFF);
    EXPECT_EQ(filter_v6_->src_port_start_, 0);
    EXPECT_EQ(filter_v6_->src_port_end_, 0xFFFF);
    EXPECT_EQ(filter_v6_->dst_port_start_, 0);
    EXPECT_EQ(filter_v6_->dst_port_end_, 0xFFFF);

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.11", 1, 1,
                false);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.11", 6, 1, 1);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->trace());

    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry()->trace());
}

TEST_F(FlowTraceFilterTest, Disable_1) {
    filter_v4_->SetFilter(false, Address::INET, "0.0.0.0", 0, "0.0.0.0", 0,
                          0, 255, 0, 0xFFFF, 0, 0xFFFF);
    EXPECT_FALSE(filter_v4_->enabled_);
    EXPECT_TRUE(filter_v4_->src_addr_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->src_mask_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->dst_addr_ == StringToIp4("0.0.0.0"));
    EXPECT_TRUE(filter_v4_->dst_mask_ == StringToIp4("0.0.0.0"));
    EXPECT_EQ(filter_v4_->proto_start_, 0);
    EXPECT_EQ(filter_v4_->proto_end_, 0xFF);
    EXPECT_EQ(filter_v4_->src_port_start_, 0);
    EXPECT_EQ(filter_v4_->src_port_end_, 0xFFFF);
    EXPECT_EQ(filter_v4_->dst_port_start_, 0);
    EXPECT_EQ(filter_v4_->dst_port_end_, 0xFFFF);

    filter_v6_->SetFilter(false, Address::INET6, "::0", 0, "::0", 0,
                          0, 255, 0, 0xFFFF, 0, 0xFFFF);
    EXPECT_FALSE(filter_v6_->enabled_);
    EXPECT_TRUE(filter_v6_->src_addr_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->src_mask_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->dst_addr_ == StringToIp6("::0"));
    EXPECT_TRUE(filter_v6_->dst_mask_ == StringToIp6("::0"));
    EXPECT_EQ(filter_v6_->proto_start_, 0);
    EXPECT_EQ(filter_v6_->proto_end_, 0xFF);
    EXPECT_EQ(filter_v6_->src_port_start_, 0);
    EXPECT_EQ(filter_v6_->src_port_end_, 0xFFFF);
    EXPECT_EQ(filter_v6_->dst_port_start_, 0);
    EXPECT_EQ(filter_v6_->dst_port_end_, 0xFFFF);

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.11", 1, 1,
                false);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.11", 6, 1, 1);
    EXPECT_TRUE(fe != NULL);
    EXPECT_FALSE(fe->trace());

    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_FALSE(fe->reverse_flow_entry()->trace());
}

TEST_F(FlowTraceFilterTest, Enable_2) {
    filter_v4_->SetFilter(true, Address::INET, "1.1.1.10", 32, "1.1.1.1", 24,
                          6, 6, 0, 1000, 0, 2000);
    EXPECT_TRUE(filter_v4_->enabled_);
    EXPECT_TRUE(filter_v4_->src_addr_ == StringToIp4("1.1.1.10"));
    EXPECT_TRUE(filter_v4_->src_mask_ == StringToIp4("255.255.255.255"));
    EXPECT_TRUE(filter_v4_->dst_addr_ == StringToIp4("1.1.1.0"));
    EXPECT_TRUE(filter_v4_->dst_mask_ == StringToIp4("255.255.255.0"));
    EXPECT_EQ(filter_v4_->proto_start_, 6);
    EXPECT_EQ(filter_v4_->proto_end_, 6);
    EXPECT_EQ(filter_v4_->src_port_start_, 0);
    EXPECT_EQ(filter_v4_->src_port_end_, 1000);
    EXPECT_EQ(filter_v4_->dst_port_start_, 0);
    EXPECT_EQ(filter_v4_->dst_port_end_, 2000);

    // Enable tracing based on forward flow
    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.11", 1, 1,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.11", 6, 1, 1);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->trace());
    EXPECT_TRUE(fe->reverse_flow_entry()->trace());

    // Enable tracing based on reverse flow
    TxTcpPacket(VmInterfaceGet(2)->id(), "1.1.1.11", "1.1.1.10", 1, 2,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                 "1.1.1.11", "1.1.1.10", 6, 1, 2);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->trace());
    EXPECT_TRUE(fe->reverse_flow_entry()->trace());

    // No tracing due to port-mismatch
    TxTcpPacket(VmInterfaceGet(2)->id(), "1.1.1.11", "1.1.1.10", 1, 30000,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                 "1.1.1.11", "1.1.1.10", 6, 1, 30000);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_FALSE(fe->trace());
    EXPECT_FALSE(fe->reverse_flow_entry()->trace());

    // No tracing due to address-mismatch
    TxTcpPacket(VmInterfaceGet(2)->id(), "1.1.1.11", "1.1.2.10", 1, 2,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                 "1.1.1.11", "1.1.2.10", 6, 1, 2);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_FALSE(fe->trace());
    EXPECT_FALSE(fe->reverse_flow_entry()->trace());

    // No tracing due to protocol mismatch
    TxUdpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.11", 1, 2,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                 "1.1.1.10", "1.1.1.11", 17, 1, 2);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_FALSE(fe->trace());
    EXPECT_FALSE(fe->reverse_flow_entry()->trace());
}

TEST_F(FlowTraceFilterTest, Enable_3) {
    filter_v4_->SetFilter(true, Address::INET, "1.1.1.10", 32, "1.1.1.1", 24,
                          6, 6, 0, 1000, 22, 22);
    EXPECT_TRUE(filter_v4_->enabled_);
    EXPECT_TRUE(filter_v4_->src_addr_ == StringToIp4("1.1.1.10"));
    EXPECT_TRUE(filter_v4_->src_mask_ == StringToIp4("255.255.255.255"));
    EXPECT_TRUE(filter_v4_->dst_addr_ == StringToIp4("1.1.1.0"));
    EXPECT_TRUE(filter_v4_->dst_mask_ == StringToIp4("255.255.255.0"));
    EXPECT_EQ(filter_v4_->proto_start_, 6);
    EXPECT_EQ(filter_v4_->proto_end_, 6);
    EXPECT_EQ(filter_v4_->src_port_start_, 0);
    EXPECT_EQ(filter_v4_->src_port_end_, 1000);
    EXPECT_EQ(filter_v4_->dst_port_start_, 22);
    EXPECT_EQ(filter_v4_->dst_port_end_, 22);

    // Enable tracing based on forward flow
    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.11", 1, 22,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.11", 6, 1, 22);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->trace());
    EXPECT_TRUE(fe->reverse_flow_entry()->trace());

    // Enable tracing based on reverse flow
    TxTcpPacket(VmInterfaceGet(2)->id(), "1.1.1.11", "1.1.1.10", 22, 2,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                 "1.1.1.11", "1.1.1.10", 6, 22, 2);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->trace());
    EXPECT_TRUE(fe->reverse_flow_entry()->trace());

    // No tracing due to port-mismatch
    TxTcpPacket(VmInterfaceGet(2)->id(), "1.1.1.11", "1.1.1.10", 1, 30000,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                 "1.1.1.11", "1.1.1.10", 6, 1, 30000);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_FALSE(fe->trace());
    EXPECT_FALSE(fe->reverse_flow_entry()->trace());
}

int main(int argc, char *argv[]) {
    int ret = 0;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
