/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/vxlan.h>
#include <oper/interface_common.h>
#include <oper/global_vrouter.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;
using namespace boost::assign;

bool CheckVnAdd(int vn_id, int timeout) {
    VnEntry *vn = NULL;
    while (timeout > 0) {
        client->WaitForIdle();
        vn = VnGet(vn_id);
        if (vn != NULL)
            return true;
        timeout--;
    }
    EXPECT_TRUE(vn != NULL);
    return false;
}

bool CheckVnDel(int vn_id, int timeout) {
    VnEntry *vn = NULL;
    while (timeout > 0) {
        client->WaitForIdle();
        vn = VnGet(vn_id);
        if (vn == NULL)
            return true;
        timeout--;
    }
    EXPECT_TRUE(vn == NULL);
    return false;
}

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class CfgTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
    }

    virtual void TearDown() {
        DelNode("global-vrouter-config", "vrouter-config");
        client->WaitForIdle();
    }

private:
    Agent *agent_;

};

#if 0
Fails in IFMapDependencyTracker::PropagateEdge (IFMapDependencyTracker) issue
TEST_F(CfgTest, VnBasic_1) {
    char buff[4096];
    int len = 0;

    IpamInfo ipam_info[] = {
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    IpamInfo ipam_updated_info[] = {
        {"4.2.3.128", 27, "4.2.3.129", true},
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"3.3.3.0", 24, "3.3.3.12", true},
    };

    client->WaitForIdle();
    //Test for no node and link present
    memset(buff, 0, 4096);
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1);
    AddNodeString(buff, len, "virtual-machine", "vm1", 1);
    AddNodeString(buff, len, "virtual-machine-interface", "vnet2", 1);
    LinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet3");
    LinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    LinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    AddNodeString(buff, len, "virtual-network-network-ipam", "default-network-ipam,vn1", ipam_info, 3);
    LinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnAdd(1, 1);
    //Try changing key of VN
    VnEntry *vn = VnGet(1);
    VnKey *oldKey =
           new VnKey((static_cast<VnKey*>((vn->GetDBRequestKey()).get()))->uuid_);
    VnKey *newKey = new VnKey(MakeUuid(200));
    string s1;
    string s2;

    vn->SetKey(static_cast<DBRequestKey*>(newKey));
    s1 = UuidToString(oldKey->uuid_);
    s2 = vn->ToString();
    EXPECT_FALSE(s1.compare(s2) == 0);
    vn->SetKey(static_cast<DBRequestKey*>(oldKey));
    s2 = vn->ToString();
    EXPECT_TRUE(s1.compare(s2) == 0);


    // Send updated Ipam
    memset(buff, 0, 4096);
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1);
    AddNodeString(buff, len, "virtual-network-network-ipam", "default-network-ipam,vn1", ipam_updated_info, 3);
    LinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    //Mock the sandesh request, no expecatation just catch crashes.
    VnListReq *vn_list_req = new VnListReq();
    std::vector<int> result ={1};
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vn_list_req->set_name("vn1");
    vn_list_req->HandleRequest();
    client->WaitForIdle();
    vn_list_req->set_name("vn10");
    vn_list_req->HandleRequest();
    client->WaitForIdle();
    vn_list_req->Release();

    memset(buff, 0, 4096);
    DelXmlHdr(buff, len);
    LinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    LinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    LinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet3");
    LinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
    DelNodeString(buff, len, "virtual-machine-interface", "vnet2");
    DelNodeString(buff, len, "virtual-machine", "vm1");
    DelNodeString(buff, len, "virtual-network-network-ipam", "default-network-ipam,vn1");
    DelNodeString(buff, len, "virtual-network", "vn1");
    DelXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnDel(1, 1);
    delete(oldKey);
    delete(newKey);

    // Verify helper add/del routines for crashes
    VnAddReq(1, "vntest");
    VnDelReq(1);
}
#endif

TEST_F(CfgTest, VnDepOnVrfAcl_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1);
    AddNodeString(buff, len, "virtual-network", "vn2", 2);
    AddNodeString(buff, len, "routing-instance", "vrf6", 1);
    AddNodeString(buff, len, "access-control-list", "acl1", 1);
    LinkString(buff, len, "virtual-network", "vn1", "routing-instance", "vrf6");
    LinkString(buff, len, "access-control-list", "acl1", "virtual-network",
                  "vn1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnAdd(1, 1);
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vn->GetAcl() != NULL);

    AddXmlHdr(buff, len);
    DelLink("virtual-network", "vn1", "routing-instance", "vrf6");
    DelLink("access-control-list", "acl1", "virtual-network", "vn1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    client->WaitForIdle();

    DelXmlHdr(buff, len);
    DelNodeString(buff, len, "routing-instance", "vrf6");
    DelNodeString(buff, len, "virtual-network", "vn1");
    DelNodeString(buff, len, "virtual-network", "vn2");
    DelNodeString(buff, len, "access-control-list", "acl1");
    DelXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnDel(1, 1);
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VnFind(2));
    EXPECT_FALSE(AclFind(1));
    WAIT_FOR(1000, 1000, (VrfFind("vrf6") == false));
}

TEST_F(CfgTest, VrfChangeVxlanTest) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    AddXmlHdr(buff, len);
    LinkString(buff, len, "virtual-network", "vn1", "routing-instance", "vrf1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnAdd(1, 1);
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vn->vxlan_id_ref() != NULL);

    AddXmlHdr(buff, len);
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();

    EXPECT_TRUE(vn->GetVrf() == NULL);
    EXPECT_TRUE(vn->vxlan_id_ref() == NULL);


    DelVn("vn1");
    DelVrf("vrf1");
    client->WaitForIdle();

    CheckVnDel(1, 1);
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VrfFind("vrf1"));
}

TEST_F(CfgTest, mcast_mpls_label_check) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    BridgeRouteEntry *l2_flood_rt = L2RouteGet("vrf1",
                                               MacAddress::BroadcastMac());
    uint32_t label = l2_flood_rt->GetActiveLabel();
    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->FindMplsLabel(label) != NULL);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->FindMplsLabel(label) == NULL);
    client->WaitForIdle();
}

TEST_F(CfgTest, Global_vxlan_network_identifier_mode_config) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    VnEntry *vn = VnGet(1);
    //Auto vxlan id as mode is automatic
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    std::stringstream str;

    //Set to configured
    str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVxLanId() == 101);

    //Any other string than configured/automatic should default to automatic
    str << "<vxlan-network-identifier-mode>junk</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    //Set to configured and then delete node
    str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    DelNode("global-vrouter-config", "vrouter-config");
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    client->WaitForIdle();
}

TEST_F(CfgTest, Global_vxlan_network_identifier_mode_config_sandesh) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    VnEntry *vn = VnGet(1);
    //Auto vxlan id as mode is automatic
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    std::stringstream str;

    //Set to configured
    str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVxLanId() == 101);
    VxLanReq *vxlan_req = new VxLanReq();
    std::vector<int> result = {1};
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vxlan_req->HandleRequest();
    client->WaitForIdle();
    vxlan_req->Release();
    client->WaitForIdle();

    client->WaitForIdle();
    DelNode("global-vrouter-config", "vrouter-config");
    client->WaitForIdle();
    EXPECT_TRUE(vn->GetVxLanId() == 1);
    vxlan_req = new VxLanReq();
    std::vector<int> result_after_delete = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1,
                                               result_after_delete));
    vxlan_req->HandleRequest();
    client->WaitForIdle();
    vxlan_req->Release();
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, vn_forwarding_mode_changed_0) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, vn_forwarding_mode_changed_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    string vrf_name = "vrf1";
    MacAddress vxlan_vm_mac("00:00:01:01:01:10");
    MacAddress vxlan_flood_mac = MacAddress::BroadcastMac();
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address subnet_ip = Ip4Address::from_string("1.1.1.255");
    Ip4Address flood_ip = Ip4Address::from_string("255.255.255.255");
    CompositeNH *cnh = NULL;

    //By default l2_l3 mode
    BridgeRouteEntry *l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    BridgeRouteEntry *l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    InetUnicastRouteEntry *uc_rt = RouteGet(vrf_name, vm_ip, 32);
    InetUnicastRouteEntry *subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    Inet4MulticastRouteEntry *flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt != NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    //Move to l2 mode
    ModifyForwardingModeVn("vn1", 1, "l2");
    client->WaitForIdle(5);
    l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    uc_rt = RouteGet(vrf_name, vm_ip, 32);
    subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt == NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Fabric COMP + Interface COMP + EVPN comp NH
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    //Move to l2_l3 mode
    ModifyForwardingModeVn("vn1", 1, "l2_l3");
    client->WaitForIdle();
    l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    uc_rt = RouteGet(vrf_name, vm_ip, 32);
    subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt != NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Fabric COMP + EVPN comp + Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, vn_forwarding_mode_changed_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    CreateL2VmportEnv(input, 1);
    client->WaitForIdle(3);
    EXPECT_TRUE(VmPortL2Active(input, 0));
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle(3);
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    string vrf_name = "vrf1";
    MacAddress vxlan_vm_mac("00:00:01:01:01:10");
    MacAddress vxlan_flood_mac = MacAddress::BroadcastMac();
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address subnet_ip = Ip4Address::from_string("1.1.1.255");
    Ip4Address flood_ip = Ip4Address::from_string("255.255.255.255");
    CompositeNH *cnh = NULL;

    BridgeRouteEntry *l2_uc_rt;
    BridgeRouteEntry *l2_flood_rt;
    InetUnicastRouteEntry *uc_rt;
    InetUnicastRouteEntry *subnet_rt;
    Inet4MulticastRouteEntry *flood_rt;

    //default to l2 mode
    client->WaitForIdle();
    l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    uc_rt = RouteGet(vrf_name, vm_ip, 32);
    subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt == NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Fabric COMP + EVPN comp + Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    //Move to l2_l3 mode
    ModifyForwardingModeVn("vn1", 1, "l2_l3");
    client->WaitForIdle();
    l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    uc_rt = RouteGet(vrf_name, vm_ip, 32);
    subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt != NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Fabric COMP + EVPN comp + Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    //Move back to l2
    ModifyForwardingModeVn("vn1", 1, "l2");
    client->WaitForIdle();
    l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    uc_rt = RouteGet(vrf_name, vm_ip, 32);
    subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt == NULL);
    EXPECT_TRUE(subnet_rt == NULL);
    EXPECT_TRUE(flood_rt == NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Fabric COMP + EVPN comp + Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    client->Reset();

    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

// check that setting vn admin state to false makes the vm ports inactive
TEST_F(CfgTest, vn_admin_state_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
        {"vnet2", 2, "2.2.2.20", "00:00:02:02:02:20", 1, 2},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.0", 24, "2.2.2.200", true},
    };

    client->Reset();
    // vm admin_state is set to true in the call below
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    // set the vm admin_state to false and check that vms are inactive
    CreateVmportEnv(input, 2, 0, NULL, NULL, NULL, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
    EXPECT_FALSE(VmPortActive(input, 1));

    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
}

// check that setting vn admin state to false makes the vm ports inactive
TEST_F(CfgTest, vn_admin_state_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
        {"vnet2", 2, "2.2.2.20", "00:00:02:02:02:20", 1, 2},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.0", 24, "2.2.2.200", true},
    };

    client->Reset();
    // vm admin_state is set to false in the call below
    CreateVmportEnv(input, 2, 0, NULL, NULL, NULL, false);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
    EXPECT_FALSE(VmPortActive(input, 1));

    // set the vm admin_state to false and check that vms are inactive
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    CreateVmportEnv(input, 2, 0, NULL, NULL, NULL, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
    EXPECT_FALSE(VmPortActive(input, 1));

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
}

TEST_F(CfgTest, change_in_gateway) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    IpamInfo ipam_info_2[] = {
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.200", 32)));

    //Change IPAM
    AddIPAM("vn1", ipam_info_2, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.100", 32)));

    EXPECT_TRUE(!RouteFind("vrf1", "11.1.1.200", 32));
    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(CfgTest, change_in_gatewaywith_no_vrf) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    IpamInfo ipam_info_2[] = {
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.200", 32)));

    DelVrf("vrf1");
    client->WaitForIdle();

    //Change IPAM
    AddIPAM("vn1", ipam_info_2, 1);
    client->WaitForIdle();

    AddVrf("vrf1", 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.100", 32)));
    EXPECT_TRUE(!RouteFind("vrf1", "11.1.1.200", 32));

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(CfgTest, l2_mode_configured_via_ipam_non_linklocal_gw) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "169.253.0.1", true},
    };

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    VnEntry *vn = VnGet(1);;
    EXPECT_TRUE(vn->layer3_forwarding());
    EXPECT_TRUE(vn->bridging());

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(CfgTest, RpfEnableDisable) {
    AddVn("vn10", 10, true);
    client->WaitForIdle();

    VnEntry *vn = VnGet(10);
    EXPECT_TRUE(vn->enable_rpf());

    DisableRpf("vn10", 10);
    EXPECT_FALSE(vn->enable_rpf());

    EnableRpf("vn10", 10);
    EXPECT_TRUE(vn->enable_rpf());

    DelVn("vn10");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VnGet(10) == NULL));
}

TEST_F(CfgTest, UnknownBroadcastEnableDisable_1) {
    AddVn("vn10", 10, true);
    AddVrf("vrf10");
    AddLink("virtual-network", "vn10", "routing-instance", "vrf10");
    client->WaitForIdle();

    VnEntry *vn = VnGet(10);
    EXPECT_TRUE(vn->flood_unknown_unicast() == false);

    VrfNHKey key("vrf10", false, true);
    const VrfNH *vrf_nh = static_cast<const VrfNH *>(GetNH(&key));
    EXPECT_TRUE(vrf_nh->flood_unknown_unicast() == false);

    EnableUnknownBroadcast("vn10", 10);
    EXPECT_TRUE(vn->flood_unknown_unicast());
    EXPECT_TRUE(vrf_nh->flood_unknown_unicast() == true);

    DisableUnknownBroadcast("vn10", 10);
    EXPECT_FALSE(vn->flood_unknown_unicast());
    EXPECT_TRUE(vrf_nh->flood_unknown_unicast() == false);

    DelLink("virtual-network", "vn10", "routing-instance", "vrf10");
    client->WaitForIdle();
    DelVrf("vrf10");
    DelVn("vn10");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VnGet(10) == NULL));
    WAIT_FOR(1000, 1000, (VrfFind("vrf10") == false));
}

TEST_F(CfgTest, UnknownBroadcastEnableDisable_2) {
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:01:01:01:10", 10, 10},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(10));
    EXPECT_FALSE(intf->flood_unknown_unicast());

    EnableUnknownBroadcast("vn10", 10);
    EXPECT_TRUE(intf->flood_unknown_unicast());

    DisableUnknownBroadcast("vn10", 10);
    EXPECT_FALSE(intf->flood_unknown_unicast());

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}


TEST_F(CfgTest, CfgUuidNullDelete) {
    client->WaitForIdle();
    // Add VN with UUID
    AddVn("vn1", 1);
    client->WaitForIdle();

    VnEntry *vn = VnGet(1);
    ASSERT_TRUE(vn != NULL);

    //Get the config table
    IFMapTable *table =
        IFMapTable::FindTable(Agent::GetInstance()->db(),"virtual-network");
    ASSERT_TRUE(table != NULL);

    //Get the config node
    IFMapNode *node = table->FindNode("vn1");
    ASSERT_TRUE(node != NULL);

    //Ensure that Oper exists
    ASSERT_TRUE(VnFind(1));

    //We no longer consider absence of id-perms as a delete trigger.
    //So modifying the following to DelVn
    //Send Uuid zero and verify that node is deleted
    //AddVn("vn1", 0);
    DelVn("vn1");
    client->WaitForIdle();

    node = table->FindNode("vn1");
    ASSERT_TRUE(node == NULL);

    //Ensure that Oper is deleted
    ASSERT_FALSE(VnFind(1));
}


TEST_F(CfgTest, CfgUuidChange) {
    client->WaitForIdle();
    // Add VN with UUID
    AddVn("vn1", 1);
    client->WaitForIdle();

    VnEntry *vn = VnGet(1);
    ASSERT_TRUE(vn != NULL);

    //Get the config table
    IFMapTable *table =
        IFMapTable::FindTable(Agent::GetInstance()->db(),"virtual-network");
    ASSERT_TRUE(table != NULL);

    //Get the config node
    IFMapNode *node = table->FindNode("vn1");
    ASSERT_TRUE(node != NULL);

    //Ensure that Oper exists
    ASSERT_TRUE(VnFind(1));

    //Send different Uuid
    AddVn("vn1", 2);
    client->WaitForIdle();

    node = table->FindNode("vn1");
    ASSERT_TRUE(node != NULL);

    //Ensure that Oper exists
    ASSERT_TRUE(VnFind(2));

    //Ensure that Oper does not exists
    ASSERT_FALSE(VnFind(1));

    DelVn("vn1");
    client->WaitForIdle();

    node = table->FindNode("vn1");
    ASSERT_FALSE(node);

    //Ensure that Oper deleted
    ASSERT_FALSE(VnFind(1));
    ASSERT_FALSE(VnFind(2));

}

TEST_F(CfgTest, multicast_fabric_routes) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.200", 32)));
    EXPECT_FALSE(RouteFind("vrf1", "224.0.0.0", 8));
    EXPECT_FALSE(RouteFindV6("vrf1", "ff00::", 8));
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(),
                          "224.0.0.0", 8));
    EXPECT_TRUE(RouteFindV6(Agent::GetInstance()->fabric_vrf_name(),
                            "ff00::", 8));

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

// Check that flat subnet config in an IPAM is used to update VN entries
TEST_F(CfgTest, flat_subnet_config) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "12.1.1.1", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    char ipam_attr[] = "<network-ipam-mgmt>\n "
                            "<ipam-dns-method>default-dns-server</ipam-dns-method>\n "
                       "</network-ipam-mgmt>\n "
                       "<ipam-subnet-method>flat-subnet</ipam-subnet-method>\n "
                       "<ipam-subnets>\n "
                           "<subnets>\n "
                               "<subnet>\n "
                                   "<ip-prefix>12.1.0.0</ip-prefix>\n "
                                   "<ip-prefix-len>16</ip-prefix-len>\n "
                               "</subnet>\n "
                               "<default-gateway>12.1.0.1</default-gateway>\n "
                               "<dns-server-address>12.1.0.2</dns-server-address>\n "
                               "<enable-dhcp>true</enable-dhcp>\n "
                           "</subnets>\n "
                       "</ipam-subnets>\n ";
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1, ipam_attr);
    client->WaitForIdle();

    CheckVnAdd(1, 1);
    VnEntry *vn = VnGet(1);
    const std::vector<VnIpam> vn_ipam = vn->GetVnIpam();
    EXPECT_TRUE(vn_ipam[0].ip_prefix.to_string() == "12.1.0.0");
    EXPECT_TRUE(vn_ipam[0].plen == 16);
    EXPECT_TRUE(vn_ipam[0].default_gw.to_string() == "12.1.0.1");
    EXPECT_TRUE(vn_ipam[0].dns_server.to_string() == "12.1.0.2");
    EXPECT_TRUE(vn_ipam[0].dhcp_enable);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
