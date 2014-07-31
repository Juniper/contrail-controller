/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include <netinet/if_ether.h>
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
#include <services/dhcp_proto.h>
#include <vr_interface.h>
#include <test/test_cmn_util.h>
#include <services/services_sandesh.h>
#include "vr_types.h"

#define MAC_LEN 6
#define CLIENT_REQ_IP "1.2.3.4"
#define CLIENT_REQ_PREFIX "1.2.3.0"
#define CLIENT_REQ_GW "1.2.3.1"
#define MAX_WAIT_COUNT 500
#define BUF_SIZE 8192
char src_mac[MAC_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[MAC_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };
#define HOST_ROUTE_STRING "Host Routes : 10.1.1.0/24 -> 1.1.1.200;10.1.2.0/24 -> 1.1.1.200;150.25.75.0/24 -> 1.1.1.200;192.168.1.128/28 -> 1.1.1.200;"
#define CHANGED_HOST_ROUTE_STRING "Host Routes : 150.2.2.0/24 -> 1.1.1.200;192.1.1.1/28 -> 1.1.1.200;"
#define IPAM_DHCP_OPTIONS_STRING "DNS : 1.2.3.4; Domain Name : test.com; NTP : 3.2.14.5"
#define SUBNET_DHCP_OPTIONS_STRING "DNS : 11.12.13.14; Domain Name : subnet.com; NTP : 13.12.14.15;"
#define PORT_DHCP_OPTIONS_STRING "DNS : 21.22.23.24; Domain Name : interface.com; NTP : 23.22.24.25;"
#define PORT_HOST_ROUTE_STRING "Host Routes : 99.2.3.0/24 -> 1.1.1.200;99.5.0.0/16 -> 1.1.1.200;"

#define DHCP_CHECK(condition)                                                  \
                    do {                                                       \
                      usleep(1000);                                            \
                      client->WaitForIdle();                                   \
                      stats = Agent::GetInstance()->GetDhcpProto()->GetStats();\
                      if (++count == MAX_WAIT_COUNT)                           \
                          assert(0);                                           \
                    } while (condition);                                       \

class DhcpTest : public ::testing::Test {
public:
    DhcpTest() : itf_count_(0) {
        rid_ = Agent::GetInstance()->interface_table()->Register(
                boost::bind(&DhcpTest::ItfUpdate, this, _2));
    }

    ~DhcpTest() {
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
                LOG(DEBUG, "DHCP test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "DHCP test : interface added " << itf->id());
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

    std::size_t fabric_interface_id() {
        PhysicalInterfaceKey key(Agent::GetInstance()->params()->eth_port().c_str());
        Interface *intf = static_cast<Interface *>
            (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
        if (intf)
            return intf->id();
        else
            assert(0);
    }

    void CheckSandeshResponse(Sandesh *sandesh, bool check_host_routes,
                              const char *option_string,
                              const char *dhcp_option_string) {
        if (memcmp(sandesh->Name(), "DhcpPktSandesh",
                   strlen("DhcpPktSandesh")) == 0) {
            DhcpPktSandesh *dhcp_pkt = (DhcpPktSandesh *)sandesh;
            if (check_host_routes) {
                DhcpPkt pkt = (dhcp_pkt->get_pkt_list())[3];
                if (pkt.dhcp_hdr.dhcp_options.find(option_string) ==
                    std::string::npos) {
                    assert(0);
                }
                if (pkt.dhcp_hdr.dhcp_options.find(dhcp_option_string) ==
                    std::string::npos) {
                    assert(0);
                }
                // Also check that when host routes are specified, GW option is not sent
                if (pkt.dhcp_hdr.dhcp_options.find("Gateway : ") !=
                    std::string::npos) {
                    assert(0);
                }
            }
        }
    }

    void CheckAllSandeshResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "PktStats",
                   strlen("PktStats")) == 0) {
            PktStats *pkt_stats = (PktStats *)sandesh;
            EXPECT_EQ(pkt_stats->get_total_rcvd(), 9);
            EXPECT_EQ(pkt_stats->get_dhcp_rcvd(), 9);
        }
    }

    void ClearPktTrace() {
        // clear existing ptk trace entries
        ClearAllInfo *clear_info = new ClearAllInfo();
        clear_info->HandleRequest();
        client->WaitForIdle();
        clear_info->Release();
    }

    void SendRelayResponse(uint8_t msg_type, uint8_t *options, int num_options,
                           uint32_t yiaddr, uint32_t vmifindex = 0) {
        int len = 512;
        uint8_t *buf = new uint8_t[len];
        memset(buf, 0, len);

        dhcphdr *dhcp = (dhcphdr *) buf;
        dhcp->op = BOOT_REPLY;
        dhcp->htype = HW_TYPE_ETHERNET;
        dhcp->hlen = ETHER_ADDR_LEN;
        dhcp->hops = 0;
        dhcp->xid = 0x01020304;
        dhcp->secs = 0;
        dhcp->flags = 0;
        dhcp->ciaddr = 0;
        dhcp->yiaddr = htonl(yiaddr);
        dhcp->siaddr = 0;
        dhcp->giaddr = 0;
        memcpy(dhcp->chaddr, src_mac, ETHER_ADDR_LEN);
        memset(dhcp->sname, 0, DHCP_NAME_LEN);
        memset(dhcp->file, 0, DHCP_FILE_LEN);
        len = DHCP_FIXED_LEN;
        len += AddOptions(dhcp->options, msg_type, vmifindex, options, num_options);

        Agent::GetInstance()->GetDhcpProto()->SendDhcpIpc(buf, len);
    }

    void SendDhcp(short ifindex, uint16_t flags, uint8_t msg_type,
                  uint8_t *options, int num_options, bool error = false,
                  bool response = false, uint32_t yiaddr = 0,
                  uint32_t vmifindex = 0) {
        int len = 512;
        boost::scoped_array<uint8_t> buf(new uint8_t[len]);
        memset(buf.get(), 0, len);

#if defined(__linux__)
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
        ip->protocol = IPPROTO_UDP;
        ip->check = 0;
        if (response) {
            ip->saddr = inet_addr("1.2.3.254");
            ip->daddr = 0;
        } else {
            ip->saddr = 0;
            ip->daddr = inet_addr("255.255.255.255");
        }

        udphdr *udp = (udphdr *) (ip + 1);
        if (response) {
            udp->source = htons(DHCP_SERVER_PORT);
            udp->dest = htons(DHCP_SERVER_PORT);
        } else {
            udp->source = htons(DHCP_CLIENT_PORT);
            udp->dest = htons(DHCP_SERVER_PORT);
        }
        udp->check = 0;

        dhcphdr *dhcp = (dhcphdr *) (udp + 1);
        if (response) {
            dhcp->op = BOOT_REPLY;
        } else {
            dhcp->op = BOOT_REQUEST;
        }
        dhcp->htype = HW_TYPE_ETHERNET;
        dhcp->hlen = ETH_ALEN;
        dhcp->hops = 0;
        dhcp->xid = 0x01020304;
        dhcp->secs = 0;
        dhcp->flags = htons(flags);
        dhcp->ciaddr = 0;
        dhcp->yiaddr = htonl(yiaddr);
        dhcp->siaddr = 0;
        dhcp->giaddr = 0;
        memcpy(dhcp->chaddr, src_mac, ETH_ALEN);
        memset(dhcp->sname, 0, DHCP_NAME_LEN);
        memset(dhcp->file, 0, DHCP_FILE_LEN);
        len = sizeof(udphdr) + DHCP_FIXED_LEN;
        len += AddOptions(dhcp->options, msg_type, vmifindex, options, num_options);
        if (error) {
            // send an error message by modifying the DHCP OPTIONS COOKIE
            memcpy(dhcp->options, "1234", 4);
        }

        udp->len = htons(len);
        ip->tot_len = htons(len + sizeof(iphdr));
        len += sizeof(iphdr) + sizeof(ethhdr) + IPC_HDR_LEN;
#elif defined(__FreeBSD__)
        ether_header *eth = (ether_header *)buf.get();
        eth->ether_dhost[5] = 1;
        eth->ether_shost[5] = 2;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
        agent->hdr_vrf = htons(0);
        agent->hdr_cmd = htons(AGENT_TRAP_NEXTHOP);

        eth = (ether_header *) (agent + 1);
        memcpy(eth->ether_dhost, dest_mac, ETHER_ADDR_LEN);
        memcpy(eth->ether_shost, src_mac, ETHER_ADDR_LEN);
        eth->ether_type = htons(ETHERTYPE_IP);

        ip *ip = (struct ip *) (eth + 1);
        ip->ip_hl = 5;
        ip->ip_v = 4;
        ip->ip_tos = 0;
        ip->ip_id = 0;
        ip->ip_off = 0;
        ip->ip_ttl = 16;
        ip->ip_p = IPPROTO_UDP;
        ip->ip_sum = 0;
        if (response) {
            ip->ip_src.s_addr = inet_addr("1.2.3.254");
            ip->ip_dst.s_addr = 0;
        } else {
            ip->ip_src.s_addr = 0;
            ip->ip_dst.s_addr = inet_addr("255.255.255.255");
        }

        udphdr *udp = (udphdr *) (ip + 1);
        if (response) {
            udp->uh_sport = htons(DHCP_SERVER_PORT);
            udp->uh_dport = htons(DHCP_SERVER_PORT);
        } else {
            udp->uh_sport = htons(DHCP_CLIENT_PORT);
            udp->uh_dport = htons(DHCP_SERVER_PORT);
        }
        udp->uh_sum = 0;

        dhcphdr *dhcp = (dhcphdr *) (udp + 1);
        if (response) {
            dhcp->op = BOOT_REPLY;
        } else {
            dhcp->op = BOOT_REQUEST;
        }
        dhcp->htype = HW_TYPE_ETHERNET;
        dhcp->hlen = ETHER_ADDR_LEN;
        dhcp->hops = 0;
        dhcp->xid = 0x01020304;
        dhcp->secs = 0;
        dhcp->flags = htons(flags);
        dhcp->ciaddr = 0;
        dhcp->yiaddr = htonl(yiaddr);
        dhcp->siaddr = 0;
        dhcp->giaddr = 0;
        memcpy(dhcp->chaddr, src_mac, ETHER_ADDR_LEN);
        memset(dhcp->sname, 0, DHCP_NAME_LEN);
        memset(dhcp->file, 0, DHCP_FILE_LEN);
        len = sizeof(udphdr) + DHCP_FIXED_LEN;
        len += AddOptions(dhcp->options, msg_type, vmifindex, options, num_options);
        if (error) {
            // send an error message by modifying the DHCP OPTIONS COOKIE
            memcpy(dhcp->options, "1234", 4);
        }

        udp->uh_ulen = htons(len);
        ip->ip_len = htons(len + sizeof(ip));
        len += sizeof(ip) + sizeof(ether_header) + IPC_HDR_LEN;
#else
#error "Unsupported platform"
#endif
        TestTapInterface *tap = (TestTapInterface *)
            (Agent::GetInstance()->pkt()->pkt_handler()->tap_interface());
        tap->GetTestPktHandler()->TestPktSend(buf.get(), len);
    }

    int AddOptions(uint8_t *ptr, uint8_t msg_type, uint32_t ifindex,
                   uint8_t *options, int num_options) {
        memcpy(ptr, DHCP_OPTIONS_COOKIE, 4);
        ptr += 4;
        int len = 4;
        for (int i = 0; i < num_options; i++) {
            *ptr = options[i];
            ptr += 1;
            len += 1;
            switch(options[i]) {
                case DHCP_OPTION_PAD:
                    break;
                case DHCP_OPTION_HOST_NAME:
                    *ptr = 10;
                    memcpy(ptr+1, "host1.test", 10);
                    ptr += 11;
                    len += 11;
                    break;
                case DHCP_OPTION_REQ_IP_ADDRESS:
                    *ptr = 4;
                    *(in_addr_t *)(ptr+1) = inet_addr(CLIENT_REQ_IP);
                    ptr += 5;
                    len += 5;
                    break;
                case DHCP_OPTION_MSG_TYPE:
                    *ptr = 1;
                    *(ptr+1) = msg_type;
                    ptr += 2;
                    len += 2;
                    break;
                case DHCP_OPTION_DOMAIN_NAME:
                    *ptr = 11;
                    memcpy(ptr+1, "test.domain", 11);
                    ptr += 12;
                    len += 12;
                    break;
                case DHCP_OPTION_82: {
                    *ptr = sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2;
                    ptr++;
                    *ptr = DHCP_SUBOP_CKTID;
                    *(ptr + 1) = sizeof(uint32_t);
                    *(uint32_t *)(ptr + 2) = htonl(ifindex);
                    ptr += sizeof(uint32_t) + 2;
                    *ptr = DHCP_SUBOP_REMOTEID;
                    *(ptr + 1) = sizeof(VmInterface *);
                    Interface *vm = InterfaceTable::GetInstance()->FindInterface(ifindex);
                    assert(vm != NULL);
                    memcpy(ptr+2, &vm, sizeof(VmInterface *));
                    ptr += sizeof(VmInterface *) + 2;
                    len += sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2 + 1;
                    break;
                }
                case DHCP_OPTION_END:
                    break;
                default:
                    assert(0);
            }
        }

        return len;
    }

    void DhcpEnableTest(bool order) {
        struct PortInfo input[] = {
            {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
            {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
        };
        uint8_t options[] = {
            DHCP_OPTION_MSG_TYPE,
            DHCP_OPTION_HOST_NAME,
            DHCP_OPTION_DOMAIN_NAME,
            DHCP_OPTION_END
        };
        DhcpProto::DhcpStats stats;

        IpamInfo ipam_info[] = {
            {"1.1.1.0", 24, "1.1.1.200", true},
            {"1.2.3.128", 27, "1.2.3.129", true},
            {"7.8.9.0", 24, "7.8.9.12", false},
        };
        char vdns_attr[] = "<virtual-DNS-data>\n <domain-name>test.contrail.juniper.net</domain-name>\n <dynamic-records-from-client>true</dynamic-records-from-client>\n <record-order>fixed</record-order>\n <default-ttl-seconds>120</default-ttl-seconds>\n </virtual-DNS-data>\n";
        char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

        if (order) {
            CreateVmportEnv(input, 2, 0);
            client->WaitForIdle();
            client->Reset();
            AddVDNS("vdns1", vdns_attr);
            client->WaitForIdle();
            AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
            client->WaitForIdle();
        } else {
            client->Reset();
            AddVDNS("vdns1", vdns_attr);
            client->WaitForIdle();
            AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
            client->WaitForIdle();
            CreateVmportEnv(input, 2, 0);
            client->WaitForIdle();
        }

        // Check the dhcp_enable flag
        VnEntry *vn = VnGet(1);
        std::vector<VnIpam> vn_ipam = vn->GetVnIpam();
        for (int i = 0; i < sizeof(ipam_info) / sizeof(IpamInfo); ++i) {
            EXPECT_TRUE(vn_ipam[i].dhcp_enable == ipam_info[i].dhcp_enable);
        }

        SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
        SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
        int count = 0;
        DHCP_CHECK (stats.acks < 1);
        EXPECT_EQ(1U, stats.discover);
        EXPECT_EQ(1U, stats.request);
        EXPECT_EQ(1U, stats.offers);
        EXPECT_EQ(1U, stats.acks);

        // modify IPAM dhcp_enable
        for (int i = 0; i < sizeof(ipam_info) / sizeof(IpamInfo); ++i) {
            ipam_info[i].dhcp_enable = !ipam_info[i].dhcp_enable;
        }
        AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
        client->WaitForIdle();
        vn_ipam = vn->GetVnIpam();
        for (int i = 0; i < sizeof(ipam_info) / sizeof(IpamInfo); ++i) {
            EXPECT_TRUE(vn_ipam[i].dhcp_enable == ipam_info[i].dhcp_enable);
        }

        // now DHCP should be disabled for 1.1.1.0 subnet
        SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
        SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
        client->WaitForIdle();
        count = 0;
        DHCP_CHECK (stats.acks < 1);
        EXPECT_EQ(1U, stats.discover);
        EXPECT_EQ(1U, stats.request);
        EXPECT_EQ(1U, stats.offers);
        EXPECT_EQ(1U, stats.acks);

        client->Reset();
        DelIPAM("vn1", "vdns1");
        client->WaitForIdle();
        DelVDNS("vdns1");
        client->WaitForIdle();

        client->Reset();
        DeleteVmportEnv(input, 2, 1, 0);
        client->WaitForIdle();

        Agent::GetInstance()->GetDhcpProto()->ClearStats();
    }

private:
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

TEST_F(DhcpTest, DhcpReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    ClearPktTrace();
    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    char vdns_attr[] = "<virtual-DNS-data>\n <domain-name>test.contrail.juniper.net</domain-name>\n <dynamic-records-from-client>true</dynamic-records-from-client>\n <record-order>fixed</record-order>\n <default-ttl-seconds>120</default-ttl-seconds>\n </virtual-DNS-data>\n";
    char ipam_attr[] = "<network-ipam-mgmt>\n <ipam-dns-method>virtual-dns-server</ipam-dns-method>\n <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\n </network-ipam-mgmt>\n";

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();
    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);
    SendDhcp(GetItfId(1), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(1), 0x8000, DHCP_REQUEST, options, 4);
    SendDhcp(GetItfId(1), 0, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(1), 0, DHCP_REQUEST, options, 4);
    count = 0;
    DHCP_CHECK (stats.acks < 3);
    EXPECT_EQ(3U, stats.discover);
    EXPECT_EQ(3U, stats.request);
    EXPECT_EQ(3U, stats.offers);
    EXPECT_EQ(3U, stats.acks);
    SendDhcp(GetItfId(1), 0x8000, DHCP_INFORM, options, 4);
    SendDhcp(GetItfId(1), 0x8000, DHCP_DECLINE, options, 4);
    count = 0;
    DHCP_CHECK (stats.decline < 1);
    EXPECT_EQ(3U, stats.discover);
    EXPECT_EQ(3U, stats.request);
    EXPECT_EQ(1U, stats.inform);
    EXPECT_EQ(1U, stats.decline);
    EXPECT_EQ(3U, stats.offers);
    EXPECT_EQ(4U, stats.acks);
    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4, true);
    count = 0;
    DHCP_CHECK (stats.errors < 1);
    EXPECT_EQ(3U, stats.discover);
    EXPECT_EQ(3U, stats.request);
    EXPECT_EQ(1U, stats.inform);
    EXPECT_EQ(1U, stats.decline);
    EXPECT_EQ(3U, stats.offers);
    EXPECT_EQ(4U, stats.acks);
    EXPECT_EQ(1U, stats.errors);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(
        boost::bind(&DhcpTest::CheckSandeshResponse, this, _1, false, "", ""));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    ShowAllInfo *all_sandesh = new ShowAllInfo();
    Sandesh::set_response_callback(
        boost::bind(&DhcpTest::CheckAllSandeshResponse, this, _1));
    all_sandesh->HandleRequest();
    client->WaitForIdle();
    all_sandesh->Release();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpOtherReqTest) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    SendDhcp(GetItfId(0), 0x8000, DHCP_RELEASE, options, 2);
    SendDhcp(GetItfId(0), 0x8000, DHCP_LEASE_QUERY, options, 2);
    SendDhcp(GetItfId(0), 0x8000, DHCP_ACK, options, 2);
    int count = 0;
    DHCP_CHECK (stats.other < 3);
    EXPECT_EQ(3U, stats.other);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpOptionTest) {
    struct PortInfo input[] = {
        {"vnet3", 3, CLIENT_REQ_IP, "00:00:00:03:03:03", 1, 3},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_REQ_IP_ADDRESS,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_PAD,
        DHCP_OPTION_PAD,
        DHCP_OPTION_PAD,
        DHCP_OPTION_PAD,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {CLIENT_REQ_PREFIX, 24, CLIENT_REQ_GW, true},
    };
    char vdns_attr[] = "<virtual-DNS-data>\n <domain-name>test.domain</domain-name>\n <dynamic-records-from-client>true</dynamic-records-from-client>\n <record-order>fixed</record-order>\n <default-ttl-seconds>120</default-ttl-seconds>\n </virtual-DNS-data>\n";
    char ipam_attr[] =
    "<network-ipam-mgmt>\
        <ipam-dns-method>virtual-dns-server</ipam-dns-method>\
        <ipam-dns-server>\
            <virtual-dns-server-name>vdns1</virtual-dns-server-name>\
        </ipam-dns-server>\
        <dhcp-option-list>\
            <dhcp-option>\
                <dhcp-option-name>6</dhcp-option-name>\
                <dhcp-option-value>1.2.3.4</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>15</dhcp-option-name>\
                <dhcp-option-value>test.com</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>3.2.14.5</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>junk</dhcp-option-value>\
            </dhcp-option>\
        </dhcp-option-list>\
    </network-ipam-mgmt>";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1, ipam_attr, "vdns1");
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 9);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 9);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, false, "", ""));
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

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpNakTest) {
    struct PortInfo input[] = {
        {"vnet4", 4, "5.6.7.8", "00:00:00:04:04:04", 1, 4},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_REQ_IP_ADDRESS,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {"5.6.7.0", 24, "5.6.7.1", true},
    };
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.nacks < 1);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.nacks);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, false, "", ""));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpShortLeaseTest) {
    struct PortInfo input[] = {
        {"vnet5", 5, "9.6.7.8", "00:00:00:05:05:05", 1, 5},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IntfCfgAdd(input, 0);
    WaitForItfUpdate(1);

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 3);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 3);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 3);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 3);
    count = 0;
    DHCP_CHECK (stats.acks < 2);
    EXPECT_EQ(2U, stats.discover);
    EXPECT_EQ(2U, stats.request);
    EXPECT_EQ(2U, stats.offers);
    EXPECT_EQ(2U, stats.acks);

    IpamInfo ipam_info[] = {
        {"9.6.7.0", 24, "9.6.7.254", true},
    };
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 3);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 3);
    count = 0;
    DHCP_CHECK (stats.acks < 3);
    EXPECT_EQ(3U, stats.discover);
    EXPECT_EQ(3U, stats.request);
    EXPECT_EQ(3U, stats.offers);
    EXPECT_EQ(3U, stats.acks);

    IntfCfgDel(input, 0);
    WaitForItfUpdate(0);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpTenantDnsTest) {
    struct PortInfo input[] = {
        {"vnet6", 6, "3.2.5.7", "00:00:00:06:06:06", 1, 6},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {"3.2.5.0", 24, "3.2.5.254", true},
    };
    char ipam_attr[] =
    "<network-ipam-mgmt>\
        <ipam-dns-method>tenant-dns-server</ipam-dns-method>\
        <ipam-dns-server>\
            <tenant-dns-server-address>\
                <ip-address>3.2.4.5</ip-address>\
                <ip-address>5.5.4.5</ip-address>\
                <ip-address>junk</ip-address>\
            </tenant-dns-server-address>\
        </ipam-dns-server>\
        <dhcp-option-list>\
            <dhcp-option>\
                <dhcp-option-name>6</dhcp-option-name>\
                <dhcp-option-value>1.2.3.4</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>15</dhcp-option-name>\
                <dhcp-option-value>test.com</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>3.2.14.5</dhcp-option-value>\
            </dhcp-option>\
        </dhcp-option-list>\
    </network-ipam-mgmt>";

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 1, ipam_attr);
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpFabricPortTest) {
    struct PortInfo input[] = {
        {"vnet7", 7, "1.1.1.1", "00:00:00:07:07:07", 1, 7},
    };
    Ip4Address vmaddr(Agent::GetInstance()->router_id().to_ulong() + 1);
    strncpy(input[0].addr, vmaddr.to_string().c_str(), 32);
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    CreateVmportEnv(input, 1, 0, NULL,
                    Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 3);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 3);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0, NULL,
                    Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpZeroIpTest) {
    struct PortInfo input[] = {
        {"vnet8", 8, "0.0.0.0", "00:00:00:08:08:08", 1, 8},
    };
    uint8_t req_options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    uint8_t resp_options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_82,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    CreateVmportEnv(input, 1, 0, NULL,
                    Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();

    Ip4Address vmaddr(Agent::GetInstance()->router_id().to_ulong() + 1);
    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, req_options, 3);
    // SendDhcp(fabric_interface_id(), 0x8000, DHCP_OFFER, resp_options, 4, false, true, vmaddr.to_ulong(), GetItfId(0));
    SendRelayResponse(DHCP_OFFER, resp_options, 4, vmaddr.to_ulong(), GetItfId(0));
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, req_options, 3);
    // SendDhcp(fabric_interface_id(), 0x8000, DHCP_ACK, resp_options, 4, false, true, vmaddr.to_ulong(), GetItfId(0));
    SendRelayResponse(DHCP_ACK, resp_options, 4, vmaddr.to_ulong(), GetItfId(0));
    client->WaitForIdle();
    int count = 0;
    DHCP_CHECK (stats.relay_resp < 2);
    EXPECT_EQ(2U, stats.relay_req);
    EXPECT_EQ(2U, stats.relay_resp);
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), vmaddr, 32));

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0, NULL,
                    Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();

    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, IpamSpecificDhcpOptions) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    char vdns_attr[] = "<virtual-DNS-data>\n <domain-name>test.contrail.juniper.net</domain-name>\n <dynamic-records-from-client>true</dynamic-records-from-client>\n <record-order>fixed</record-order>\n <default-ttl-seconds>120</default-ttl-seconds>\n </virtual-DNS-data>\n";
    char ipam_attr[] =
    "<network-ipam-mgmt>\
        <ipam-dns-method>virtual-dns-server</ipam-dns-method>\
        <ipam-dns-server><virtual-dns-server-name>vdns1</virtual-dns-server-name></ipam-dns-server>\
        <dhcp-option-list>\
            <dhcp-option>\
                <dhcp-option-name>6</dhcp-option-name>\
                <dhcp-option-value>1.2.3.4</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>15</dhcp-option-name>\
                <dhcp-option-value>test.com</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>3.2.14.5</dhcp-option-value>\
            </dhcp-option>\
        </dhcp-option-list>\
        <host-routes>\
            <route><prefix>10.1.1.0/24</prefix> <next-hop /> <next-hop-type /></route>\
            <route><prefix>10.1.2.0/24</prefix> <next-hop /> <next-hop-type /></route>\
            <route><prefix>150.25.75.0/24</prefix> <next-hop /> <next-hop-type /></route>\
            <route><prefix>192.168.1.128/28</prefix> <next-hop /> <next-hop-type /></route>\
        </host-routes>\
    </network-ipam-mgmt>";

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();
    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1");
    client->WaitForIdle();

    ClearPktTrace();
    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, true,
                                               HOST_ROUTE_STRING,
                                               IPAM_DHCP_OPTIONS_STRING));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    // change host routes
    ClearPktTrace();
    std::vector<std::string> vm_host_routes;
    vm_host_routes.push_back("150.2.2.0/24");
    vm_host_routes.push_back("192.1.1.1/28");
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1", &vm_host_routes);
    client->WaitForIdle();

    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    DhcpInfo *new_sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, true,
                                               CHANGED_HOST_ROUTE_STRING,
                                               IPAM_DHCP_OPTIONS_STRING));
    new_sand->HandleRequest();
    client->WaitForIdle();
    new_sand->Release();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    ClearPktTrace();
    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

// Check that options at subnet override options at ipam level
TEST_F(DhcpTest, SubnetSpecificDhcpOptions) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    char vdns_attr[] = "<virtual-DNS-data>\n <domain-name>test.contrail.juniper.net</domain-name>\n <dynamic-records-from-client>true</dynamic-records-from-client>\n <record-order>fixed</record-order>\n <default-ttl-seconds>120</default-ttl-seconds>\n </virtual-DNS-data>\n";
    char ipam_attr[] =
    "<network-ipam-mgmt>\
        <ipam-dns-method>default-dns-server</ipam-dns-method>\
        <dhcp-option-list>\
            <dhcp-option>\
                <dhcp-option-name>6</dhcp-option-name>\
                <dhcp-option-value>1.2.3.4</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>15</dhcp-option-name>\
                <dhcp-option-value>test.com</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>3.2.14.5</dhcp-option-value>\
            </dhcp-option>\
        </dhcp-option-list>\
        <host-routes>\
            <route><prefix>1.2.3.0/24</prefix> <next-hop /> <next-hop-type /></route>\
            <route><prefix>4.5.0.0/16</prefix> <next-hop /> <next-hop-type /></route>\
        </host-routes>\
    </network-ipam-mgmt>";
    char add_subnet_tags[] =
    "<dhcp-option-list>\
        <dhcp-option>\
            <dhcp-option-name>6</dhcp-option-name>\
            <dhcp-option-value>11.12.13.14</dhcp-option-value>\
        </dhcp-option>\
        <dhcp-option>\
            <dhcp-option-name>15</dhcp-option-name>\
            <dhcp-option-value>subnet.com</dhcp-option-value>\
        </dhcp-option>\
        <dhcp-option>\
            <dhcp-option-name>4</dhcp-option-name>\
            <dhcp-option-value>13.12.14.15</dhcp-option-value>\
        </dhcp-option>\
     </dhcp-option-list>";

    std::vector<std::string> vm_host_routes;
    vm_host_routes.push_back("10.1.1.0/24");
    vm_host_routes.push_back("10.1.2.0/24");
    vm_host_routes.push_back("150.25.75.0/24");
    vm_host_routes.push_back("192.168.1.128/28");

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    client->Reset();
    AddVDNS("vdns1", vdns_attr);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1", &vm_host_routes, add_subnet_tags);
    client->WaitForIdle();

    ClearPktTrace();
    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, true,
                                               HOST_ROUTE_STRING,
                                               SUBNET_DHCP_OPTIONS_STRING));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    ClearPktTrace();
    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

// Check that options at vm interface override options at subnet & ipam levels
TEST_F(DhcpTest, PortSpecificDhcpOptions) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    uint8_t options[] = {
        DHCP_OPTION_MSG_TYPE,
        DHCP_OPTION_HOST_NAME,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_END
    };
    DhcpProto::DhcpStats stats;

    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    char ipam_attr[] =
    "<network-ipam-mgmt>\
        <ipam-dns-method>default-dns-server</ipam-dns-method>\
        <dhcp-option-list>\
            <dhcp-option>\
                <dhcp-option-name>6</dhcp-option-name>\
                <dhcp-option-value>1.2.3.4</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>15</dhcp-option-name>\
                <dhcp-option-value>test.com</dhcp-option-value>\
            </dhcp-option>\
            <dhcp-option>\
                <dhcp-option-name>4</dhcp-option-name>\
                <dhcp-option-value>3.2.14.5</dhcp-option-value>\
            </dhcp-option>\
        </dhcp-option-list>\
        <host-routes>\
            <route><prefix>1.2.3.0/24</prefix> <next-hop /> <next-hop-type /></route>\
            <route><prefix>4.5.0.0/16</prefix> <next-hop /> <next-hop-type /></route>\
        </host-routes>\
    </network-ipam-mgmt>";

    char add_subnet_tags[] =
    "<dhcp-option-list>\
        <dhcp-option>\
            <dhcp-option-name>6</dhcp-option-name>\
            <dhcp-option-value>11.12.13.14</dhcp-option-value>\
        </dhcp-option>\
        <dhcp-option>\
            <dhcp-option-name>15</dhcp-option-name>\
            <dhcp-option-value>subnet.com</dhcp-option-value>\
        </dhcp-option>\
        <dhcp-option>\
            <dhcp-option-name>4</dhcp-option-name>\
            <dhcp-option-value>13.12.14.15</dhcp-option-value>\
        </dhcp-option>\
     </dhcp-option-list>";

    char vm_interface_attr[] =
    "<virtual-machine-interface-dhcp-option-list>\
        <dhcp-option>\
            <dhcp-option-name>6</dhcp-option-name>\
            <dhcp-option-value>21.22.23.24</dhcp-option-value>\
         </dhcp-option>\
         <dhcp-option>\
            <dhcp-option-name>15</dhcp-option-name>\
            <dhcp-option-value>interface.com</dhcp-option-value>\
         </dhcp-option>\
         <dhcp-option>\
            <dhcp-option-name>4</dhcp-option-name>\
            <dhcp-option-value>23.22.24.25</dhcp-option-value>\
         </dhcp-option>\
     </virtual-machine-interface-dhcp-option-list>\
     <virtual-machine-interface-host-routes>\
         <route><prefix>99.2.3.0/24</prefix> <next-hop /> <next-hop-type /> </route>\
         <route><prefix>99.5.0.0/16</prefix> <next-hop /> <next-hop-type /> </route>\
    </virtual-machine-interface-host-routes>";

    std::vector<std::string> vm_host_routes;
    vm_host_routes.push_back("10.1.1.0/24");
    vm_host_routes.push_back("10.1.2.0/24");
    vm_host_routes.push_back("150.25.75.0/24");
    vm_host_routes.push_back("192.168.1.128/28");

    CreateVmportEnv(input, 2, 0, NULL, NULL, vm_interface_attr);
    client->WaitForIdle();
    client->Reset();
    AddIPAM("vn1", ipam_info, 3, ipam_attr, "vdns1", &vm_host_routes, add_subnet_tags);
    client->WaitForIdle();

    ClearPktTrace();
    SendDhcp(GetItfId(0), 0x8000, DHCP_DISCOVER, options, 4);
    SendDhcp(GetItfId(0), 0x8000, DHCP_REQUEST, options, 4);
    int count = 0;
    DHCP_CHECK (stats.acks < 1);
    EXPECT_EQ(1U, stats.discover);
    EXPECT_EQ(1U, stats.request);
    EXPECT_EQ(1U, stats.offers);
    EXPECT_EQ(1U, stats.acks);

    DhcpInfo *sand = new DhcpInfo();
    Sandesh::set_response_callback(boost::bind(&DhcpTest::CheckSandeshResponse,
                                               this, _1, true,
                                               PORT_HOST_ROUTE_STRING,
                                               PORT_DHCP_OPTIONS_STRING));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    client->Reset();
    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();
    DelVDNS("vdns1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    ClearPktTrace();
    Agent::GetInstance()->GetDhcpProto()->ClearStats();
}

TEST_F(DhcpTest, DhcpEnableTestForward) {
    DhcpEnableTest(true);
}

TEST_F(DhcpTest, DhcpEnableTestReverse) {
    DhcpEnableTest(false);
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
