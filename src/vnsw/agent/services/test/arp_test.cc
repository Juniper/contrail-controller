/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <netinet/if_ether.h>
#include <boost/uuid/string_generator.hpp>
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
#include <oper/mirror_table.h>
#include <pugixml/pugixml.hpp>
#include <services/arp_proto.h>
#include <vr_interface.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include "vr_types.h"
#include "xmpp/test/xmpp_test_util.h"
#include <services/services_sandesh.h>

#define MAC_LEN 6
#define GRAT_IP "4.5.6.7"
#define DIFF_NET_IP "3.2.6.9"
#define MAX_WAIT_COUNT 50
short req_ifindex = 1, reply_ifindex = 1;
MacAddress src_mac(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
MacAddress dest_mac(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
MacAddress mac(0x00, 0x05, 0x07, 0x09, 0x0a, 0x0b);
ulong src_ip, dest_ip, target_ip, gw_ip, bcast_ip, static_ip;

class ArpTest : public ::testing::Test {
public:
    ArpTest() : trigger_(boost::bind(&ArpTest::AddVhostRcvRoute, this),
                         TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
                         0) {
    }

    void CheckSandeshResponse(Sandesh *sandesh) {
    }

    void TriggerAddVhostRcvRoute(Ip4Address &ip) {
        vhost_rcv_route_ = ip;
        trigger_.Set();
    }

    bool AddVhostRcvRoute() {
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            AddVHostRecvRoute(Agent::GetInstance()->local_peer(),
                              Agent::GetInstance()->fabric_vrf_name(),
                              "vhost0", vhost_rcv_route_, 32, "", false);
        return true;
    }

    void SendArpReq(short ifindex, short vrf, uint32_t sip, uint32_t tip) {
        int len = 2 * sizeof(struct ether_header) + sizeof(agent_hdr) +
                  sizeof(ether_arp);
        uint8_t *ptr(new uint8_t[len]);
        uint8_t *buf  = ptr;
        memset(buf, 0, len);

        struct ether_header *eth = (struct ether_header *)buf;
        eth->ether_dhost[5] = 1;
        eth->ether_shost[5] = 2;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
        agent->hdr_vrf = htons(vrf);
        agent->hdr_cmd = htons(AgentHdr::TRAP_RESOLVE);

        eth = (struct ether_header *) (agent + 1);
        dest_mac.ToArray(eth->ether_dhost, sizeof(eth->ether_dhost));
        src_mac.ToArray(eth->ether_shost, sizeof(eth->ether_shost));
        eth->ether_type = htons(0x806);

        ether_arp *arp = (ether_arp *) (eth + 1);
        arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
        arp->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
        arp->ea_hdr.ar_hln = 6;
        arp->ea_hdr.ar_pln = 4;
        arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);
        src_mac.ToArray(arp->arp_sha, sizeof(arp->arp_sha));

        sip = htonl(sip);
        memcpy(arp->arp_spa, &sip, sizeof(in_addr_t));
        tip = htonl(tip);
        memcpy(arp->arp_tpa, &tip, sizeof(in_addr_t));

        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(ptr, len);
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
        src_mac.ToArray(arp->arp_tha, sizeof(arp->arp_tha));
        dest_mac.ToArray(arp->arp_sha, sizeof(arp->arp_sha));

        sip = htonl(sip);
        tip = htonl(tip);
        memcpy(arp->arp_spa, &tip, sizeof(in_addr_t));
        memcpy(arp->arp_tpa, &sip, sizeof(in_addr_t));

        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(ptr, len);
    }

    PktGen *SendIpPacket(int ifindex, const char *sip, const char *dip,
                         int proto) {
        PktGen *pkt = new PktGen();
        pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
        pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_RESOLVE);
        pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
        pkt->AddIpHdr(sip, dip, proto);

        uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
        memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());

        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(ptr, pkt->GetBuffLen());
        delete pkt;
        return NULL;
    }

    void SendArpMessage(ArpProto::ArpMsgType type, uint32_t addr) {
        ArpProto::ArpIpc *ipc = 
                new ArpProto::ArpIpc(type, addr, 
                    Agent::GetInstance()->vrf_table()->
                    FindVrfFromName(Agent::GetInstance()->fabric_vrf_name()));
        Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
    }

    bool FindArpNHEntry(uint32_t addr, const string &vrf_name, bool validate = false) {
        Ip4Address ip(addr);
        ArpNHKey key(vrf_name, ip);
        ArpNH *arp_nh = static_cast<ArpNH *>(Agent::GetInstance()->
                                             nexthop_table()->
                                             FindActiveEntry(&key));
        if (arp_nh) {
            if (validate)
                return arp_nh->IsValid();
            return true;
        } else
            return false;
    }

    bool FindArpRoute(uint32_t addr, const string &vrf_name) {
        Agent *agent = Agent::GetInstance();
        Ip4Address ip(addr);
        InetUnicastRouteKey rt_key(agent->local_peer(), vrf_name, ip, 32);
        VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
        if (!vrf || !(vrf->GetInet4UnicastRouteTable()))
            return false;
        InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>
            (static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&rt_key));
        if (rt)
            return true;
        else
            return false;
    }

    void ArpNHUpdate(DBRequest::DBOperation op, in_addr_t addr) {
        Ip4Address ip(addr);
        InetUnicastAgentRouteTable::ArpRoute(op, ip, MacAddress(),
                          Agent::GetInstance()->fabric_vrf_name(),
                          *Agent::GetInstance()->GetArpProto()->ip_fabric_interface(),
                          false, 32);
    }

    void TunnelNH(DBRequest::DBOperation op, uint32_t saddr, uint32_t daddr) {
        Ip4Address sip(saddr);
        Ip4Address dip(daddr);

        NextHopKey *key = new TunnelNHKey(Agent::GetInstance()->fabric_vrf_name(), sip, dip,
                                          false, TunnelType::DefaultType());
        TunnelNHData *data = new TunnelNHData();

        DBRequest req;
        req.oper = op;
        req.key.reset(key);
        req.data.reset(data);
        Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    }

    void CfgIntfSync(int id, const char *cfg_name, int vn, int vm) {
    }

    void ItfDelete(const string &itf_name) {
        PhysicalInterface::DeleteReq(Agent::GetInstance()->interface_table(),
                                itf_name);
    }

    void WaitForCompletion(unsigned int size) {
        int count = 0;
        do {
            usleep(1000);
            client->WaitForIdle();
            if (++count == MAX_WAIT_COUNT) {
                LOG(ERROR, "WaitForCompletion failed for ArpCacheSize "<< size);
                LOG(ERROR, "WaitForCompletion failed for ArpCacheSize "<< Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
                assert(0);
            }
        } while (Agent::GetInstance()->GetArpProto()->GetArpCacheSize() != size);
        usleep(1000);
        client->WaitForIdle();
    }
private:
    Ip4Address vhost_rcv_route_;
    TaskTrigger trigger_;
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

TEST_F(ArpTest, ArpReqTest) {
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize()); // For GW
    EXPECT_TRUE(FindArpNHEntry(gw_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(gw_ip, Agent::GetInstance()->fabric_vrf_name()));
    SendArpReq(req_ifindex, 0, src_ip, target_ip);
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    usleep(175000); // wait for retry timer to expire
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    SendArpReq(req_ifindex, 0, src_ip, src_ip);
    SendArpReply(reply_ifindex, 0, src_ip, src_ip);
    client->WaitForIdle();
    SendArpReq(req_ifindex, 0, src_ip, target_ip);
    SendArpReq(req_ifindex, 0, src_ip, target_ip);
    SendArpReply(reply_ifindex, 0, src_ip, target_ip);
    SendArpReply(reply_ifindex, 0, src_ip, target_ip);
    SendArpReq(req_ifindex, 0, src_ip, target_ip);
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    SendArpMessage(ArpProto::AGING_TIMER_EXPIRED, target_ip);
    usleep(175000); // wait for retry timer
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpRoute(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpNHEntry(target_ip, Agent::GetInstance()->fabric_vrf_name()));
    SendIpPacket(req_ifindex, "1.1.1.1", "1.1.1.2", 1);
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(ntohl(inet_addr("1.1.1.2")), Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(ntohl(inet_addr("1.1.1.2")), Agent::GetInstance()->fabric_vrf_name()));
    usleep(175000); // wait for retry timer to expire
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(ntohl(inet_addr("1.1.1.2")), Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(ntohl(inet_addr("1.1.1.2")), Agent::GetInstance()->fabric_vrf_name()));
    ArpInfo *sand = new ArpInfo();
    Sandesh::set_response_callback(boost::bind(
        &ArpTest::CheckSandeshResponse, this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    ShowArpCache *arp_cache_sandesh = new ShowArpCache();
    Sandesh::set_response_callback(boost::bind(
        &ArpTest::CheckSandeshResponse, this, _1));
    arp_cache_sandesh->HandleRequest();
    client->WaitForIdle();
    arp_cache_sandesh->Release();
}

TEST_F(ArpTest, ArpGratuitousTest) {
    for (int i = 0; i < 2; i++) {
        SendArpReq(req_ifindex, 0, ntohl(inet_addr(GRAT_IP)), 
                             ntohl(inet_addr(GRAT_IP)));
        WaitForCompletion(2);
        EXPECT_TRUE(FindArpNHEntry(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
        EXPECT_TRUE(FindArpRoute(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
    }
    SendArpMessage(ArpProto::AGING_TIMER_EXPIRED, ntohl(inet_addr(GRAT_IP)));
    usleep(175000); // wait for retry timer to expire
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
}

TEST_F(ArpTest, ArpTunnelGwTest) {
    TunnelNH(DBRequest::DB_ENTRY_ADD_CHANGE, src_ip,
                       ntohl(inet_addr(DIFF_NET_IP)));
    SendArpReq(req_ifindex, 0, src_ip, gw_ip);
    WaitForCompletion(1);
    SendArpReply(reply_ifindex, 0, src_ip, gw_ip);
    WaitForCompletion(1);
    EXPECT_TRUE(FindArpNHEntry(gw_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(gw_ip, Agent::GetInstance()->fabric_vrf_name()));
    TunnelNH(DBRequest::DB_ENTRY_DELETE, src_ip, ntohl(inet_addr(DIFF_NET_IP)));
    WaitForCompletion(1);
}

TEST_F(ArpTest, ArpDelTest) {
    SendArpReq(req_ifindex, 0, ntohl(inet_addr(GRAT_IP)), 
                         ntohl(inet_addr(GRAT_IP)));
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
    ArpNHUpdate(DBRequest::DB_ENTRY_DELETE, ntohl(inet_addr(GRAT_IP)));
    client->WaitForIdle();
    usleep(175000);
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(ntohl(inet_addr(GRAT_IP)), Agent::GetInstance()->fabric_vrf_name()));
}

TEST_F(ArpTest, ArpTunnelTest) {
    TunnelNH(DBRequest::DB_ENTRY_ADD_CHANGE, src_ip, dest_ip);
    SendArpReq(req_ifindex, 0, src_ip, dest_ip);
    WaitForCompletion(2);
    SendArpReply(reply_ifindex, 0, src_ip, dest_ip);
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
    TunnelNH(DBRequest::DB_ENTRY_DELETE, src_ip, dest_ip);
    client->WaitForIdle();
    SendArpMessage(ArpProto::AGING_TIMER_EXPIRED, dest_ip);
    usleep(175000);
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
}

TEST_F(ArpTest, ArpTunnelNoRequestTest) {
    // Send Arp reply first to check that entry is created
    SendArpReply(reply_ifindex, 0, src_ip, dest_ip);
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(dest_ip, Agent::GetInstance()->fabric_vrf_name(), true));
    EXPECT_TRUE(FindArpRoute(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
    // TunnelNH(DBRequest::DB_ENTRY_ADD_CHANGE, src_ip, dest_ip);
    // WaitForCompletion(2);
    // TunnelNH(DBRequest::DB_ENTRY_DELETE, src_ip, dest_ip);
    client->WaitForIdle();
    SendArpMessage(ArpProto::AGING_TIMER_EXPIRED, dest_ip);
    usleep(175000);
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());
    EXPECT_FALSE(FindArpNHEntry(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(dest_ip, Agent::GetInstance()->fabric_vrf_name()));
}

TEST_F(ArpTest, ArpErrorTest) {
    Agent::GetInstance()->GetArpProto()->ClearStats();
    PacketInterfaceKey key(nil_uuid(), "pkt0");
    Interface *pkt_intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (!pkt_intf)
        assert(0);
    SendArpReq(7, 0, src_ip, target_ip);
    SendArpReq(pkt_intf->id(), 0, src_ip, target_ip);
    ArpProto::ArpStats stats;
    int count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->GetArpProto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.errors < 1);
    EXPECT_EQ(1U, stats.errors);
    SendArpReq(req_ifindex, 0, src_ip, 0xffffffff);
    count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = Agent::GetInstance()->GetArpProto()->GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.errors < 2);
    EXPECT_EQ(2U, stats.errors);
    Agent::GetInstance()->GetArpProto()->ClearStats();
}

TEST_F(ArpTest, ArpVrfDeleteTest) {
    Agent *agent = Agent::GetInstance();
    agent->set_fabric_vrf_name("vrf1");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    CreateVmportEnv(input, 1);
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 1));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 1));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == true));
    usleep(1000);
    client->WaitForIdle();

    SendArpReq(3, 1, src_ip, target_ip);
    SendArpReply(3, 1, src_ip, target_ip);
    SendArpReq(3, 1, src_ip, target_ip+5);
    SendArpReply(3, 1, src_ip, target_ip+5);
    WaitForCompletion(3);
    EXPECT_TRUE(FindArpNHEntry(target_ip, "vrf1"));
    EXPECT_TRUE(FindArpRoute(target_ip, "vrf1"));
    EXPECT_TRUE(FindArpNHEntry(target_ip+5, "vrf1"));
    EXPECT_TRUE(FindArpRoute(target_ip+5, "vrf1"));
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == false));

    EXPECT_FALSE(FindArpRoute(target_ip, "vrf1"));
    EXPECT_FALSE(FindArpRoute(target_ip+5, "vrf1"));
    agent->set_fabric_vrf_name("default-domain:default-project:ip-fabric:__default__");
}

TEST_F(ArpTest, GratArpSendTest) {
    Ip4Address ip1 = Ip4Address::from_string("1.1.1.1");
    //Add a vhost rcv route and check that grat arp entry gets created
    TriggerAddVhostRcvRoute(ip1);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->GetArpProto()->gratuitous_arp_entry()->key().ip == ip1.to_ulong());

    Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(
                                                    Agent::GetInstance()->local_peer(), 
                                                    Agent::GetInstance()->fabric_vrf_name(),
                                                    ip1, 32, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->GetArpProto()->gratuitous_arp_entry() == NULL);

    Ip4Address ip2 = Ip4Address::from_string("1.1.1.10");
    //Add yet another vhost rcv route and check that grat arp entry get created
    TriggerAddVhostRcvRoute(ip2);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->GetArpProto()->gratuitous_arp_entry()->key().ip == ip2.to_ulong());
    Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(
                                                    Agent::GetInstance()->local_peer(), 
                                                    Agent::GetInstance()->fabric_vrf_name(),
                                                    ip2, 32, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->GetArpProto()->gratuitous_arp_entry() == NULL);
}

#if 0
TEST_F(ArpTest, ArpItfDeleteTest) {
    struct PortInfo input[] = {
        {"vnet2", 2, "2.2.2.2", "00:00:00:00:00:02", 2, 2},
    };
    CreateVmportEnv(input, 1);
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vm_table()->Size() == 1));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vn_table()->Size() == 1));
    WAIT_FOR(500, 1000, (VrfFind("vrf2") == true));
    usleep(1000);
    client->WaitForIdle();
    SendArpReq(3, 2, src_ip, target_ip+8);
    SendArpReply(3, 2, src_ip, target_ip+8);
    SendArpReq(req_ifindex, 0, src_ip, target_ip+9);
    SendArpReply(reply_ifindex, 0, src_ip, target_ip+9);
    WaitForCompletion(3);
    EXPECT_TRUE(FindArpNHEntry(target_ip+8, "vrf2"));
    EXPECT_TRUE(FindArpRoute(target_ip+8, "vrf2"));
    EXPECT_TRUE(FindArpNHEntry(target_ip+9, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(FindArpRoute(target_ip+9, Agent::GetInstance()->fabric_vrf_name()));
    ItfDelete(Agent::GetInstance()->fabric_interface_name());
    usleep(1000);
    client->WaitForIdle();
    usleep(175000);
    EXPECT_FALSE(FindArpNHEntry(target_ip+8, "vrf2"));
    EXPECT_FALSE(FindArpRoute(target_ip+8, "vrf2"));
    EXPECT_FALSE(FindArpNHEntry(target_ip+9, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_FALSE(FindArpRoute(target_ip+9, Agent::GetInstance()->fabric_vrf_name()));
    // EXPECT_EQ(1, ArpProto::GetInstance()->GetArpCacheSize());
    DeleteVmportEnv(input, 1, true);
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf2") == false));
}
#endif

//Test to verify sending of ARP request on new interface addition
TEST_F(ArpTest, ArpReqOnVmInterface) {
    Agent *agent = Agent::GetInstance();
    agent->GetArpProto()->ClearStats();
    client->WaitForIdle();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    CreateVmportEnv(input, 1);
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 1));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 1));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == true));
    client->WaitForIdle();
    EXPECT_TRUE(agent->GetArpProto()->GetStats().vm_arp_req == 0);

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    AddIPAM("vn1", ipam_info, 1, NULL, "vdns1");
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 1));

    agent->GetArpProto()->ClearStats();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    EXPECT_TRUE(agent->GetArpProto()->GetStats().vm_arp_req == 0);

    //Readd IPAM
    AddIPAM("vn1", ipam_info, 1, NULL, "vdns1");
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 1));

    DelIPAM("vn1", "vdns1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == false));
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    if (Agent::GetInstance()->router_id_configured()) {
        src_ip = Agent::GetInstance()->router_id().to_ulong();
        gw_ip = Agent::GetInstance()->vhost_default_gateway().to_ulong();
    } else {
        LOG(DEBUG, "Router id not configured in config file");
        exit(0);
    }
    dest_ip = src_ip + 1;
    target_ip = dest_ip + 1;
    bcast_ip = (src_ip & 0xFFFFFF00) | 0xFF;
    static_ip = src_ip + 10;
    Agent::GetInstance()->GetArpProto()->set_max_retries(1);
    Agent::GetInstance()->GetArpProto()->set_retry_timeout(30);
    Agent::GetInstance()->GetArpProto()->set_aging_timeout(50);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
