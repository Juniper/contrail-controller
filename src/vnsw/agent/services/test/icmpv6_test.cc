/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <netinet/if_ether.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/scoped_array.hpp>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <openstack/instance_service_server.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <services/icmpv6_proto.h>
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

class Icmpv6Test : public ::testing::Test {
public:
    Icmpv6Test() : itf_count_(0), icmp_seq_(0) {
        rid_ = Agent::GetInstance()->interface_table()->Register(
                boost::bind(&Icmpv6Test::ItfUpdate, this, _2));
    }

    ~Icmpv6Test() {
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
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
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

    uint32_t Sum(uint16_t *ptr, std::size_t len, uint32_t sum) {
        while (len > 1) {
            sum += *ptr++;
            len -= 2;
            if (sum & 0x80000000)
                sum = (sum & 0xFFFF) + (sum >> 16);
        }

        if (len > 0)
            sum += *(uint8_t *)ptr;

        return sum;
    }

    uint16_t Csum(uint16_t *ptr, std::size_t len, uint32_t sum) {
        sum = Sum(ptr, len, sum);

        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);

        return ~sum;
    }

    void CheckSandeshResponse(Sandesh *sandesh, int count,
                              int req, int resp, int drop,
                              const char *type, const char *rest) {
        if (memcmp(sandesh->Name(), "Icmpv6Stats", strlen("Icmpv6Stats")) == 0) {
            Icmpv6Stats *icmp = (Icmpv6Stats *)sandesh;
            EXPECT_TRUE(icmp->get_icmpv6_router_solicit() == req ||
                        icmp->get_icmpv6_ping_request() == req);
            EXPECT_TRUE(icmp->get_icmpv6_router_advert() == resp ||
                        icmp->get_icmpv6_ping_response() == resp);
            EXPECT_EQ(icmp->get_icmpv6_drop(), drop);
        } else if (memcmp(sandesh->Name(), "Icmpv6PktSandesh",
                          strlen("Icmpv6PktSandesh")) == 0) {
            Icmpv6PktSandesh *icmp = (Icmpv6PktSandesh *)sandesh;
            EXPECT_EQ(icmp->get_pkt_list().size(),
                      std::min((int)PktTrace::kPktNumBuffers, count));
            if (strlen(type)) {
                for (int i = 0; i < count; ++i) {
                    Icmpv6Pkt pkt = (icmp->get_pkt_list())[i];
                    if (pkt.icmp_hdr.type.find(type) != std::string::npos) {
                        if (strlen(rest)) {
                            if (pkt.icmp_hdr.rest.find(rest) != std::string::npos)
                                return;
                        } else return;
                    }
                }
                assert(0);
            }
        }
    }

    void SendIcmp(short ifindex, uint8_t *src_ip, uint8_t *dest_ip,
                  uint32_t type, bool csum) {
        int len = 512;
        uint8_t *buf = new uint8_t[len];
        memset(buf, 0, len);

        struct ether_header *eth = (struct ether_header *)buf;
        eth->ether_dhost[5] = 1;
        eth->ether_shost[5] = 2;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
        agent->hdr_vrf = htons(0);
        agent->hdr_cmd = htons(AgentHdr::TRAP_NEXTHOP);

        eth = (struct ether_header *) (agent + 1);
        memcpy(eth->ether_dhost, dest_mac, MAC_LEN);
        memcpy(eth->ether_shost, src_mac, MAC_LEN);
        eth->ether_type = htons(ETHERTYPE_IPV6);

        ip6_hdr *ip = (ip6_hdr *) (eth + 1);
        ip->ip6_flow = htonl(0x60000000); // version 6, TC and Flow set to 0
        ip->ip6_plen = htons(64);
        ip->ip6_nxt = IPPROTO_ICMPV6;
        ip->ip6_hlim = 16;
        memcpy(ip->ip6_src.s6_addr, src_ip, 16);
        memcpy(ip->ip6_dst.s6_addr, dest_ip, 16);

        icmp6_hdr *icmp = (icmp6_hdr *) (ip + 1);
        icmp->icmp6_type = type;
        icmp->icmp6_code = 0;
        icmp->icmp6_cksum = 0;
        if (csum) {
            uint32_t plen = htonl((uint32_t)64);
            uint32_t next = htonl((uint32_t)IPPROTO_ICMPV6);
            uint32_t pseudo = 0;
            pseudo = Sum((uint16_t *)src_ip, 16, 0);
            pseudo = Sum((uint16_t *)dest_ip, 16, pseudo);
            pseudo = Sum((uint16_t *)&plen, 4, pseudo);
            pseudo = Sum((uint16_t *)&next, 4, pseudo);
            icmp->icmp6_cksum = Csum((uint16_t *)icmp, 64, pseudo);
        }
        len = 64;

        len += sizeof(struct ip6_hdr) + sizeof(struct ether_header) +
            Agent::GetInstance()->pkt()->pkt_handler()->EncapHeaderLen();
        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(buf, len);
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
        Agent::GetInstance()->event_manager()->Run();
        return true;
    }
};

TEST_F(Icmpv6Test, Icmpv6PingTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd15::2"},
        {"vnet2", 2, "7.8.9.2", "00:00:00:02:02:02", 1, 2, "1234::2"},
    };
    Icmpv6Proto::Icmpv6Stats stats;

    IpamInfo ipam_info[] = {
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"fd15::", 120, "fd15::1", true},
        {"1234::",  64, "1234::1", true},
    };

    IpamInfo ipam_updated_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"fd15::", 120, "fd15::1", true},
        {"1234::",  64, "1234::10", true},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

    ClearAllInfo *clear_req1 = new ClearAllInfo();
    clear_req1->HandleRequest();
    client->WaitForIdle();
    clear_req1->Release();

    boost::system::error_code ec;
    Ip6Address src1_ip = Ip6Address::from_string("fd15::5", ec);
    Ip6Address dest1_ip = Ip6Address::from_string("fd15::1", ec);
    Ip6Address src2_ip = Ip6Address::from_string("1234::5", ec);
    Ip6Address dest2_ip = Ip6Address::from_string("1234::1", ec);
    Ip6Address dest3_ip = Ip6Address::from_string("1234::10", ec);

    // Send multiple ping and check that they are responded to
    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest2_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest2_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    int count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->icmpv6_proto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.icmpv6_ping_response_ < 4);
    client->WaitForIdle();
    EXPECT_EQ(4U, stats.icmpv6_ping_request_);
    EXPECT_EQ(4U, stats.icmpv6_ping_response_);
    EXPECT_EQ(0U, stats.icmpv6_drop_);

    Icmpv6Info *sand1 = new Icmpv6Info();
    Sandesh::set_response_callback(
        boost::bind(&Icmpv6Test::CheckSandeshResponse, this, _1, 8, 4, 4, 0, "echo reply", ""));
    sand1->HandleRequest();
    client->WaitForIdle();
    sand1->Release();

    // Send updated Ipam
    char buf[BUF_SIZE];
    int len = 0;

    memset(buf, 0, BUF_SIZE);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "virtual-network", "vn1", 1);
    AddNodeString(buf, len, "virtual-network-network-ipam", "default-network-ipam,vn1", ipam_updated_info, 3);
    AddLinkString(buf, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
    client->WaitForIdle();

    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest3_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, true);
    count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->icmpv6_proto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.icmpv6_ping_response_ < 6);
    client->WaitForIdle();
    EXPECT_EQ(6U, stats.icmpv6_ping_request_);
    EXPECT_EQ(6U, stats.icmpv6_ping_response_);
    EXPECT_EQ(0U, stats.icmpv6_drop_);

    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, false);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest3_ip.to_bytes().data(),
             ICMP6_ECHO_REQUEST, false);
    count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->icmpv6_proto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.icmpv6_drop_ < 2);
    client->WaitForIdle();
    EXPECT_EQ(8U, stats.icmpv6_ping_request_);
    EXPECT_EQ(6U, stats.icmpv6_ping_response_);
    EXPECT_EQ(2U, stats.icmpv6_drop_);

    Agent::GetInstance()->icmpv6_proto()->ClearStats();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
}

TEST_F(Icmpv6Test, Icmpv6RATest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd15::2"},
        {"vnet2", 2, "7.8.9.2", "00:00:00:02:02:02", 1, 2, "1234::2"},
    };
    Icmpv6Proto::Icmpv6Stats stats;

    IpamInfo ipam_info[] = {
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"fd15::", 120, "fd15::1", true},
        {"1234::",  64, "1234::1", true},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

    ClearAllInfo *clear_req1 = new ClearAllInfo();
    clear_req1->HandleRequest();
    client->WaitForIdle();
    clear_req1->Release();

    boost::system::error_code ec;
    Ip6Address src1_ip = Ip6Address::from_string("fd15::5", ec);
    Ip6Address src2_ip = Ip6Address::from_string("1234::5", ec);
    Ip6Address dest1_ip = Ip6Address::from_string("ff01::2", ec);

    // Send Router Solications and check that they are responded to
    // two are sent with created vm address, whereas two are sent with not created vm
    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ND_ROUTER_SOLICIT, true);
    SendIcmp(GetItfId(0), src1_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ND_ROUTER_SOLICIT, true);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ND_ROUTER_SOLICIT, true);
    SendIcmp(GetItfId(1), src2_ip.to_bytes().data(), dest1_ip.to_bytes().data(),
             ND_ROUTER_SOLICIT, true);
    int count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->icmpv6_proto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.icmpv6_drop_ < 2);
    client->WaitForIdle();
    EXPECT_EQ(4U, stats.icmpv6_router_solicit_);
    EXPECT_EQ(2U, stats.icmpv6_router_advert_);
    EXPECT_EQ(2U, stats.icmpv6_drop_);

    Icmpv6Info *sand1 = new Icmpv6Info();
    Sandesh::set_response_callback(
        boost::bind(&Icmpv6Test::CheckSandeshResponse, this, _1, 6, 4, 2, 2, "router advertisement", ""));
    sand1->HandleRequest();
    client->WaitForIdle();
    sand1->Release();

    Agent::GetInstance()->icmpv6_proto()->ClearStats();

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
