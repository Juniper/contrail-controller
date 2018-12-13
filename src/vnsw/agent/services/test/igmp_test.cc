/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"
#include "igmp_test.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <boost/scoped_array.hpp>
#include <base/logging.h>

#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <pugixml/pugixml.hpp>
#include <services/icmp_proto.h>
#include <vr_interface.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include <services/services_sandesh.h>
#include "vr_types.h"
#include "services/igmp_proto.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void gmp_set_def_ipv4_ivl_params(uint32_t robust_count, uint32_t qivl,
                        uint32_t qrivl, uint32_t lmqi);
#ifdef __cplusplus
}
#endif

#define MSECS_PER_SEC 1000
#define MAX_WAIT_COUNT 1000
#define BUF_SIZE 8192

char src_mac[ETHER_ADDR_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[ETHER_ADDR_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };

IpamInfo ipam_info[] = {
    {"10.1.1.0", 24, "10.1.1.1", true},
};

struct PortInfo input[] = {
    {"vnet1-0", 1, "10.1.1.3", "00:00:10:01:01:03", 1, 1},
    {"vnet1-1", 2, "10.1.1.4", "00:00:10:01:01:04", 1, 1},
    {"vnet1-2", 3, "10.1.1.5", "00:00:10:01:01:05", 1, 1},
    {"vnet1-3", 4, "10.1.1.6", "00:00:10:01:01:06", 1, 1},
    {"vnet1-4", 5, "10.1.1.7", "00:00:10:01:01:07", 1, 1},
    {"vnet1-5", 6, "10.1.1.8", "00:00:10:01:01:08", 1, 1},
    {"vnet1-6", 7, "10.1.1.9", "00:00:10:01:01:09", 1, 1},
};

char print_buf[1024*3];

struct IgmpGroupSource {
    uint32_t group;
    uint8_t record_type;
    uint32_t source_count;
    uint32_t sources[10];
};

struct IgmpGroupSource igmp_gs[5];

struct igmp_common {
    u_int8_t igmp_type;             /* IGMP type */
    u_int8_t igmp_code;             /* IGMP code */
    u_int16_t igmp_cksum;           /* checksum */
};

struct igmp_query {
    struct in_addr igmp_group;
};

struct igmp_v3_query {
    struct in_addr igmp_group;
    u_int8_t resv_s_qrv;
    u_int8_t qqic;
    u_int16_t num_sources;
    struct in_addr igmp_sources[0];
};

struct igmp_leave {
    struct in_addr igmp_group;
};

struct igmp_report {
    struct in_addr igmp_group;
};

struct igmp_v3_grecord {
    u_int8_t igmp_record_type;
    u_int8_t igmp_aux_data_len;
    u_int16_t igmp_num_sources;
    struct in_addr igmp_group;
    struct in_addr igmp_source[0];
};

struct igmp_v3_report {
    u_int16_t reserved;
    u_int16_t num_groups;
    struct igmp_v3_grecord grecord[0];
};

class IgmpTest : public ::testing::Test {
public:
    enum IgmpType {
        IgmpTypeV1Query = 0x11,
        IgmpTypeV1Report = 0x12,
        IgmpTypeV2Report = 0x16,
        IgmpTypeV2Leave = 0x17,
        IgmpTypeV3Report = 0x22,
    };

    enum IgmpV3Mode {
        IgmpV3ModeInclude = 1,
        IgmpV3ModeExclude = 2,
        IgmpV3ModeCtoInclude = 3,
        IgmpV3ModeCtoExclude = 4,
        IgmpV3ModeAllNew = 5,
        IgmpV3ModeBlockOld = 6,
    };

    IgmpTest() : itf_count_(0) {

        rx_count = 0;
        robust_count = 1;
        qivl = 10000;
        qrivl = 1000;
        lmqi = 200;

        lmqt = ((lmqi * 1)+ lmqi/2 + 5) * 1000;

        def_lmqt = ((1000 * 2) + 1000/2 + 5) * 1000;

        TestPkt0Interface *tap = (TestPkt0Interface *)
                    (Agent::GetInstance()->pkt()->control_interface());
        tap->RegisterCallback(
                boost::bind(&IgmpTest::IgmpTestClientReceive, this, _1, _2));

        vrfid_ = Agent::GetInstance()->vrf_table()->Register(
                boost::bind(&IgmpTest::VrfUpdate, this, _2));
        rid_ = Agent::GetInstance()->interface_table()->Register(
                boost::bind(&IgmpTest::ItfUpdate, this, _2));

        memset(igmp_gs, 0x00, sizeof(igmp_gs));
        igmp_gs[0].group = ntohl(inet_addr("224.1.0.10"));
        igmp_gs[0].sources[0] = ntohl(inet_addr("100.1.0.10"));
        igmp_gs[0].sources[1] = ntohl(inet_addr("100.1.0.20"));
        igmp_gs[0].sources[2] = ntohl(inet_addr("100.1.0.30"));
        igmp_gs[0].sources[3] = ntohl(inet_addr("100.1.0.40"));
        igmp_gs[0].sources[4] = ntohl(inet_addr("100.1.0.50"));

        igmp_gs[1].group = ntohl(inet_addr("224.2.0.10"));
        igmp_gs[1].sources[0] = ntohl(inet_addr("100.2.0.10"));
        igmp_gs[1].sources[1] = ntohl(inet_addr("100.2.0.20"));
        igmp_gs[1].sources[2] = ntohl(inet_addr("100.2.0.30"));
        igmp_gs[1].sources[3] = ntohl(inet_addr("100.2.0.40"));
        igmp_gs[1].sources[4] = ntohl(inet_addr("100.2.0.50"));

        igmp_gs[2].group = ntohl(inet_addr("224.3.0.10"));
        igmp_gs[2].sources[0] = ntohl(inet_addr("100.3.0.10"));
        igmp_gs[2].sources[1] = ntohl(inet_addr("100.3.0.20"));
        igmp_gs[2].sources[2] = ntohl(inet_addr("100.3.0.30"));
        igmp_gs[2].sources[3] = ntohl(inet_addr("100.3.0.40"));
        igmp_gs[2].sources[4] = ntohl(inet_addr("100.3.0.50"));

        rx_count = 0;
    }

    ~IgmpTest() {
        Agent::GetInstance()->vrf_table()->Unregister(vrfid_);
        Agent::GetInstance()->interface_table()->Unregister(rid_);
    }

    BgpPeer *bgp_peer(uint32_t idx) {
        if (idx == 1) {
            return bgp_peer_1;
        } else {
            return NULL;
        }
    }

    void Set_Up(bool set_lmqt = true) {
        agent = Agent::GetInstance();
        client->WaitForIdle();

        if (set_lmqt) {
            // Force timeout for IGMP faster.
            gmp_set_def_ipv4_ivl_params(robust_count, qivl, qrivl, lmqi);
            // formula from gmpr_intf_update_lmqt
        }

        Ip4Address peer_ip;
        boost::system::error_code ec;

        bgp_peer_0 = agent->controller_xmpp_channel(0)->bgp_peer_id();

        peer_ip = Ip4Address::from_string("127.0.0.1", ec);
        bgp_peer_1 = CreateBgpPeer("127.0.0.1", "remote-1");
        client->WaitForIdle();

        CreateVmportEnv(input, sizeof(input)/sizeof(struct PortInfo), 0);
        client->WaitForIdle();
        client->Reset();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        agent->GetIgmpProto()->ClearStats();
        agent->GetIgmpProto()->GetGmpProto()->ClearStats();
        client->WaitForIdle();

        IpAddress gateway = IpAddress(Ip4Address::from_string(ipam_info[0].gw));
        VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
        const VnEntry *vn = vm_itf->vn();
        agent->GetIgmpProto()->ClearItfStats(vn, gateway);
        client->WaitForIdle();
    }

    void Tear_Down() {

        DeleteBgpPeer(bgp_peer_1);
        client->WaitForIdle();

        client->Reset();
        DelIPAM("vn1");
        client->WaitForIdle();

        client->Reset();
        DeleteVmportEnv(input, sizeof(input)/sizeof(struct PortInfo), 1, 0);
        client->WaitForIdle();
    }

    void VrfUpdate(DBEntryBase *entry) {
        static std::string vrf_name("");
        VrfEntry *vrf = static_cast<VrfEntry *>(entry);
        if (vrf) {
            vrf_name = vrf->GetName();
        }
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
                LOG(DEBUG, "Igmp test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "Igmp test : interface added " << itf->id());
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

    bool IgmpGlobalEnable(bool enable) {
        SetIgmpConfig(enable);
        client->WaitForIdle();
    }

    void IgmpVnEnable(std::string vn_name, uint32_t vn_idx, bool enable) {
        SetIgmpVnConfig(vn_name, vn_idx, enable);
        client->WaitForIdle();
    }

    void IgmpVmiEnable(uint32_t idx, bool enable) {
        SetIgmpIntfConfig(input[idx].name, input[idx].intf_id, enable);
        client->WaitForIdle();
    }

    void IgmpGlobalClear() {
        ClearIgmpConfig();
        client->WaitForIdle();
    }

    void PrintIgmp(uint8_t *buf, uint32_t len) {

        uint32_t print_loc = 0;
        char *point = NULL;

        point = print_buf;
        for (uint32_t i = 0; i < len; i++) {
            print_loc += snprintf (point, 1024*3 - print_loc, "%02x ", buf[i]);
            point = print_buf + print_loc;
        }
        print_buf[print_loc] = '\0';
        TEST_LOG(DEBUG, print_buf << endl);

        return;
    }

    uint32_t FormIgmp(uint8_t *buf, int len, short ifindex, const char *src_ip,
                            IgmpType igmp_type, struct IgmpGroupSource *igmp_gs,
                            uint32_t gs_count) {

        struct ether_header *eth = (struct ether_header *)buf;
        eth->ether_dhost[5] = 1;
        eth->ether_shost[5] = 2;
        eth->ether_type = htons(0x800);

        agent_hdr *agent = (agent_hdr *)(eth + 1);
        agent->hdr_ifindex = htons(ifindex);
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
        ip->ip_ttl = 1;
        ip->ip_p = IPPROTO_IGMP;
        ip->ip_sum = 0;
        ip->ip_src.s_addr = inet_addr(src_ip);
        if (igmp_type == IgmpTypeV1Query) {

            if (igmp_gs == NULL) {
                ip->ip_dst.s_addr = inet_addr("224.0.0.1");
            } else {
                ip->ip_dst.s_addr = htonl(igmp_gs[0].group);
            }
        } else if ((igmp_type == IgmpTypeV1Report) ||
                   (igmp_type == IgmpTypeV2Report)) {

            ip->ip_dst.s_addr = htonl(igmp_gs[0].group);
        } else if (igmp_type == IgmpTypeV2Leave) {

            ip->ip_dst.s_addr = inet_addr("224.0.0.2");
        } else if (igmp_type == IgmpTypeV3Report) {

            ip->ip_dst.s_addr = inet_addr("224.0.0.22");
        }

        uint32_t igmp_length = 0;
        struct igmp_common *igmp = (struct igmp_common *) (ip + 1);
        igmp->igmp_type = igmp_type;
        igmp->igmp_code = 0;
        igmp->igmp_cksum = 0;

        igmp_length += sizeof(struct igmp_common);
        if (igmp_type == IgmpTypeV1Query) {
            if (gs_count == 0) {
                struct igmp_query *query = (struct igmp_query *) (igmp + 1);
                query->igmp_group.s_addr = 0;
                igmp_length += sizeof(struct igmp_query);
            } else if ((gs_count == 1) && (igmp_gs[0].source_count == 0)) {
                struct igmp_query *query = (struct igmp_query *) (igmp + 1);
                query->igmp_group.s_addr = htonl(igmp_gs[0].group);
                igmp->igmp_code = 50; /* 5 seconds */
                igmp_length += sizeof(struct igmp_query);
            } else {
                struct igmp_v3_query *query = (struct igmp_v3_query *) (igmp + 1);
                query->igmp_group.s_addr = htonl(igmp_gs[0].group);
                igmp->igmp_code = 50; /* 5 seconds */
                query->resv_s_qrv = 0;
                query->qqic = 0;

                igmp_length += sizeof(struct igmp_v3_query);

                struct in_addr *igmp_source = &query->igmp_sources[0];
                for (uint32_t i = 0; i < igmp_gs[0].source_count; i++) {
                    igmp_source->s_addr = htonl(igmp_gs[0].sources[i]);
                    igmp_source++;
                    igmp_length += sizeof(struct in_addr);
                }
            }
        } else if (igmp_type == IgmpTypeV2Leave) {
            struct igmp_leave *leave = (struct igmp_leave *) (igmp + 1);
            leave->igmp_group.s_addr = htonl(igmp_gs[0].group);

            igmp_length += sizeof(struct igmp_leave);
        } else if ((igmp_type == IgmpTypeV1Report) ||
                   (igmp_type == IgmpTypeV2Report)) {
            struct igmp_report *report = (struct igmp_report *) (igmp + 1);
            report->igmp_group.s_addr = htonl(igmp_gs[0].group);
            igmp_length += sizeof(struct igmp_report);
        } else if (igmp_type == IgmpTypeV3Report) {
            struct igmp_v3_report *report = (struct igmp_v3_report *) (igmp + 1);
            report->reserved = 0;
            report->num_groups = htons(gs_count);

            igmp_length += sizeof(struct igmp_v3_report);

            struct igmp_v3_grecord *grecord = &report->grecord[0];
            for (uint32_t i = 0; i < gs_count; i++) {
                grecord->igmp_record_type = igmp_gs[i].record_type;
                grecord->igmp_aux_data_len = 0;
                grecord->igmp_num_sources = htons(igmp_gs[i].source_count);
                grecord->igmp_group.s_addr = htonl(igmp_gs[i].group);
                igmp_length += sizeof(struct igmp_v3_grecord);
                struct in_addr *source = &grecord->igmp_source[0];
                for (u_int32_t j = 0; j < igmp_gs[i].source_count; j++) {
                    source->s_addr = htonl(igmp_gs[i].sources[j]);
                    source++;
                    igmp_length += sizeof(struct in_addr);
                }
                grecord = (struct igmp_v3_grecord *)source;
            }
        } else {
            return 0;
        }

        igmp->igmp_cksum = IpUtils::IPChecksum((uint16_t *)igmp, igmp_length);
        len = igmp_length > 64 ? igmp_length : 64;

        ip->ip_len = htons(len + sizeof(struct ip));

        len += sizeof(struct ip) + sizeof(struct ether_header) +
            Agent::GetInstance()->pkt()->pkt_handler()->EncapHeaderLen();

        return len;
    }

    void SendIgmp(short ifindex, const char *src_ip, IgmpType igmp_type,
                    struct IgmpGroupSource *igmp_gs, uint32_t gs_count) {

        int len = 1024;
        uint8_t *buf = new uint8_t[len];
        memset(buf, 0, len);

        len = FormIgmp(buf, len, ifindex, src_ip, igmp_type, igmp_gs, gs_count);

        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->TxPacket(buf, len);
    }

    bool WaitForRejPktCount(uint32_t ex_count) {

        IgmpProto::IgmpStats stats;

        int count = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            stats = Agent::GetInstance()->GetIgmpProto()->GetStats();
            if (++count == MAX_WAIT_COUNT)
                return false;
        } while (stats.rejected_pkt != ex_count);
        return true;
    }

    bool WaitForRxOkCount(uint32_t vm_idx, uint32_t index,
                                    uint32_t ex_count) {

        VmInterface *vm_itf = VmInterfaceGet(input[vm_idx].intf_id);
        IgmpInfo::IgmpItfStats stats;

        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            const VnEntry *vn = vm_itf->vn();
            IpAddress gateway = IpAddress(Ip4Address::from_string(ipam_info[0].gw));
            Agent::GetInstance()->GetIgmpProto()->GetItfStats(vn, gateway,
                                    stats);
            counter = stats.rx_okpacket[index-1];
            if (++count == MAX_WAIT_COUNT)
                return false;
        } while (counter != ex_count);
        return true;
    }

    bool WaitForSghCount(bool add, uint32_t ex_count) {

        GmpProto::GmpStats stats;
        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            Agent *agent = Agent::GetInstance();
            stats = agent->GetIgmpProto()->GetGmpProto()->GetStats();
            counter = add ? stats.gmp_sgh_add_count_ :
                                stats.gmp_sgh_del_count_;
            if (++count == MAX_WAIT_COUNT) {
                return false;
            }
        } while (counter != ex_count);
        return true;
    }

    bool WaitForTxCount(uint32_t vm_idx, bool tx, uint32_t ex_count) {

        VmInterface *vm_itf = VmInterfaceGet(input[vm_idx].intf_id);
        IgmpInfo::IgmpItfStats stats;
        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            const VnEntry *vn = vm_itf->vn();
            IpAddress gateway = IpAddress(Ip4Address::from_string(ipam_info[0].gw));
            Agent::GetInstance()->GetIgmpProto()->GetItfStats(vn, gateway,
                                    stats);
            counter = tx ? stats.tx_packet : stats.tx_drop_packet;
            if (++count == MAX_WAIT_COUNT)
                return false;
        } while (counter < ex_count);
        return true;
    }

    void IgmpTestClientReceive(uint8_t *buf, std::size_t len) {

        struct ether_header *eth = (struct ether_header *)buf;

        agent_hdr *agent = (agent_hdr *)(eth + 1);

        eth = (struct ether_header *) (agent + 1);

        struct ip *ip = (struct ip *) (eth + 1);
        if (ip->ip_p == IPPROTO_IGMP) {
            rx_count++;
        }

        return;
    }

public:
    uint32_t rx_count;
    uint32_t robust_count;
    uint32_t qivl;
    uint32_t qrivl;
    uint32_t lmqi;
    uint32_t lmqt;
    uint32_t def_lmqt;

private:
    DBTableBase::ListenerId vrfid_;
    DBTableBase::ListenerId rid_;
    uint32_t itf_count_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
    int icmp_seq_;
    BgpPeer *bgp_peer_0;
    BgpPeer *bgp_peer_1;
    Agent *agent;
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

void RouterIdDepInit(Agent *agent) {
}

TEST_F(IgmpTest, PrintPackets) {

    uint32_t buf_len = 1024;
    uint32_t len = 0;
    uint8_t *buf = new uint8_t[buf_len];

    memset(buf, 0, buf_len);

    struct IgmpGroupSource igmp_gs[5];

    Set_Up();

    cout << "Not Printing for now. TEST_LOG is commented." << endl;

    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV1Query, NULL, 0);
    TEST_LOG(DEBUG, "IgmpTypeV1Query Packet : " << endl);
    PrintIgmp(buf, len);

    igmp_gs[0].group = inet_addr("224.0.0.10");
    igmp_gs[0].source_count = 0;
    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV1Query, igmp_gs, 1);
    TEST_LOG(DEBUG, "IgmpTypeV2Query Packet : " << endl);
    PrintIgmp(buf, len);

    memset(igmp_gs, 0x00, sizeof(igmp_gs));
    igmp_gs[0].group = inet_addr("224.0.0.10");
    igmp_gs[0].group = inet_addr("224.0.0.11");
    igmp_gs[0].group = inet_addr("224.0.0.12");
    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV1Query, igmp_gs, 3);
    TEST_LOG(DEBUG, "IgmpTypeV3Query Packet : " << endl);
    PrintIgmp(buf, len);

    memset(igmp_gs, 0x00, sizeof(igmp_gs));
    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV2Leave, igmp_gs, 0);
    TEST_LOG(DEBUG, "IgmpTypeV2Leave Packet : " << endl);
    PrintIgmp(buf, len);

    memset(igmp_gs, 0x00, sizeof(igmp_gs));
    igmp_gs[0].group = inet_addr("224.0.0.10");
    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV1Report, igmp_gs, 1);
    TEST_LOG(DEBUG, "IgmpTypeV1Report Packet : " << endl);
    PrintIgmp(buf, len);

    memset(igmp_gs, 0x00, sizeof(igmp_gs));
    igmp_gs[0].group = inet_addr("224.0.0.10");
    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV2Report, igmp_gs, 1);
    TEST_LOG(DEBUG, "IgmpTypeV2Report Packet : " << endl);
    PrintIgmp(buf, len);

    memset(igmp_gs, 0x00, sizeof(igmp_gs));
    igmp_gs[0].group = inet_addr("224.1.0.10");
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    igmp_gs[0].sources[0] = inet_addr("100.1.0.10");
    igmp_gs[0].sources[0] = inet_addr("100.1.0.20");
    igmp_gs[0].sources[0] = inet_addr("100.1.0.30");

    igmp_gs[1].group = inet_addr("224.2.0.10");
    igmp_gs[1].record_type = 1;
    igmp_gs[1].source_count = 4;
    igmp_gs[1].sources[0] = inet_addr("100.2.0.10");
    igmp_gs[1].sources[0] = inet_addr("100.2.0.20");
    igmp_gs[1].sources[0] = inet_addr("100.2.0.30");
    igmp_gs[1].sources[0] = inet_addr("100.2.0.40");

    igmp_gs[2].group = inet_addr("224.3.0.10");
    igmp_gs[2].record_type = 1;
    igmp_gs[2].source_count = 5;
    igmp_gs[2].sources[0] = inet_addr("100.3.0.10");
    igmp_gs[2].sources[0] = inet_addr("100.3.0.20");
    igmp_gs[2].sources[0] = inet_addr("100.3.0.30");
    igmp_gs[2].sources[0] = inet_addr("100.3.0.40");
    igmp_gs[2].sources[0] = inet_addr("100.3.0.50");

    len = FormIgmp(buf, buf_len, 0, "1.1.1.1", IgmpTypeV3Report, igmp_gs, 3);
    TEST_LOG(DEBUG, "IgmpTypeV3Report Packet : " << endl);
    PrintIgmp(buf, len);

    delete [] buf;

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3Reports) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;

    Set_Up();

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    IgmpGlobalEnable(true);
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 1;
    igmp_gs[0].record_type = 2;
    igmp_gs[0].source_count = 4;
    local_sgh_add_count += 0;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 3;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 4;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 5;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    igmp_gs[1].record_type = 1;
    igmp_gs[1].source_count = 4;
    local_sgh_add_count += igmp_gs[0].source_count;
    local_sgh_add_count += igmp_gs[1].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 2);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[2].record_type = 1;
    igmp_gs[2].source_count = 5;
    local_sgh_add_count += igmp_gs[2].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    Agent *agent = Agent::GetInstance();

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(5, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 4;
    local_sgh_del_count += 0;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 3;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 4;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 5;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    igmp_gs[1].record_type = 6;
    igmp_gs[1].source_count = 4;
    local_sgh_del_count += igmp_gs[0].source_count + igmp_gs[1].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 2);
    ret = WaitForRxOkCount(idx, IgmpTypeV3Report, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[2].record_type = 6;
    igmp_gs[2].source_count = 5;
    local_sgh_del_count += igmp_gs[2].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs[2], 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3ReportsAndFabricOlist) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;

    Set_Up();

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;
    const TunnelNH *tnh;
    const InterfaceNH *inh;

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    Agent *agent = Agent::GetInstance();

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 100,
                    Ip4Address::from_string("1.1.1.1"),
                    TunnelType::MplsType()));

    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    1000, olist, 1);
    client->WaitForIdle();

    agent->oper_db()->multicast()->ModifyMvpnVrfRegistration(bgp_peer(1),
                            vm_itf->vrf()->GetName(), group, source, 0);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    tnh = dynamic_cast<const TunnelNH *>(nh);
    EXPECT_EQ((tnh != NULL), true);

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    cnh1 = cnh->Get(1);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    inh = dynamic_cast<const InterfaceNH *>(nh);
    EXPECT_EQ((inh != NULL), true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    TunnelOlist olist1;
    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    0, olist1, ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, SendV3ReportsAndNoFabricOlist) {

    bool ret = false;
    boost::system::error_code ec;

    uint32_t idx = 0;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;

    Set_Up();

    IgmpGlobalEnable(true);
    idx = 0;
    IgmpVmiEnable(idx, true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 1;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    Agent *agent = Agent::GetInstance();

    VmInterface *vm_itf = VmInterfaceGet(input[0].intf_id);
    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    vm_itf->vrf()->GetName(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(3, cnh->ActiveComponentNHCount());

    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 100,
                    Ip4Address::from_string("1.1.1.1"),
                    TunnelType::MplsType()));

    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    1000, olist, 1);
    client->WaitForIdle();

    agent->oper_db()->multicast()->ModifyMvpnVrfRegistration(bgp_peer(1),
                            vm_itf->vrf()->GetName(), group, source,
                            ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 2;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 1;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    TunnelOlist olist1;
    agent->oper_db()->multicast()->ModifyFabricMembers(
                    agent->multicast_tree_builder_peer(),
                    agent->fabric_policy_vrf_name(), group, source,
                    0, olist1, ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    nh = MCRouteToNextHop(agent->multicast_tree_builder_peer(),
                                    agent->fabric_policy_vrf_name(), group,
                                    source);
    EXPECT_EQ((nh == NULL), true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpIntfConfig) {

    bool ret = false;
    boost::system::error_code ec;

    VmInterface *vm_itf = NULL;

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t report_count = 0;
    uint32_t rej_count = 0;
    uint32_t idx = 0;

    const NextHop *nh;
    const CompositeNH *cnh;
    const ComponentNH *cnh1;

    Agent *agent = Agent::GetInstance();

    Set_Up();

    VmInterface *vmi_0 = VmInterfaceGet(input[0].intf_id);
    std::string vrf_name = vmi_0->vrf()->GetName();

    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    Ip4Address group = Ip4Address::from_string("224.1.0.10", ec);
    Ip4Address source = Ip4Address::from_string("100.1.0.10", ec);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    idx = 1;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    // Disable IGMP on idx = 0
    idx = 0;
    IgmpVmiEnable(idx, false);
    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    rej_count = 0;

    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    local_sgh_add_count += igmp_gs[0].source_count;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 6;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(false, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    // Enable IGMP on idx = 0
    idx = 0;
    IgmpVmiEnable(idx, true);
    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    local_sgh_add_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_add_count);
    EXPECT_EQ(ret, true);

    nh = MCRouteToNextHop(agent->local_vm_peer(), vrf_name, group, source);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    cnh1 = cnh->Get(0);
    nh = cnh1->nh();
    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    local_sgh_del_count += igmp_gs[0].source_count;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);
    ret = WaitForSghCount(true, local_sgh_del_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpVnEnable) {

    bool ret = false;

    uint32_t report_count = 0;
    uint32_t idx = 0;

    Set_Up();

    IgmpVnEnable("vn1", 1, true);

    idx = 0;
    VmInterface *vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpVnDisable) {

    bool ret = false;

    uint32_t idx = 0;
    uint32_t rej_count = 0;

    Set_Up();

    IgmpVnEnable("vn1", 1, false);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpSystemEnable) {

    bool ret = false;

    uint32_t report_count = 0;
    uint32_t idx = 0;

    Set_Up();

    IgmpGlobalEnable(true);

    idx = 0;
    VmInterface *vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    idx = 0;
    igmp_gs[0].record_type = 6;
    igmp_gs[0].source_count = 3;
    report_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpSystemDisable) {

    bool ret = false;

    uint32_t idx = 0;
    uint32_t rej_count = 0;

    Set_Up();

    IgmpGlobalEnable(false);

    idx = 0;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    idx = 6;
    igmp_gs[0].record_type = 1;
    igmp_gs[0].source_count = 3;
    rej_count++;
    SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, igmp_gs, 1);
    ret = WaitForRejPktCount(rej_count);
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_1) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    Set_Up();

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(false);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalEnable(true);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    IgmpVnEnable("vn1", 1, true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    IgmpVnEnable("vn1", 1, false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_2) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    Set_Up();

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpGlobalEnable(false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpMode_3) {

    VmInterface *vm_itf = NULL;
    uint32_t idx = 0;

    Set_Up();

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpVmiEnable(idx, false);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), false);

    IgmpVnEnable("vn1", 1, false);

    IgmpVmiEnable(idx, true);

    vm_itf = VmInterfaceGet(input[idx].intf_id);
    EXPECT_EQ(vm_itf->igmp_enabled(), true);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQuerySend) {

    bool ret = false;

    Set_Up();

    IgmpGlobalEnable(true);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    ret = WaitForTxCount(idx, true, vms*(sleep+1)); // 7 VMs and 2 queries in 10 secs
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQueryDisabled) {

    bool ret = false;

    Set_Up();

    IgmpGlobalEnable(false);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    ret = WaitForTxCount(idx, false, vms*(sleep+1)); // 7 VMs and 2 queries in 10 secs
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpTaskTrigger) {

    bool ret = false;
    uint32_t idx = 0;
    struct IgmpGroupSource igmp_gs;

    Set_Up();

    IgmpGlobalEnable(true);

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t local_sgh_add_count = 0;
    uint32_t local_sgh_del_count = 0;
    uint32_t group = inet_addr("224.1.0.10");
    uint32_t source = inet_addr("100.1.0.10");

    memset(&igmp_gs, 0x00, sizeof(igmp_gs));

    igmp_gs.record_type = 1;
    igmp_gs.source_count = 2;

    igmp_gs.group = ntohl(group);

    uint32_t report_count = 0;
    uint32_t loop = 10;
    uint32_t pkt_per_loop = 75;
    for (uint32_t i = 0; i < loop; i++) {
        for (uint32_t j = 0; j < pkt_per_loop; j++) {
            igmp_gs.sources[0] = ntohl(source);
            igmp_gs.sources[1] = ntohl(source+1);
            report_count++;
            SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
            source += igmp_gs.source_count;
            local_sgh_add_count += igmp_gs.source_count;
            usleep(1000);
        }
        usleep(10000);
    }
    client->WaitForIdle();

    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    GmpProto::GmpStats stats;
    uint32_t count = 0;
    uint32_t ex_count = loop*pkt_per_loop*igmp_gs.source_count;
    do {
        usleep(1000);
        client->WaitForIdle();
        Agent *agent = Agent::GetInstance();
        stats = agent->GetIgmpProto()->GetGmpProto()->GetStats();
        if (++count == 10000) {
            break;
        }
    } while (stats.gmp_sgh_add_count_ != ex_count);

    uint32_t actual_count = stats.gmp_sgh_add_count_;
    EXPECT_EQ(actual_count, ex_count);

    igmp_gs.record_type = 6;
    igmp_gs.source_count = 2;

    igmp_gs.group = ntohl(group);

    for (uint32_t i = 0; i < loop; i++) {
        for (uint32_t j = 0; j < pkt_per_loop; j++) {
            igmp_gs.sources[0] = ntohl(source);
            igmp_gs.sources[1] = ntohl(source+1);
            report_count++;
            SendIgmp(GetItfId(idx), input[idx].addr, IgmpTypeV3Report, &igmp_gs, 1);
            source += igmp_gs.source_count;
            local_sgh_del_count += igmp_gs.source_count;
            usleep(1000);
        }
        usleep(10000);
    }
    client->WaitForIdle();

    ret = WaitForRxOkCount(idx, IGMP_V3_MEMBERSHIP_REPORT, report_count);
    EXPECT_EQ(ret, true);

    count = 0;
    ex_count = loop*pkt_per_loop*igmp_gs.source_count;
    do {
        usleep(1000);
        client->WaitForIdle();
        Agent *agent = Agent::GetInstance();
        stats = agent->GetIgmpProto()->GetGmpProto()->GetStats();
        if (++count == 10000) {
            break;
        }
    } while (stats.gmp_sgh_del_count_ != ex_count);

    actual_count = stats.gmp_sgh_del_count_;
    EXPECT_EQ(actual_count, ex_count);

    IgmpGlobalClear();

    Tear_Down();
    client->WaitForIdle();

    return;
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(DEFAULT_VNSW_CONFIG_FILE, ksync_init, true, true);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();

    client->WaitForIdle();
    TestShutdown();

    delete client;

    return ret;
}
