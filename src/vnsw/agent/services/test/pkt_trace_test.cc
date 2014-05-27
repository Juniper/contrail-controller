/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/scoped_array.hpp>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <pkt/tap_interface.h>
#include <pkt/test_tap_interface.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <openstack/instance_service_server.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <services/icmp_proto.h>
#include <vr_interface.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include <services/services_sandesh.h>
#include "vr_types.h"

#define MAC_LEN 6
#define MAX_WAIT_COUNT 60
#define BUF_SIZE 8192
char src_mac[MAC_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[MAC_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };

#define TRACE_CHECK(condition)                                                 \
                    do {                                                       \
                      usleep(1000);                                            \
                      client->WaitForIdle();                                   \
                      stats = Agent::GetInstance()->GetIcmpProto()->GetStats();\
                      if (++count == MAX_WAIT_COUNT)                           \
                          assert(0);                                           \
                    } while (condition);                                       \

class PktTraceTest : public ::testing::Test {
public:
    enum IcmpError {
        NO_ERROR,
        TYPE_ERROR,
        CHECKSUM_ERROR
    };

    PktTraceTest() : itf_count_(0), icmp_seq_(0) {
        rid_ = Agent::GetInstance()->GetInterfaceTable()->Register(
                boost::bind(&PktTraceTest::ItfUpdate, this, _2));
    }

    ~PktTraceTest() {
        Agent::GetInstance()->GetInterfaceTable()->Unregister(rid_);
    }

    void ItfUpdate(DBEntryBase *entry) {
        Interface *itf = static_cast<Interface *>(entry);
        tbb::mutex::scoped_lock lock(mutex_);
        unsigned int i;
        for (i = 0; i < itf_id_.size(); ++i)
            if (itf_id_[i] == itf->id())
                break;
        if (entry->IsDeleted()) {
            if (itf_count_ && i < itf_id_.size()) {
                itf_count_--;
                LOG(DEBUG, "Icmp test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "Icmp test : interface added " << itf->id());
            }
        }
    }

    uint32_t GetItfCount() { 
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_count_; 
    }

    std::size_t GetItfId(int index) { 
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_id_[index]; 
    }

    void CheckSandeshResponse(Sandesh *sandesh, int count) {
        if (memcmp(sandesh->Name(), "IcmpPktSandesh",
                          strlen("IcmpPktSandesh")) == 0) {
            IcmpPktSandesh *icmp = (IcmpPktSandesh *)sandesh;
            EXPECT_EQ(icmp->get_pkt_list().size(),
                      std::min((int)PktTrace::kPktNumBuffers, 2 * count));
        } else if (memcmp(sandesh->Name(), "PktTraceInfoResponse",
                          strlen("PktTraceInfoResponse")) == 0) {
            PktTraceInfoResponse *resp = (PktTraceInfoResponse *)sandesh;
            if (count == -1) {
                EXPECT_TRUE(resp->get_resp() == "Invalid Input !!");
            } else {
                EXPECT_EQ(resp->get_num_buffers(), count);
                EXPECT_EQ(resp->get_flow_num_buffers(), count);
            }
        }
    }

    void SendIcmp(short ifindex, uint32_t dest_ip, IcmpError error = NO_ERROR) {
        int len = 512;
        boost::scoped_array<uint8_t> buf(new uint8_t[len]);
        memset(buf.get(), 0, len);

        ethhdr *eth = (ethhdr *)buf.get();
        eth->h_dest[5] = 1;
        eth->h_source[5] = 2;
        eth->h_proto = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
        agent->hdr_vrf = htons(0);
        agent->hdr_cmd = htons(AGENT_TRAP_NEXTHOP);

        eth = (ethhdr *) (agent + 1);
        memcpy(eth->h_dest, dest_mac, MAC_LEN);
        memcpy(eth->h_source, src_mac, MAC_LEN);
        eth->h_proto = htons(0x800);

        iphdr *ip = (iphdr *) (eth + 1);
        ip->ihl = 5;
        ip->version = 4;
        ip->tos = 0;
        ip->id = 0;
        ip->frag_off = 0;
        ip->ttl = 16;
        ip->protocol = IPPROTO_ICMP;
        ip->check = 0;
        ip->saddr = 0;
        ip->daddr = htonl(dest_ip);

        icmphdr *icmp = (icmphdr *) (ip + 1);
        if (error == TYPE_ERROR)
            icmp->type = ICMP_ECHOREPLY;
        else
            icmp->type = ICMP_ECHO;
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->un.echo.id = 0x1234;
        icmp->un.echo.sequence = icmp_seq_++;
        if (error == CHECKSUM_ERROR)
            icmp->checksum = 0;
        else
            icmp->checksum = IpUtils::IPChecksum((uint16_t *)icmp, 64);
        len = 64;

        ip->tot_len = htons(len + sizeof(iphdr));
        len += sizeof(iphdr) + sizeof(ethhdr) + IPC_HDR_LEN;
        TestTapInterface *tap = (TestTapInterface *)
            (Agent::GetInstance()->pkt()->pkt_handler()->tap_interface());
        tap->GetTestPktHandler()->TestPktSend(buf.get(), len);
    }

private:
    DBTableBase::ListenerId rid_;
    uint32_t itf_count_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
    int icmp_seq_;
};

class AsioRunEvent : public Task {
public:
    AsioRunEvent() : Task(75) { };
    virtual  ~AsioRunEvent() { };
    bool Run() {
        Agent::GetInstance()->GetEventManager()->Run();
        return true;
    }
};

TEST_F(PktTraceTest, TraceTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "7.8.9.2", "00:00:00:02:02:02", 1, 2},
    };
    IcmpProto::IcmpStats stats;

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
        {"7.8.9.0", 24, "7.8.9.12"},
    };

    CreateVmportEnv(input, 2, 0); 
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 2); 
    client->WaitForIdle();

    ClearAllInfo *clear_req1 = new ClearAllInfo();
    clear_req1->HandleRequest();
    client->WaitForIdle();
    clear_req1->Release();

    SendIcmp(GetItfId(0), ntohl(inet_addr(ipam_info[0].gw)));
    SendIcmp(GetItfId(1), ntohl(inet_addr(ipam_info[1].gw)));
    int count = 0;
    TRACE_CHECK(stats.icmp_gw_ping < 2);

    IcmpInfo *sand1 = new IcmpInfo();
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 2));
    sand1->HandleRequest();
    client->WaitForIdle();
    sand1->Release();

    for (int i = 0; i < 5; ++i) {
        SendIcmp(GetItfId(0), ntohl(inet_addr(ipam_info[0].gw)));
        SendIcmp(GetItfId(1), ntohl(inet_addr(ipam_info[1].gw)));
    }
    count = 0;
    TRACE_CHECK(stats.icmp_gw_ping < 12);

    IcmpInfo *sand2 = new IcmpInfo();
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 8));
    sand2->HandleRequest();
    client->WaitForIdle();
    sand2->Release();

    PktTraceInfo *pkt_info = new PktTraceInfo();
    pkt_info->set_num_buffers(6);
    pkt_info->set_flow_num_buffers(6);
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 6));
    pkt_info->HandleRequest();
    client->WaitForIdle();
    pkt_info->Release();

    IcmpInfo *sand3 = new IcmpInfo();
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 0));
    sand3->HandleRequest();
    client->WaitForIdle();
    sand3->Release();

    for (int i = 0; i < 5; ++i) {
        SendIcmp(GetItfId(0), ntohl(inet_addr(ipam_info[0].gw)));
        SendIcmp(GetItfId(1), ntohl(inet_addr(ipam_info[1].gw)));
    }
    count = 0;
    TRACE_CHECK(stats.icmp_gw_ping < 22);

    IcmpInfo *sand4 = new IcmpInfo();
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 3));
    sand4->HandleRequest();
    client->WaitForIdle();
    sand4->Release();

    ClearAllInfo *clear_req2 = new ClearAllInfo();
    clear_req2->HandleRequest();
    client->WaitForIdle();
    clear_req2->Release();

    IcmpInfo *sand5 = new IcmpInfo();
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 0));
    sand5->HandleRequest();
    client->WaitForIdle();
    sand5->Release();

    Agent::GetInstance()->GetIcmpProto()->ClearStats();

    PktTraceInfo *pkt_info1 = new PktTraceInfo();
    pkt_info1->set_num_buffers(-1);
    pkt_info1->set_flow_num_buffers(10);
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, -1));
    pkt_info1->HandleRequest();
    client->WaitForIdle();
    pkt_info1->Release();

    PktTraceInfo *pkt_info2 = new PktTraceInfo();
    pkt_info2->set_num_buffers(6);
    pkt_info2->set_flow_num_buffers(-1);
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, -1));
    pkt_info2->HandleRequest();
    client->WaitForIdle();
    pkt_info2->Release();

    PktTraceInfo *pkt_info3 = new PktTraceInfo();
    pkt_info3->set_num_buffers(16);
    pkt_info3->set_flow_num_buffers(16);
    Sandesh::set_response_callback(
        boost::bind(&PktTraceTest::CheckSandeshResponse, this, _1, 16));
    pkt_info2->HandleRequest();
    client->WaitForIdle();
    pkt_info3->Release();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
