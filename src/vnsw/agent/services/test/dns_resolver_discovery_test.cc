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

        //TestInit initilaizes xmpp connection to 127.0.0.1, so disconnect
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->DisConnect();
        client->WaitForIdle();

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

        dnshdr *dns = (dnshdr *) buf;
        BindUtil::BuildDnsHeader(dns, g_xid, DNS_QUERY_RESPONSE,
                                 DNS_OPCODE_QUERY, 0, 0, 0, 0);
        if (flag) {
            dns->flags.ret = DNS_ERR_NO_SUCH_NAME;
            Agent::GetInstance()->GetDnsProto()->SendDnsIpc(buf, sizeof(dnshdr));
            return;
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

    void SendDnsParseRespError(int numQues, DnsItem *items, int numAuth, DnsItem *auth,
                               int numAdd, DnsItem *add) {
        uint16_t len = 1024;
        uint8_t *buf  = new uint8_t[len];
        memset(buf, 0, len);

        dnshdr *dns = (dnshdr *) buf;
        BindUtil::BuildDnsHeader(dns, g_xid, DNS_QUERY_RESPONSE,
                                 DNS_OPCODE_QUERY, 0, 0, 0, 0);
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
        for (int i = 0; i < (numQues-1); i++)
            ptr = BindUtil::AddAnswerSection(ptr, items[i], len);

        for (int i = 0; i < (numAuth-1); i++)
            ptr = BindUtil::AddAnswerSection(ptr, auth[i], len);

        for (int i = 0; i < numAdd; i++)
            ptr = BindUtil::AddAnswerSection(ptr, add[i], len);

        Agent::GetInstance()->GetDnsProto()->SendDnsIpc(buf, len);
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

    DnsProto::DnsStats stats;
    int count = 0;
    Agent::GetInstance()->GetDnsProto()->set_timeout(30);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(1);
    Agent::GetInstance()->GetDnsProto()->ClearStats();
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items);
    client->WaitForIdle();
    CHECK_CONDITION(stats.fail < 1);
    CHECK_STATS(stats, 1, 0, 0, 0, 1, 0);
    Agent::GetInstance()->GetDnsProto()->set_timeout(2000);
    Agent::GetInstance()->GetDnsProto()->set_max_retries(2);
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

    // all good DNS query responses
    while (!Agent::GetInstance()->GetDnsProto()->IsDnsQueryInProgress(g_xid))
        g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 3, 1, 2, 0, 0, 0);
    g_xid++;
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 3, 1, 2, 0, 0, 0);
    g_xid++;
    SendDnsResp(1, a_items, 1, auth_items, 1, add_items);
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 1);
    CHECK_STATS(stats, 3, 1, 2, 0, 0, 0);

    //all good DNS query responses
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 5, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, a_items, 5, auth_items, 5, add_items);
    g_xid++;
    SendDnsResp(5, a_items, 5, auth_items, 5, add_items);
    g_xid++;
    SendDnsResp(5, a_items, 5, auth_items, 5, add_items);
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 2);
    CHECK_STATS(stats, 4, 2, 2, 0, 0, 0);

    //first response - no dmain name (DNS_ERR_NO_SUCH_NAME)
    //DNS client gets the second and third valid resolved response
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 5, ptr_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, ptr_items, 5, auth_items, 5, add_items, true);
    usleep(1000);
    CHECK_STATS(stats, 4, 2, 2, 0, 0, 0);
    g_xid++;
    SendDnsResp(5, ptr_items, 5, auth_items, 5, add_items);
    g_xid++;
    SendDnsResp(5, ptr_items, 5, auth_items, 5, add_items);
    usleep(1000);
    CHECK_CONDITION(stats.resolved < 3);
    CHECK_STATS(stats, 5, 3, 2, 0, 0, 0);

    //second, third bad response - no dmain name (DNS_ERR_NO_SUCH_NAME)
    //DNS client gets the first valid resolved response
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 5, ptr_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, ptr_items, 5, auth_items, 5, add_items);
    CHECK_CONDITION(stats.resolved < 4);
    CHECK_STATS(stats, 6, 4, 2, 0, 0, 0);
    g_xid++;
    SendDnsResp(5, ptr_items, 5, auth_items, 0, add_items, true);
    g_xid++;
    SendDnsResp(5, ptr_items, 5, auth_items, 0, add_items, true);
    CHECK_STATS(stats, 6, 4, 2, 0, 0, 0);

    //all bad response - no dmain name (DNS_ERR_NO_SUCH_NAME)
    //DNS client gets no-domain-name second response
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 3, a_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsResp(5, cname_items, 0, NULL, 0, NULL, true);
    g_xid++;
    SendDnsResp(5, cname_items, 0, NULL, 0, NULL, true);
    g_xid++;
    SendDnsResp(5, cname_items, 0, NULL, 0, NULL, true);
    CHECK_CONDITION(stats.fail < 1);
    // check fail stats incremented
    CHECK_STATS(stats, 7, 4, 2, 0, 1, 0);

    //all bad response, parse failure
    //DNS client receives no response.
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 3, ptr_items);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    SendDnsParseRespError(3, ptr_items, 3, auth_items, 3, add_items);
    g_xid++;
    SendDnsParseRespError(3, ptr_items, 3, auth_items, 3, add_items);
    g_xid++;
    SendDnsParseRespError(3, ptr_items, 3, auth_items, 3, add_items);
    CHECK_CONDITION(stats.resolved < 4);
    CHECK_STATS(stats, 8, 4, 2, 0, 1, 0);

    // Unsupported case
    dns_flags flags = default_flags;
    flags.op = 1;
    flags.cd = 1;
    SendDnsReq(DNS_OPCODE_QUERY, GetItfId(0), 1, a_items, flags);
    g_xid++;
    usleep(1000);
    client->WaitForIdle();
    CHECK_CONDITION(stats.unsupported < 1);
    CHECK_STATS(stats, 9, 4, 2, 1, 1, 0);

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


int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    Agent::GetInstance()->reset_controller_ifmap_xmpp_server(0);
    Agent::GetInstance()->reset_controller_ifmap_xmpp_server(1);

    Agent::GetInstance()->reset_dns_server(0);
    Agent::GetInstance()->reset_dns_server(1);

    usleep(100000);
    client->WaitForIdle();

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    // Bind to a local port and use it as the port for DNS.
    // Avoid using port 53 as there could be a local DNS server running.
    boost::system::error_code ec;
    udp::socket sock(*Agent::GetInstance()->event_manager()->io_service());
    udp::endpoint ep;
    sock.open(udp::v4(), ec);
    sock.bind(udp::endpoint(udp::v4(), 0), ec);
    ep = sock.local_endpoint(ec);

    udp::socket sock2(*Agent::GetInstance()->event_manager()->io_service());
    udp::endpoint ep2;
    sock2.open(udp::v4(), ec);
    sock2.bind(udp::endpoint(udp::v4(), 0), ec);
    ep2 = sock2.local_endpoint(ec);

    udp::socket sock3(*Agent::GetInstance()->event_manager()->io_service());
    udp::endpoint ep3;
    sock3.open(udp::v4(), ec);
    sock3.bind(udp::endpoint(udp::v4(), 0), ec);
    ep3 = sock3.local_endpoint(ec);

    // Discovery learnt response for DNS Resolvers
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(ep.port());
    ds_response.push_back(resp);

    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(ep2.port());
    ds_response.push_back(resp);

    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(ep3.port());
    ds_response.push_back(resp);

    Agent::GetInstance()->UpdateDiscoveryDnsServerResponseList(ds_response);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
