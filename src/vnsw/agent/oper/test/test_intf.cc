/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>

#include "testing/gunit.h"

#include <boost/uuid/string_generator.hpp>

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h> 
#include <ksync/ksync_sock_user.h> 
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

void RouterIdDepInit() {
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

class IntfTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        intf_count = agent->GetInterfaceTable()->Size();
        DoInterfaceSandesh("");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DoInterfaceSandesh("");
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->GetInterfaceTable()->Size() == intf_count));
        WAIT_FOR(100, 1000, (agent->GetVrfTable()->Size() == 1U));
        WAIT_FOR(100, 1000, (agent->GetVmTable()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->GetVnTable()->Size() == 0U));
    }

    int intf_count;
    Agent *agent;
};

static void NovaIntfAdd(int id, const char *name, const char *addr,
                        const char *mac) {
    IpAddress ip = Ip4Address::from_string(addr);
    VmInterface::Add(Agent::GetInstance()->GetInterfaceTable(),
                     MakeUuid(id), name, ip.to_v4(), mac, "",
                     MakeUuid(kProjectUuid),
                     VmInterface::kInvalidVlanId, Agent::NullString());
}

static void NovaDel(int id) {
    VmInterface::Delete(Agent::GetInstance()->GetInterfaceTable(),
                        MakeUuid(id));
}

static void FloatingIpAdd(VmInterface::FloatingIpList &list, const char *addr, 
                          const char *vrf) {
    IpAddress ip = Ip4Address::from_string(addr);
    list.list_.insert(VmInterface::FloatingIp(ip.to_v4(), vrf, MakeUuid(1)));
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
    Agent::GetInstance()->GetMirrorTable()->AddMirrorEntry(analyzer_info.analyzer_name,
                                                           std::string(),
                                                           Agent::GetInstance()->GetRouterId(),
                                                           Agent::GetInstance()->GetMirrorPort(),
                                                           dip,
                                                           analyzer_info.dport);
}

static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm, 
                        VmInterface::FloatingIpList list, string vrf_name,
                        string ip, AnalyzerInfo &analyzer_info) {
    uuid intf_uuid = MakeUuid(id);
    uuid vn_uuid = MakeUuid(vn);
    uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    CreateMirror(analyzer_info);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC,
                                                     intf_uuid, "");
    req.key.reset(key);

    VmInterfaceConfigData *cfg_data = new VmInterfaceConfigData();
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_ip_list_ = list;
    cfg_data->vrf_name_ = vrf_name;
    cfg_data->addr_ = Ip4Address::from_string(ip);
    cfg_data->fabric_port_ = false;
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
    Agent::GetInstance()->GetInterfaceTable()->Enqueue(&req);
}


static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm, 
                        VmInterface::FloatingIpList list, string vrf_name,
                        string ip) {
    uuid intf_uuid = MakeUuid(id);
    uuid vn_uuid = MakeUuid(vn);
    uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    req.key.reset(key);

    VmInterfaceConfigData *cfg_data = new VmInterfaceConfigData();
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_ip_list_ = list;
    cfg_data->vrf_name_ = vrf_name;
    cfg_data->addr_ = Ip4Address::from_string(ip);
    cfg_data->fabric_port_ = false;
    req.data.reset(cfg_data);
    Agent::GetInstance()->GetInterfaceTable()->Enqueue(&req);
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
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetInterfaceTable()->Find(&key, true)
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
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    intf_idx = VmPortGetId(8);
    client->Reset();

    sock->SetBlockMsgProcessing(true);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    client->Reset();

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(9));
    EXPECT_NE(VmPortGetId(9), intf_idx);
    client->Reset();
    DeleteVmportEnv(input2, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(9));
    sock->SetBlockMsgProcessing(false);
    client->WaitForIdle();
    usleep(2000);
    client->WaitForIdle();
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, 
             (Agent::GetInstance()->GetInterfaceTable()->Find(&key1, true) 
              == NULL));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(9), "");
    WAIT_FOR(100, 1000, 
             (Agent::GetInstance()->GetInterfaceTable()->Find(&key2, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, entry_reuse) {
    uint32_t    intf_idx;

    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    intf_idx = VmPortGetId(8);
    client->Reset();

    sock->SetBlockMsgProcessing(true);
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    client->Reset();

    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(8));
    EXPECT_EQ(VmPortGetId(8), intf_idx);
    client->Reset();
    sock->SetBlockMsgProcessing(false);
    usleep(2000);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
}

TEST_F(IntfTest, ActivateInactivate) {
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
    EXPECT_EQ(3U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    NovaDel(1);
    EXPECT_TRUE(client->NotifyWait(1, 0, 0));
    EXPECT_FALSE(VmPortFind(1));
    WAIT_FOR(100, 1000, 
             (Agent::GetInstance()->GetInterfaceTable()->Size() == 3U));

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
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());

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
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    VmAddReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());

    client->Reset();
    VmDelReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());

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
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf2", "1.1.1.1", analyzer_info);
    client->WaitForIdle();
    MirrorEntryKey mirror_key(analyzer_info.analyzer_name);
    EXPECT_TRUE(Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&mirror_key) != NULL);
    EXPECT_EQ(VmPortGetAnalyzerName(1), "Analyzer1");
    EXPECT_EQ(VmPortGetMirrorDirection(1), Interface::MIRROR_RX_TX);

    analyzer_info.analyzer_name = "Analyzer1";
    analyzer_info.vrf_name = std::string();
    analyzer_info.analyzer_ip = "1.1.1.2";
    analyzer_info.dport = 8099;
    analyzer_info.direction = "egress";
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf2", "1.1.1.1", analyzer_info);
    client->WaitForIdle();
    EXPECT_EQ(VmPortGetMirrorDirection(1), Interface::MIRROR_TX);
    
    client->Reset();
    VmDelReq(1);
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf2", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(0U, Agent::GetInstance()->GetVmTable()->Size());

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
    EXPECT_TRUE(Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&mirror_key) == NULL);
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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, NULL_VRF, ZERO_IP);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());

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
    EXPECT_TRUE(VmPortInactive(1));
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
}

// VM create, VN create, VRF create, 3 VmPorts
TEST_F(IntfTest, MultipleVmPorts_1) {
    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

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
    EXPECT_EQ(6U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf4", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf4", "1.1.1.2");
    CfgIntfSync(3, "cfg-vnet3", 1, 1, "vrf4", "1.1.1.3");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(3));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_TRUE(VmPortActive(3));
    EXPECT_EQ(6U, Agent::GetInstance()->GetInterfaceTable()->Size());

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
}

// VN has ACL set before VM Port is created
TEST_F(IntfTest, VmPortPolicy_1) {

    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());

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
    EXPECT_EQ(5U, Agent::GetInstance()->GetInterfaceTable()->Size());

    client->Reset();
    AclDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    // ACL not deleted due to reference from VN
    EXPECT_EQ(1U, Agent::GetInstance()->GetAclTable()->Size());
    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf5");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->GetAclTable()->Size());
    // Ports not yet notified. So, they still have policy enabled
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(VmPortPolicyEnable(2));

    client->Reset();
    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf5", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf5", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmPortPolicyEnable(1));
    EXPECT_FALSE(VmPortPolicyEnable(2));

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
}

// ACL set in VN after VM Port is created
TEST_F(IntfTest, VmPortPolicy_2) {
    client->Reset();
    VmAddReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());

    client->Reset();
    VrfAddReq("vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VnAddReq(1, "vn1", 1, "vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());

    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    NovaIntfAdd(2, "vnet2", "1.1.1.2", "00:00:00:00:00:02");

    CfgIntfSync(1, "cfg-vnet1", 1, 1, "vrf6", "1.1.1.1");
    CfgIntfSync(2, "cfg-vnet2", 1, 1, "vrf6", "1.1.1.2");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_TRUE(VmPortPolicyDisable(1));
    EXPECT_TRUE(VmPortPolicyDisable(2));

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
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetAclTable()->Size() == 0U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 1U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetAclTable()->Size() == 0U));
    // Ports already notified. So, they still have policy disabled
    EXPECT_TRUE(VmPortPolicyDisable(1));
    EXPECT_TRUE(VmPortPolicyDisable(2));

    client->Reset();
    NovaDel(1);
    NovaDel(2);
    VmDelReq(1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmPortFind(1));
    EXPECT_FALSE(VmPortFind(2));
    WAIT_FOR(100, 1000, 
             (Agent::GetInstance()->GetInterfaceTable()->Size() == 3U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetVmTable()->Size() == 0U));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 1U));

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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(3U, Agent::GetInstance()->GetVrfTable()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2");
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
    EXPECT_TRUE(VmPortPolicyDisable(1));
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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(3U, Agent::GetInstance()->GetVrfTable()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");
    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2");
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
    EXPECT_TRUE(VmPortPolicyDisable(1));

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
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(5U, Agent::GetInstance()->GetVrfTable()->Size());

    // Nova add followed by config interface
    client->Reset();
    NovaIntfAdd(1, "vnet1", "1.1.1.1", "00:00:00:00:00:01");

    VmInterface::FloatingIpList list;
    FloatingIpAdd(list, "2.2.2.2", "vrf2");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));
    EXPECT_TRUE(VmPortPolicyEnable(1));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));

    // Add 2 more floating-ip
    VmInterface::FloatingIpList list1;
    FloatingIpAdd(list1, "2.2.2.2", "vrf2");
    FloatingIpAdd(list1, "3.3.3.3", "vrf3");
    FloatingIpAdd(list1, "4.4.4.4", "vrf4");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list1, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 3));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_TRUE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list2;
    FloatingIpAdd(list2, "3.3.3.3", "vrf3");
    FloatingIpAdd(list2, "4.4.4.4", "vrf4");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list2, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 2));
    EXPECT_FALSE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_TRUE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list3;
    FloatingIpAdd(list3, "2.2.2.2", "vrf2");
    FloatingIpAdd(list3, "3.3.3.3", "vrf3");
    CfgIntfSync(1, "cfg-vnet1", 1, 1, list3, "vrf1", "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 2));
    EXPECT_TRUE(RouteFind("vrf2", "2.2.2.2", 32));
    EXPECT_TRUE(RouteFind("vrf3", "3.3.3.3", 32));
    EXPECT_FALSE(RouteFind("vrf4", "4.4.4.4", 32));
    DoInterfaceSandesh("");

    // Remove a floating-ip
    VmInterface::FloatingIpList list4;
    FloatingIpAdd(list4, "2.2.2.2", "vrf2");
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
}

TEST_F(IntfTest, VmPortFloatingIpDelete_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
 
    AddVn("vn2", 2);
    AddVrf("vn2:vn2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and floating ip map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 0));

    //Clean up
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance", "vn2:vn2");
    DelVrf("vn2:vn2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vn2:vn2"));
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
    DoInterfaceSandesh("");
    client->WaitForIdle();

    //Delete config for vnet1, forcing interface to deactivate
    //verify that route and service vlan map gets cleaned up
    DelNode("virtual-machine-interface", input[0].name);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf2", service_ip, 32));
    EXPECT_TRUE(VmPortServiceVlanCount(1, 0));
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

    //Delete the interface, all service vlan routes should be deleted
    //and interface should be released
    NovaDel(1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFindRetDel(1) == false);

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
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("vrf2"));
}
//A
//Add and delete static route 
TEST_F(IntfTest, IntfStaticRoute) {
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
   EXPECT_FALSE(nh->PolicyEnabled());

   EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                         static_route[1].plen_));
   nh = RouteGet("vrf1", static_route[1].addr_,
           static_route[1].plen_)->GetActiveNextHop();
   EXPECT_FALSE(nh->PolicyEnabled());

   //Add a acl to interface and verify NH policy changes
   AddAcl("Acl", 1, "vn1", "vn1");
   AddLink("virtual-network", "vn1", "access-control-list", "Acl");
   client->WaitForIdle();
   nh = RouteGet("vrf1", static_route[0].addr_,
                 static_route[0].plen_)->GetActiveNextHop();
   EXPECT_TRUE(nh->PolicyEnabled());

   nh = RouteGet("vrf1", static_route[1].addr_,
                 static_route[1].plen_)->GetActiveNextHop();
   EXPECT_TRUE(nh->PolicyEnabled());

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
    EXPECT_TRUE(vm_interface->vm_mac() == "00:00:00:01:01:01");
    client->Reset();

    CreateVmportEnv(input1_mac_changed, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();
    //No change expected as vm interface change results in no modifications
    EXPECT_TRUE(vm_interface->vm_mac() == "00:00:00:01:01:01");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000, (agent->GetInterfaceTable()->Find(&key, true)
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
    WAIT_FOR(100, 1000, (agent->GetInterfaceTable()->Find(&key2, true)
                == NULL));
    client->Reset();
}

TEST_F(IntfTest, packet_interface_get_key_verification) {
    PacketInterfaceKey key(nil_uuid(), "pkt0");
    Interface *intf = 
        static_cast<Interface *>(agent->GetInterfaceTable()->FindActiveEntry(&key));
    PacketInterfaceKey *entry_key = static_cast<PacketInterfaceKey *>(intf->GetDBRequestKey().release());
    EXPECT_TRUE(entry_key != NULL);
    client->Reset();

    //Issue sandesh request
    DoInterfaceSandesh("pkt0");
    client->WaitForIdle();
}

TEST_F(IntfTest, sandesh_vm_interface_l2_only) {
    struct PortInfo input1[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1"); 
    client->WaitForIdle();
    CreateL2VmportEnv(input1, 1);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortL2Active(input1, 0) == true));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();

    //Issue sandesh request
    DoInterfaceSandesh("vnet8");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input1, 1, true); 
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
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

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);

    return ret;
}
