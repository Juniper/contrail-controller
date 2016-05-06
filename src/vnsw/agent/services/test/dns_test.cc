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
#include <pugixml/pugixml.hpp>
#include <services/dns_proto.h>
#include <vr_interface.h>
#include "bind/bind_util.h"
#include "bind/bind_resolver.h"
#include <test/test_cmn_util.h>
#include <services/services_sandesh.h>
#include <controller/controller_dns.h>
#include "vr_types.h"

using boost::asio::ip::udp;

#define DNS_CLIENT_PORT 9999
#define MAX_WAIT_COUNT 5000
#define BUF_SIZE 8192
char src_mac[ETHER_ADDR_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[ETHER_ADDR_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };
unsigned long src_ip = 1234;
unsigned long dest_ip = 5678;
short ifindex = 1;

#define MAX_ITEMS 5
uint16_t g_xid;
dns_flags default_flags;
DnsItem a_items[MAX_ITEMS];
DnsItem ptr_items[MAX_ITEMS];
DnsItem cname_items[MAX_ITEMS];
DnsItem auth_items[MAX_ITEMS];
DnsItem add_items[MAX_ITEMS];
DnsItem mx_items[MAX_ITEMS];

std::string names[MAX_ITEMS] = {"www.google.com",
                                "www.cnn.com",
                                "test.example.com",
                                "contrail.juniper.net",
                                "movies" };
std::string addresses[MAX_ITEMS] = {"1.2.3.4",
                                    "5.6.7.8",
                                    "1.1.2.2",
                                    "4.6.8.2",
                                    "12.24.46.68" };
std::string ptr_names[MAX_ITEMS] = {"4.3.2.1.in-addr.arpa",
                                    "8.7.6.5.in-addr.arpa",
                                    "2.2.1.1.in-addr.arpa",
                                    "2.8.6.4.in-addr.arpa",
                                    "68.46.24.12.in-addr.arpa" };
std::string cname_names[MAX_ITEMS] = {"www.google.com",
                                      "server.google.com",
                                      "www.cnn.com",
                                      "host1.cnn.akamai.net",
                                      "test.example.com" };
std::string cname_data[MAX_ITEMS] = {"server.google.com",
                                     "2.23.11.55",
                                     "host1.cnn.akamai.net",
                                     "29.23.45.39",
                                     "58.45.2.1" };
std::string auth_names[MAX_ITEMS] = {"ns.google.com",
                                     "ns.cnn.com",
                                     "ns1.test.example.com",
                                     "nameserver.contrail.juniper.net",
                                     "ns.local" };
std::string auth_data[MAX_ITEMS] = {"8.8.8.254",
                                    "7.7.7.254",
                                    "34.34.34.254",
                                    "67.49.23.254",
                                    "127.0.0.1" };

#define CHECK_CONDITION(condition)                                       \
            count = 0;                                                   \
            do {                                                         \
                usleep(1000);                                            \
                client->WaitForIdle();                                   \
                stats = Agent::GetInstance()->GetDnsProto()->GetStats(); \
                if (++count == MAX_WAIT_COUNT)                           \
                    assert(0);                                           \
            } while (condition);                                         \

class DnsTest : public ::testing::Test {
public:
    DnsTest() { 
        Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
        Agent::GetInstance()->set_ifmap_active_xmpp_server("127.0.0.1", 0);
        rid_ = Agent::GetInstance()->interface_table()->Register(
                boost::bind(&DnsTest::ItfUpdate, this, _2));
        for (int i = 0; i < MAX_ITEMS; i++) {
            a_items[i].eclass   = ptr_items[i].eclass   = DNS_CLASS_IN;
            a_items[i].type     = DNS_A_RECORD;
            ptr_items[i].type   = DNS_PTR_RECORD;
            a_items[i].ttl      = ptr_items[i].ttl      = (i + 1) * 1000;
            a_items[i].name     = names[i];
            a_items[i].data     = addresses[i];
            ptr_items[i].name   = ptr_names[i];
            ptr_items[i].data   = names[i];

            cname_items[i].eclass = DNS_CLASS_IN;
            if (i%2)
                cname_items[i].type = DNS_A_RECORD;
            else
                cname_items[i].type = DNS_CNAME_RECORD;
            cname_items[i].ttl = (i + 1) * 2000;
            cname_items[i].name = cname_names[i];
            cname_items[i].data = cname_data[i];

            mx_items[i].eclass = DNS_CLASS_IN;
            mx_items[i].type = DNS_MX_RECORD;
            mx_items[i].ttl = (i + 1) * 2000;
            mx_items[i].priority = (i + 1) * 20;
            mx_items[i].name = names[i];
            mx_items[i].data = cname_names[i];

            auth_items[i].eclass = DNS_CLASS_IN;
            auth_items[i].type = DNS_NS_RECORD;
            auth_items[i].ttl = 2000;
            auth_items[i].name = auth_names[i];
            auth_items[i].data = auth_data[i];

            add_items[i].eclass = DNS_CLASS_IN;
            add_items[i].type = DNS_TYPE_SOA;
            add_items[i].ttl = 2000;
            add_items[i].soa.primary_ns = auth_names[i];
            add_items[i].soa.mailbox = "mx" + auth_names[i];
            add_items[i].soa.serial = 100;
            add_items[i].soa.refresh = add_items[i].soa.retry = 500;
            add_items[i].soa.expiry = add_items[i].soa.ttl = 1000;
        }
    }
    ~DnsTest() { 
        Agent::GetInstance()->interface_table()->Unregister(rid_);
    }

    void ItfUpdate(DBEntryBase *entry) {
        Interface *itf = static_cast<Interface *>(entry);
        tbb::mutex::scoped_lock lock(mutex_);
        if (entry->IsDeleted()) {
            LOG(DEBUG, "DNS test : interface deleted " << itf_id_);
            itf_id_ = 0;
            itf_count_ = 0;
        } else {
            itf_count_ = 1;
            itf_id_ = itf->id();
            LOG(DEBUG, "DNS test : interface added " << itf_id_);
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
        return itf_id_; 
    }

    void CHECK_STATS(DnsProto::DnsStats &stats, uint32_t req, uint32_t res,
                     uint32_t rexmit, uint32_t unsupp, uint32_t fail, uint32_t drop) {
        EXPECT_EQ(req, stats.requests);
        EXPECT_EQ(res, stats.resolved);
        EXPECT_EQ(rexmit, stats.retransmit_reqs);
        EXPECT_EQ(unsupp, stats.unsupported);
        EXPECT_EQ(fail, stats.fail);
        EXPECT_EQ(drop, stats.drop);
    }

    void CheckSandeshResponse(Sandesh *sandesh) {
    }

    int SendDnsQuery(dnshdr *dns, int numItems, DnsItem *items, dns_flags flags) {
        DnsItems questions;
        for (int i = 0; i < numItems; i++) {
            questions.push_back(items[i]);
        }
        int len = BindUtil::BuildDnsQuery((uint8_t *)dns, 0x0102,
                                          "default-vdns", questions);
        dns->flags = flags;
        return len;
    }

    int SendDnsUpdate(dnshdr *dns, BindUtil::Operation op, std::string domain,
                      std::string zone, int numItems, DnsItem *items) {
        DnsItems updates;
        for (int i = 0; i < numItems; i++) {
            updates.push_back(items[i]);
        }
        return BindUtil::BuildDnsUpdate((uint8_t *)dns, op, 0x0202,
                                        domain, zone, updates);
    }

    void SendDnsReq(int type, short itf_index, int numItems,
                    DnsItem *items, dns_flags flags = default_flags,
                    bool update = false) {
        int len = 1024;
        uint8_t *buf  = new uint8_t[len];
        memset(buf, 0, len);

        struct ether_header *eth = (struct ether_header *)buf;
        eth->ether_dhost[5] = 1;
        eth->ether_shost[5] = 2;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(itf_index);
        agent->hdr_vrf = htons(0);
        agent->hdr_cmd = htons(AgentHdr::TRAP_NEXTHOP);

        eth = (struct ether_header *) (agent + 1);
        memcpy(eth->ether_dhost, dest_mac, ETHER_ADDR_LEN);
        memcpy(eth->ether_shost, src_mac, ETHER_ADDR_LEN);
        eth->ether_type = htons(0x800);

        struct ip *ip = (struct ip *) (eth + 1);
        ip->ip_hl = 5;
        ip->ip_v = 4;
        ip->ip_tos = 0;
        ip->ip_id = 0;
        ip->ip_off = 0;
        ip->ip_ttl = 16;
        ip->ip_p = IPPROTO_UDP;
        ip->ip_sum = 0;
        ip->ip_src.s_addr = htonl(src_ip);
        ip->ip_dst.s_addr = htonl(dest_ip);

        udphdr *udp = (udphdr *) (ip + 1);
        udp->uh_sport = htons(DNS_CLIENT_PORT);
        udp->uh_dport = htons(DNS_SERVER_PORT);
        udp->uh_sum = 0;

        dnshdr *dns = (dnshdr *) (udp + 1);
        if (type == DNS_OPCODE_QUERY) {
            len = SendDnsQuery(dns, numItems, items, flags);
        } else if (type == DNS_OPCODE_UPDATE) {
            BindUtil::Operation op =
                update ? BindUtil::ADD_UPDATE : BindUtil::DELETE_UPDATE;
            len = SendDnsUpdate(dns, op, "vdns1", "test.contrail.juniper.net",
                                numItems, items);
        } else
            assert(0);

        len += sizeof(udphdr);
        udp->uh_ulen = htons(len);
        ip->ip_len = htons(len + sizeof(struct ip));

        len += sizeof(struct ip) + sizeof(struct ether_header) + Agent::GetInstance()->pkt()->pkt_handler()->EncapHeaderLen();
        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(buf, len);
    }

    void SendDnsResp(int numQues, DnsItem *items, int numAuth, DnsItem *auth,
                     int numAdd, DnsItem *add, bool flag = false) {
        uint16_t len = 1024;
        uint8_t *buf  = new uint8_t[len];
        memset(buf, 0, len);

        while (!Agent::GetInstance()->GetDnsProto()->IsDnsQueryInProgress(g_xid))
            g_xid++;
        dnshdr *dns = (dnshdr *) buf;
        BindUtil::BuildDnsHeader(dns, g_xid, DNS_QUERY_RESPONSE, 
                                 DNS_OPCODE_QUERY, 0, 0, 0, 0);
        if (flag) {
            dns->flags.ret = DNS_ERR_NO_SUCH_NAME;
        }
        dns->ques_rrcount = htons(numQues);
        dns->ans_rrcount = htons(numQues);
        dns->auth_rrcount = htons(numAuth);
        dns->add_rrcount = htons(numAdd);
        len = sizeof(dnshdr);
        uint8_t *ptr = (uint8_t *) (dns + 1);
        for (int i = 0; i < numQues; i++)
            ptr = BindUtil::AddQuestionSection(ptr, items[i].name, 
                                               items[i].type, items[i].eclass, 
                                               len);
        for (int i = 0; i < numQues; i++)
            ptr = BindUtil::AddAnswerSection(ptr, items[i], len);

        for (int i = 0; i < numAuth; i++)
            ptr = BindUtil::AddAnswerSection(ptr, auth[i], len);

        for (int i = 0; i < numAdd; i++)
            ptr = BindUtil::AddAnswerSection(ptr, add[i], len);

        Agent::GetInstance()->GetDnsProto()->SendDnsIpc(buf, len);
    }

    void FillDnsUpdateData(DnsUpdateData &data, int count) {
        data.virtual_dns = "vdns1";
        data.zone = "juniper.net";
        data.items.clear();
        for (int i = 0; i < count; ++i) {
            DnsItem item;
            item.name = "test";
            item.data = "1.2.3.4";
            item.ttl = i + 1;
            data.items.push_back(item);
        }
    }

    void CheckSendXmppUpdate() {
        // Call the SendXmppUpdate directly and check that all items are done
        AgentDnsXmppChannel *tmp_xmpp_channel = 
            new AgentDnsXmppChannel(Agent::GetInstance(), "server", 0);
        Agent *agent = Agent::GetInstance();
        boost::shared_ptr<PktInfo> pkt_info(new PktInfo(Agent::GetInstance(),
                                                        100, PktHandler::FLOW,
                                                        0));
        DnsHandler *dns_handler = new DnsHandler(agent, pkt_info, *agent->event_manager()->io_service());
        DnsUpdateData data;
        FillDnsUpdateData(data, 10);
        dns_handler->SendXmppUpdate(tmp_xmpp_channel, &data);
        EXPECT_EQ(data.items.size(), 10);
        FillDnsUpdateData(data, 20);
        dns_handler->SendXmppUpdate(tmp_xmpp_channel, &data);
        EXPECT_EQ(data.items.size(), 20);
        FillDnsUpdateData(data, 30);
        dns_handler->SendXmppUpdate(tmp_xmpp_channel, &data);
        EXPECT_EQ(data.items.size(), 30);
        delete dns_handler;
        delete tmp_xmpp_channel;
    }

    void FloatingIPSetup() {
        client->WaitForIdle();
        AddVm("vm1", 1);
        AddVn("vn1", 1);
        AddVn("default-project:vn2", 2);
        AddVrf("vrf1");
        AddVrf("default-project:vn2:vn2");
        AddPort("vnet1", 1);
        AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
        AddLink("virtual-network", "default-project:vn2",
                "routing-instance", "default-project:vn2:vn2");
        AddFloatingIpPool("fip-pool2", 2);
        AddFloatingIp("fip1", 2, "2.2.2.100");
        AddLink("floating-ip-pool", "fip-pool2", "virtual-network",
                "default-project:vn2");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool2");

        // Add vm-port interface to vrf link
        AddVmPortVrf("vmvrf1", "", 0);
        AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "routing-instance", "vrf1");
        AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
        client->WaitForIdle();

        client->WaitForIdle();
        EXPECT_TRUE(VmFind(1));
        EXPECT_TRUE(VnFind(1));
        EXPECT_TRUE(VnFind(2));
        EXPECT_TRUE(VrfFind("vrf1"));
        EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
        client->WaitForIdle();
    }

    void FloatingIPTearDown(bool delete_fip_vn) {
        DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
        DelLink("virtual-network", "default-project:vn2", "routing-instance",
                "default-project:vn2:vn2");
        DelLink("floating-ip-pool", "fip-pool2", "virtual-network",
                "default-project:vn2");
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool2");
        client->WaitForIdle();

        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool2");
        client->WaitForIdle();

        // Delete virtual-machine-interface to vrf link attribute
        DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "routing-instance", "vrf1");
        DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "virtual-machine-interface", "vnet1");
        DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
        client->WaitForIdle();

        DelPort("vnet1");
        DelVn("vn1");
        if (delete_fip_vn)
            DelVn("default-project:vn2");
        DelVrf("vrf1");
        DelVrf("default-project:vn2:vn2");
        DelVm("vm1");
        client->WaitForIdle();

        EXPECT_FALSE(VrfFind("vrf1"));
        EXPECT_FALSE(VrfFind("default-project:vn2:vn2"));
        EXPECT_FALSE(VnFind(1));
        if (delete_fip_vn)
            EXPECT_FALSE(VnFind(2));
        EXPECT_FALSE(VmFind(1));
    }
private:
    DBTableBase::ListenerId rid_;
    uint32_t itf_id_;
    uint32_t itf_count_;
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
    std::string Description() const { return "AsioRunEvent"; }
};

TEST_F(DnsTest, VirtualDnsReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] = 
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    Agent::GetInstance()->GetDnsProto()->set_timeout(30);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(1);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.fail < 1);
    CHECK_STATS(stats, 1, 0, 0, 0, 1, 0);
    Agent::GetInstance()->GetDnsProto()->set_timeout(30);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(1);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    // retransmit the same req a couple of times
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 3, 1, 2, 0, 0, 0);

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 5, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, a_items, 5, auth_items, 5, add_items);
    CHECK_CONDITION(stats.resolved < 2);
    CHECK_STATS(stats, 4, 2, 2, 0, 0, 0);

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 5, ptr_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, ptr_items, 5, auth_items, 5, add_items);
    CHECK_CONDITION(stats.resolved < 3);
    CHECK_STATS(stats, 5, 3, 2, 0, 0, 0);

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 3, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, cname_items, 0, NULL, 0, NULL);
    CHECK_CONDITION(stats.resolved < 4);
    CHECK_STATS(stats, 6, 4, 2, 0, 0, 0);

    // Unsupported case
    dns_flags flags = default_flags;
    flags.op = 1;
    flags.cd = 1;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items, flags);
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.unsupported < 1);
    CHECK_STATS(stats, 7, 4, 2, 1, 0, 0);

    // Failure response
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 3, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(2, a_items, 2, auth_items, 2, add_items, true);
    CHECK_CONDITION(stats.fail < 1);
    CHECK_STATS(stats, 8, 4, 2, 1, 1, 0);

    // Retrieve DNS entries via Introspect
    ShowDnsEntries *sand = new ShowDnsEntries();
    Sandesh::set_response_callback(
        boost::bind(&DnsTest::CheckSandeshResponse, this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    Agent::GetInstance()->GetDnsProto()->set_timeout(30);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(1);
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    g_xid++;
    usleep(100000); // wait for retry timer to expire
    client->WaitForIdle();
    CHECK_CONDITION(stats.drop < 1);
    CHECK_STATS(stats, 9, 4, 2, 1, 1, 1);
    Agent::GetInstance()->GetDnsProto()->set_timeout(2000);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(2);

    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, default_flags, true);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 5);
    CHECK_STATS(stats, 10, 5, 2, 1, 1, 1);
    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 6);
    CHECK_STATS(stats, 11, 6, 2, 1, 1, 1);

    //send MX requests
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, mx_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(4, mx_items, 0, NULL, 0, NULL);
    CHECK_CONDITION(stats.resolved < 7);
    CHECK_STATS(stats, 12, 7, 2, 1, 1, 1);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1"); 
    client->WaitForIdle();
}

// Order the config such that Ipam gets updated last
TEST_F(DnsTest, VirtualDnsIpamUpdateReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    AddIPAM("vn1", ipam_info, 3, "", "vdns1");
    client->WaitForIdle();

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();
}

// Order the config such that Vdns comes first
TEST_F(DnsTest, VirtualDnsVdnsFirstReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();
}

// Order the config such that Vdns comes first
TEST_F(DnsTest, VirtualDnsVdnsFirstReorderTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();
}

// Order the config such that Ipam comes first
TEST_F(DnsTest, VirtualDnsIpamFirstTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();
}

// Order the config such that Ipam comes first
TEST_F(DnsTest, VirtualDnsIpamFirstReorderTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    DnsProto::DnsStats stats;
    int count = 0;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    usleep(1000);
    client->WaitForIdle();
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();
}

TEST_F(DnsTest, VirtualDnsLinkLocalReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[5] = {
        { "test_service1", "169.254.1.10", 1000, "", fabric_ip_list, 100 },
        { "test_service1", "169.254.1.20", 2000, "", fabric_ip_list, 200 },
        { "test_service2", "169.254.1.30", 3000, "", fabric_ip_list, 300 },
        { "test_service3", "169.254.1.30", 4000, "", fabric_ip_list, 400 },
        { "test_service4", "169.254.1.50", 5000, "", fabric_ip_list, 500 }
    };
    AddLinkLocalConfig(services, 5);
    client->WaitForIdle();

    char vdns_attr[] =
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();

    DnsProto::DnsStats stats;
    int count = 0;
    DnsItem query_items[MAX_ITEMS] = a_items;
    query_items[0].name     = "test_service1";
    query_items[1].name     = "test_service2";
    query_items[2].name     = "test_service3";
    query_items[3].name     = "test_service4";

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, query_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    DnsItem query_items_2[MAX_ITEMS] = a_items;
    // keep the 0th element as it is
    query_items_2[1].name     = "test_service1";
    query_items_2[2].name     = "test_service3";
    query_items_2[3].name     = "test_service4";

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, query_items_2);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.requests < 1);
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    DnsItem ptr_query_items[MAX_ITEMS] = ptr_items;
    // keep the 0th element as it is
    ptr_query_items[1].name     = "10.1.254.169.in-addr.arpa";
    ptr_query_items[2].name     = "20.1.254.169.in-addr.arpa";
    ptr_query_items[3].name     = "30.1.254.169.in-addr.arpa";
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, ptr_query_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, ptr_items, 1, auth_items, 1, add_items);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();

    DelLinkLocalConfig();
    client->WaitForIdle();
}

TEST_F(DnsTest, DnsXmppTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char vdns_attr[] = 
        "<virtual-DNS-data>\
            <domain-name>test.contrail.juniper.net</domain-name>\
            <dynamic-records-from-client>true</dynamic-records-from-client>\
            <record-order>fixed</record-order>\
            <default-ttl-seconds>120</default-ttl-seconds>\
        </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    DnsProto::DnsStats stats;
    int count = 0;
    dns_flags flags = default_flags;
    flags.op = 1;
    flags.cd = 1;
    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, flags);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    //TODO : create an XMPP channel
    Agent::GetInstance()->GetDnsProto()->SendDnsUpdateIpc(NULL);

    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 2);
    CHECK_STATS(stats, 2, 2, 0, 0, 0, 0);

    DnsInfo *sand = new DnsInfo();
    Sandesh::set_response_callback(boost::bind(&DnsTest::CheckSandeshResponse, this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    CheckSendXmppUpdate();

    client->Reset();
    DelIPAM("vn1", "vdns1"); 
    client->WaitForIdle();
    DelVDNS("vdns1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    Agent::GetInstance()->GetDnsProto()->ClearStats();
}

TEST_F(DnsTest, DefaultDnsReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>default-dns-server</ipam-dns-method>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    DnsItem query_items[MAX_ITEMS] = a_items;
    query_items[0].name     = "localhost";
    query_items[1].name     = "localhost";

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items);
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items);
    usleep(1000);
    client->WaitForIdle();
    DnsProto::DnsStats stats;
    int count = 0;
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 1);
    EXPECT_EQ(2U, stats.requests);
    EXPECT_TRUE(stats.resolved == 1 || stats.resolved == 2);
    EXPECT_TRUE(stats.retransmit_reqs == 1 || stats.resolved == 2);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    DnsItem ptr_query_items[MAX_ITEMS] = ptr_items;
    ptr_query_items[0].name     = "1.0.0.127.in-addr.arpa";
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, ptr_query_items);
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_EQ(1U, stats.resolved);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

#if 0
    // Failure response
    query_items[0].name     = "test.non-existent.domain";
    query_items[1].name     = "onemore.non-existent.domain";
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items);
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.fail < 1);
    CHECK_STATS(stats, 1, 0, 0, 0, 1, 0);
#endif

    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 2, query_items, default_flags, true);
    client->WaitForIdle();
    CHECK_CONDITION(stats.unsupported < 1);
    CHECK_STATS(stats, 1, 0, 0, 1, 0, 0);

    client->Reset();
    DelIPAM("vn1", "vdns1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
}

TEST_F(DnsTest, DefaultDnsLinklocalReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[5] = {
        { "test_service1", "169.254.1.10", 1000, "", fabric_ip_list, 100 },
        { "test_service1", "169.254.1.20", 2000, "", fabric_ip_list, 200 },
        { "test_service2", "169.254.1.30", 3000, "", fabric_ip_list, 300 },
        { "test_service3", "169.254.1.30", 4000, "", fabric_ip_list, 400 },
        { "test_service4", "169.254.1.50", 5000, "", fabric_ip_list, 500 }
    };
    AddLinkLocalConfig(services, 5);
    client->WaitForIdle();

    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>default-dns-server</ipam-dns-method>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    DnsItem query_items[MAX_ITEMS] = a_items;
    query_items[0].name     = "test_service1";
    query_items[1].name     = "test_service2";
    query_items[2].name     = "test_service3";
    query_items[3].name     = "test_service4";

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, query_items);
    usleep(1000);
    client->WaitForIdle();
    DnsProto::DnsStats stats;
    int count = 0;
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_TRUE(stats.resolved == 1);
    EXPECT_TRUE(stats.retransmit_reqs == 0);

    DnsItem query_items_2[MAX_ITEMS] = a_items;
    query_items_2[0].name     = "test_service1";
    query_items_2[1].name     = "localhost";
    query_items_2[2].name     = "test_service3";
    query_items_2[3].name     = "test_service4";

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, query_items_2);
    usleep(1000);
    client->WaitForIdle();
    count = 0;
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 2);
    EXPECT_EQ(2U, stats.requests);
    EXPECT_TRUE(stats.resolved == 2);
    EXPECT_TRUE(stats.retransmit_reqs == 0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    DnsItem ptr_query_items[MAX_ITEMS] = ptr_items;
    ptr_query_items[0].name     = "10.1.254.169.in-addr.arpa";
    ptr_query_items[1].name     = "20.1.254.169.in-addr.arpa";
    ptr_query_items[2].name     = "30.1.254.169.in-addr.arpa";
    ptr_query_items[3].name     = "1.0.0.127.in-addr.arpa";
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 4, ptr_query_items);
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_EQ(1U, stats.resolved);
    Agent::GetInstance()->GetDnsProto()->ClearStats();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);

    DelLinkLocalConfig();
    client->WaitForIdle();
}

TEST_F(DnsTest, DnsDropTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    dns_flags flags = default_flags;
    DnsProto::DnsStats stats;
    int count = 0;
    DnsItem query_items[MAX_ITEMS] = a_items;
    query_items[0].name     = "localhost";
    query_items[1].name     = "localhost";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.fail < 1);
    CHECK_STATS(stats, 1, 0, 0, 0, 1, 0);

    client->Reset();
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>default-dns-server</ipam-dns-method>\n </network-ipam-mgmt>\n";
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    flags = default_flags;
    flags.req = 1;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items, flags);
    client->WaitForIdle();
    count = 0;
    CHECK_CONDITION(stats.drop < 1);
    CHECK_STATS(stats, 2, 0, 0, 0, 1, 1);

    client->Reset();
    DelIPAM("vn1", "vdns1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
}

// Check that floating ip entries are created in dns floating ip list
TEST_F(DnsTest, DnsFloatingIp) {
    FloatingIPSetup();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    AddPort(input[0].name, input[0].intf_id);
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", Ip4Address::from_string("2.2.2.100"), 32));

    // Check DNS entries for floating ips
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    const DnsProto::DnsFipSet &fip_set = dns_proto->fip_list();
    EXPECT_TRUE(fip_set.size() == 1);
    DnsProto::DnsFipSet::const_iterator fip_it = fip_set.begin();
    EXPECT_TRUE((*fip_it)->floating_ip_.to_string() == "2.2.2.100");

    // Cleanup
    LOG(DEBUG, "Doing cleanup");

    IntfCfgDel(input, 0);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();

    // Delete links
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    // Check DNS entries for floating ips are cleared
    const DnsProto::DnsFipSet &fip_set_new = dns_proto->fip_list();
    EXPECT_TRUE(fip_set_new.size() == 0);

    FloatingIPTearDown(true);
}

// Check that floating ip entries are deleted when vn is deleted without fip de-association
TEST_F(DnsTest, DnsFloatingIp_VnDelWithoutFipDeAssoc) {
    FloatingIPSetup();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    AddPort(input[0].name, input[0].intf_id);
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", Ip4Address::from_string("2.2.2.100"), 32));

    // Check DNS entries for floating ips
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    const DnsProto::DnsFipSet &fip_set = dns_proto->fip_list();
    EXPECT_TRUE(fip_set.size() == 1);
    DnsProto::DnsFipSet::const_iterator fip_it = fip_set.begin();
    EXPECT_TRUE((*fip_it)->floating_ip_.to_string() == "2.2.2.100");

    // Cleanup
    LOG(DEBUG, "Doing cleanup");

    // Delete links
    // DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    // DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    // DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    // DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    // client->WaitForIdle();

    DelVn("default-project:vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(2));

    // Check DNS entries for floating ips are cleared
    const DnsProto::DnsFipSet &fip_set_new = dns_proto->fip_list();
    EXPECT_TRUE(fip_set_new.size() == 0);

    FloatingIPTearDown(false);
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    // Bind to a local port and use it as the port for DNS.
    // Avoid using port 53 as there could be a local DNS server running.
    boost::system::error_code ec;
    udp::socket sock(*Agent::GetInstance()->event_manager()->io_service());
    udp::endpoint ep;
    sock.open(udp::v4(), ec);
    sock.bind(udp::endpoint(udp::v4(), 0), ec);
    ep = sock.local_endpoint(ec);

    Agent::GetInstance()->set_dns_server("127.0.0.1", 0);
    Agent::GetInstance()->set_dns_server_port(ep.port(), 0);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
