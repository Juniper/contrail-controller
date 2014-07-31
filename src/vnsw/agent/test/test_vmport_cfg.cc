/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_ifmap.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/vm_interface.h>
#include <oper/agent_sandesh.h>
#include <oper/interface_common.h>
#include <oper/vxlan.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "xmpp/test/xmpp_test_util.h"

using namespace std;
using namespace boost::assign;

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

class CfgTest : public ::testing::Test {
    virtual void SetUp() {
    }

    virtual void TearDown() {
        EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());
    }

};

TEST_F(CfgTest, AddDelVmPortNoVn_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    IntfCfgAdd(input, 0);
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->interface_config_table()->Size());

    client->Reset();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->interface_config_table()->Size());
}

TEST_F(CfgTest, AddDelExport) {

    client->Reset();

    CfgIntKey *key = new CfgIntKey(MakeUuid(1)); 
    CfgIntData *data = new CfgIntData();
    boost::system::error_code ec;
    IpAddress ip = Ip4Address::from_string("1.1.1.1", ec);
    data->Init(MakeUuid(1), MakeUuid(1), MakeUuid(kProjectUuid),
               "vnet1", ip, "00:00:00:01:01:01", "",
               VmInterface::kInvalidVlanId, CfgIntEntry::CfgIntVMPort, 0);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->interface_config_table()->Enqueue(&req);

    CfgIntKey *key1 = new CfgIntKey(MakeUuid(1)); 
    CfgIntData *data1 = new CfgIntData();
    ip = Ip4Address::from_string("1.1.1.1", ec);
    data1->Init(MakeUuid(1), MakeUuid(1), MakeUuid(kProjectUuid),
                "vnet1", ip, "00:00:00:01:01:01", "",
                VmInterface::kInvalidVlanId, CfgIntEntry::CfgIntVMPort, 0);
    req.key.reset(key1);
    req.data.reset(data1);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    usleep(1000);

    EXPECT_EQ(0U, Agent::GetInstance()->interface_config_table()->Size());
}

TEST_F(CfgTest, AddDelVmPortDepOnVmVn_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    // Nova Port add message - Should be inactive since VM and VN not present
    client->Reset();
    IntfCfgAdd(input, 0);
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->interface_config_table()->Size());

    // Config VM Add - Port inactive since VN not present
    AddVm("vm1", 1);
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    AddVrf("vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("vrf1"));

    // Config VN Add - Port inactive since interface oper-db not aware of
    // VM and VN added
    AddVn("vn1", 1);
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    // Config Port add - Interface oper-db still inactive since no link between
    // VN and VRF
    client->Reset();
    AddPort(input[0].name, input[0].intf_id);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add VN and VRF link. Port in-active since not linked to VM and VN
    client->Reset();
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add VM and Port link. Port in-active since port not linked to VN
    client->Reset();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    //EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add Port to VN link - Port is active
    client->Reset();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    client->Reset();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    // Delete Port to VM link. Port is becomes inactive
    client->Reset();
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Delete Port to VN link. Port is inactive
    client->Reset();
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle();

    // Delete config port entry. Port still present but inactive
    client->Reset();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-machine-interface", "vnet1");
    DelNode("virtual-machine", "vm1");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_FALSE(VmFind(1));

    DelNode("routing-instance", "vrf1");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->interface_config_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_FALSE(VnFind(1));
}

TEST_F(CfgTest, AddDelVmPortDepOnVmVn_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    // Config VM Add - Port inactive since VN not present
    client->Reset();
    AddVm("vm1", 1);
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    // Nova Port add message - Should be inactive since VN not present
    client->Reset();
    IntfCfgAdd(input, 0);
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->interface_config_table()->Size());

    // Config VN Add - Port inactive since interface oper-db not aware of
    // VM and VN added
    AddVn("vn1", 1);
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VnFind(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    // Add link between VN and VRF. Interface still inactive
    client->Reset();
    AddVrf("vrf2");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf2");
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("vrf2"));

    // Config Port add - Interface still inactive
    client->Reset();
    AddPort(input[0].name, input[0].intf_id);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add Port to VM link - Port is inactive
    client->Reset();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add vm-port interface to vrf link
    AddVmPortVrf("vnet1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add Port to VN link - Port is active
    client->Reset();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortActive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->interface_config_table()->Size());

    client->Reset();
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf2");
    client->WaitForIdle();
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VmFind(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    DelNode("routing-instance", "vrf2");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(CfgTest, AddDelVmPortDepOnVmVn_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    // Nova Port add message - Should be inactive since VM and VN not present
    client->Reset();
    IntfCfgAdd(input, 0);
    EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->interface_config_table()->Size());

    // Config VM Add - Port inactive since VN not present
    AddVm("vm1", 1);
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());

    AddVrf("vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("vrf1"));

    // Config VN Add - Port inactive since interface oper-db not aware of
    // VM and VN added
    AddVn("vn1", 1);
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    // Config Port add - Interface oper-db still inactive since no link between
    // VN and VRF
    client->Reset();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    client->Reset();
    AddPort(input[0].name, input[0].intf_id);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add vm-port interface to vrf link
    AddVmPortVrf("vnet1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add VN and VRF link. Port in-active since not linked to VM and VN
    client->Reset();
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add VM and Port link. Port in-active since port not linked to VN
    client->Reset();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    //EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Add instance ip configuration
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    //EXPECT_TRUE(client->PortNotifyWait(1));
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Add Port to VN link - Port is active
    client->Reset();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_TRUE(VmPortActive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    client->WaitForIdle();

    // Delete VM and its associated links. NOVA cfg is still not deleted
    // Vmport should be inactive
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
}

// VN has ACL set before VM Port is created
TEST_F(CfgTest, VmPortPolicy_1) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 1},
    };

    client->Reset();
    AddVm("vm1", 1);
    client->WaitForIdle();
    AddAcl("acl1", 1);
    client->WaitForIdle();
    AddVrf("vrf3");
    client->WaitForIdle();
    AddVn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->acl_table()->Size());
    EXPECT_TRUE(VrfFind("vrf3"));

    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddVmPortVrf("vmvrf2", "", 0);
    client->WaitForIdle();

    AddPort(input[0].name, input[0].intf_id);
    AddPort(input[1].name, input[1].intf_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf3");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf3");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "vrf3");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "virtual-machine-interface", "vnet2");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddInstanceIp("instance1", input[0].vm_id, input[1].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");
    client->WaitForIdle();

    client->Reset();
    IntfCfgAdd(input, 0);
    IntfCfgAdd(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_FALSE(VmPortPolicyEnable(input, 0));
    EXPECT_FALSE(VmPortPolicyEnable(input, 1));

    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));

    client->Reset();
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(0));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VmPortPolicyDisable(input, 0));
    EXPECT_TRUE(VmPortPolicyDisable(input, 1));

    client->Reset();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());

    // Del VN to VRF link. Port should become inactive
    client->Reset();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf3");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf3");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "vrf3");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf2");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 1));

    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine-interface", input[0].name, "instance-ip",
            "instance0");
    DelLink("virtual-machine-interface", input[1].name, "instance-ip",
            "instance1");

    // Delete config vm entry - no-op for oper-db. Port is active
    client->Reset();
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    // VM not deleted. Interface still refers to it
    EXPECT_FALSE(VmFind(1));

    client->Reset();
    DelNode("virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface", "vnet2");
    EXPECT_TRUE(client->PortNotifyWait(2));

    //After deleting vmport interface config, verify config name is set to ""
    const Interface *intf = VmPortGet(1);
    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    EXPECT_TRUE((vm_intf->cfg_name() == ""));

    intf = VmPortGet(2);
    vm_intf = static_cast<const VmInterface *>(intf);
    EXPECT_TRUE((vm_intf->cfg_name() == ""));

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    EXPECT_TRUE(client->PortDelNotifyWait(2));
    EXPECT_FALSE(VmFind(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    DelNode("routing-instance", "vrf3");
    DelInstanceIp("instance0");
    DelInstanceIp("instance1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf3"));
}

// ACL added after VM Port is created
TEST_F(CfgTest, VmPortPolicy_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 1},
    };

    client->Reset();
    AddVm("vm1", 1);
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));

    AddVn("vn1", 1);
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    AddAcl("acl1", 1);
    EXPECT_TRUE(client->AclNotifyWait(1));
    client->Reset();

    AddPort(input[0].name, input[0].intf_id);
    AddPort(input[1].name, input[1].intf_id);
    client->Reset();

    IntfCfgAdd(input, 0);
    IntfCfgAdd(input, 1);
    EXPECT_TRUE(client->PortNotifyWait(2));
    // Port inactive since VRF is not yet present
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 1));
    EXPECT_TRUE(VmPortPolicyDisable(input, 0));
    EXPECT_TRUE(VmPortPolicyDisable(input, 1));
    EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

    AddVrf("vrf4");
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("vrf4"));

    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddVmPortVrf("vmvrf2", "", 0);

    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf4");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "vrf4");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 1));

    AddLink("virtual-network", "vn1", "routing-instance", "vrf4");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddInstanceIp("instance1", input[0].vm_id, input[1].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    client->Reset();
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));

    client->Reset();
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(0));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VmPortPolicyDisable(input, 0));
    EXPECT_TRUE(VmPortPolicyDisable(input, 1));

    client->Reset();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf4");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "vrf4");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf2");
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "routing-instance", "vrf4");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine-interface", input[0].name, "instance-ip", 
            "instance0");
    DelLink("virtual-machine-interface", input[1].name, "instance-ip", 
            "instance1");

    // Delete config vm entry - no-op for oper-db. Port is active
    client->Reset();
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_FALSE(VmFind(1));
    EXPECT_TRUE(VmPortFind(input, 0));
    EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

    DelPort(input[0].name);
    DelPort(input[1].name);
    client->Reset();

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    EXPECT_TRUE(client->PortDelNotifyWait(2));
    EXPECT_FALSE(VmFind(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(3U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    // Del VN to VRF link. Port should become inactive
    client->Reset();
    DelNode("virtual-network", "vn1");
    DelInstanceIp("instance0");
    DelInstanceIp("instance1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    DelNode("routing-instance", "vrf4");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf4"));
}

TEST_F(CfgTest, VnDepOnVrfAcl_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 1},
    };

    client->Reset();
    AddVm("vm1", 1);
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));

    client->Reset();
    AddVrf("vrf5");
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("vrf5"));

    AddVn("vn1", 1);
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    client->Reset();
    AddAcl("acl1", 1);
    EXPECT_TRUE(client->AclNotifyWait(1));

    AddLink("virtual-network", "vn1", "routing-instance", "vrf5");
    client->WaitForIdle();
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vn->GetAcl() == NULL);

    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vn->GetAcl() != NULL);

    AddPort(input[0].name, input[0].intf_id);
    AddPort(input[1].name, input[1].intf_id);
    client->Reset();

    client->Reset();
    IntfCfgAdd(input, 0);
    IntfCfgAdd(input, 1);
    EXPECT_TRUE(client->PortNotifyWait(2));

    // Add vm-port interface to vrf link
    AddVmPortVrf("vnet1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf5");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));
    // Add vm-port interface to vrf link
    AddVmPortVrf("vnet2", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vnet2",
            "routing-instance", "vrf5");
    AddLink("virtual-machine-interface-routing-instance", "vnet2",
            "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 1));

    // Port Active since VRF and VM already added
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddInstanceIp("instance1", input[0].vm_id, input[1].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));
    EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

    client->Reset();
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf5");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet2",
            "routing-instance", "vrf5");
    DelLink("virtual-machine-interface-routing-instance", "vnet2",
            "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine-interface-routing-instance", "vnet2");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 1));

    client->Reset();
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_TRUE(VmPortPolicyDisable(input, 0));
    EXPECT_TRUE(VmPortPolicyDisable(input, 1));

    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine-interface", input[0].name, "instance-ip", 
            "instance0");
    DelLink("virtual-machine-interface", input[1].name, "instance-ip", 
            "instance1");
    client->WaitForIdle();

    DelPort(input[0].name);
    DelPort(input[1].name);
    client->Reset();

    client->Reset();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->AclNotifyWait(1));
    EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());

    // Delete config vm entry - no-op for oper-db. Port is active
    client->Reset();
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_FALSE(VmFind(1));
    EXPECT_TRUE(VmPortFind(input, 0));
    EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmFind(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    WAIT_FOR(100, 1000, 
             (3U == Agent::GetInstance()->interface_table()->Size()));
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    // Del VN to VRF link. Port should become inactive
    client->Reset();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf5");
    DelNode("virtual-network", "vn1");
    DelInstanceIp("instance0");
    DelInstanceIp("instance1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    DelNode("routing-instance", "vrf5");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf5"));
}

//TBD
//Reduce the waitforidle to improve on timing of UT
TEST_F(CfgTest, FloatingIp_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    client->WaitForIdle();
    client->Reset();
    AddVm("vm1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    EXPECT_TRUE(VmFind(1));

    client->Reset();
    AddVrf("vrf6");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("vrf6"));

    AddVn("vn1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());

    AddLink("virtual-network", "vn1", "routing-instance", "vrf6");
    client->WaitForIdle();

    client->Reset();
    IntfCfgAdd(input, 0);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(1));

    AddPort(input[0].name, input[0].intf_id);
    client->WaitForIdle();

    // Create floating-ip on default-project:vn2
    client->Reset();
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    AddVrf("default-project:vn2:vn2");
    AddVrf("vrf8");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(2));
    EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
    EXPECT_TRUE(VrfFind("vrf8"));
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.1");
    AddFloatingIp("fip3", 3, "2.2.2.5");
    AddFloatingIp("fip4", 4, "2.2.2.1");
    client->WaitForIdle();
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip", "fip3", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    AddLink("floating-ip", "fip4", "floating-ip-pool", "fip-pool1");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip3");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip4");
    client->WaitForIdle();

    LOG(DEBUG, "Adding Floating-ip fip2");
    AddFloatingIp("fip2", 2, "2.2.2.2");
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    client->Reset();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    // Add vm-port interface to vrf link
    AddVmPortVrf("vnvrf1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vnvrf1",
            "routing-instance", "vrf6");
    AddLink("virtual-machine-interface-routing-instance", "vnvrf1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 3));

    LOG(DEBUG, "Link fip2 to fip-pool1");
    AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip3");
    DelLink("floating-ip", "fip3", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelNode("floating-ip", "fip3");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip4");
    DelLink("floating-ip", "fip4", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelNode("floating-ip", "fip4");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 2));

    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "vrf6");
    client->WaitForIdle();
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "vrf8");
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf6");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "vrf6");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "vrf8");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    client->WaitForIdle();
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "floating-ip-pool",
            "fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", input[0].name, "instance-ip", 
            "instance1");
    client->WaitForIdle();

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnvrf1",
            "routing-instance", "vrf6");
    DelLink("virtual-machine-interface-routing-instance", "vnvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnvrf1");
    client->WaitForIdle();

    DelNode("floating-ip", "fip1");
    client->WaitForIdle();
    DelNode("floating-ip", "fip2");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFloatingIpCount(1, 0));
    DelNode("floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelNode("routing-instance", "vrf6");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf6"));
    DelNode("routing-instance", "default-project:vn2:vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("default-project:vn2:vn2"));
    DelNode("routing-instance", "vrf8");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf8"));
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    DelNode("virtual-network", "default-project:vn2");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(2));
    DelNode("virtual-machine", "vm1");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    EXPECT_FALSE(VmFind(1));
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input, 0));
 

#if 0
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    LOG(DEBUG, "Cleanup implementation pending...");
    // Delete config vm entry - no-op for oper-db. Port is active
    client->Reset();
    DelNode("virtual-machine", "vm1");
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_FALSE(VmFind(1));
    EXPECT_TRUE(VmPortFind(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

    // Delete Nova Port entry.
    client->Reset();
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    EXPECT_TRUE(client->PortNotifyWait(2));
    EXPECT_FALSE(VmFind(1));
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_EQ(2U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    // Del VN to VRF link. Port should become inactive
    client->Reset();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf5");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
#endif
}

TEST_F(CfgTest, Basic_1) {
    string eth_intf = "eth10";
    string vrf_name = "__non_existent_vrf__";
    //char buff[4096];
    //int len = 0;
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 5, 5},
    };

    client->Reset();
    PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                            eth_intf, vrf_name, false);
    client->WaitForIdle();
    PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                            eth_intf, Agent::GetInstance()->fabric_vrf_name(),
                            false);
    client->WaitForIdle();
    InetInterface::CreateReq(Agent::GetInstance()->interface_table(),
                             "vhost10", InetInterface::VHOST,
                             Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address(0), 0, Ip4Address(0), eth_intf, "");

    client->WaitForIdle();

    AddVn("default-project:vn5", 5);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
    AddVm("vm5", 5);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(1));
    AddVrf("default-project:vn5:vn5");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("default-project:vn5:vn5"));
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "10.10.10.1");
    AddFloatingIp("fip2", 2, "2.2.2.2");
    AddFloatingIp("fip3", 3, "30.30.30.1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    client->WaitForIdle();

    AddPort(input[0].name, input[0].intf_id);
    client->WaitForIdle();
    AddLink("virtual-network", "default-project:vn5", "routing-instance",
            "default-project:vn5:vn5");
    client->WaitForIdle();
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn5");
    client->WaitForIdle();
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    AddLink("floating-ip", "fip3", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip3");
    client->WaitForIdle();
    AddLink("virtual-network", "default-project:vn5", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm5", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "default-project:vn5:vn5");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    client->WaitForIdle();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    AgentIntfSandesh *sand_1 = new AgentIntfSandesh("", "vnet1");
    sand_1->DoSandesh();
    client->WaitForIdle();

    AgentIntfSandesh *sand_2 = new AgentIntfSandesh("", "eth10");
    sand_2->DoSandesh();
    client->WaitForIdle();

    AgentIntfSandesh *sand_3 = new AgentIntfSandesh("", "pkt0");
    sand_3->DoSandesh();
    client->WaitForIdle();

    AgentIntfSandesh *sand_4 = new AgentIntfSandesh("", "vhost10");
    sand_4->DoSandesh();
    client->WaitForIdle();

    InetInterface::DeleteReq(Agent::GetInstance()->interface_table(),
                                    "vhost10");
    client->WaitForIdle();
    PhysicalInterface::DeleteReq(Agent::GetInstance()->interface_table(),
                            eth_intf);
    client->WaitForIdle();

    client->Reset();
    DelLink("virtual-network", "default-project:vn5", "routing-instance",
            "default-project:vn5:vn5");
    client->WaitForIdle();
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn5");
    client->WaitForIdle();
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("floating-ip", "fip3", "floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelNode("floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip3");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn5", "virtual-machine-interface",
            "vnet1");
    client->WaitForIdle();
    DelLink("virtual-machine", "vm5", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    
    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "default-project:vn5:vn5");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    client->WaitForIdle();

    client->Reset();
    IntfCfgDel(input, 0);
    DelPort(input[0].name);
    client->WaitForIdle();

    client->Reset();
    DelNode("floating-ip", "fip1");
    DelNode("floating-ip", "fip2");
    DelNode("floating-ip", "fip3");
    client->WaitForIdle();
    DelNode("virtual-machine", "vm5");
    client->WaitForIdle();
    DelNode("routing-instance", "default-project:vn5:vn5");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    DelNode("virtual-network", "default-project:vn5");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->vm_table()->Size()));
    WAIT_FOR(1000, 1000, (VnFind(5) == false));
    WAIT_FOR(1000, 1000, (VmFind(5) == false));
}

TEST_F(CfgTest, Basic_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    VmInterface *intf = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL) {
        return;
    }

    Inet4UnicastAgentRouteTable *table = 
        Agent::GetInstance()->fabric_inet4_unicast_table();
    Inet4UnicastRouteEntry *rt = static_cast<Inet4UnicastRouteEntry *>
        (table->FindRoute(intf->mdata_ip_addr()));
    EXPECT_TRUE(rt != NULL);
    if (rt == NULL) {
        return;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL) {
        return;
    }
    EXPECT_TRUE(nh->PolicyEnabled());

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");

    table = static_cast<Inet4UnicastAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->GetInet4UnicastRouteTable("vrf1"));
    rt = table->FindRoute(addr); 

    EXPECT_TRUE(rt != NULL);
    if (rt == NULL) {
        return;
    }

    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL) {
        return;
    }
    EXPECT_FALSE(nh->PolicyEnabled());

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

TEST_F(CfgTest, SecurityGroup_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddSg("sg1", 1);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    client->WaitForIdle();

    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    VmInterface *intf = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL) {
        return;
    }
    EXPECT_TRUE(intf->sg_list().list_.size() == 1);
    DoInterfaceSandesh("vnet1");

    Ip4Address addr(Ip4Address::from_string("1.1.1.1"));
    Inet4UnicastAgentRouteTable *table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->GetInet4UnicastRouteTable("vrf1"));
    Inet4UnicastRouteEntry *rt = table->FindRoute(addr); 
    EXPECT_TRUE(rt != NULL);
    if (rt == NULL) {
        return;
    }

    const AgentPath *path = rt->GetActivePath();
    EXPECT_EQ(path->sg_list().size(), 1);
    EXPECT_TRUE(path->vxlan_id() == VxLanTable::kInvalidvxlan_id);
    EXPECT_TRUE(path->tunnel_bmap() == TunnelType::MplsType());
    DoInterfaceSandesh("vnet1");

    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "security-group", "acl1");
    client->WaitForIdle();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    DelNode("security-group", "sg1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

TEST_F(CfgTest, SecurityGroup_ignore_invalid_sgid_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddSg("sg1", 1, 0);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    client->WaitForIdle();

    //Query for SG
    SgKey *key = new SgKey(MakeUuid(1));
    const SgEntry *sg_entry =
        static_cast<const SgEntry *>(Agent::GetInstance()->sg_table()->
        FindActiveEntry(key));
    EXPECT_TRUE(sg_entry == NULL);

    //Modify SGID
    AddSg("sg1", 1, 2);
    client->WaitForIdle();
    key = new SgKey(MakeUuid(1));
    sg_entry = static_cast<const SgEntry *>(Agent::GetInstance()->sg_table()->
                                            FindActiveEntry(key));

    EXPECT_TRUE(sg_entry != NULL);
    EXPECT_TRUE(sg_entry->GetSgId() == 2);

    // Try modifying with another sg id for same uuid and it should not happen
    // in oper. Old sgid i.e. 2 shud be retained.
    AddSg("sg1", 1, 3);
    client->WaitForIdle();
    key = new SgKey(MakeUuid(1));
    sg_entry = static_cast<const SgEntry *>(Agent::GetInstance()->sg_table()->
                                            FindActiveEntry(key));

    EXPECT_TRUE(sg_entry != NULL);
    EXPECT_TRUE(sg_entry->GetSgId() == 2);

    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "security-group", "acl1");
    client->WaitForIdle();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    DelNode("security-group", "sg1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

// Test invalid sgid with interface update
TEST_F(CfgTest, SecurityGroup_ignore_invalid_sgid_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    AddSg("sg1", 1, 0);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    client->WaitForIdle();

    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    VmInterface *intf = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL) {
        return;
    }
    EXPECT_TRUE(intf->sg_list().list_.size() == 0);

    // Add with proper sg id
    AddSg("sg1", 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(intf->sg_list().list_.size() == 1);
    VmInterface::SecurityGroupEntrySet::const_iterator it =
        intf->sg_list().list_.begin();
    EXPECT_TRUE(it->sg_.get() != NULL);
    EXPECT_TRUE(it->sg_->GetSgId() == 1);

    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "access-control-list", "acl1");
    DelLink("virtual-machine-interface", "vnet1", "security-group", "acl1");
    client->WaitForIdle();
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    DelNode("security-group", "sg1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;

    return ret;
}
