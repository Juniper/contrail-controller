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
#include <pkt/tap_interface.h>
#include <pkt/test_tap_interface.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <openstack/instance_service_server.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <services/dns_proto.h>
#include <vr_interface.h>
#include "uve/uve_init.h"
#include "bind/bind_util.h"
#include "bind/bind_resolver.h"
#include <test/test_cmn_util.h>
#include <services/services_sandesh.h>
#include "vr_types.h"

#define MAC_LEN 6
#define DNS_CLIENT_PORT 9999
#define MAX_WAIT_COUNT 500
#define BUF_SIZE 8192
char src_mac[MAC_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[MAC_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };
ulong src_ip = 1234;
ulong dest_ip = 5678;
short ifindex = 1;

#define MAX_ITEMS 5
uint16_t g_xid;
DnsItem a_items[MAX_ITEMS];
DnsItem ptr_items[MAX_ITEMS];
DnsItem cname_items[MAX_ITEMS];
DnsItem auth_items[MAX_ITEMS];
DnsItem add_items[MAX_ITEMS];

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

class DnsTesting : public ::testing::Test {
public:
    DnsTesting() { 
        Agent::GetInstance()->SetXmppServer("127.0.0.1", 0);
        Agent::GetInstance()->SetXmppCfgServer("127.0.0.1", 0);
        Agent::GetInstance()->SetXmppDnsCfgServer(0);
        rid_ = Agent::GetInstance()->GetInterfaceTable()->Register(
                boost::bind(&DnsTesting::ItfUpdate, this, _2));
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
    ~DnsTesting() { 
        Agent::GetInstance()->GetInterfaceTable()->Unregister(rid_);
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

    int SendDnsQuery(dnshdr *dns, int numItems, DnsItem *items, bool error_req) {
        std::vector<DnsItem> questions;
        for (int i = 0; i < numItems; i++) {
            questions.push_back(items[i]);
        }
        int len = BindUtil::BuildDnsQuery((uint8_t *)dns, 0x0102,
                                          "default-vdns", questions);
        if (error_req) {
            dns->flags.op = 1;
            dns->flags.cd = 1;
        }
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
                    DnsItem *items, bool flag = false) {
        int len = 1024;
        uint8_t *buf  = new uint8_t[len];
        memset(buf, 0, len);

        ethhdr *eth = (ethhdr *)buf;
        eth->h_dest[5] = 1;
        eth->h_source[5] = 2;
        eth->h_proto = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(itf_index);
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
        ip->protocol = IPPROTO_UDP;
        ip->check = 0;
        ip->saddr = htonl(src_ip);
        ip->daddr = htonl(dest_ip);

        udphdr *udp = (udphdr *) (ip + 1);
        udp->source = htons(DNS_CLIENT_PORT);
        udp->dest = htons(DNS_SERVER_PORT);
        udp->check = 0;

        dnshdr *dns = (dnshdr *) (udp + 1);
        if (type == DNS_OPCODE_QUERY) {
            len = SendDnsQuery(dns, numItems, items, flag);
        } else if (type == DNS_OPCODE_UPDATE) {
            BindUtil::Operation op = 
                flag ? BindUtil::ADD_UPDATE : BindUtil::DELETE_UPDATE;
            len = SendDnsUpdate(dns, op, "vdns1", "test.contrail.juniper.net",
                                numItems, items);
        } else
            assert(0);

        len += sizeof(udphdr);
        udp->len = htons(len);
        ip->tot_len = htons(len + sizeof(iphdr));
        len += sizeof(iphdr) + sizeof(ethhdr) + IPC_HDR_LEN;
        TestTapInterface *tap = (TestTapInterface *)
            (Agent::GetInstance()->pkt()->pkt_handler()->tap_interface());
        tap->GetTestPktHandler()->TestPktSend(buf, len);
    }

    void SendDnsResp(int numQues, DnsItem *items, int numAuth, DnsItem *auth,
                     int numAdd, DnsItem *add, bool flag = false) {
        uint16_t len = 1024;
        uint8_t *buf  = new uint8_t[len];
        memset(buf, 0, len);

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

        DnsHandler::SendDnsIpc(buf);
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
        Agent::GetInstance()->GetEventManager()->Run();
        return true;
    }
};

TEST_F(DnsTesting, VirtualDnsReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129"},
        {"7.8.9.0", 24, "7.8.9.12"},
        {"1.1.1.0", 24, "1.1.1.200"},
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

    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

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
    DnsProto::DnsStats stats;
    int count = 0;
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
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items, true);
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

    Agent::GetInstance()->GetDnsProto()->SetTimeout(30);
    Agent::GetInstance()->GetDnsProto()->SetMaxRetries(1);
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    g_xid++;
    usleep(100000); // wait for retry timer to expire
    client->WaitForIdle();
    CHECK_CONDITION(stats.drop < 1);
    CHECK_STATS(stats, 9, 4, 2, 1, 1, 1);
    Agent::GetInstance()->GetDnsProto()->SetTimeout(2000);
    Agent::GetInstance()->GetDnsProto()->SetMaxRetries(2);

    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, true);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 5);
    CHECK_STATS(stats, 10, 5, 2, 1, 1, 1);
    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, false);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 6);
    CHECK_STATS(stats, 11, 6, 2, 1, 1, 1);

    client->Reset();
    DelIPAM("vn1", "vdns1"); 
    client->WaitForIdle();
    DelVDNS("vdns1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
}

TEST_F(DnsTesting, DnsXmppTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129"},
        {"7.8.9.0", 24, "7.8.9.12"},
        {"1.1.1.0", 24, "1.1.1.200"},
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
    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, true);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 1, 1, 0, 0, 0, 0);

    //TODO : create an XMPP channel
    DnsHandler::SendDnsUpdateIpc(NULL);

    SendDnsReq(DNS_OPCODE_UPDATE, GetItfId(0), 1, a_items, false);
    client->WaitForIdle();
    CHECK_CONDITION(stats.resolved < 2);
    CHECK_STATS(stats, 2, 2, 0, 0, 0, 0);

    DnsInfo *sand = new DnsInfo();
    Sandesh::set_response_callback(boost::bind(&DnsTesting::CheckSandeshResponse, this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

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

TEST_F(DnsTesting, DefaultDnsReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129"},
        {"7.8.9.0", 24, "7.8.9.12"},
        {"1.1.1.0", 24, "1.1.1.200"},
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

    // Failure response
    query_items[0].name     = "test.non-existent.domain";
    query_items[1].name     = "onemore.non-existent.domain";
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 2, query_items);
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.fail < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_EQ(1U, stats.fail);

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

#if 0
TEST_F(DnsTesting, DnsDropTest) {
    SendDnsReq(ifindex, 1, DNS_A_RECORD);
    DnsProto::DnsStats stats;
    int count = 0;
    do {
        usleep(1000);
        client->WaitForIdle();
        stats = DnsHandler::GetStats();
        if (++count == MAX_WAIT_COUNT)
            assert(0);
    } while (stats.drop < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_EQ(1U, stats.drop);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
}
#endif

void RouterIdDepInit() {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    //TestShutdown();
    delete client;
    return ret;
}
