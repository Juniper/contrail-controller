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
#include <oper/interface_common.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;
using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

class CfgTest : public ::testing::Test {
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

void CheckVmAdd(int vm_id, int timeout) {
    VmEntry *vm = NULL;
    while (timeout > 0) {
        client->WaitForIdle();
        vm = VmGet(vm_id);
        if (vm != NULL)
            return;
        timeout--;
    }
    EXPECT_TRUE(vm != NULL);
}

void CheckVmDel(int vm_id, int timeout) {
    VmEntry *vm = NULL;
    while (timeout > 0) {
        client->WaitForIdle();
        vm = VmGet(vm_id);
        if (vm == NULL)
            return;
        timeout--;
    }
    EXPECT_TRUE(vm == NULL);
}

TEST_F(CfgTest, VmBasic_1) {
    char buff[4096];
    int len = 0;

    //Test for no node and link present
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddNodeString(buff, len, "virtual-machine", "vm1", 1);
    AddNodeString(buff, len, "virtual-machine-interface", "vnet2", 1);
    AddLinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    CheckVmAdd(1, 1);
    
    //Try changing key of VN
    VmEntry *vm = VmGet(1);
    VmKey *oldKey = 
           new VmKey((static_cast<VnKey*>((vm->GetDBRequestKey()).get()))->uuid_);
    VmKey *newKey = new VmKey(MakeUuid(200));
    string s1;
    string s2;

    vm->SetKey(static_cast<DBRequestKey*>(newKey));
    s1 = UuidToString(oldKey->uuid_);
    s2 = vm->ToString();
    EXPECT_FALSE(s1.compare(s2) == 0);
    vm->SetKey(static_cast<DBRequestKey*>(oldKey));
    s2 = vm->ToString();
    EXPECT_TRUE(s1.compare(s2) == 0);

    //Mock the sandesh request, no expecatation just catch crashes.
    VmListReq *vm_list_req = new VmListReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vm_list_req->set_uuid(UuidToString(MakeUuid(1)));
    vm_list_req->HandleRequest();
    client->WaitForIdle();
    vm_list_req->set_uuid(UuidToString(MakeUuid(10)));
    vm_list_req->HandleRequest();
    client->WaitForIdle();
    vm_list_req->Release();

    DelXmlHdr(buff, len);
    DelLinkString(buff, len, "virtual-machine", "vm1", "virtual-machine-interface", "vnet2");
    DelNodeString(buff, len, "virtual-machine-interface", "vnet2");
    DelNodeString(buff, len, "virtual-machine", "vm1");
    DelNodeString(buff, len, "virtual-network", "vn1");
    DelXmlTail(buff, len);
    ApplyXmlString(buff);
    CheckVmDel(1, 1);

    delete(oldKey);
    delete(newKey);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
