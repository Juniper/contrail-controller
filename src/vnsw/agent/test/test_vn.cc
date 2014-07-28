/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
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
};

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
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet3");
    AddLinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    AddNodeString(buff, len, "virtual-network-network-ipam", "default-network-ipam,vn1", ipam_info, 3);
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
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
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    
    //Mock the sandesh request, no expecatation just catch crashes.
    VnListReq *vn_list_req = new VnListReq();
    std::vector<int> result = list_of(1);
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
    DelLinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    DelLinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    DelLinkString(buff, len, "virtual-network", "vn1", "virtual-machine-interface", "vnet3");
    DelLinkString(buff, len, "virtual-network", "vn1", "virtual-network-network-ipam", "default-network-ipam,vn1");
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

TEST_F(CfgTest, VnDepOnVrfAcl_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1);
    AddNodeString(buff, len, "virtual-network", "vn2", 2);
    AddNodeString(buff, len, "routing-instance", "vrf6", 1);
    AddNodeString(buff, len, "access-control-list", "acl1", 1);
    AddLinkString(buff, len, "virtual-network", "vn1", "routing-instance", "vrf6");
    AddLinkString(buff, len, "access-control-list", "acl1", "virtual-network", 
                  "vn1");
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-network", "vn2");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    CheckVnAdd(1, 1);
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVrf() != NULL);
    EXPECT_TRUE(vn->GetAcl() != NULL);

    AddXmlHdr(buff, len);
    DelLink("virtual-network", "vn1", "routing-instance", "vrf6");
    DelLink("access-control-list", "acl1", "virtual-network", "vn1");
    DelLink("virtual-network", "vn1", "virtual-network", "vn2");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);

    client->WaitForIdle();

    //vn = VnGet(1);
    //EXPECT_TRUE(vn->GetVrf() == NULL);
    //EXPECT_TRUE(vn->GetAcl() == NULL);

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

}

TEST_F(CfgTest, VrfChangeVxlanTest) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    AddXmlHdr(buff, len);
    AddLinkString(buff, len, "virtual-network", "vn1", "routing-instance", "vrf1");
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
    std::vector<int> result = list_of(1);
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
    struct ether_addr vxlan_vm_mac;
    memcpy(&vxlan_vm_mac, ether_aton("00:00:01:01:01:10"), sizeof(struct ether_addr));
    struct ether_addr vxlan_flood_mac;
    memcpy(&vxlan_flood_mac, ether_aton("ff:ff:ff:ff:ff:ff"), sizeof(struct ether_addr));
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address subnet_ip = Ip4Address::from_string("1.1.1.255");
    Ip4Address flood_ip = Ip4Address::from_string("255.255.255.255");
    CompositeNH *cnh = NULL;

    //By default l2_l3 mode
    Layer2RouteEntry *l2_uc_rt = L2RouteGet(vrf_name, vxlan_vm_mac);
    Layer2RouteEntry *l2_flood_rt = L2RouteGet(vrf_name, vxlan_flood_mac);
    Inet4UnicastRouteEntry *uc_rt = RouteGet(vrf_name, vm_ip, 32);
    Inet4UnicastRouteEntry *subnet_rt = RouteGet(vrf_name, subnet_ip, 32);
    Inet4MulticastRouteEntry *flood_rt = MCRouteGet(vrf_name, flood_ip);
    EXPECT_TRUE(l2_uc_rt != NULL);
    EXPECT_TRUE(l2_flood_rt != NULL);
    EXPECT_TRUE(uc_rt != NULL);
    EXPECT_TRUE(subnet_rt != NULL);
    EXPECT_TRUE(flood_rt != NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) flood_rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) subnet_rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    //Move to l2 mode
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
    //Interface COMP
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
    EXPECT_TRUE(subnet_rt != NULL);
    EXPECT_TRUE(flood_rt != NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) flood_rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) subnet_rt->GetActiveNextHop());
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
    client->WaitForIdle();
    EXPECT_TRUE(VmPortL2Active(input, 0));
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetVxLanId() == 1);

    string vrf_name = "vrf1";
    struct ether_addr vxlan_vm_mac;
    memcpy(&vxlan_vm_mac, ether_aton("00:00:01:01:01:10"), sizeof(struct ether_addr));
    struct ether_addr vxlan_flood_mac;
    memcpy(&vxlan_flood_mac, ether_aton("ff:ff:ff:ff:ff:ff"), sizeof(struct ether_addr));
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    Ip4Address subnet_ip = Ip4Address::from_string("1.1.1.255");
    Ip4Address flood_ip = Ip4Address::from_string("255.255.255.255");
    CompositeNH *cnh = NULL;

    Layer2RouteEntry *l2_uc_rt;
    Layer2RouteEntry *l2_flood_rt;
    Inet4UnicastRouteEntry *uc_rt;
    Inet4UnicastRouteEntry *subnet_rt;
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
    //Interface COMP nh
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
    EXPECT_TRUE(subnet_rt != NULL);
    EXPECT_TRUE(flood_rt != NULL);
    EXPECT_TRUE(l2_uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(l2_flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(flood_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    cnh = ((CompositeNH *) l2_flood_rt->GetActiveNextHop());
    //Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) flood_rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    cnh = ((CompositeNH *) subnet_rt->GetActiveNextHop());
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
    //Interface COMP
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);
    client->Reset();

    DelIPAM("vn1"); 
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
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

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
