/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <base/logging.h>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <pkt/pkt_handler.h>
#include <services/services_init.h>
#include <test/test_cmn_util.h>
#include <diag/diag_types.h>

#define DIAG_MSG_LEN 2000
#define MAX_WAIT_COUNT 5000
#define DIAG_CHECK(condition)                                                  \
    do {                                                                       \
            usleep(1000);                                                      \
            client->WaitForIdle();                                             \
            if (++try_count == MAX_WAIT_COUNT)                                 \
                assert(0);                                                     \
            } while (condition);                                               \

class DiagTest : public ::testing::Test {
public:
    DiagTest() : count_(0) {
        tap_ = static_cast<TestPkt0Interface *>(
               Agent::GetInstance()->pkt()->control_interface());
        tap_->RegisterCallback(
              boost::bind(&DiagTest::DiagCallback, this, _1, _2));
        rid_ = Agent::GetInstance()->interface_table()->Register(
                      boost::bind(&DiagTest::ItfUpdate, this, _2));
    }
    ~DiagTest() {
        Agent::GetInstance()->interface_table()->Unregister(rid_);
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
                LOG(DEBUG, "Diag test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "Diag test : interface added " << itf->id());
            }
        }
    }

    uint32_t GetItfCount() {
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_count_;
    }

    void WaitForItfUpdate(unsigned int expect_count) {
        int count = 0;
        while (GetItfCount() != expect_count) {
            if (++count == MAX_WAIT_COUNT)
                assert(0);
            usleep(1000);
        }
    }

    std::size_t GetItfId(int index) {
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_id_[index];
    }

    void DiagCallback(uint8_t *msg, std::size_t length) {
        if (length < DIAG_MSG_LEN) {
            return;
        }

        uint8_t *buf = new uint8_t[length];
        memcpy(buf, msg, length);

        // Change the agent header
        struct ether_header *eth = (struct ether_header *)buf;
        MacAddress mac(eth->ether_dhost);
        memcpy(eth->ether_dhost, eth->ether_shost, sizeof(eth->ether_dhost));
        mac.ToArray(eth->ether_shost, sizeof(eth->ether_shost));

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        std::size_t intf_id = ntohs(agent->hdr_ifindex);
        LOG(DEBUG, "Diag Callback; Agent index : " <<
            ntohs(agent->hdr_ifindex) << " Interface index : " <<
            GetItfId(0) << " " << GetItfId(1));
        if (GetItfId(0) == intf_id)
            agent->hdr_ifindex = htons(GetItfId(1));
        else if (GetItfId(1) == intf_id)
            agent->hdr_ifindex = htons(GetItfId(0));
        else {
            LOG(DEBUG, "Invalid Interface; Agent index : " <<
                ntohs(agent->hdr_ifindex) << " Interface index : " <<
                GetItfId(0) << " " << GetItfId(1));
            delete [] buf;
            return;
        }
        agent->hdr_cmd = htons(AgentHdr::TRAP_DIAG);
        agent->hdr_cmd_param = htonl(ntohs(agent->hdr_ifindex));

        MacAddress smac(0x00, 0x25, 0x90, 0xc4, 0x82, 0x2c);
        MacAddress dmac(0x02, 0xce, 0xa0, 0x6c, 0x96, 0x34);
        eth = (struct ether_header *) (agent + 1);
        dmac.ToArray(eth->ether_dhost, sizeof(eth->ether_dhost));
        smac.ToArray(eth->ether_shost, sizeof(eth->ether_shost));

        // send the recieved packet back
        tap_->TxPacket(buf, length);
    }

    void SendDiag(const std::string &sip, int32_t sport, const std::string &dip,
                  int32_t dport, int16_t prot, const std::string &vrf,
                  int16_t size, int16_t count, int16_t interval) {
        PingReq *req = new PingReq();
        req->set_source_ip(sip);
        req->set_source_port(sport);
        req->set_dest_ip(dip);
        req->set_dest_port(dport);
        req->set_protocol(prot);
        req->set_vrf_name(vrf);
        req->set_packet_size(size);
        req->set_count(count);
        req->set_interval(interval);
        Sandesh::set_response_callback(
            boost::bind(&DiagTest::CheckSandeshResponse, this, _1, count));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void CheckSandeshResponse(Sandesh *sandesh, uint16_t count) {
        if (memcmp(sandesh->Name(), "PingResp", strlen("PingResp")) == 0) {
            PingResp *resp = (PingResp *)sandesh;
            LOG(DEBUG, "Received Diag response : " << resp->get_resp());
            if (resp->get_resp() == "Success") {
                count_++;
            }
        }
        if (memcmp(sandesh->Name(), "PingSummaryResp",
                   strlen("PingSummaryResp")) == 0) {
            PingSummaryResp *resp = (PingSummaryResp *)sandesh;
            LOG(DEBUG, "Ping Summary Response; requests = " <<
                resp->get_request_sent() << "responses = " <<
                resp->get_response_received() << "pkt loss = " <<
                resp->get_pkt_loss());
            if (resp->get_request_sent() != count ||
                resp->get_response_received() != count ||
                resp->get_pkt_loss() != 0) {
                assert(0);
            }
        }
    }

    uint32_t count() { return count_; }
    void set_count(uint32_t count) { count_ = count; }

private:
    uint32_t count_;
    TestPkt0Interface *tap_;
    DBTableBase::ListenerId rid_;
    uint32_t itf_count_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
};

class AsioRunEvent : public Task {
public:
    AsioRunEvent() : Task(75) { };
    virtual  ~AsioRunEvent() { };
    bool Run() {
        Agent::GetInstance()->event_manager()->Run();
        return true;
    }
};

TEST_F(DiagTest, DiagReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 4, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
        {"vnet2", 5, "1.1.1.2", "00:00:00:02:02:02", 1, 3},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();

    SendDiag("1.1.1.1", 1000, "1.1.1.2", 1000, IPPROTO_UDP, "vrf1", DIAG_MSG_LEN, 1, 1);
    client->WaitForIdle();

    uint32_t try_count = 0;
    DIAG_CHECK(count() < 1);
    EXPECT_TRUE(count() == 1);

    set_count(0);
    try_count = 0;
    SendDiag("1.1.1.1", 1000, "1.1.1.2", 1000, IPPROTO_UDP, "vrf1", DIAG_MSG_LEN, 4, 2);
    client->WaitForIdle();
    DIAG_CHECK(count() < 4);
    EXPECT_TRUE(count() == 4);

    set_count(0);
    try_count = 0;
    SendDiag("1.1.1.1", 1000, "1.1.1.2", 1000, IPPROTO_TCP, "vrf1", DIAG_MSG_LEN, 3, 2);
    client->WaitForIdle();
    DIAG_CHECK(count() < 3);
    EXPECT_TRUE(count() == 3);

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0); 
    client->WaitForIdle();
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
