/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

void DoInterfaceSandesh(std::string name) {
    ItfReq *itf_req = new ItfReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    if (name != "") {
        itf_req->set_name(name);
    }
    itf_req->HandleRequest();
    client->WaitForIdle();
    itf_req->Release();
    client->WaitForIdle();
}

bool GetNhPolicy(const string &vrf, Ip4Address addr, int plen) {
   const NextHop *nh = RouteGet(vrf, addr, plen)->GetActiveNextHop();
   return nh->PolicyEnabled();
}

struct TestFatFlowEntry {
    std::string protocol;
    uint16_t port;
    std::string ignore_address;
    std::string src_prefix;
    int src_prefix_mask;
    int src_aggregate_plen;
    std::string dst_prefix;
    int dst_prefix_mask;
    int dst_aggregate_plen;
    TestFatFlowEntry(string proto, uint16_t num) :
        protocol(proto), port(num), ignore_address("none"), src_prefix(""), src_prefix_mask(0),
        src_aggregate_plen(0), dst_prefix(""), dst_prefix_mask(0), dst_aggregate_plen(0) {
    }
    TestFatFlowEntry(string proto, uint16_t num, const std::string &addr) :
        protocol(proto), port(num), ignore_address(addr), src_prefix(""), src_prefix_mask(0),
        src_aggregate_plen(0), dst_prefix(""), dst_prefix_mask(0), dst_aggregate_plen(0) {
    }
    TestFatFlowEntry(string proto, uint16_t num, const std::string &addr, const std::string &in_src_prefix,
                     int in_src_prefix_mask, int in_src_aggregate_plen, const std::string &in_dst_prefix,
                     int in_dst_prefix_mask, int in_dst_aggregate_plen) :
        protocol(proto), port(num), ignore_address(addr), src_prefix(in_src_prefix),
        src_prefix_mask(in_src_prefix_mask), src_aggregate_plen(in_src_aggregate_plen),
        dst_prefix(in_dst_prefix), dst_prefix_mask(in_dst_prefix_mask), dst_aggregate_plen(in_dst_aggregate_plen) {
    }
};

class IntfTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        intf_count = agent->interface_table()->Size();
        DoInterfaceSandesh("");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DoInterfaceSandesh("");
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->interface_table()->Size() == intf_count));
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == 2U));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
    }

    void VnAdd(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        AddVn(vn_name, id);
        WAIT_FOR(1000, 100, (client->vn_notify_ >= 1));
        EXPECT_TRUE(VnFind(id));
        EXPECT_EQ((vn_count + 1), Agent::GetInstance()->vn_table()->Size());
    }

    void VnDelete(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        DelNode("virtual-network", vn_name);
        EXPECT_TRUE(client->VnNotifyWait(1));
        EXPECT_EQ((vn_count - 1), Agent::GetInstance()->vn_table()->Size());
        EXPECT_FALSE(VnFind(id));
    }

    void VmAdd(int id) {
        char vm_name[80];

        sprintf(vm_name, "vm%d", id);
        uint32_t vm_count = Agent::GetInstance()->vm_table()->Size();
        client->Reset();
        AddVm(vm_name, id);
        EXPECT_TRUE(client->VmNotifyWait(1));
        EXPECT_TRUE(VmFind(id));
        EXPECT_EQ((vm_count + 1), Agent::GetInstance()->vm_table()->Size());
    }

    void VmDelete(int id) {
        char vm_name[80];

        sprintf(vm_name, "vm%d", id);
        uint32_t vm_count = Agent::GetInstance()->vm_table()->Size();
        client->Reset();
        DelNode("virtual-machine", vm_name);
        client->WaitForIdle();
        EXPECT_TRUE(client->VmNotifyWait(1));
        EXPECT_FALSE(VmFind(id));
        EXPECT_EQ((vm_count - 1), Agent::GetInstance()->vm_table()->Size());
    }

    void VrfAdd(int id) {
        char vrf_name[80];

        sprintf(vrf_name, "vrf%d", id);
        client->Reset();
        EXPECT_FALSE(VrfFind(vrf_name));
        AddVrf(vrf_name);
        client->WaitForIdle(3);
        WAIT_FOR(1000, 5000, (VrfFind(vrf_name) == true));
    }

    void NovaPortAdd(struct PortInfo *input) {
        client->Reset();
        IntfCfgAddThrift(input, 0);
        //IntfCfgAdd(input, 0);
        EXPECT_TRUE(client->PortNotifyWait(1));
    }

    void ConfigPortAdd(struct PortInfo *input, bool admin_state) {
        client->Reset();
        AddPortByStatus(input[0].name, input[0].intf_id, admin_state);
        client->WaitForIdle();
    }

    void AddFatFlow(struct PortInfo *input, std::string protocol, int port,
                    const string &ignore_address, const string &src_prefix, int src_prefix_mask, int src_aggregate_plen,
                    const string &dst_prefix, int dst_prefix_mask, int dst_aggregate_plen) {

        ostringstream str;

        str << "<virtual-machine-interface-fat-flow-protocols>"
               "<fat-flow-protocol>"
               "<protocol>" << protocol << "</protocol>"
               "<port>" << port << "</port>"
               "<ignore-address>" << ignore_address << "</ignore-address>"
               "<source-prefix>"
               "<ip-prefix>" << src_prefix << "</ip-prefix>"
               "<ip-prefix-len>" << src_prefix_mask << "</ip-prefix-len>"
               "</source-prefix>"
               "<source-aggregate-prefix-length>" << src_aggregate_plen << "</source-aggregate-prefix-length>"
               "<destination-prefix>"
               "<ip-prefix>" << dst_prefix << "</ip-prefix>"
               "<ip-prefix-len>" << dst_prefix_mask << "</ip-prefix-len>"
               "</destination-prefix>"
               "<destination-aggregate-prefix-length>" << dst_aggregate_plen << "</destination-aggregate-prefix-length>"
               "</fat-flow-protocol>"
               "</virtual-machine-interface-fat-flow-protocols>";
        AddNode("virtual-machine-interface", input[0].name, input[0].intf_id,
                str.str().c_str());
    }

    void DeleteFatFlow(struct PortInfo *input) {

        ostringstream str;

        str << "<virtual-machine-interface-fat-flow-protocols>"
               "</virtual-machine-interface-fat-flow-protocols>";
        AddNode("virtual-machine-interface", input[0].name, input[0].intf_id,
                str.str().c_str());
    }

    void AddVnFatFlow(int vn_id, vector<TestFatFlowEntry> list) {

        ostringstream str;
        char vn_name[64];
        sprintf(vn_name, "vn%d", vn_id);

        str << "<virtual-network-fat-flow-protocols>";
        vector<TestFatFlowEntry>::iterator it = list.begin();
        while (it != list.end()) {
            str << "<fat-flow-protocol>"
                   "<protocol>" << it->protocol << "</protocol>"
                   "<port>" << it->port << "</port>"
                   "<ignore-address>" << it->ignore_address << "</ignore-address>"
                   "<source-prefix>"
                   "<ip-prefix>" << it->src_prefix << "</ip-prefix>"
                   "<ip-prefix-len>" << it->src_prefix_mask << "</ip-prefix-len>"
                   "</source-prefix>"
                   "<source-aggregate-prefix-length>" << it->src_aggregate_plen << "</source-aggregate-prefix-length>"
                   "<destination-prefix>"
                   "<ip-prefix>" << it->dst_prefix << "</ip-prefix>"
                   "<ip-prefix-len>" << it->dst_prefix_mask << "</ip-prefix-len>"
                   "</destination-prefix>"
                   "<destination-aggregate-prefix-length>" << it->dst_aggregate_plen << "</destination-aggregate-prefix-length>"
                   "</fat-flow-protocol>";
            ++it;
        }
        str << "</virtual-network-fat-flow-protocols>";
        AddNode("virtual-network", vn_name, vn_id, str.str().c_str());
    }

    unsigned int intf_count;
    Agent *agent;
};

static void NovaIntfAdd(int id, const char *name, const char *addr,
                        const char *mac) {
    IpAddress ip = Ip4Address::from_string(addr);
    VmInterface::NovaAdd(Agent::GetInstance()->interface_table(),
                         MakeUuid(id), name, ip.to_v4(), mac, "",
                         MakeUuid(kProjectUuid), VmInterface::kInvalidVlanId,
                         VmInterface::kInvalidVlanId, Agent::NullString(),
                         Ip6Address(), VmInterface::vHostUserClient,
                         Interface::TRANSPORT_ETHERNET, 1);
}

static void NovaDel(int id) {
    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                        MakeUuid(id), VmInterface::INSTANCE_MSG);
}

static void ConfigDel(int id) {
    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                        MakeUuid(id), VmInterface::CONFIG);
}

static void FloatingIpAdd(VmInterface::FloatingIpList &list, const char *addr,
                          const char *vrf, const char *track_ip) {
    IpAddress ip = Ip4Address::from_string(addr);
    IpAddress tracking_ip = Ip4Address::from_string(track_ip);
    list.list_.insert(VmInterface::FloatingIp
                      (ip.to_v4(), vrf, MakeUuid(1), tracking_ip,
                       VmInterface::FloatingIp::DIRECTION_BOTH, false,
                       VmInterface::FloatingIp::PortMap(),
                       VmInterface::FloatingIp::PortMap(), false));
}

struct AnalyzerInfo {
    std::string analyzer_name;
    std::string vrf_name;
    std::string source_ip;
    std::string analyzer_ip;
    uint16_t sport;
    uint16_t dport;
    std::string direction;
};

static void CreateMirror(AnalyzerInfo &analyzer_info) {
    if (analyzer_info.analyzer_name.empty()) {
        return;
    }
    boost::system::error_code ec;
    Ip4Address dip = Ip4Address::from_string(analyzer_info.analyzer_ip, ec);
    if (ec.value() != 0) {
        return;
    }
    Agent::GetInstance()->mirror_table()->AddMirrorEntry(analyzer_info.analyzer_name,
                                                           std::string(),
                                                           Agent::GetInstance()->router_id(),
                                                           Agent::GetInstance()->mirror_port(),
                                                           dip,
                                                           analyzer_info.dport);
}

static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm,
                        VmInterface::FloatingIpList list, string vrf_name,
                        string ip, AnalyzerInfo &analyzer_info) {
    boost::uuids::uuid intf_uuid = MakeUuid(id);
    boost::uuids::uuid vn_uuid = MakeUuid(vn);
    boost::uuids::uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    CreateMirror(analyzer_info);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC,
                                                     intf_uuid, "");
    req.key.reset(key);

    VmInterfaceConfigData *cfg_data = new VmInterfaceConfigData(NULL, NULL);
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_ip_list_ = list;
    cfg_data->vrf_name_ = vrf_name;
    cfg_data->addr_ = Ip4Address::from_string(ip);
    cfg_data->fabric_port_ = false;
    cfg_data->admin_state_ = true;
    cfg_data->analyzer_name_ = analyzer_info.analyzer_name;
    if (analyzer_info.direction.compare("egress") == 0) {
        cfg_data->mirror_direction_ = Interface::MIRROR_TX;
    } else if (analyzer_info.direction.compare("ingress") == 0) {
        cfg_data->mirror_direction_ = Interface::MIRROR_RX;
    } else {
        cfg_data->mirror_direction_ = Interface::MIRROR_RX_TX;
    }
    LOG(DEBUG, "Analyzer name config:" << cfg_data->analyzer_name_);
    LOG(DEBUG, "Mirror direction config" << cfg_data->mirror_direction_);
    req.data.reset(cfg_data);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
}


static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm,
                        VmInterface::FloatingIpList list, string vrf_name,
                        string ip) {
    boost::uuids::uuid intf_uuid = MakeUuid(id);
    boost::uuids::uuid vn_uuid = MakeUuid(vn);
    boost::uuids::uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    req.key.reset(key);

    VmInterfaceConfigData *cfg_data = new VmInterfaceConfigData(NULL, NULL);
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_ip_list_ = list;
    cfg_data->vrf_name_ = vrf_name;
    cfg_data->addr_ = Ip4Address::from_string(ip);
    cfg_data->fabric_port_ = false;
    cfg_data->admin_state_ = true;
    req.data.reset(cfg_data);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
}

static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm,
                        string vrf, string ip) {
    VmInterface::FloatingIpList list;
    CfgIntfSync(id, cfg_str, vn, vm, list, vrf, ip);
}

TEST_F(IntfTest, basic_1) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    DoInterfaceSandesh("");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, index_reuse) {
    uint32_t    intf_idx;

    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:00:00:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    intf_idx = VmPortGetId(8);
    client->Reset();

    InterfaceRef intf(VmPortGet(8));
    DeleteVmportEnv(input1, 1, true);
    WAIT_FOR(1000, 1000, (VmPortFind(8) == false));
    client->Reset();

    CreateVmportEnv(input2, 1);
    WAIT_FOR(1000, 1000, (VmPortFind(9) == true));
    EXPECT_NE(VmPortGetId(9), intf_idx);
    client->Reset();
    DeleteVmportEnv(input2, 1, true);
    WAIT_FOR(1000, 1000, (VmPortFind(9) == false));
    intf.reset();
    client->WaitForIdle();
    usleep(2000);
    client->WaitForIdle();
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key1, true)
              == NULL));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(9), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key2, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, entry_reuse) {
    uint32_t    intf_idx;

    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    intf_idx = VmPortGetId(8);
    client->Reset();

    InterfaceRef intf(VmPortGet(8));
    DeleteVmportEnv(input1, 1, false);
    WAIT_FOR(1000, 1000, (VmPortFind(8) == false));
    client->Reset();

    CreateVmportEnv(input1, 1);
    WAIT_FOR(1000, 1000, (VmPortFind(8) == true));
    EXPECT_EQ(VmPortGetId(8), intf_idx);
    client->Reset();
    intf.reset();
    usleep(2000);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, ActivateInactivate_vm) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Delete VM, and delay deletion of nova
    //message (BGP connection drop case)
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet8");
    DelVm("vm1");
    client->WaitForIdle();
    // Interface is active even if VM is deleted
    EXPECT_TRUE(VmPortActive(input1, 0));
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, ActivateInactivate_vn) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Delete VM, and delay deletion of nova
    //message (BGP connection drop case)
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet8");
    client->WaitForIdle();
    // Interface is active even if VM is deleted
    EXPECT_FALSE(VmPortActive(input1, 0));
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, CfgSync_NoNova_1) {
    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    NovaDel(1);
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_FALSE(VmPortFind(1));
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Size() == 3U));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

TEST_F(IntfTest, AddDelNova_NoCfg_1) {
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    NovaDel(1);
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_FALSE(VmPortFind(1));
}

// VmPort create, VM create, VN create, VRF create
TEST_F(IntfTest, AddDelVmPortDepOnVmVn_1) {
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    VmAddReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    VrfAddReq("vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));

    client->Reset();
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(1));

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    VmDelReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortInactive(1));
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    VnDelReq(1);
    VrfDelReq("vrf1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    client->Reset();
    VrfDelReq("vrf1");
    client->WaitForIdle();
}

// VM create, VMPort create, VN create, VRF create
TEST_F(IntfTest, AddDelVmPortDepOnVmVn_2_Mirror) {
    client->Reset();
    VmAddReq(1);
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    VrfAddReq("vrf2");
    VnAddReq(1, "vn1", 0, "vrf2");

    AnalyzerInfo analyzer_info;
    analyzer_info.analyzer_name = "Analyzer1";
    analyzer_info.vrf_name = std::string();
    analyzer_info.analyzer_ip = "1.1.1.2";
    analyzer_info.dport = 8099;
    analyzer_info.direction = "both";
    VmInterface::FloatingIpList list;
    CfgIntfSync(1, "vnet1", 1, 1, list, "vrf2", "1.1.1.1", analyzer_info);
    client->WaitForIdle();
    MirrorEntryKey mirror_key(analyzer_info.analyzer_name);
    EXPECT_TRUE(Agent::GetInstance()->mirror_table()->FindActiveEntry(&mirror_key) != NULL);
    EXPECT_EQ(VmPortGetAnalyzerName(1), "Analyzer1");
    EXPECT_EQ(VmPortGetMirrorDirection(1), Interface::MIRROR_RX_TX);

    analyzer_info.analyzer_name = "Analyzer1";
    analyzer_info.vrf_name = std::string();
    analyzer_info.analyzer_ip = "1.1.1.2";
    analyzer_info.dport = 8099;
    analyzer_info.direction = "egress";
    CfgIntfSync(1, "vnet1", 1, 1, list, "vrf2", "1.1.1.1", analyzer_info);
    client->WaitForIdle();
    EXPECT_EQ(VmPortGetMirrorDirection(1), Interface::MIRROR_TX);

    client->Reset();
    VmDelReq(1);
    CfgIntfSync(1, "vnet1", 1, 1, "vrf2", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortInactive(1));
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    VrfDelReq("vrf2");
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->mirror_table()->FindActiveEntry(&mirror_key) == NULL);

    ConfigDel(1);
    client->WaitForIdle();
}

// VM create, VMPort create, VN create, VRF create
TEST_F(IntfTest, AddDelVmPortDepOnVmVn_2) {
    client->Reset();
    VmAddReq(1);
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    VrfAddReq("vrf2");
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf2", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VmDelReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf2", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortInactive(1));
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    VrfDelReq("vrf2");
    client->WaitForIdle();

    ConfigDel(1);
    client->WaitForIdle();
}

// VM create, VN create, VRF create, 3 VmPorts
TEST_F(IntfTest, MultipleVmPorts_1) {
    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    VrfAddReq("vrf4");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf4");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    NovaIntfAdd(2, "vnet2", "1.1.1.2", "00:00:00:00:00:01");
    NovaIntfAdd(3, "vnet3", "1.1.1.3", "00:00:00:00:00:01");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(3));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(VmPortInactive(2));
    EXPECT_TRUE(VmPortInactive(3));
    EXPECT_EQ(6U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf4", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf4", "1.1.1.2");
    CfgIntfSync(3, "cfg-vnet3", 1, 1, "vrf4", "1.1.1.3");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(3));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_TRUE(VmPortActive(3));
    EXPECT_EQ(6U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    NovaDel(1);
    NovaDel(2);
    NovaDel(3);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(3));

    client->Reset();
    VmDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    VrfDelReq("vrf4");
    client->WaitForIdle();

    ConfigDel(1);
    ConfigDel(2);
    ConfigDel(3);
    client->WaitForIdle();
}

// VN has ACL set before VM Port is created
TEST_F(IntfTest, VmPortPolicy_1) {

    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    AclAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));

    client->Reset();
    VrfAddReq("vrf5");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf5");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    NovaIntfAdd(2, "vnet2", "1.1.1.2", "00:00:00:00:00:02");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf5", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf5", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(1));

    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortPolicyEnable(2));
    EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());

    client->Reset();
    AclDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    // ACL not deleted due to reference from VN
    EXPECT_EQ(1U, Agent::GetInstance()->acl_table()->Size());
    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf5");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());
    // Ports not yet notified. So, they still have policy enabled
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortPolicyEnable(2));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf5", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf5", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortPolicyEnable(2));

    client->Reset();
    NovaDel(1);
    NovaDel(2);
    VmDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmPortFind(1));
    EXPECT_FALSE(VmPortFind(2));

    client->Reset();
    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    VrfDelReq("vrf5");
    client->WaitForIdle();

    ConfigDel(1);
    ConfigDel(2);
    client->WaitForIdle();
}

// ACL set in VN after VM Port is created
TEST_F(IntfTest, VmPortPolicy_2) {
    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    client->Reset();
    VrfAddReq("vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    NovaIntfAdd(2, "vnet2", "1.1.1.2", "00:00:00:00:00:02");

    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf6", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf6", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_FALSE(VmPortPolicyDisable(1));
    EXPECT_FALSE(VmPortPolicyDisable(2));

    client->Reset();
    AclAddReq(1);
    VnAddReq(1, "vn1", 1, "vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf6", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf6", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortPolicyEnable(2));

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf6");
    AclDelReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf6", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf6", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(client->PortNotifyWait(2));
    // ACL deleted since no reference from VN
    WAIT_FOR(100, 1000, (Agent::GetInstance()->acl_table()->Size() == 0U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->vn_table()->Size() == 1U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->acl_table()->Size() == 0U));
    // Ports already notified. So, they still have policy disabled
    EXPECT_FALSE(VmPortPolicyDisable(1));
    EXPECT_FALSE(VmPortPolicyDisable(2));

    client->Reset();
    NovaDel(1);
    NovaDel(2);
    VmDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmPortFind(1));
    EXPECT_FALSE(VmPortFind(2));

    ConfigDel(1);
    ConfigDel(2);
    client->WaitForIdle();

    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Size() == 3U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->vm_table()->Size() == 0U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->vn_table()->Size() == 1U));

    client->Reset();
    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    client->Reset();
    VrfDelReq("vrf6");
    client->WaitForIdle();

}

// Floating IP add
TEST_F(IntfTest, VmPortFloatingIp_1) {
    client->Reset();
    VrfAddReq("vrf1");
    VrfAddReq("vrf2");
    VmAddReq(1);
    VnAddReq(1, "vn1", 1, "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(4U, Agent::GetInstance()->vrf_table()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2", "1.1.1.1");
    // Floating IP on invalid VRF
    //FloatingIpAdd(list, "2.2.2.2", "vrf-x");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");
    client->WaitForIdle();

    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    // Ensure 2 FIP added to intf, policy enabled on interface and
    // FIP route exported to FIP VRF
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    InetUnicastRouteEntry *rt =
        RouteGet("vrf1", Ip4Address::from_string("2.2.2.2"), 32);
    if (rt) {
        VnListType vn_list;
        vn_list.insert("vn2");
        EXPECT_TRUE(rt->GetActivePath()->dest_vn_list() == vn_list);
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.1"));
    }

    DoInterfaceSandesh("");
    client->WaitForIdle();

    // Remove all floating IP and check floating-ip count is 0
    client->Reset();
    list.list_.clear();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 0));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_FALSE(VmPortPolicyDisable(1));
    EXPECT_FALSE(RouteFind("vrf2", "2.2.2.2", 32));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    client->Reset();
    VnDelReq(1);
    VmDelReq(1);
    VrfDelReq("vrf1");
    VrfDelReq("vrf2");
    client->WaitForIdle();
    ConfigDel(1);
    client->WaitForIdle();
}

// Floating IP add
TEST_F(IntfTest, VmPortFloatingIpPolicy_1) {
    client->Reset();
    VrfAddReq("vrf1");
    VrfAddReq("vrf2");
    VmAddReq(1);
    VnAddReq(1, "vn1", 1, "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(4U, Agent::GetInstance()->vrf_table()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");

    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    DoInterfaceSandesh("");
    client->WaitForIdle();

    // Add ACL. Policy should be enabled
    client->Reset();
    AclAddReq(1);
    VnAddReq(1, "vn1", 1, "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortActive(1));

    // Remove all floating IP and check floating-ip count is 0
    client->Reset();
    list.list_.clear();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    // Policy enabled due to ACL
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 0));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_FALSE(RouteFind("vrf2", "2.2.2.2", 32));

    // Del ACL. Policy should be disabled
    client->Reset();
    VnAddReq(1, "vn1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortPolicyDisable(1));

    // ACL first followed by interface
    VnAddReq(1, "vn1", 1, "vrf1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortPolicyEnable(1));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    client->Reset();
    VnDelReq(1);
    VmDelReq(1);
    VrfDelReq("vrf1");
    VrfDelReq("vrf2");
    client->WaitForIdle();
    ConfigDel(1);
    client->WaitForIdle();
}

TEST_F(IntfTest, VmPortFloatingIpResync_1) {
    client->Reset();
    VrfAddReq("vrf1");
    VrfAddReq("vrf2");
    VrfAddReq("vrf3");
    VrfAddReq("vrf4");
    VmAddReq(1);
    VnAddReq(1, "vn1", 1, "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(6U, Agent::GetInstance()->vrf_table()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");

    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));

    // Add 2 more floating-ip
    VmInterface::FloatingIpList list1;
    FloatingIpAdd(list1, "2.2.2.2", "vrf2", "1.1.1.1");
    FloatingIpAdd(list1, "3.3.3.3", "vrf3", "1.1.1.1");
    FloatingIpAdd(list1, "4.4.4.4", "vrf4", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 3));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_TRUE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list2;
    FloatingIpAdd(list2, "3.3.3.3", "vrf3", "1.1.1.1");
    FloatingIpAdd(list2, "4.4.4.4", "vrf4", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list2, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 2));
    EXPECT_FALSE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_TRUE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list3;
    FloatingIpAdd(list3, "2.2.2.2", "vrf2", "1.1.1.1");
    FloatingIpAdd(list3, "3.3.3.3", "vrf3", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list3, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 2));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_FALSE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list4;
    FloatingIpAdd(list4, "2.2.2.2", "vrf2", "1.1.1.1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list4, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_FALSE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_FALSE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list5;
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list5, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 0));
    EXPECT_FALSE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_FALSE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_FALSE(RouteFind("vrf4", "4.4.4.4", 32));

    // Shutdown
    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    client->Reset();
    VnDelReq(1);
    VmDelReq(1);
    VrfDelReq("vrf1");
    VrfDelReq("vrf2");
    VrfDelReq("vrf3");
    VrfDelReq("vrf4");
    client->WaitForIdle();
    ConfigDel(1);
    client->WaitForIdle();
}

TEST_F(IntfTest, VmPortFloatingIpEvpn_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Change Encap
    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    Ip4Address fip = Ip4Address::from_string("2.1.1.100");
    MacAddress smac(0, 0, 0, 0x1, 0x1, 0x1);
    //Check if EVPN route is populated with vxlan ID
    WAIT_FOR(1000, 1000, (EvpnRouteGet("default-project:vn2:vn2", smac, fip, 0) == NULL));
    WAIT_FOR(1000, 1000, (EvpnRouteGet("default-project:vn2:vn2", smac, fip, 1) != NULL));

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and floating ip map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    // Interface not deleted till config is deleted
    EXPECT_TRUE(VmPortFind(1));

    //Clean up
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    DeleteVmportEnv(input, 1, true);
    DelEncapList();
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("default-project:vn2:vn2", true));
}

TEST_F(IntfTest, VmPortFloatingIpDelete_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and floating ip map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    // Interface not deleted till config is deleted
    EXPECT_TRUE(VmPortFind(1));

    //Clean up
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("default-project:vn2:vn2", true));
}

//Test to ensure reference to floating IP VRF is release upon
//interface deactivation
TEST_F(IntfTest, VmPortFloatingIpDelete_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Delete link from vm to vm interface, forcing interface
    //to deactivate verify that route for floating ip is deleted
    //and the reference to vrf is released
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("default-projec:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Delete floating IP and make sure reference to VRF is released
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    DelVn("default-project:vn2");
    client->WaitForIdle();
    WAIT_FOR(100, 1000, VrfFind("default-project:vn2:vn2", true) == false);

    //Readd the config and activate the interface
    //and make sure floating IP routes are added
    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Clean up the config
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("default-project:vn2:vn2", true));
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Add and delete of floating-ip dependent on secondary IP
TEST_F(IntfTest, FloatingIpFixedIpAddChange) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddInstanceIp("instance2", input1[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    AddInstanceIp("instance3", input1[0].vm_id, "1.1.1.11");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance3");
    client->WaitForIdle();

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    client->WaitForIdle();

    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(8, 1));
    InetUnicastRouteEntry *rt =
        RouteGet("default-project:vn2:vn2", floating_ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
            Ip4Address::from_string("1.1.1.10"));
    client->WaitForIdle();

    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.11");
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
            Ip4Address::from_string("1.1.1.11"));
    client->WaitForIdle();

    //Clean up
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance3");
    DelInstanceIp("instance2");
    DelInstanceIp("instance3");
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Delete the config node for interface, and verify interface NH are deleted
//Add the config node and verify the interface NH are readded
TEST_F(IntfTest, IntfActivateDeactivate_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    //Make sure unicast route and nexthop are deleted, since
    //config is deleted
    EXPECT_FALSE(RouteFind("vrf1", "1.1.1.10", 32));

    boost::uuids::uuid intf_uuid = MakeUuid(1);
    MacAddress mac = MacAddress::FromString("00:00:00:01:01:01");
    VmInterfaceKey *intf_key1 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key2 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key4 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key5 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");

    InterfaceNHKey unicast_nh_key(intf_key1, false, InterfaceNHFlags::INET4, mac);
    EXPECT_FALSE(FindNH(&unicast_nh_key));

    InterfaceNHKey unicast_policy_nh_key(intf_key2, true, InterfaceNHFlags::INET4,
                                         mac);
    EXPECT_FALSE(FindNH(&unicast_policy_nh_key));

    InterfaceNHKey bridge_nh_key(intf_key4, false, InterfaceNHFlags::BRIDGE, mac);
    EXPECT_FALSE(FindNH(&bridge_nh_key));

    InterfaceNHKey bridge_policy_nh_key(intf_key5, true,
                                        InterfaceNHFlags::BRIDGE, mac);
    EXPECT_FALSE(FindNH(&bridge_policy_nh_key));

    AddPort(input[0].name, 1);

    client->WaitForIdle();
    EXPECT_TRUE(FindNH(&unicast_nh_key));
    EXPECT_TRUE(FindNH(&unicast_policy_nh_key));
    EXPECT_TRUE(FindNH(&bridge_nh_key));
    EXPECT_TRUE(FindNH(&bridge_policy_nh_key));

    //Clean up
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(FindNH(&unicast_nh_key));
    EXPECT_FALSE(FindNH(&unicast_policy_nh_key));
    EXPECT_FALSE(FindNH(&bridge_nh_key));
    EXPECT_FALSE(FindNH(&bridge_policy_nh_key));
    EXPECT_FALSE(VrfFind("vrf1", true));
}

//1> Deactivate the interface by deleting the link to vrf,
//   verify nexthop get deleted.
//2> Reactivate the interface by adding the link to vrf,
//   verify nexthop get added
TEST_F(IntfTest, IntfActivateDeactivate_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    boost::uuids::uuid intf_uuid = MakeUuid(1);
    MacAddress mac = MacAddress::FromString("00:00:00:01:01:01");
    VmInterfaceKey *intf_key1 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key2 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key4 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key5 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");

    InterfaceNHKey unicast_nh_key(intf_key1, false, InterfaceNHFlags::INET4, mac);
    InterfaceNHKey unicast_policy_nh_key(intf_key2, true, InterfaceNHFlags::INET4,
                                         mac);
    InterfaceNHKey bridge_nh_key(intf_key4, false, InterfaceNHFlags::BRIDGE,
                                 mac);
    InterfaceNHKey bridge_policy_nh_key(intf_key5, true, InterfaceNHFlags::BRIDGE,
                                        mac);
    client->WaitForIdle();
    EXPECT_TRUE(FindNH(&unicast_nh_key));
    EXPECT_TRUE(FindNH(&unicast_policy_nh_key));
    EXPECT_TRUE(FindNH(&bridge_nh_key));
    EXPECT_TRUE(FindNH(&bridge_policy_nh_key));

    //Deactivate the interface
    DelLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (FindNH(&unicast_nh_key) == false));
    EXPECT_FALSE(FindNH(&unicast_nh_key));
    EXPECT_FALSE(FindNH(&unicast_policy_nh_key));
    EXPECT_FALSE(FindNH(&bridge_nh_key));
    EXPECT_FALSE(FindNH(&bridge_policy_nh_key));

    //Activate the interface
    AddLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(FindNH(&unicast_nh_key));
    EXPECT_TRUE(FindNH(&unicast_policy_nh_key));
    EXPECT_TRUE(FindNH(&bridge_nh_key));
    EXPECT_TRUE(FindNH(&bridge_policy_nh_key));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(FindNH(&unicast_nh_key));
    EXPECT_FALSE(FindNH(&unicast_policy_nh_key));
    EXPECT_FALSE(FindNH(&bridge_nh_key));
    EXPECT_FALSE(FindNH(&bridge_policy_nh_key));
    EXPECT_FALSE(VrfFind("vrf1", true));
}

//1> Create interface without IP, make bridge nexthop are present and
//   layer3 nexthop are absent
//2> Add instance IP and make sure layer3 nexthop are added
//3> Delete instance ip and layer3 nexthop are deleted
TEST_F(IntfTest, IntfActivateDeactivate_5) {
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnvWithoutIp(input, 1);
    client->WaitForIdle();
    // VMI created without IP address
    VmInterface *vmi = dynamic_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi != NULL);
    EXPECT_TRUE(vmi->primary_ip_addr() == Ip4Address());

    struct PortInfo input1[] = {
        {"vnet1", 1, "0.0.0.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnvWithoutIp(input1, 1);
    client->WaitForIdle();
    // VMI created now
    EXPECT_TRUE(VmPortGet(1) != NULL);

    boost::uuids::uuid intf_uuid = MakeUuid(1);
    MacAddress mac = MacAddress::FromString("00:00:00:01:01:01");
    VmInterfaceKey *intf_key1 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key2 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key4 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    VmInterfaceKey *intf_key5 = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");

    InterfaceNHKey unicast_nh_key(intf_key1, false, InterfaceNHFlags::INET4, mac);
    InterfaceNHKey unicast_policy_nh_key(intf_key2, true,
                                         InterfaceNHFlags::INET4, mac);
    InterfaceNHKey bridge_nh_key(intf_key4, false, InterfaceNHFlags::BRIDGE, mac);
    InterfaceNHKey bridge_policy_nh_key(intf_key5, true,
                                        InterfaceNHFlags::BRIDGE, mac);
    client->WaitForIdle();
    EXPECT_TRUE(FindNH(&unicast_nh_key));
    EXPECT_TRUE(FindNH(&unicast_policy_nh_key));
    EXPECT_TRUE(FindNH(&bridge_nh_key));
    EXPECT_TRUE(FindNH(&bridge_policy_nh_key));

    //Add instance ip
    AddInstanceIp("instance1", input[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance1");
    client->Reset();
    client->WaitForIdle();
    EXPECT_TRUE(FindNH(&unicast_nh_key));
    EXPECT_TRUE(FindNH(&unicast_policy_nh_key));
    EXPECT_TRUE(FindNH(&bridge_nh_key));
    EXPECT_TRUE(FindNH(&bridge_policy_nh_key));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(FindNH(&unicast_nh_key));
    EXPECT_FALSE(FindNH(&unicast_policy_nh_key));
    EXPECT_FALSE(FindNH(&bridge_nh_key));
    EXPECT_FALSE(FindNH(&bridge_policy_nh_key));
    EXPECT_FALSE(VrfFind("vrf1", true));
}

TEST_F(IntfTest, VmPortServiceVlanDelete_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    if (rt) {
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.10"));
    }
    DoInterfaceSandesh("");
    client->WaitForIdle();

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and service vlan map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));
    //EXPECT_TRUE(VmPortServiceVlanCount(1, 0));
    DoInterfaceSandesh("");
    client->WaitForIdle();

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Test to ensure refernce to service vrf is released upon interface
//deactivation
TEST_F(IntfTest, VmPortServiceVlanDelete_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    if (rt) {
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.10"));
    }

    DoInterfaceSandesh("");
    client->WaitForIdle();

    //Delete link from vm to vm interface, forcing interface
    //to deactivate verify that route and
    //service vlan map gets cleaned up
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    DoInterfaceSandesh("");
    client->WaitForIdle();

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2", true));

    //Readd the configuration
    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));

    rt = RouteGet("vrf2", service_ip, 32);
    if (rt) {
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.10"));
    }

    DoInterfaceSandesh("");
    client->WaitForIdle();

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2", true));
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

TEST_F(IntfTest, VmPortServiceVlanAdd_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    //DeActivate the interface
    DelLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));

    //Activate the interface
    AddLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    if (rt) {
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.10"));
    }

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and service vlan map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Add a interface with service Vlan
//Delete interface with nova msg
//Make sure all service vlan routes are deleted and interface is free
TEST_F(IntfTest, VmPortServiceVlanAdd_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    if (rt) {
        EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                    Ip4Address::from_string("1.1.1.10"));
    }

    //Delete the interface, all service vlan routes should be deleted
    //and interface should be released
    NovaDel(1);
    ConfigDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1) == false);

    //Cleanup
    DelNode("virtual-machine-interface", input[0].name);
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFindRetDel(1) == false);
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Deactivate the interface, and make sure
//vlan configuration is deleted
//Reactivate and verify vlan NH and vrf entry are created again
TEST_F(IntfTest, VmPortServiceVlanAdd_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));

    //Deactivate the VM and make sure mpls label allocated for VM
    //gets deleted
    DelLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
    service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));

    //Verify vrf assign rule and NH are deleted
    VlanNHKey vlan_nh_key(MakeUuid(1), 10);
    EXPECT_FALSE(FindNH(&vlan_nh_key));
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 10) == NULL);

    //Activate VM
    AddLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(FindNH(&vlan_nh_key));
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 10) != NULL);

    //Clean up
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    DelVmPortVrf("vmvrf1");
    DelVrf("vrf2");
    DelVn("vn2");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

/* Test to verify behavior of ServiceVlan activation when service-vrf is
 * delete marked.*/
TEST_F(IntfTest, VmPortServiceVlanVrfDelete_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    client->WaitForIdle();

    //Mark service VRF as deleted
    VrfEntry *vrf = VrfGet("vrf2", false);
    EXPECT_TRUE(vrf != NULL);
    vrf->MarkDelete();

    //Trigger change on VMI (we are doing this by associating secondary IP)
    AddInstanceIp("instance2", input[0].vm_id, "3.1.1.10");
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    //Clear the delete flag from service VRF so that it can be deleted
    vrf->ClearDelete();

    //Verify that service vlan count is still 1 and route is still present in
    //service VRF
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));

    //Clean-up
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance2");
    DelInstanceIp("instance2");
    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and service vlan map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));
    client->WaitForIdle();

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVrf("vrf2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(IntfTest, VmPortServiceVlanServiceIp_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                Ip4Address::from_string("1.1.1.10"));

    AddServiceInstanceIp("serviceip1", 100, "1.1.1.100", false, NULL);
    AddLink("virtual-machine-interface", "vnet1", "instance-ip", "serviceip1");
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                Ip4Address::from_string("1.1.1.100"));
    EXPECT_TRUE(rt->GetActivePath()->path_preference().ecmp() == false);

    AddServiceInstanceIp("serviceip1", 100, "1.1.1.100", true, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActivePath()->path_preference().ecmp() == true);

    DelLink("virtual-machine-interface", "vnet1", "instance-ip", "serviceip1");
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActivePath()->path_preference().dependent_ip() ==
                            Ip4Address::from_string("1.1.1.10"));
    EXPECT_TRUE(rt->GetActivePath()->path_preference().ecmp() == false);

    DelNode("instance-ip", "serviceip1");
    //Clean up
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelLink("virtual-network", "vn1", "virtual-machine-interface",
            input[0].name);
    DelVmPortVrf("vmvrf1");
    DelVrf("vrf2");
    DelVn("vn2");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(IntfTest, VmPortServiceVlanChange_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddVrf("vrf3", 3);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt);

    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf3");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt == NULL);

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf3");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVn("vn2");
    DelVrf("vrf3");
    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Change IP address of Service vlan configuration
//and verify old route gets deleted and new route is added
TEST_F(IntfTest, VmPortServiceVlanChange_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    Ip4Address service_ip2 = Ip4Address::from_string("2.2.2.101");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt);

    MacAddress smac = MacAddress::FromString("02:00:00:00:00:02");
    MacAddress dmac = MacAddress::FromString("02:00:00:00:00:01");

    AddVmPortVrf("vmvrf1", "2.2.2.101", 10);
    client->WaitForIdle();

    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt == NULL);
    rt = RouteGet("vrf2", service_ip2, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(L2RouteFind("vrf2", smac));
    EXPECT_TRUE(L2RouteFind("vrf2", dmac));

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVn("vn2");
    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Change tag of Service vlan configuration
//and verify old route gets deleted and new route is added
TEST_F(IntfTest, VmPortServiceVlanChange_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt);

    AddVmPortVrf("vmvrf1", "2.2.2.100", 9);
    client->WaitForIdle();

    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt != NULL);

    MacAddress smac = MacAddress::FromString("02:00:00:00:00:02");
    MacAddress dmac = MacAddress::FromString("02:00:00:00:00:01");
    EXPECT_TRUE(L2RouteFind("vrf2", smac));
    EXPECT_TRUE(L2RouteFind("vrf2", dmac));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVn("vn2");
    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Change mac of Service vlan configuration
//and verify old route gets deleted and new route is added
TEST_F(IntfTest, VmPortServiceVlanChange_4) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    EXPECT_TRUE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt);

    MacAddress smac = MacAddress::FromString("02:00:00:00:00:02");
    MacAddress dmac = MacAddress::FromString("02:00:00:00:00:01");
    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(vlan_nh->GetSMac() == smac);
    EXPECT_TRUE(vlan_nh->GetDMac() == dmac);

    AddVmPortVrf("vmvrf1", "2.2.2.100", 10, "", true);
    client->WaitForIdle();
    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt != NULL);
    vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(vlan_nh->GetSMac() == dmac);
    EXPECT_TRUE(vlan_nh->GetDMac() == smac);

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVn("vn2");
    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Bring interface down change the mac and service IP
//verify that route are present for updated service IP
TEST_F(IntfTest, VmPortServiceVlanChange_5) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    //Add service vlan for vnet1
    client->WaitForIdle();
    AddVmPortVrf("vmvrf1", "2.2.2.100", 10);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    Ip4Address service_ip = Ip4Address::from_string("2.2.2.100");
    Ip4Address service_ip2 = Ip4Address::from_string("2.2.2.101");
    EXPECT_TRUE(VmPortServiceVlanCount(1, 1));
    InetUnicastRouteEntry *rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt);

    MacAddress smac = MacAddress::FromString("02:00:00:00:00:02");
    MacAddress dmac = MacAddress::FromString("02:00:00:00:00:01");
    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(vlan_nh->GetSMac() == smac);
    EXPECT_TRUE(vlan_nh->GetDMac() == dmac);

    //Deactivate the interface
    DelLink("virtual-machine-interface", "vnet1", "virtual-network", "vn1");
    //Change the config
    AddVmPortVrf("vmvrf1", "2.2.2.101", 10, "", true);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "virtual-network", "vn1");
    client->WaitForIdle();

    rt = RouteGet("vrf2", service_ip, 32);
    EXPECT_TRUE(rt == NULL);
    rt = RouteGet("vrf2", service_ip2, 32);
    EXPECT_TRUE(rt != NULL);
    vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(vlan_nh->GetSMac() == dmac);
    EXPECT_TRUE(vlan_nh->GetDMac() == smac);

    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("vmvrf1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelVn("vn2");
    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Add and delete static route
TEST_F(IntfTest, IntfStaticRoute) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };
    InetUnicastRouteEntry *rt;

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));

   rt = RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);

   EXPECT_TRUE(rt->intf_route_type().compare(VmInterface::kInterfaceStatic) == 0);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Add and delete static route with community
TEST_F(IntfTest, IntfStaticRouteWithCommunity) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   std::vector<std::string> communities = list_of("no-advertise")("64512:9999");
   AddInterfaceRouteTable("static_route", 1, static_route, 2, NULL, communities);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   InetUnicastRouteEntry *rt =
        RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   const AgentPath *path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 2);
   EXPECT_EQ(path->communities(), communities);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Add and delete static route with community with nexthop
TEST_F(IntfTest, IntfStaticRouteWithCommunityWithNexthop) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   std::vector<std::string> communities = list_of("no-advertise")("64512:9999");
   AddInterfaceRouteTable("static_route", 1, static_route, 2, "1.1.1.2", communities);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));

   InetUnicastRouteEntry *rt =
        RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   const AgentPath *path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 2);
   EXPECT_EQ(path->communities(), communities);
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

// Update the communities on static route config
// No community to valid list of communities
TEST_F(IntfTest, UpdateIntfStaticRouteCommunity_0) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   InetUnicastRouteEntry *rt =
        RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   const AgentPath *path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 0);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();


   // Update the communities
   std::vector<std::string> update_communities =
       list_of("no-reoriginate")("64512:8888");
   AddInterfaceRouteTable("static_route", 1, static_route, 2,
                          NULL, update_communities);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();

   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   rt = RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 2);
   EXPECT_EQ(path->communities(), update_communities);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

// Update the communities on static route config
TEST_F(IntfTest, UpdateIntfStaticRouteCommunity_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   std::vector<std::string> communities = list_of("no-advertise")("64512:9999");
   AddInterfaceRouteTable("static_route", 1, static_route, 2, NULL, communities);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   InetUnicastRouteEntry *rt =
        RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   const AgentPath *path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 2);
   EXPECT_EQ(path->communities(), communities);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   // Update the communities
   std::vector<std::string> update_communities =
       list_of("no-reoriginate")("64512:8888");
   AddInterfaceRouteTable("static_route", 1, static_route, 2,
                          NULL, update_communities);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();

   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   rt = RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt != NULL);
   path = rt->GetActivePath();
   EXPECT_TRUE(path != NULL);
   EXPECT_EQ(path->communities().size(), 2);
   EXPECT_EQ(path->communities(), update_communities);

   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Add static route, deactivate interface and make static routes are deleted
TEST_F(IntfTest, IntfStaticRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "virtual-network", "vn1");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));

   //Activate interface and make sure route are added again
   AddLink("virtual-machine-interface", "vnet1",
           "virtual-network", "vn1");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Add static route, change static route entries and verify its reflected
TEST_F(IntfTest, IntfStaticRoute_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
       { Ip4Address::from_string("8.1.1.0"),  8}
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Verify all 3 routes are present
   AddInterfaceRouteTable("static_route", 1, static_route, 3);
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[2].addr_,
                         static_route[2].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Add static route, add acl on interface, check static routes also point to
//policy enabled interface NH
TEST_F(IntfTest, IntfStaticRoute_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   const NextHop *nh;
   nh = RouteGet("vrf1", static_route[0].addr_,
                 static_route[0].plen_)->GetActiveNextHop();
   EXPECT_TRUE(nh->PolicyEnabled());

   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   nh = RouteGet("vrf1", static_route[1].addr_,
           static_route[1].plen_)->GetActiveNextHop();
   EXPECT_TRUE(nh->PolicyEnabled());

   //Add a acl to interface and verify NH policy changes
   AddAcl("Acl", 1, "vn1", "vn1", "pass");
   AddLink("virtual-network", "vn1", "access-control-list", "Acl");
   client->WaitForIdle();
   WAIT_FOR(100, 1000, (GetNhPolicy("vrf1", static_route[0].addr_,
                                    static_route[0].plen_) == true));

   WAIT_FOR(100, 1000, (GetNhPolicy("vrf1", static_route[1].addr_,
                                    static_route[1].plen_) == true));
   DelLink("virtual-network", "vn1", "access-control-list", "Acl");
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

//Activate interface along with static routes
TEST_F(IntfTest, IntfStaticRoute_4) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Interface gets delete
   NovaDel(1);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   //Add static routes and activate interface
   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();
   EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                          static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   //Send nova interface add message
   NovaIntfAdd(1, "vnet1", "1.1.1.10", "00:00:00:01:01:01");
   //Sync the config
   AddPort("vnet1", 1);
   client->WaitForIdle();
   EXPECT_TRUE(VmPortActive(input, 0));
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   DoInterfaceSandesh("vnet1");
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

TEST_F(IntfTest, IntfStaticRoute_5) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
   };

   //Add static routes and activate interface
   AddInterfaceRouteTable("static_route", 1, static_route, 1);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();

   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                          static_route[0].plen_));
   InetUnicastRouteEntry *rt =
       RouteGet("vrf1", static_route[0].addr_, static_route[0].plen_);
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
   client->WaitForIdle();

   AddInterfaceRouteTable("static_route", 1, static_route, 1, "1.1.1.18");
   client->WaitForIdle();
   //Since 1.1.1.254 is not present, nexthop should be discard
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::DISCARD);

   EXPECT_TRUE(VmPortActive(input, 0));
   EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                         static_route[0].plen_));
   client->WaitForIdle();

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();
   EXPECT_FALSE(VmPortFind(1));
}

TEST_F(IntfTest, vm_interface_change_req) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    struct PortInfo input1_mac_changed[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:11", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(vm_interface->vm_mac().ToString() == "00:00:00:01:01:01");
    client->Reset();

    CreateVmportEnv(input1_mac_changed, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();
    //No change expected as vm interface change results in no modifications
    EXPECT_TRUE(vm_interface->vm_mac().ToString() == "00:00:00:01:01:01");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (agent->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, vm_interface_key_verification) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "new_vnet8");
    vm_interface->SetKey(&key);
    EXPECT_TRUE(vm_interface->name() == "new_vnet8");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (agent->interface_table()->Find(&key2, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, packet_interface_get_key_verification) {
    PacketInterfaceKey key(boost::uuids::nil_uuid(), "pkt0");
    Interface *intf =
        static_cast<Interface *>(agent->interface_table()->FindActiveEntry(&key));
    if (intf) {
    DBEntryBase::KeyPtr entry_key = intf->GetDBRequestKey();
    EXPECT_TRUE(entry_key.get() != NULL);
    client->Reset();
    }

    //Issue sandesh request
    DoInterfaceSandesh("pkt0");
    client->WaitForIdle();
}

TEST_F(IntfTest, packet_interface_creation_deletion_test) {
    PacketInterface::CreateReq(agent->interface_table(),
                                "pkt5",
                                Interface::TRANSPORT_ETHERNET);
    client->WaitForIdle();
    PacketInterfaceKey key1(boost::uuids::nil_uuid(), "pkt5");
    Interface *intf1 =
        static_cast<Interface *>(agent->interface_table()->FindActiveEntry(&key1));
    EXPECT_TRUE(intf1 != NULL);
    client->Reset();

    PacketInterface::DeleteReq(agent->interface_table(),
                                "pkt5");
    client->WaitForIdle();
    PacketInterfaceKey key3(boost::uuids::nil_uuid(), "pkt5");
    intf1 =
        static_cast<Interface *>(agent->interface_table()->FindActiveEntry(&key3));
    EXPECT_TRUE(intf1 == NULL);
    client->Reset();
}

TEST_F(IntfTest, sandesh_vm_interface_without_ip) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnvWithoutIp(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(8));
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    //Issue sandesh request
    DoInterfaceSandesh("vnet8");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, AdminState_1) {
    struct PortInfo input[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:09", 1, 1}
    };

    //Add VN
    VnAdd(input[0].vn_id);

    // Nova Port add message
    NovaPortAdd(input);

    // Config Port add
    ConfigPortAdd(input, true);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Add VM
    VmAdd(input[0].vm_id);

    //Add necessary objects and links to make vm-intf active
    VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    AddVmPortVrf(input[0].name, "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", input[0].name,
            "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    // Disable the admin status
    ConfigPortAdd(input, false);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", input[0].name,
            "virtual-machine-interface", input[0].name);
    client->WaitForIdle();

    //Verify that the port is inactiveV
    EXPECT_TRUE(VmPortInactive(input, 0));

    //other cleanup
    VnDelete(input[0].vn_id);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", input[0].name);
    DelLink("virtual-network", "vn1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-machine-interface-routing-instance", input[0].name);
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    DelNode("virtual-machine-interface", input[0].name);
    DelInstanceIp("instance0");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
}

TEST_F(IntfTest, AdminState_2) {
    struct PortInfo input[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:09", 1, 1}
    };

    //Add VN
    VnAdd(input[0].vn_id);

    // Nova Port add message
    NovaPortAdd(input);

    // Config Port add
    ConfigPortAdd(input, false);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Add VM
    VmAdd(input[0].vm_id);

    //Add necessary objects and links to make vm-intf active
    VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    AddVmPortVrf(input[0].name, "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", input[0].name,
            "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Enable the admin status
    ConfigPortAdd(input, true);

    //Verify that the port is active
    EXPECT_TRUE(VmPortActive(input, 0));

    // Disable the admin status
    ConfigPortAdd(input, false);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", input[0].name,
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", input[0].name,
            "virtual-machine-interface", input[0].name);
    client->WaitForIdle();

    //Verify that the port is inactiveV
    EXPECT_TRUE(VmPortInactive(input, 0));

    //other cleanup
    VnDelete(input[0].vn_id);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", input[0].name);
    DelLink("virtual-network", "vn1", "virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-machine-interface-routing-instance", input[0].name);
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    DelNode("virtual-machine-interface", input[0].name);
    DelInstanceIp("instance0");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
}

TEST_F(IntfTest, Intf_l2mode_deactivate_activat_via_os_state) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    //Setup L2 environment
    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));
    client->WaitForIdle();

    //Verify L2 interface
    EXPECT_TRUE(VmPortFind(1));
    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_interface->vxlan_id() != 0);
    int32_t vxlan_id = vm_interface->vxlan_id();

    //Deactivate OS state (IF down)
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_interface->GetUuid(),
                                     vm_interface->name()));
    req.data.reset(new VmInterfaceOsOperStateData(false));
    vm_interface->set_test_oper_state(false);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
    client->WaitForIdle();

    EXPECT_FALSE(vm_interface->IsL2Active());
    EXPECT_TRUE(FindVxLanId(agent, vxlan_id));

    //Activate OS state (IF up)
    DBRequest req2(DBRequest::DB_ENTRY_ADD_CHANGE);
    req2.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_interface->GetUuid(),
                                     vm_interface->name()));
    req2.data.reset(new VmInterfaceOsOperStateData(true));
    vm_interface->set_test_oper_state(true);
    Agent::GetInstance()->interface_table()->Enqueue(&req2);
    client->WaitForIdle();

    EXPECT_TRUE(vm_interface->IsL2Active());
    EXPECT_TRUE(FindVxLanId(agent, vxlan_id));
    EXPECT_TRUE(vm_interface->vxlan_id() == vxlan_id);

    //Cleanup
    DeleteVmportEnv(input, 1, true);
    DelEncapList();
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

TEST_F(IntfTest, MetadataRoute_1) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(8));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(),
                                          intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(rt != NULL);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->peer()->GetType() == Peer::LINKLOCAL_PEER);

    AddArp(intf->mdata_ip_addr().to_string().c_str(), "00:00:00:00:00:01",
           agent->fabric_interface_name().c_str());
    path = rt->GetActivePath();
    EXPECT_TRUE(path->peer()->GetType() == Peer::LINKLOCAL_PEER);

    DelArp(intf->mdata_ip_addr().to_string(), "00:00:00:00:00:01",
           agent->fabric_interface_name().c_str());

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, InstanceIpDelete) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Delete the link to instance ip and check that
    //interface still has ecmp mode set
    DelLink("virtual-machine-interface", input1[0].name, "instance-ip",
             "instance1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(vm_intf->ecmp() == true);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, GwIntfAdd) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportWithoutNova(input1, 1);
    client->WaitForIdle();

    AddPhysicalDevice(agent->host_name().c_str(), 1);
    AddPhysicalInterface("pi1", 1, "pi1");
    AddLogicalInterface("lp1", 1, "lp1", 1);
    AddLink("physical-router", agent->host_name().c_str(),
            "physical-interface", "pi1");
    AddLink("logical-interface", "lp1", "physical-interface", "pi1");
    AddLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");

    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    //Verify that route is pointing to resolve NH
    //and the route points to table NH
    Ip4Address addr = Ip4Address::from_string("8.1.1.0");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", addr, 24);
    const VrfEntry *vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveLabel() != MplsTable::kInvalidLabel);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    DelLink("virtual-machine-interface", input1[0].name,
             "subnet", "subnet");
    DelLink("physical-router", agent->host_name().c_str(), "physical-interface", "pi1");
    DelLink("logical-interface", "lp1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");
    DeletePhysicalDevice(agent->host_name().c_str());
    DeletePhysicalInterface("pi1");
    DeleteLogicalInterface("lp1");

    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", "8.1.1.0", 24));

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, GwSubnetChange) {
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
    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name,
            "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    //Verify that route is pointing to resolve NH
    //and the route points to table NH
    Ip4Address addr = Ip4Address::from_string("8.1.1.0");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", addr, 24);
    const VrfEntry *vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    AddSubnetType("subnet", 1, "9.1.1.0", 24);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (RouteFind("vrf1", "8.1.1.0", 24) == false));
    addr = Ip4Address::from_string("9.1.1.0");
    rt = RouteGet("vrf1", addr, 24);
    vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    DelLink("virtual-machine-interface", input1[0].name,
             "subnet", "subnet");
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelLink("logical-interface", "lp1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vnet8", "logical-interface", "lp1");
    DeletePhysicalDevice(agent->host_name().c_str());
    DeletePhysicalInterface("pi1");
    DeleteLogicalInterface("lp1");
    DelLink("virtual-machine-interface", input1[0].name,
             "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", "9.1.1.0", 24));
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, VrfTranslateAddDelete) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    AddInterfaceVrfAssignRule("vnet8", 8, "8.1.1.1", "9.1.1.1", 1,
                              "vrf1", "true");
    client->WaitForIdle();
    VmInterface *intf = dynamic_cast<VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(intf->vrf_assign_acl() != NULL);

    //Get oper state down
    intf->set_test_oper_state(false);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, intf->GetUuid(),
                intf->name()));
    req.data.reset(new VmInterfaceOsOperStateData(false));
    Agent::GetInstance()->interface_table()->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_TRUE(intf->vrf_assign_acl() == NULL);

    intf->set_test_oper_state(true);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, intf->GetUuid(),
                intf->name()));
    req.data.reset(new VmInterfaceOsOperStateData(true));
    Agent::GetInstance()->interface_table()->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_TRUE(intf->vrf_assign_acl() != NULL);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(IntfTest, VMI_Sequence_1) {
    AddVn("vn1", 1);
    AddPort("vmi1", 1, "");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vmi1", "virtual-network", "vn1");
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    VrfEntry *vrf = VrfGet("vrf1");
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vrf->vn() != NULL);

    DelLink("virtual-machine-interface", "vmi1", "virtual-network", "vn1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelVrf("vrf1");
    DelVn("vn1");
    DelPort("vmi1");
    client->WaitForIdle();
}

TEST_F(IntfTest, Layer2Mode_1) {
    client->Reset();
    VmAddReq(1);
    EXPECT_TRUE(client->VmNotifyWait(1));

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    WAIT_FOR(100, 1000, (VmPortInactive(1)));
    WAIT_FOR(100, 1000, (1U == Agent::GetInstance()->vm_table()->Size()));
    WAIT_FOR(100, 1000, (1U == Agent::GetInstance()->vm_table()->Size()));

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    VrfAddReq("vrf1");
    AddL2Vn("vn1", 1);
    AddAcl("Acl", 1, "vn1", "vn1", "pass");
    AddLink("virtual-network", "vn1", "access-control-list", "Acl");
    client->WaitForIdle();
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_intf->policy_enabled() == false);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    const MacAddress mac("00:00:00:00:00:01");
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");
    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);

    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(1));

    DelAcl("Acl");
    DelLink("virtual-network", "vn1", "access-control-list", "Acl");
    DelVn("vn1");
    VrfDelReq("vrf1");
    ConfigDel(1);
    VmDelReq(1);
    client->WaitForIdle();
}

TEST_F(IntfTest, Layer2Mode_2) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    IpamInfo ipam_info[] = {
        {"8.1.1.0", 24, "8.1.1.10", true},
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(vm_intf->policy_enabled() == true);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    const MacAddress mac("00:00:00:01:01:01");
    Ip4Address ip = Ip4Address::from_string("8.1.1.1");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");
    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);

    //Make the VN as layer2 only
    //EVPN route should be added with IP set to 0
    AddL2Vn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->policy_enabled() == false);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);
    EXPECT_TRUE(vm_intf->dhcp_enable_config() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == false);
    uint32_t label = vm_intf->l2_label();
    MplsLabel *mpls_label = GetActiveLabel(label);
    EXPECT_TRUE(mpls_label->nexthop()->PolicyEnabled() == false);
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    WAIT_FOR(100, 1000, (RouteFind("vrf1", "8.1.1.1", 32) == false));

    //Verify L3 route gets added
    AddVn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->policy_enabled() == true);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    label = vm_intf->l2_label();
    mpls_label = GetActiveLabel(label);
    EXPECT_TRUE(mpls_label->nexthop()->PolicyEnabled() == true);
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.1", 32));

    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, Layer2Mode_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    IpamInfo ipam_info[] = {
        {"fd11::", 96, "fd11::1"},
    };

    CreateV6VmportEnv(input, 1, 0, NULL, NULL, false);
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    const MacAddress mac("00:00:00:01:01:01");
    Ip6Address zero_ip;
    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    evpn_rt = EvpnRouteGet("vrf1", mac, addr, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);

    //Make the VN as layer2 only
    //EVPN route should be added with IP set to 0
    AddL2Vn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->policy_enabled() == false);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    evpn_rt = EvpnRouteGet("vrf1", mac, addr, vm_intf->ethernet_tag());
    WAIT_FOR(100, 1000, (evpn_rt == NULL));
    EXPECT_FALSE(RouteFindV6("vrf1", addr, 128));

    //Verify L3 route gets added
    AddVn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    evpn_rt = EvpnRouteGet("vrf1", mac, addr, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(RouteFindV6("vrf1", addr, 128));

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, false, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Add and delete of secondary IP
TEST_F(IntfTest, MultipleIp) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    IpamInfo ipam_info[] = {
        {"8.1.1.0", 24, "8.1.1.10"},
        {"1.1.1.0", 24, "1.1.1.100"},
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddInstanceIp("instance2", input1[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    const MacAddress mac("00:00:00:01:01:01");
    Ip4Address ip = Ip4Address::from_string("8.1.1.1");
    Ip4Address secondary_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");

    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", secondary_ip, 32));
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 2);

    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is not found since link is deleted
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    EXPECT_FALSE(RouteFind("vrf1", secondary_ip, 32));
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 1);

    DelInstanceIp("instance2");
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Addition and deletion of secondary IP
TEST_F(IntfTest, MultipleIp1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    IpamInfo ipam_info[] = {
        {"fd11::", 96, "fd11:1"},
    };

    CreateV6VmportEnv(input, 1, 1, NULL, NULL, false);
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);

    AddInstanceIp("instance2", input[0].vm_id, "fd11::3");
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
    const MacAddress mac("00:00:00:01:01:01");
    Ip6Address ip = Ip6Address::from_string("fd11::2");
    Ip6Address secondary_ip = Ip6Address::from_string("fd11::3");

    //Verify primary route is found
    EvpnRouteEntry *evpn_rt = NULL;
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    //Verify secondary IP route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFindV6("vrf1", secondary_ip, 128));
    EXPECT_TRUE(vm_intf->instance_ipv6_list().list_.size() == 2);

    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    //Verify secondary IP route is node found since link is deleted
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    EXPECT_FALSE(RouteFindV6("vrf1", secondary_ip, 128));
    EXPECT_TRUE(vm_intf->instance_ipv6_list().list_.size() == 1);

    DelInstanceIp("instance2");
    DeleteVmportEnv(input, 1, 1, 1, NULL, NULL, false, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Test to verify secondary IP is deleted when VN is in l2 mode
//and readded when VN is in layer 3 mode
TEST_F(IntfTest, MultipleIp2) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    IpamInfo ipam_info[] = {
        {"8.1.1.0", 24, "8.1.1.10"},
        {"1.1.1.0", 24, "1.1.1.100"},
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddInstanceIp("instance2", input1[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(vm_intf->policy_enabled() == true);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    const MacAddress mac("00:00:00:01:01:01");
    Ip4Address ip = Ip4Address::from_string("8.1.1.1");
    Ip4Address secondary_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");

    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is found
    EXPECT_TRUE(RouteFind("vrf1", secondary_ip, 32));
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 2);

    //Make the VN as layer2 only
    //EVPN route should be added with IP set to 0
    AddL2Vn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->policy_enabled() == false);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == false);
    uint32_t label = vm_intf->l2_label();
    MplsLabel *mpls_label = GetActiveLabel(label);
    EXPECT_TRUE(mpls_label->nexthop()->PolicyEnabled() == false);

    //VN is on l2 only mode, verify ip + mac evpn route is deleted
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    WAIT_FOR(100, 1000, (RouteFind("vrf1", "8.1.1.1", 32) == false));
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);

    EXPECT_TRUE(RouteFind("vrf", secondary_ip, 32) == false);
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);

    //Verify L3 route gets added
    //and policy get enabled
    AddVn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->policy_enabled() == true);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);

    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    label = vm_intf->l2_label();
    mpls_label = GetActiveLabel(label);
    EXPECT_TRUE(mpls_label->nexthop()->PolicyEnabled() == true);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", secondary_ip, 32));
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 2);

    DelInstanceIp("instance2");
    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

//Change vxlan id and verify secondary IP
//gets updated
TEST_F(IntfTest, MultipleIp3) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    IpamInfo ipam_info[] = {
        {"8.1.1.0", 24, "8.1.1.10"},
        {"1.1.1.0", 24, "1.1.1.100"},
    };

    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddInstanceIp("instance2", input1[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    EXPECT_TRUE(vm_intf->policy_enabled() == true);
    EXPECT_TRUE(vm_intf->IsL2Active() == true);
    EXPECT_TRUE(vm_intf->primary_ip_addr().to_string() == "8.1.1.1");

    const MacAddress mac("00:00:00:01:01:01");
    Ip4Address ip = Ip4Address::from_string("8.1.1.1");
    Ip4Address secondary_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");

    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is found
    EXPECT_TRUE(RouteFind("vrf1", secondary_ip, 32));
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);

    uint32_t old_ethernet_tag = vm_intf->ethernet_tag();
    //Modify VN id
    AddVn("vn1", 1, 100, true);
    client->WaitForIdle();

    EXPECT_TRUE(old_ethernet_tag != vm_intf->ethernet_tag());
    evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);

    //VN is on l2 only mode, verify ip + mac evpn route is deleted
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, old_ethernet_tag);
    EXPECT_TRUE(evpn_rt == NULL);

    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, old_ethernet_tag);
    EXPECT_TRUE(evpn_rt == NULL);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, secondary_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", secondary_ip, 32));

    DelInstanceIp("instance2");
    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    DelEncapList();
    client->Reset();
}

//Addition and deletion of service health check IP
TEST_F(IntfTest, ServiceHealthCheckIP) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    IpamInfo ipam_info[] = {
        {"8.1.1.0", 24, "8.1.1.10"},
        {"1.1.1.0", 24, "1.1.1.100"},
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddHealthCheckServiceInstanceIp("instance2", input1[0].vm_id, "1.1.1.10");
    AddLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(8));
    const MacAddress mac("00:00:00:01:01:01");
    Ip4Address ip = Ip4Address::from_string("8.1.1.1");
    Ip4Address service_hc_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address zero_ip = Ip4Address::from_string("0.0.0.0");

    EXPECT_TRUE(L2RouteFind("vrf1", mac));
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, zero_ip,
                                           vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify service health check IP route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, service_hc_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    EXPECT_TRUE(RouteFind("vrf1", service_hc_ip, 32));
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 2);
    EXPECT_TRUE(vm_intf->service_health_check_ip().to_v4() == service_hc_ip);

    DelLink("virtual-machine-interface", input1[0].name,
            "instance-ip", "instance2");
    client->WaitForIdle();

    //Verify primary route is found
    evpn_rt = EvpnRouteGet("vrf1", mac, ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(evpn_rt->GetActiveNextHop()->PolicyEnabled() == true);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    //Verify secondary IP route is not found since link is deleted
    evpn_rt = EvpnRouteGet("vrf1", mac, service_hc_ip, vm_intf->ethernet_tag());
    EXPECT_TRUE(evpn_rt == NULL);
    EXPECT_FALSE(RouteFind("vrf1", service_hc_ip, 32));
    EXPECT_TRUE(vm_intf->instance_ipv4_list().list_.size() == 1);
    EXPECT_TRUE(vm_intf->service_health_check_ip().to_v4() == zero_ip);

    DelInstanceIp("instance2");
    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, Add_vmi_with_zero_mac_and_update_with_correct_mac) {
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:00:00:00", 10, 10},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(10));

    struct PortInfo input2[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:00:00:10", 10, 10},
    };

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(10));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(10));
    client->Reset();
}

TEST_F(IntfTest, FatFlow) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    AddFatFlow(input, "udp", 53, "none", "", 0, 0, "", 0, 0);

    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 53, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 54, &ignore_addr) == false);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 54, &ignore_addr) == false);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == false);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, FatFlowDel) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    AddFatFlow(input, "udp", 53, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 53, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == false);

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 53, &ignore_addr) == false);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == false);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* Verify that 0 can used for port in fat-flow configuration. This is used to
 * ignore ports while setting up flows */
TEST_F(IntfTest, FatFlowPortIgnore) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    AddFatFlow(input, "udp", 0, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 53, &ignore_addr) == false);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* Verify that Fat-flow configured at VN level is available at VMI oper objects
 */
TEST_F(IntfTest, VnFatFlow) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55);
    TestFatFlowEntry e2("tcp", 1234);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);


    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 2);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 1234, &ignore_addr) == true);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* When Fat-flow is configured at VMI and VN level, verify the VMI oper object
 * has the correct set of Fat-flow entries. Also verify that when VN level
 * fat-flow config is removed, the VMI level config is retained at VMI oper
 * object. */
TEST_F(IntfTest, VnVmiFatFlow1) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55);
    TestFatFlowEntry e2("tcp", 1234);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    AddFatFlow(input, "udp", 0, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 3);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 1234, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == true);

    list.clear();
    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == false);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 1234, &ignore_addr) == false);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == true);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* When Fat-flow is configured at VMI and VN level, verify the VMI oper object
 * has the correct set of Fat-flow entries. Also verify that when VMI level
 * fat-flow config is removed, the VN level config is retained at VMI oper
 * object. */
TEST_F(IntfTest, VnVmiFatFlow2) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55);
    TestFatFlowEntry e2("tcp", 1234);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    AddFatFlow(input, "udp", 0, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 3);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 1234, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == true);

    DeleteFatFlow(input);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 2);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_TCP, 1234, &ignore_addr) == true);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 0, &ignore_addr) == false);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* Verify that when same fat-flow is configured at VN and VMI level, the VMI
 * level fat-flow gets precedence. Also verify that when VMI level fat-flow
 * config is removed, VN level fat-flow config is applied to VMI
 * TODO: Deprecate this test case as it is no longer valid
 */
TEST_F(IntfTest, VnVmiFatFlow3) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    //AddFatFlow(input, "udp", 55, "source", "", 0, 0, "", 0, 0);
    AddFatFlow(input, "udp", 55, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    DeleteFatFlow(input);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    list.clear();
    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);
    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

/* Verify that when same fat-flow is configured at VN and VMI level, the VMI
 * level fat-flow gets precedence. Also verify that when VN level fat-flow
 * config is removed, VMI level fat-flow config is retained
 * TODO: Deprecate this test case as it is no longer valid
 */
TEST_F(IntfTest, VnVmiFatFlow4) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    //AddFatFlow(input, "udp", 55, "destination", "", 0, 0, "", 0, 0);
    AddFatFlow(input, "udp", 55, "none", "", 0, 0, "", 0, 0);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    list.clear();
    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);
    EXPECT_TRUE(intf->IsFatFlowPortBased(IPPROTO_UDP, 55, &ignore_addr) == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    DeleteFatFlow(input);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);
    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

// test fatflow cfg with both src and dst prefix
TEST_F(IntfTest, FatFlowPrefixAggr1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    uint16_t port;
    bool same_port;
    IpAddress SrcIP, DstIP;
    bool is_src_prefix, is_dst_prefix;
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    AddFatFlow(input, "udp", 53, "none", "10.0.0.0", 8, 28, "20.0.0.0", 16, 28);
    VmInterface::FatFlowEntrySet::iterator start, end;

    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);

    port = 53;
    SrcIP = IpAddress::from_string("10.0.0.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_UDP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);

    EXPECT_TRUE(is_src_prefix == true);
    EXPECT_TRUE(is_dst_prefix == true);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

// test fatflow cfg with ignore_address=src and dst_prefix ipv4
TEST_F(IntfTest, FatFlowPrefixAggr2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    uint16_t port;
    bool same_port;
    IpAddress SrcIP, DstIP;
    bool is_src_prefix, is_dst_prefix;
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    AddFatFlow(input, "udp", 53, "source", "", 0, 0, "20.0.0.0", 8, 28);
    VmInterface::FatFlowEntrySet::iterator start, end;

    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 1);

    port = 53;
    SrcIP = IpAddress::from_string("10.0.0.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_UDP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == false);
    EXPECT_TRUE(is_dst_prefix == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_SOURCE);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

// test fatflow cfg with multiple cfgs
TEST_F(IntfTest, FatFlowPrefixAggr3) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    uint16_t port;
    bool same_port;
    IpAddress SrcIP, DstIP;
    bool is_src_prefix, is_dst_prefix;
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55, "none", "2001::1", 64, 64, "5000::2", 64, 64);
    TestFatFlowEntry e2("tcp", 17, "source", "10.0.0.0", 16, 28, "20.0.0.0", 8, 28);
    TestFatFlowEntry e3("tcp", 22, "destination", "10.0.0.0", 16, 28, "20.0.0.0", 8, 28);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);
    list.push_back(e3);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 3);
    port = 55;
    SrcIP = IpAddress::from_string("2001::10");
    DstIP = IpAddress::from_string("5000::50");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_UDP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == true);
    EXPECT_TRUE(is_dst_prefix == true);

    port = 17;
    SrcIP = IpAddress::from_string("10.0.0.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_TCP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == false);
    EXPECT_TRUE(is_dst_prefix == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_SOURCE);

    port = 22;
    SrcIP = IpAddress::from_string("10.0.0.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_TCP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == true);
    EXPECT_TRUE(is_dst_prefix == false);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_DESTINATION);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

// test fatflow cfg with -ve cases
TEST_F(IntfTest, FatFlowPrefixAggr4) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55, "none", "2001::1", 64, 48, "5000::2", 64, 64);
    TestFatFlowEntry e2("tcp", 17, "source", "10.0.0.0", 16, 28, "20.0.0.0", 16, 8);
    TestFatFlowEntry e3("tcp", 22, "destination", "2001::2", 64, 64, "20.0.0.0", 8, 28);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);
    list.push_back(e3);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

// test multiple fat flow cfgs with same protocol port
TEST_F(IntfTest, FatFlowPrefixAggr5) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    uint16_t port;
    bool same_port;
    IpAddress SrcIP, DstIP;
    bool is_src_prefix, is_dst_prefix;
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("udp", 55, "none", "2001::1", 64, 64, "5000::2", 64, 64);
    TestFatFlowEntry e2("tcp", 17, "source", "", 0, 0, "20.0.0.0", 8, 28);
    TestFatFlowEntry e3("udp", 55, "none", "", 0, 0, "", 0, 0);
    TestFatFlowEntry e4("tcp", 17, "none", "10.0.0.0", 16, 24, "20.0.0.0", 16, 28);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);
    list.push_back(e3);
    list.push_back(e4);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 4);

    port = 17;
    SrcIP = IpAddress::from_string("10.0.0.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_TCP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == false);
    EXPECT_TRUE(is_dst_prefix == true);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_SOURCE);

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}


// test multiple fat flow cfgs with overlapping prefix
TEST_F(IntfTest, FatFlowPrefixAggr6) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    uint16_t port;
    bool same_port;
    IpAddress SrcIP, DstIP;
    bool is_src_prefix, is_dst_prefix;
    VmInterface::FatFlowIgnoreAddressType ignore_addr;

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VnFind(input[0].vn_id));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    TestFatFlowEntry e1("tcp", 17, "none", "10.1.1.0", 8, 16, "", 0, 0);
    TestFatFlowEntry e2("tcp", 17, "none", "10.1.1.0", 16, 16, "", 0, 0);
    TestFatFlowEntry e3("tcp", 17, "none", "10.1.1.0", 24, 24, "", 0, 0);
    vector<TestFatFlowEntry> list;
    list.push_back(e1);
    list.push_back(e2);
    list.push_back(e3);

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 3);

    port = 17;
    SrcIP = IpAddress::from_string("10.1.1.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_TCP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == true);
    EXPECT_TRUE(is_dst_prefix == false);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    // check if src prefix matches
    Ip4Address src_prefix = IpAddress::from_string("10.1.1.0").to_v4();
    EXPECT_TRUE(src_prefix == (SrcIP.to_v4()));

    // now remove /24 prefix and check if /16 is choosen
    list.pop_back();

    AddVnFatFlow(input[0].vn_id, list);
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 2);

    port = 17;
    SrcIP = IpAddress::from_string("10.1.1.1");
    DstIP = IpAddress::from_string("20.0.0.1");

    EXPECT_TRUE(intf->IsFatFlowPrefixAggregation(true, IPPROTO_TCP, &port, &port,
                                                 &same_port, &SrcIP, &DstIP,
                                                 &is_src_prefix, &is_dst_prefix,
                                                 &ignore_addr) == true);
    EXPECT_TRUE(is_src_prefix == true);
    EXPECT_TRUE(is_dst_prefix == false);
    EXPECT_TRUE(ignore_addr == VmInterface::IGNORE_NONE);

    // check if src prefix matches
    src_prefix = IpAddress::from_string("10.1.0.0").to_v4();
    EXPECT_TRUE(src_prefix == (SrcIP.to_v4()));

    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(intf->fat_flow_list().list_.size() == 0);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, IntfAddDel) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    InterfaceRef intf = static_cast<Interface *>(VmPortGet(1));
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.1", 32));

    DelLink("virtual-network", "vn1", "virtual-machine-interface",
            "vnet1");
    client->WaitForIdle();

    NovaDel(1);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-machine-interface",
            "vnet1");
    client->WaitForIdle();

    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    AddNode("virtual-machine-interface", "vnet1", 1);
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.1", 32));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, BridgeDomain) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    AddBridgeDomain("bridge1", 1, 1);
    client->WaitForIdle();
    AddVmportBridgeDomain(input[0].name, 0);
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name,
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    VmInterface *vintf = static_cast<VmInterface*>(VmPortGet(1));
    EXPECT_TRUE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 1);

    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1");
    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name);
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelNode("virtual-machine-interface-bridge-domain", input[0].name);
    client->WaitForIdle();
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 0);

    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, BridgeDomain1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    AddBridgeDomain("bridge1", 1, 1);
    AddBridgeDomain("bridge2", 2, 2);
    client->WaitForIdle();
    AddVmportBridgeDomain("bridge1", 0);
    AddVmportBridgeDomain("bridge2", 1);

    AddLink("virtual-machine-interface-bridge-domain", "bridge1",
            "bridge-domain", "bridge1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-machine-interface-bridge-domain", "bridge1",
            "virtual-machine-interface", "vnet1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    VmInterface *vintf = static_cast<VmInterface*>(VmPortGet(1));
    EXPECT_TRUE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 1);

    AddLink("virtual-machine-interface-bridge-domain", "bridge2",
            "bridge-domain", "bridge2",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-machine-interface-bridge-domain", "bridge2",
            "virtual-machine-interface", "vnet1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge2");
    client->WaitForIdle();

    EXPECT_TRUE(VrfFind("vrf1:00000000-0000-0000-0000-000000000002"));
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 2);

    DelLink("virtual-machine-interface-bridge-domain", "bridge2",
            "bridge-domain", "bridge2");
    DelLink("virtual-machine-interface-bridge-domain", "bridge2",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge2");
    DelNode("virtual-machine-interface-bridge-domain", "bridge2");
    client->WaitForIdle();
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 1);

    DelLink("virtual-machine-interface-bridge-domain", "bridge1",
            "bridge-domain", "bridge1");
    DelLink("virtual-machine-interface-bridge-domain", "bridge1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelNode("virtual-machine-interface-bridge-domain", "bridge1");
    client->WaitForIdle();
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 0);

    DelNode("bridge-domain", "bridge1");
    DelNode("bridge-domain", "bridge2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001", true));
    EXPECT_FALSE(VrfFind("vrf1:2", true));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, BridgeDomain_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    AddBridgeDomain("bridge1", 1, 1);
    client->WaitForIdle();
    AddVmportBridgeDomain(input[0].name, 0);
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name,
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    VmInterface *vintf = static_cast<VmInterface*>(VmPortGet(1));
    EXPECT_FALSE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 0);
    EXPECT_TRUE(vintf->pbb_interface() == false);

    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));
    EXPECT_TRUE(vintf->pbb_interface());

    client->Reset();
    AddBridgeDomain("bridge1", 1, 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));

    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1");
    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name);
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelNode("virtual-machine-interface-bridge-domain", input[0].name);
    client->WaitForIdle();
    EXPECT_TRUE(vintf->bridge_domain_list().list_.size() == 0);

    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1:00000000-0000-0000-0000-000000000001"));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(IntfTest, intf_label) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    // 5 interface nh, 1 vrf nh and 1 for bridge route
    size_t count = (2*NH_PER_VM)+1+1;
    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == count);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    count = 1*NH_PER_VM;
    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == count);
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));
    client->Reset();
    client->WaitForIdle();
}

//Add and delete of floating-ip dependent on secondary IP
TEST_F(IntfTest, FloatingIpFixedIpAddChange_1) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    client->Reset();

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", "1.1.1.10");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    client->WaitForIdle();

    //Verify that FloatingIP is associated with VMI
    EXPECT_TRUE(VmPortFloatingIpCount(8, 1));

    DelLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    client->WaitForIdle();

    //Verify that FloatingIP count of VMI is 0 after disassociation
    EXPECT_TRUE(VmPortFloatingIpCount(8, 0));

    //Use IPv6 fixed-ip with IPv4 FloatingIP
    AddFloatingIp("fip1", 1, "2.1.1.100", "fd11::10");
    AddLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    client->WaitForIdle();

    //Verify that FloatingIP is NOT associated with VMI
    EXPECT_TRUE(VmPortFloatingIpCount(8, 0));


    //Clean up
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet8", "floating-ip", "fip1");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();

    DeleteVmportEnv(input1, 1, true, 1);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (Agent::GetInstance()->interface_table()->Find(&key,
                                                                       true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, GwIntfAddVpg) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    CreateVmportWithoutNova(input1, 1);
    client->WaitForIdle();

    AddPhysicalDevice(agent->host_name().c_str(), 1);
    AddPhysicalInterface("pi1", 1, "pi1");
    AddVirtualPortGroup("vpg1", 1, "vpg1");
    AddLinkNode("virtual-port-group-physical-interface", "vpg1_pi1", "ae1");
    AddLink("physical-router", agent->host_name().c_str(),
            "physical-interface", "pi1");
    AddLink("virtual-port-group-physical-interface", "vpg1_pi1",
            "physical-interface", "pi1",
            "virtual-port-group-physical-interface");
    AddLink("virtual-port-group-physical-interface", "vpg1_pi1",
            "virtual-port-group", "vpg1",
            "virtual-port-group-physical-interface");
    AddLink("virtual-machine-interface", "vnet8",
            "virtual-port-group", "vpg1");
    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name, "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    //Verify that route is pointing to resolve NH
    //and the route points to table NH
    Ip4Address addr = Ip4Address::from_string("8.1.1.0");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", addr, 24);
    const VrfEntry *vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveLabel() != MplsTable::kInvalidLabel);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    DelLink("virtual-machine-interface", input1[0].name,
             "subnet", "subnet");
    DelLink("virtual-machine-interface", "vnet8",
            "virtual-port-group", "vpg1");
    DelLink("physical-router", agent->host_name().c_str(),
            "physical-interface", "pi1");
    DelNode("virtual-port-group-physical-interface", "vpg1_pi1");
    DeleteVirtualPortGroup("vpg1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice(agent->host_name().c_str());

    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", "8.1.1.0", 24));

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000,
        (Agent::GetInstance()->interface_table()->Find(&key, true) == NULL));
    client->Reset();
}

TEST_F(IntfTest, GwSubnetChangeVpg) {
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
    AddPhysicalInterface("pi1", 1, "pi1");
    AddVirtualPortGroup("vpg1", 1, "vpg1");
    AddLinkNode("virtual-port-group-physical-interface", "vpg1_pi1", "ae1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("virtual-port-group-physical-interface", "vpg1_pi1",
            "physical-interface", "pi1",
            "virtual-port-group-physical-interface");
    AddLink("virtual-port-group-physical-interface", "vpg1_pi1",
            "virtual-port-group", "vpg1",
            "virtual-port-group-physical-interface");
    AddLink("virtual-machine-interface", "vnet8",
            "virtual-port-group", "vpg1");
    //Add a link to interface subnet and ensure resolve route is added
    AddSubnetType("subnet", 1, "8.1.1.0", 24);
    AddLink("virtual-machine-interface", input1[0].name, "subnet", "subnet");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(RouteFind("vrf1", "8.1.1.0", 24));

    //Verify that route is pointing to resolve NH
    //and the route points to table NH
    Ip4Address addr = Ip4Address::from_string("8.1.1.0");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", addr, 24);
    const VrfEntry *vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    AddSubnetType("subnet", 1, "9.1.1.0", 24);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (RouteFind("vrf1", "8.1.1.0", 24) == false));
    addr = Ip4Address::from_string("9.1.1.0");
    rt = RouteGet("vrf1", addr, 24);
    vrf = VrfGet("vrf1", false);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf != NULL);
    if (rt && vrf) {
        EXPECT_TRUE(rt->GetActiveLabel() == vrf->table_label());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
    }

    DelLink("virtual-machine-interface", input1[0].name, "subnet", "subnet");
    DelLink("virtual-machine-interface", "vnet8",
            "virtual-port-group", "vpg1");
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelNode("virtual-port-group-physical-interface", "vpg1_pi1");
    DeleteVirtualPortGroup("vpg1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice(agent->host_name().c_str());
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", "9.1.1.0", 24));
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000,
        (Agent::GetInstance()->interface_table()->Find(&key, true) == NULL));
    client->Reset();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
