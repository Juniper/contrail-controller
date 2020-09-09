/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include <services/services_sandesh.h>
#include "mac_learning/mac_learning_proto.h"
#include "mac_learning/mac_learning_mgmt.h"
#include "mac_learning/mac_learning_init.h"

MacAddress src_mac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
MacAddress dest_mac(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
MacAddress mac(0x00, 0x05, 0x07, 0x09, 0x0a, 0x0b);

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 1},
    {"vnet3", 3, "1.1.1.3", "00:00:01:01:01:03", 1, 1},
    {"vnet4", 4, "1.1.1.4", "00:00:01:01:01:04", 1, 1},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.254", true},
};

class MacIpLearningHCTest : public ::testing::Test {
public:
    MacIpLearningHCTest() {
        arp_try_count = 0;
        send_count = 0;
        sent_count = 0;

        TestPkt0Interface *tap = (TestPkt0Interface *)
                    (Agent::GetInstance()->pkt()->control_interface());
        tap->RegisterCallback(
                boost::bind(&MacIpLearningHCTest::MacIpLearningHCReceive, this, _1, _2));
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        CreateVmportEnv(input, 4);
        AddIPAM("vn1", &ipam_info[0], 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM("vn1");
        DeleteVmportEnv(input, 4, true);
        client->WaitForIdle();
    }

    void SendArpReply(short ifindex, short vrf, uint32_t sip, uint32_t tip) {
        int len = 2 * sizeof(struct ether_header) + sizeof(agent_hdr) + sizeof(ether_arp);
        uint8_t *ptr(new uint8_t[len]);
        uint8_t *buf  = ptr;
        memset(buf, 0, len);

        struct ether_header *eth = (struct ether_header *)buf;
        eth->ether_dhost[5] = 2;
        eth->ether_shost[5] = 1;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
        agent->hdr_vrf = htons(vrf);
        agent->hdr_cmd = htons(AgentHdr::TRAP_ARP);

        eth = (struct ether_header *) (agent + 1);
        src_mac.ToArray(eth->ether_dhost, sizeof(eth->ether_dhost));
        dest_mac.ToArray(eth->ether_shost, sizeof(eth->ether_shost));
        eth->ether_type = htons(0x806);

        ether_arp *arp = (ether_arp *) (eth + 1);
        arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
        arp->ea_hdr.ar_pro = htons(0x800);
        arp->ea_hdr.ar_hln = 6;
        arp->ea_hdr.ar_pln = 4;
        arp->ea_hdr.ar_op = htons(ARPOP_REPLY);
        src_mac.ToArray(arp->arp_sha, sizeof(arp->arp_sha));
        dest_mac.ToArray(arp->arp_tha, sizeof(arp->arp_tha));

        sip = htonl(sip);
        tip = htonl(tip);
        memcpy(arp->arp_spa, &sip, sizeof(in_addr_t));
        memcpy(arp->arp_tpa, &tip, sizeof(in_addr_t));

        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(ptr, len);
    }

    void MacIpLearningHCReceive(uint8_t *buf, std::size_t len) {
        struct ether_header *eth = (struct ether_header *)buf;

        agent_hdr *agent = (agent_hdr *)(eth + 1);

        eth = (struct ether_header *) (agent + 1);

        ether_arp *arp = (struct ether_arp *) (eth + 1);
        uint16_t *ptr = (uint16_t *) ((uint8_t*)eth + ETHER_ADDR_LEN * 2);
        uint16_t proto = ntohs(*ptr);

        uint32_t sip;
        memcpy(&sip, arp->arp_spa, sizeof(in_addr_t));
        uint32_t tip;
        memcpy(&tip, arp->arp_tpa, sizeof(in_addr_t));
        if ((proto == 0x0806) && (tip == inet_addr("1.1.1.10"))) {
            arp_try_count++;
            {
                time_t now = time(0);
                char* dt = ctime(&now);
                std::cout << "The local date and time is: " << dt << std::endl;
            }
            if (sent_count < send_count) {
                SendArpReply(ntohs(agent->hdr_ifindex), ntohs(agent->hdr_vrf),
                    ntohl(tip), ntohl(sip));
                sent_count++;
            }
        } else if (proto == 0x0800) {
            bfd_count++;
        }
    }

    Agent *agent_;
    uint8_t arp_try_count;
    uint8_t send_count;
    uint8_t sent_count;
    uint8_t bfd_count;
};

void TxL2Packet(int ifindex, const char *smac, const char *dmac,
                const char *sip, const char *dip,
                int proto, int vrf,
                uint16_t sport, uint16_t dport) {
    PktGen *pkt = new PktGen();

    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_MAC_IP_LEARNING, 0,  vrf) ;
    pkt->AddEthHdr(dmac, smac, 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1) {
        pkt->AddIcmpHdr();
    } else if (proto == IPPROTO_UDP) {
        pkt->AddUdpHdr(sport, dport, 64);
    }
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
            pkt->GetBuffLen());
    delete pkt;
}

// Test case to wait for ARP entry timeout
// by not sending the ARP reqlies
TEST_F(MacIpLearningHCTest, ArpTest1) {

    send_count = 0;
    sent_count = 0;

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();
    boost::system::error_code error_code;
    IpAddress sip = AddressFromString("1.1.1.10", &error_code);
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, AddressFromString("1.1.1.10", &error_code), 0) != NULL);
    EXPECT_TRUE(L2RouteGet("vrf1", smac) != NULL);
    EXPECT_TRUE(RouteGet("vrf1", sip.to_v4(), 32) != NULL);

    sleep(20);

    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL);
    client->WaitForIdle();
}

// Send ARP replies to prevent ARP request try timeout
// and make sure MAC IP learnt entry still exist after
// 20 seconds (8 tries * 2 seconds)
TEST_F(MacIpLearningHCTest, ArpTest2) {

    send_count = 9;
    sent_count = 0;

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();
    boost::system::error_code error_code;
    IpAddress sip = AddressFromString("1.1.1.10", &error_code);
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, AddressFromString("1.1.1.10", &error_code), 0) != NULL);
    EXPECT_TRUE(L2RouteGet("vrf1", smac) != NULL);
    EXPECT_TRUE(RouteGet("vrf1", sip.to_v4(), 32) != NULL);

    sleep(20);

    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    client->WaitForIdle();
}

// Create BFD Health and attachh to the virtual-network
// Modify the health check
// Health check parameters should reflect in MAC IP learning entry
TEST_F(MacIpLearningHCTest, BfdTest1) {
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:30", "00:00:00:11:22:31",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    intf = static_cast<const VmInterface *>(VmPortGet(2));
    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.11", "1.1.1.10", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    boost::system::error_code ec;

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 2, 5, 3, true, NULL, 0);
    client->WaitForIdle();

    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    ip = IpAddress(Ip4Address::from_string("1.1.1.11", ec));
    MacIpLearningKey key1(GetVrfId("vrf1"), ip);
    entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key1);
    EXPECT_TRUE(entry != NULL);

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();
}

// Create and attach BFD health check to VN
// Modify HC to take varying ip address (toggle from ALL)
TEST_F(MacIpLearningHCTest, BfdTest2) {

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    boost::system::error_code ec;

    intf = static_cast<const VmInterface *>(VmPortGet(2));
    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    IpStr ip_list[2] = {"1.1.1.10", "1.1.1.11"};
    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, false, ip_list, 2);
    client->WaitForIdle();

    IpStr ip_list1[3] = {"1.1.1.10", "1.1.1.11", "1.1.1.12"};
    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, false, ip_list1, 3);
    client->WaitForIdle();

    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    sleep(3);

    entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();
}

// Attach BFD HC and detach BFD HC and check in MAC IP learning entry
TEST_F(MacIpLearningHCTest, BfdTest3) {

    send_count = 9;
    sent_count = 0;

    boost::system::error_code ec;

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();

    ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key2(GetVrfId("vrf1"), ip);
    entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key2);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() == NULL);
    EXPECT_TRUE(entry->HcInstance() == NULL);
}

// Attach BFD HC, change IP address list to have learnt IP
// and then delete IP address from list for BFD HC
TEST_F(MacIpLearningHCTest, BfdTest4) {

    boost::system::error_code ec;

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    IpStr ip_list[2] = {"1.1.1.10", "1.1.1.11"};
    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, false, ip_list, 2);
    client->WaitForIdle();

    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    IpStr ip_list1[1] = {"1.1.1.11"};
    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, false, ip_list1, 1);
    client->WaitForIdle();

    ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key2(GetVrfId("vrf1"), ip);
    entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key2);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() == NULL);
    EXPECT_TRUE(entry->HcInstance() == NULL);

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();
}

// Attach BFD HC. Change timeout and retry values
// Wait for timeout and the MAC IP learning entry should get deleted
TEST_F(MacIpLearningHCTest, DISABLED_BfdTest5) {

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    boost::system::error_code ec;

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    AddVnHealthCheckService("HC-1", 1, 1, 1, 2, true, NULL, 0);
    client->WaitForIdle();

    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    intf = static_cast<const VmInterface *>(VmPortGet(2));
    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    sleep(10);

    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x31);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL);
    client->WaitForIdle();

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();
}

// Move IP to another interface and check for HC
TEST_F(MacIpLearningHCTest, BfdTest6) {

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));

    boost::system::error_code ec;

    AddVnHealthCheckService("HC-1", 1, 1, 1, 3, true, NULL, 0);
    AddLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();

    TxL2Packet(intf->id(), "00:00:00:11:22:31", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    intf = static_cast<const VmInterface *>(VmPortGet(2));
    IpAddress ip = IpAddress(Ip4Address::from_string("1.1.1.10", ec));
    MacIpLearningKey key(GetVrfId("vrf1"), ip);
    MacIpLearningEntry *entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    sleep(3);

    intf = static_cast<const VmInterface *>(VmPortGet(2));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:11:22:30",
               "1.1.1.10", "1.1.1.11", 1, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    entry = agent_->mac_learning_proto()->
                GetMacIpLearningTable()->Find(key);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->HcService() != NULL);
    EXPECT_TRUE(entry->HcService()->name() == "HC-1");
    EXPECT_TRUE(entry->HcInstance() != NULL);

    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    client->WaitForIdle();

    DelLink("virtual-network", "vn1",
            "service-health-check", "HC-1");
    client->WaitForIdle();
    DelHealthCheckService("HC-1");
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    int ret = 0;

    BgpPeer *peer;

    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    peer = CreateBgpPeer("127.0.0.1", "remote");
    client->WaitForIdle();

    ret = RUN_ALL_TESTS();

    usleep(100000);

    DeleteBgpPeer(peer);
    client->WaitForIdle();

    TestShutdown();
    delete client;

    return ret;
}
