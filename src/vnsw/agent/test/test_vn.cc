/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <cfg/init_config.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap_agent_parser.h>
#include <ifmap_agent_table.h>
#include <cfg/interface_cfg.h>
#include <cfg/init_config.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface.h>

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

void RouterIdDepInit() {
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
        {"1.2.3.128", 27, "1.2.3.129"},
        {"7.8.9.0", 24, "7.8.9.12"},
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    IpamInfo ipam_updated_info[] = {
        {"4.2.3.128", 27, "4.2.3.129"},
        {"1.1.1.0", 24, "1.1.1.200"},
        {"3.3.3.0", 24, "3.3.3.12"},
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
    s1 = boost::lexical_cast<std::string>(oldKey->uuid_);
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

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
