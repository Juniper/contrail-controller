/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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

#include <io/event_manager.h>
#include <pugixml/pugixml.hpp>
#include <xmpp/xmpp_init.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <services/services_init.h>
#include <services/igmp_proto.h>
#include <services/services_sandesh.h>
#include <pkt/pkt_init.h>
#include <vr_interface.h>
#include "vr_types.h"
#include <vrouter/ksync/ksync_init.h>

#include "test/test_cmn_util.h"
#include "test/pkt_gen.h"
#include "io/test/event_manager_test.h"
#include "xmpp/test/xmpp_test_util.h"
#include "control_node_mock.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void gmp_set_def_igmp_version(uint32_t version);
extern void gmp_set_def_ipv4_ivl_params(uint32_t robust_count, uint32_t qivl,
                        uint32_t qrivl, uint32_t lmqi);
#ifdef __cplusplus
}
#endif

// Control Node members
static const int kControlNodes = 2;
EventManager cn_evm[kControlNodes];
ServerThread *cn_thread[kControlNodes];
test::ControlNodeMock *cn_bgp_peer[kControlNodes];

#define UT_IGMP_VERSION_1       1
#define UT_IGMP_VERSION_2       2
#define UT_IGMP_VERSION_3       3

#define MSECS_PER_SEC           1000
#define USECS_PER_SEC           ((MSECS_PER_SEC)*(MSECS_PER_SEC))
#define MAX_WAIT_COUNT          1000
#define BUF_SIZE                8192

char src_mac[ETHER_ADDR_LEN] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
char dest_mac[ETHER_ADDR_LEN] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };

#define MAX_TESTNAME_LEN            100
#define MAX_VMS_PER_VN              7
#define NUM_VNS                     2

struct PortInfo input[MAX_VMS_PER_VN] = {
    {"vnet1-0", 1, "10.2.1.3", "00:00:10:01:01:03", 1, 1},
    {"vnet1-1", 2, "10.2.1.4", "00:00:10:01:01:04", 1, 1},
    {"vnet1-2", 3, "10.2.1.5", "00:00:10:01:01:05", 1, 1},
    {"vnet1-3", 4, "10.2.1.6", "00:00:10:01:01:06", 1, 1},
    {"vnet1-4", 5, "10.2.1.7", "00:00:10:01:01:07", 1, 1},
    {"vnet1-5", 6, "10.2.1.8", "00:00:10:01:01:08", 1, 1},
    {"vnet1-6", 7, "10.2.1.9", "00:00:10:01:01:09", 1, 1},
};

struct PortInfo input_2[MAX_VMS_PER_VN] = {
    {"vnet2-0", 11, "10.2.2.3", "00:00:10:01:02:03", 2, 2},
    {"vnet2-1", 12, "10.2.2.4", "00:00:10:01:02:04", 2, 2},
    {"vnet2-2", 13, "10.2.2.5", "00:00:10:01:02:05", 2, 2},
    {"vnet2-3", 14, "10.2.2.6", "00:00:10:01:02:06", 2, 2},
    {"vnet2-4", 15, "10.2.2.7", "00:00:10:01:02:07", 2, 2},
    {"vnet2-5", 16, "10.2.2.8", "00:00:10:01:02:08", 2, 2},
    {"vnet2-6", 17, "10.2.2.9", "00:00:10:01:02:09", 2, 2},
};

IpamInfo ipam_info[] = {
    {"10.2.1.0", 24, "10.2.1.1", true},
};

IpamInfo ipam_info_2[] = {
    {"10.2.2.0", 24, "10.2.2.1", true},
};

#define MSUBNET_SYSTEMS     "224.0.0.1"
#define MSUBNET_ROUTERS     "224.0.0.2"
#define MIGMP_ADDRESS       "224.0.0.22"

#define MGROUP_ADDR_1       "239.1.1.10"
#define MGROUP_ADDR_2       "239.2.1.10"
#define MGROUP_ADDR_3       "239.3.1.10"
#define MGROUP_ADDR_4       "239.4.1.10"

#define MSOURCE_ADDR_0      "0.0.0.0"
#define MSOURCE_ADDR_11     "100.1.0.10"
#define MSOURCE_ADDR_12     "100.1.0.20"
#define MSOURCE_ADDR_13     "100.1.0.30"
#define MSOURCE_ADDR_14     "100.1.0.40"
#define MSOURCE_ADDR_15     "100.1.0.50"

#define MSOURCE_ADDR_21     "100.2.0.10"
#define MSOURCE_ADDR_22     "100.2.0.20"
#define MSOURCE_ADDR_23     "100.2.0.30"
#define MSOURCE_ADDR_24     "100.2.0.40"
#define MSOURCE_ADDR_25     "100.2.0.50"

#define MSOURCE_ADDR_31     "100.3.0.10"
#define MSOURCE_ADDR_32     "100.3.0.20"
#define MSOURCE_ADDR_33     "100.3.0.30"
#define MSOURCE_ADDR_34     "100.3.0.40"
#define MSOURCE_ADDR_35     "100.3.0.50"

MulticastPolicy policy[] = {
    {MSOURCE_ADDR_11, MGROUP_ADDR_1, true},
    {MSOURCE_ADDR_12, MGROUP_ADDR_1, false},
    {MSOURCE_ADDR_13, MGROUP_ADDR_1, true},
    {MSOURCE_ADDR_14, MGROUP_ADDR_1, false},
};

MulticastPolicy policy_01[] = {
    {MSOURCE_ADDR_0, MGROUP_ADDR_1, true},
    {MSOURCE_ADDR_0, MGROUP_ADDR_2, false},
};

MulticastPolicy policy_02[] = {
    {MSOURCE_ADDR_0, MGROUP_ADDR_3, true},
    {MSOURCE_ADDR_0, MGROUP_ADDR_4, false},
};

MulticastPolicy policy_11[] = {
    {MSOURCE_ADDR_11, MGROUP_ADDR_1, true},
    {MSOURCE_ADDR_12, MGROUP_ADDR_1, false},
};

MulticastPolicy policy_12[] = {
    {MSOURCE_ADDR_11, MGROUP_ADDR_2, false},
    {MSOURCE_ADDR_12, MGROUP_ADDR_2, true},
};

MulticastPolicy policy_21[] = {
    {MSOURCE_ADDR_21, MGROUP_ADDR_1, true},
    {MSOURCE_ADDR_22, MGROUP_ADDR_1, false},
};

MulticastPolicy policy_22[] = {
    {MSOURCE_ADDR_21, MGROUP_ADDR_2, false},
    {MSOURCE_ADDR_22, MGROUP_ADDR_2, true},
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

void RouterIdDepInit(Agent *agent) {
#if 1
    Agent::GetInstance()->controller()->Connect();
#endif
}

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

        def_lmqt = ((def_lmqi * 2) + def_lmqi/2 + 5) * 1000;

        TestPkt0Interface *tap = (TestPkt0Interface *)
                    (Agent::GetInstance()->pkt()->control_interface());
        tap->RegisterCallback(
                boost::bind(&IgmpTest::IgmpTestClientReceive, this, _1, _2));

        vrfid_ = Agent::GetInstance()->vrf_table()->Register(
                boost::bind(&IgmpTest::VrfUpdate, this, _2));
        rid_ = Agent::GetInstance()->interface_table()->Register(
                boost::bind(&IgmpTest::ItfUpdate, this, _2));

        memset(igmp_gs, 0x00, sizeof(igmp_gs));
        igmp_gs[0].group = ntohl(inet_addr(MGROUP_ADDR_1));
        igmp_gs[0].sources[0] = ntohl(inet_addr(MSOURCE_ADDR_11));
        igmp_gs[0].sources[1] = ntohl(inet_addr(MSOURCE_ADDR_12));
        igmp_gs[0].sources[2] = ntohl(inet_addr(MSOURCE_ADDR_13));
        igmp_gs[0].sources[3] = ntohl(inet_addr(MSOURCE_ADDR_14));
        igmp_gs[0].sources[4] = ntohl(inet_addr(MSOURCE_ADDR_15));

        igmp_gs[1].group = ntohl(inet_addr(MGROUP_ADDR_2));
        igmp_gs[1].sources[0] = ntohl(inet_addr(MSOURCE_ADDR_21));
        igmp_gs[1].sources[1] = ntohl(inet_addr(MSOURCE_ADDR_22));
        igmp_gs[1].sources[2] = ntohl(inet_addr(MSOURCE_ADDR_23));
        igmp_gs[1].sources[3] = ntohl(inet_addr(MSOURCE_ADDR_24));
        igmp_gs[1].sources[4] = ntohl(inet_addr(MSOURCE_ADDR_25));

        igmp_gs[2].group = ntohl(inet_addr(MGROUP_ADDR_3));
        igmp_gs[2].sources[0] = ntohl(inet_addr(MSOURCE_ADDR_31));
        igmp_gs[2].sources[1] = ntohl(inet_addr(MSOURCE_ADDR_32));
        igmp_gs[2].sources[2] = ntohl(inet_addr(MSOURCE_ADDR_33));
        igmp_gs[2].sources[3] = ntohl(inet_addr(MSOURCE_ADDR_34));
        igmp_gs[2].sources[4] = ntohl(inet_addr(MSOURCE_ADDR_35));

        rx_count = 0;
    }

    ~IgmpTest() {
        Agent::GetInstance()->vrf_table()->Unregister(vrfid_);
        Agent::GetInstance()->interface_table()->Unregister(rid_);
    }

    void SetUp() {
        agent_ = Agent::GetInstance();
#if 1
        for (uint32_t i = 0; i < kControlNodes; i++) {
            bgp_peer_[i] = agent_->controller_xmpp_channel(i)->bgp_peer_id();
        }

        client->Reset();
#endif
    }

    void TearDown() {
        //Clean up any pending routes in conrol node mock
#if 1
        for (uint32_t i = 0; i < kControlNodes; i++) {
            cn_bgp_peer[i]->Clear();
        }
#endif
    }

    BgpPeer *bgp_peer(uint32_t idx) {
//        return idx ? bgp_peer_[idx] : NULL;
        return bgp_peer_[idx];
    }

    void GetVnPortInfo(uint32_t i, struct PortInfo **port, uint32_t *size,
                    IpamInfo **ipam) {
        if (i == 0) {
            if (port) *port = &input[0];
            if (size) *size = sizeof(input)/sizeof(struct PortInfo);
            if (ipam) *ipam = &ipam_info[0];
        } else {
            if (port) *port = &input_2[0];
            if (size) *size = sizeof(input_2)/sizeof(struct PortInfo);
            if (ipam) *ipam = &ipam_info_2[0];
        }
    }

    void GetVnPortInfoForVmIdx(uint32_t i, struct PortInfo **port,
                    IpamInfo **ipam) {
        if (i < MAX_VMS_PER_VN) {
            if (port) *port = &input[0];
            if (ipam) *ipam = &ipam_info[0];
        } else {
            if (port) *port = &input_2[0];
            if (ipam) *ipam = &ipam_info_2[0];
        }
    }

    void CreateVns() {

        char vn_name[MAX_TESTNAME_LEN];
        char vrf_name[MAX_TESTNAME_LEN];

        struct PortInfo *port;
        IpamInfo *ipam;
        uint32_t size = 0;
        for (uint32_t i = 0; i < NUM_VNS; i++) {
            GetVnPortInfo(i, &port, &size, &ipam);

            sprintf(vn_name, "vn%d", port->vn_id);
            sprintf(vrf_name, "vrf%d", port->vn_id);

            CreateVmportEnv(port, size, 0, vn_name, vrf_name);
            client->WaitForIdle();
            client->Reset();
            AddIPAM(vn_name, ipam, 1);
            client->WaitForIdle();

            IpAddress gateway = IpAddress(Ip4Address::from_string(ipam->gw));
            VmInterface *vm_itf = VmInterfaceGet(port->intf_id);
            const VnEntry *vn = vm_itf->vn();
            agent_->GetIgmpProto()->ClearItfStats(vn, gateway);
            client->WaitForIdle();
        }
    }

    void DeleteVns() {

        char vn_name[MAX_TESTNAME_LEN];
        char vrf_name[MAX_TESTNAME_LEN];

        struct PortInfo *port;
        uint32_t size = 0;
        for (uint32_t i = 0; i < NUM_VNS; i++) {
            GetVnPortInfo(i, &port, &size, NULL);

            sprintf(vn_name, "vn%d", port->vn_id);
            sprintf(vrf_name, "vrf%d", port->vn_id);
            client->Reset();
            DelIPAM(vn_name);
            client->WaitForIdle();

            client->Reset();

            DeleteVmportEnv(port, size, 1, 0, vn_name, vrf_name);
            client->WaitForIdle();
        }
    }

    void DeleteVnsFirst() {

        char vn_name[MAX_TESTNAME_LEN];
        char vrf_name[MAX_TESTNAME_LEN];

        struct PortInfo *port;
        uint32_t size = 0;
        for (uint32_t i = 0; i < NUM_VNS; i++) {
            GetVnPortInfo(i, &port, &size, NULL);

            sprintf(vn_name, "vn%d", port->vn_id);
            sprintf(vrf_name, "vrf%d", port->vn_id);
            client->Reset();
            DelVn(vn_name);
            client->WaitForIdle();
            DelIPAM(vn_name);
            client->WaitForIdle();

            client->Reset();

            DeleteVmportEnv(port, size, 1, 0, vn_name, vrf_name);
            client->WaitForIdle();
        }
    }

    void TestEnvInit(uint32_t version, bool set_lmqt) {
        agent_ = Agent::GetInstance();
        client->WaitForIdle();

        gmp_set_def_igmp_version(version);

        if (set_lmqt) {
            // Force timeout for IGMP faster.
            gmp_set_def_ipv4_ivl_params(robust_count, qivl, qrivl, lmqi);
            // formula from gmpr_intf_update_lmqt
        }

#if 0
        Ip4Address peer_ip;
        boost::system::error_code ec;

        bgp_peer_[0] = agent_->controller_xmpp_channel(0)->bgp_peer_id();

        peer_ip = Ip4Address::from_string("127.0.0.1", ec);
        bgp_peer_[1] = CreateBgpPeer("127.0.0.1", "remote-1");
        client->WaitForIdle();
#endif

        CreateVns();

        agent_->GetIgmpProto()->ClearStats();
        agent_->GetIgmpProto()->GetGmpProto()->ClearStats();
        client->WaitForIdle();
    }

    void TestEnvDeinit(bool del_vn_first = false) {

#if 0
        DeleteBgpPeer(bgp_peer_[1]);
        client->WaitForIdle();
#endif

        if (del_vn_first) {
            DeleteVnsFirst();
        } else {
            DeleteVns();
        }
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

        agent_hdr *agent_ = (agent_hdr *)(eth + 1);
        agent_->hdr_ifindex = htons(ifindex);
        agent_->hdr_vrf = htons(0);
        agent_->hdr_cmd = htons(AgentHdr::TRAP_NEXTHOP);

        eth = (struct ether_header *) (agent_ + 1);
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
                ip->ip_dst.s_addr = inet_addr(MSUBNET_SYSTEMS);
            } else {
                ip->ip_dst.s_addr = htonl(igmp_gs[0].group);
            }
        } else if ((igmp_type == IgmpTypeV1Report) ||
                   (igmp_type == IgmpTypeV2Report)) {

            ip->ip_dst.s_addr = htonl(igmp_gs[0].group);
        } else if (igmp_type == IgmpTypeV2Leave) {

            ip->ip_dst.s_addr = inet_addr(MSUBNET_ROUTERS);
        } else if (igmp_type == IgmpTypeV3Report) {

            ip->ip_dst.s_addr = inet_addr(MIGMP_ADDRESS);
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

        struct PortInfo *port;
        IpamInfo *ipam;
        VmInterface *vm_itf;
        GetVnPortInfoForVmIdx(vm_idx, &port, &ipam);
        IgmpInfo::IgmpItfStats stats;

        vm_itf = VmInterfaceGet(port->intf_id);
        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            const VnEntry *vn = vm_itf->vn();
            IpAddress gateway = IpAddress(Ip4Address::from_string(ipam->gw));
            Agent::GetInstance()->GetIgmpProto()->GetItfStats(vn, gateway,
                                    stats);
            counter = stats.rx_okpacket[index-1];
            if (++count == MAX_WAIT_COUNT)
                return false;
        } while (counter != ex_count);
        return true;
    }

    bool WaitForGCount(bool add, uint32_t ex_count) {

        GmpProto::GmpStats stats;
        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            stats = agent_->GetIgmpProto()->GetGmpProto()->GetStats();
            counter = add ? stats.gmp_g_add_count_ :
                                stats.gmp_g_del_count_;
            if (++count == MAX_WAIT_COUNT) {
                return false;
            }
        } while (counter != ex_count);
        return true;
    }

    bool WaitForSgCount(bool add, uint32_t ex_count) {

        GmpProto::GmpStats stats;
        int count = 0;
        uint32_t counter = 0;

        do {
            usleep(1000);
            client->WaitForIdle();
            stats = agent_->GetIgmpProto()->GetGmpProto()->GetStats();
            counter = add ? stats.gmp_sg_add_count_ :
                                stats.gmp_sg_del_count_;
            if (++count == MAX_WAIT_COUNT) {
                return false;
            }
        } while (counter != ex_count);
        return true;
    }

    bool WaitForTxCount(uint32_t vm_idx, bool tx, uint32_t ex_count) {

        struct PortInfo *port;
        IpamInfo *ipam;
        VmInterface *vm_itf;
        GetVnPortInfoForVmIdx(vm_idx, &port, &ipam);
        IgmpInfo::IgmpItfStats stats;
        int count = 0;
        uint32_t counter = 0;

        vm_itf = VmInterfaceGet(port->intf_id);
        do {
            usleep(1000);
            client->WaitForIdle();
            const VnEntry *vn = vm_itf->vn();
            IpAddress gateway = IpAddress(Ip4Address::from_string(ipam->gw));
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

    void IgmpRxCountReset(void) {
        rx_count = 0;
    }

    uint32_t IgmpRxCountGet(void) {
        return rx_count;
    }

public:
    uint32_t rx_count;
    uint32_t robust_count;
    uint32_t qivl;
    uint32_t qrivl;
    uint32_t lmqi;
    static const uint32_t def_lmqi = 1000;
    uint32_t lmqt;
    uint32_t def_lmqt;

private:

    // Agent related members
    DBTableBase::ListenerId vrfid_;
    DBTableBase::ListenerId rid_;
    uint32_t itf_count_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
    BgpPeer *bgp_peer_[kControlNodes];

    Agent *agent_;
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

void StartControlNodeMock() {

    Agent *agent = Agent::GetInstance();

    for (uint32_t i = 0; i < kControlNodes; i++) {
        cn_thread[i] = new ServerThread(&cn_evm[i]);
        cn_bgp_peer[i] = new test::ControlNodeMock(&cn_evm[i], "127.0.0.1");

        agent->set_controller_ifmap_xmpp_server("127.0.0.1", i);
        agent->set_controller_ifmap_xmpp_port(cn_bgp_peer[i]->GetServerPort(), i);
        agent->set_dns_server("", i);
        agent->set_dns_server_port(cn_bgp_peer[i]->GetServerPort(), i);

        cn_thread[i]->Start();
    }

    return;
}

void StopControlNodeMock() {

    for (uint32_t i = 0; i < kControlNodes; i++) {

        cn_bgp_peer[i]->Shutdown();
        client->WaitForIdle();
        delete cn_bgp_peer[i];

        cn_evm[i].Shutdown();
        cn_thread[i]->Join();
        delete cn_thread[i];
    }

    client->WaitForIdle();

    return;
}

