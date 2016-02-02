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
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
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
#include "oper/path_preference.h"

#define GRAT_IP "4.5.6.7"
#define DIFF_NET_IP "3.2.6.9"
#define MAX_WAIT_COUNT 50
short req_ifindex = 1, reply_ifindex = 1;
MacAddress src_mac(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
MacAddress dest_mac(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
MacAddress mac(0x00, 0x05, 0x07, 0x09, 0x0a, 0x0b);
unsigned long src_ip, dest_ip, target_ip, gw_ip, bcast_ip, static_ip;

class ArpTest : public ::testing::Test {
public:
    ArpTest() : agent(Agent::GetInstance()),
    trigger_(boost::bind(&ArpTest::AddVhostRcvRoute, this),
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
    PhysicalInterfaceKey key(Agent::GetInstance()->fabric_interface_name());
    Interface *eth = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
        ArpProto::ArpIpc *ipc = 
                new ArpProto::ArpIpc(type, addr, 
                    Agent::GetInstance()->vrf_table()->
                    FindVrfFromName(Agent::GetInstance()->fabric_vrf_name()), eth);
        Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
    }

    bool FindArpNHEntry(uint32_t addr, const string &vrf_name, bool validate = false) {
        Ip4Address ip(addr);
        ArpNHKey key(vrf_name, ip, false);
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
        InetUnicastAgentRouteTable::ArpRoute(op,
                          Agent::GetInstance()->fabric_vrf_name(),
                          ip, MacAddress(),
                          Agent::GetInstance()->fabric_vrf_name(),
                          *Agent::GetInstance()->GetArpProto()->ip_fabric_interface(),
                          false, 32, false, "", SecurityGroupList());
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
protected:
    Agent *agent;
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
    std::string Description() const { return "AsioRunEvent"; }
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
#if 0
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
    WAIT_FOR(5000, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 3));

    agent->GetArpProto()->ClearStats();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    EXPECT_TRUE(agent->GetArpProto()->GetStats().vm_arp_req == 0);

    //Readd IPAM
    AddIPAM("vn1", ipam_info, 1, NULL, "vdns1");
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 0));

    DelIPAM("vn1", "vdns1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == false));
}

//Test to verify sending of ARP request on new interface reactivation
TEST_F(ArpTest, ArpReqOnVmInterface_1) {
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
    WAIT_FOR(5000, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 3));

    agent->GetArpProto()->ClearStats();
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(agent->GetArpProto()->GetStats().vm_arp_req == 0);

    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    WAIT_FOR(5000, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 3));

    DelIPAM("vn1", "vdns1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == false));
}

//Enqueue high preference for vm route, and check that ARP request
//are not sent
TEST_F(ArpTest, ArpReqOnVmInterface_2) {
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
    WAIT_FOR(5000, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 3));

    agent->GetArpProto()->ClearStats();
    client->WaitForIdle();
    EXPECT_TRUE(agent->GetArpProto()->GetStats().vm_arp_req == 0);

    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.1");
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(vm_ip, 32, VmPortGet(1)->id(), 1, MacAddress());
    client->WaitForIdle();
    agent->GetArpProto()->ClearStats();
    client->WaitForIdle();
    sleep(2);
    WAIT_FOR(5000, 1000, (agent->GetArpProto()->GetStats().vm_arp_req == 0));

    DelIPAM("vn1", "vdns1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (agent->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (agent->vn_table()->Size() == 0));
    WAIT_FOR(500, 1000, (VrfFind("vrf1") == false));
}
#endif

//Test to check update on resolve route, would result
//in arp routes also getting updated
TEST_F(ArpTest, DISABLED_SubnetResolveWithoutPolicy) {
  struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    AddPhysicalDevice(agent->host_name().c_str(), 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("lp1", 1, "lp1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("logical-interface", "lp1", "physical-interface", "pi1");
    AddLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");
    client->WaitForIdle();

    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    Ip4Address sip = Ip4Address::from_string("8.1.1.1");
    Ip4Address dip = Ip4Address::from_string("8.1.1.2");
    VmInterface *vintf = VmInterfaceGet(8);
    SendArpReq(vintf->id(), vintf->vrf()->vrf_id(), sip.to_ulong(), dip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == true);

    AgentRoute *rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    const ArpNH *arp_nh = static_cast<const ArpNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(arp_nh->GetInterface() == vintf);
    EXPECT_TRUE(arp_nh->PolicyEnabled() == false);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1"); 

    DelLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Test to check update on resolve route, would result
//in arp routes also getting updated
TEST_F(ArpTest, DISABLED_SubnetResolveWithPolicy) {
  struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    //Create VM interface with policy
    CreateVmportWithEcmp(input1, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    Ip4Address sip = Ip4Address::from_string("8.1.1.1");
    Ip4Address dip = Ip4Address::from_string("8.1.1.2");
    VmInterface *vintf = VmInterfaceGet(8);
    SendArpReq(vintf->id(), vintf->vrf()->vrf_id(), sip.to_ulong(), dip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == true);

    AgentRoute *rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    const ArpNH *arp_nh = static_cast<const ArpNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(arp_nh->GetInterface() == vintf);
    EXPECT_TRUE(arp_nh->PolicyEnabled() == true);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");

    DelLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Verify that ARP route gets updated with policy
//when interface policy changes
TEST_F(ArpTest, DISABLED_SubnetResolveWithPolicyUpdate) {
  struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    //Create VM interface with policy
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    Ip4Address sip = Ip4Address::from_string("8.1.1.1");
    Ip4Address dip = Ip4Address::from_string("8.1.1.2");
    VmInterface *vintf = VmInterfaceGet(8);
    SendArpReq(vintf->id(), vintf->vrf()->vrf_id(), sip.to_ulong(), dip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == true);

    AgentRoute *rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    const ArpNH *arp_nh = static_cast<const ArpNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(arp_nh->GetInterface() == vintf);
    EXPECT_TRUE(arp_nh->PolicyEnabled() == false);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");

    SendArpReply(vintf->id(), vintf->vrf()->vrf_id(),
                 sip.to_ulong(), dip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, arp_nh->GetResolveState() == true);
    EXPECT_TRUE(arp_nh->PolicyEnabled() == false);
    
    //Change policy of interface
    AddAcl("acl1", 1);
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    arp_nh = static_cast<const ArpNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(arp_nh->PolicyEnabled() == true);

    DelLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);

    DelAcl("acl1");
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Test to check update on resolve route, would result
//in arp routes also getting updated
TEST_F(ArpTest, DISABLED_SubnetResolveWithSg) {
  struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    //Create VM interface with policy
    CreateVmportWithEcmp(input1, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    AddPhysicalDevice(agent->host_name().c_str(), 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("lp1", 1, "lp1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("logical-interface", "lp1", "physical-interface", "pi1");
    AddLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");
    client->WaitForIdle();

    AddSg("sg1", 1);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet8", "security-group", "sg1");
    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    Ip4Address sip = Ip4Address::from_string("8.1.1.1");
    Ip4Address dip = Ip4Address::from_string("8.1.1.2");
    VmInterface *vintf = VmInterfaceGet(8);
    SendArpReq(vintf->id(), vintf->vrf()->vrf_id(), sip.to_ulong(), dip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == true);

    AgentRoute *rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    const ArpNH *arp_nh = static_cast<const ArpNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(arp_nh->GetInterface() == vintf);
    EXPECT_TRUE(arp_nh->PolicyEnabled() == true);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);

    DelLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    DelLink("security-group", "sg1", "access-control-list", "acl1");
    DelAcl("acl1");
    DelNode("security-group", "sg1");
    DelLink("virtual-machine-interface", "vnet8", "security-group", "sg1");
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Check leaked routes dont get updated
//when original route changes
TEST_F(ArpTest, DISABLED_SubnetResolveWithSg1) {
  struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:02", 2, 2}
    };

    client->Reset();
    //Create VM interface with policy
    CreateVmportWithEcmp(input1, 2, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    AddPhysicalDevice(agent->host_name().c_str(), 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("lp1", 1, "lp1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("logical-interface", "lp1", "physical-interface", "pi1");
    AddLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");
    client->WaitForIdle();

    VmInterface *vintf9 = VmInterfaceGet(9);

    AddSg("sg1", 1);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    client->WaitForIdle();
    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "vnet8");
    Ip4Address subnet = Ip4Address::from_string("8.1.1.0");
    InetUnicastAgentRouteTable::AddResolveRoute(vintf9->peer(), "vrf2",
                                                subnet, 24, key, 0, false,
                                                "vn1", SecurityGroupList());
    client->WaitForIdle();

    Ip4Address sip = Ip4Address::from_string("9.1.1.1");
    Ip4Address dip = Ip4Address::from_string("8.1.1.2");
    Ip4Address dip1 = Ip4Address::from_string("8.1.1.3");
    SendArpReq(vintf9->id(), vintf9->vrf()->vrf_id(), sip.to_ulong(), dip.to_ulong());
    SendArpReq(vintf9->id(), vintf9->vrf()->vrf_id(), sip.to_ulong(), dip1.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000,
             Agent::GetInstance()->GetArpProto()->GetArpCacheSize() == 5);

    WAIT_FOR(500, 1000, RouteFind("vrf2", "8.1.1.2", 32) == true);
    WAIT_FOR(500, 1000, RouteFind("vrf2", "8.1.1.3", 32) == true);
    AddLink("virtual-machine-interface", "vnet8", "security-group", "sg1");
    client->WaitForIdle();

    //Verify that route update on vrf1 doesnt reevaluate vrf2 arp routes
    AgentRoute *rt = RouteGet("vrf2", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 0);
    rt = RouteGet("vrf2", dip1, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 0);

    rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);
    rt = RouteGet("vrf1", dip1, 32);
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);

    //Resync the same on leaked vrf
    InetUnicastAgentRouteTable::AddResolveRoute(vintf9->peer(), "vrf2",
                                                subnet, 24, key, 0, false,
                                                "vn1", SecurityGroupList(1));
    client->WaitForIdle();

    rt = RouteGet("vrf2", dip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);

    rt = RouteGet("vrf2", dip1, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);

    rt = RouteGet("vrf1", dip, 32);
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);
    rt = RouteGet("vrf1", dip1, 32);
    EXPECT_TRUE(rt->GetActivePath()->sg_list().size() == 1);

    DelLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    DelLink("security-group", "sg1", "access-control-list", "acl1");
    DelAcl("acl1");
    DelNode("security-group", "sg1");
    InetUnicastAgentRouteTable::DeleteReq(vintf9->peer(), "vrf2",
                                          subnet, 24, NULL);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 2, true, 1);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, RouteFind("vrf1", "8.1.1.2", 32) == false);
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize());

    EXPECT_FALSE(VmPortFind(8));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(ArpTest, IntfArpReqTest_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    Interface *itf = VmPortGet(1);
    VmInterface *vmi = static_cast<VmInterface *>(itf);
    VrfEntry *vrf = VrfGet("vrf1");

    Agent::GetInstance()->GetArpProto()->ClearInterfaceArpStats(vmi->id());
    Ip4Address arp_tip = Ip4Address::from_string("1.1.1.2");
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize()); // For GW
    SendArpReq(vmi->id(), vrf->vrf_id(), vmi->primary_ip_addr().to_ulong(),
               arp_tip.to_ulong());
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(arp_tip.to_ulong(), "vrf1"));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetArpProto()->ArpRequestStatsCounter(vmi->id()) >= 1U));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

TEST_F(ArpTest, IntfArpReqTest_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    Interface *itf = VmPortGet(1);
    VmInterface *vmi = static_cast<VmInterface *>(itf);
    VrfEntry *vrf = VrfGet("vrf1");

    EXPECT_TRUE(vmi->GetVifMac(agent) == agent->vrrp_mac());
    Agent::GetInstance()->GetArpProto()->ClearInterfaceArpStats(vmi->id());
    Ip4Address arp_tip = Ip4Address::from_string("1.1.1.2");
    EXPECT_EQ(1U, Agent::GetInstance()->GetArpProto()->GetArpCacheSize()); // For GW
    SendArpReq(vmi->id(), vrf->vrf_id(), vmi->primary_ip_addr().to_ulong(),
               arp_tip.to_ulong());
    WaitForCompletion(2);
    EXPECT_TRUE(FindArpNHEntry(arp_tip.to_ulong(), "vrf1"));
    EXPECT_TRUE(FindArpRoute(arp_tip.to_ulong(), "vrf1"));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetArpProto()->ArpRequestStatsCounter(vmi->id()) >= 1U));
    SendArpReply(vmi->id(), vrf->vrf_id(), vmi->primary_ip_addr().to_ulong(),
                 arp_tip.to_ulong());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (1U == Agent::GetInstance()->GetArpProto()->ArpReplyStatsCounter(vmi->id())));
    WAIT_FOR(500, 1000, (1U == Agent::GetInstance()->GetArpProto()->ArpResolvedStatsCounter(vmi->id())));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
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
