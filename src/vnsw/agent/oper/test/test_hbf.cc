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
#include "kstate/test/test_kstate.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

TestVrfKState *TestVrfKState::singleton_;
int TestKStateBase::handler_count_;
int TestKStateBase::fetched_count_;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

void DoInterfaceSandesh(std::string name) {
    ItfReq *itf_req = new ItfReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse,
                                               _1, result));
    if (name != "") {
        itf_req->set_name(name);
    }
    itf_req->HandleRequest();
    client->WaitForIdle();
    itf_req->Release();
    client->WaitForIdle();
}

class HbfTest : public ::testing::Test {
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

    unsigned int intf_count;
    Agent *agent;
};

#define MAX_TESTNAME_LEN 80
#define MAX_VNET 10
int tap_fd[MAX_VNET];
static void CreateCustomerVMIInternal(struct PortInfo *input, int count,
                     int acl_id, const char* proj, const char *vn,
                     const char *vrf, const char *vm_interface_attr,
                     bool l2_vn, bool with_ip, bool ecmp,
                     bool vn_admin_state, bool with_ip6, bool send_nova_msg) {
    char proj_name[MAX_TESTNAME_LEN];
    char vn_name[MAX_TESTNAME_LEN];
    char vm_name[MAX_TESTNAME_LEN];
    char vrf_name[MAX_TESTNAME_LEN];
    char acl_name[MAX_TESTNAME_LEN];
    char instance_ip[MAX_TESTNAME_LEN];
    char instance_ip6[MAX_TESTNAME_LEN];

    if (client->agent_init()->ksync_enable()) {
        CreateTapInterfaces("vnet", count, tap_fd);
    }

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
        AddAcl(acl_name, acl_id);
    }

    // Add Project node
    if (proj)
        strncpy(proj_name, proj, MAX_TESTNAME_LEN);
    else
        sprintf(proj_name, "default-domain:default-project");
    AddNode("project", proj_name, 1);

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "default-domain:default-project:vn%d",
                    input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "default-domain:default-project:vn%d:vn%d",
                    input[i].vn_id, input[i].vn_id);

        sprintf(vm_name, "vm%d", input[i].vm_id);
        sprintf(instance_ip, "instance%d", input[i].intf_id);

        if (!l2_vn) {
            AddVn(vn_name, input[i].vn_id, vn_admin_state);
            AddVrf(vrf_name);
        }
        AddVm(vm_name, input[i].vm_id);
        AddVmPortVrf(input[i].name, "", 0);

        if (send_nova_msg) {
            IntfCfgAddThrift(input, i);
        }
        AddPortWithMac(input[i].name, input[i].intf_id,
                       input[i].mac, vm_interface_attr);
        if (with_ip) {
            if (ecmp) {
                AddActiveActiveInstanceIp(instance_ip, input[i].intf_id,
                                          input[i].addr);
            } else {
                AddInstanceIp(instance_ip, input[i].intf_id, input[i].addr);
            }
        }
        if (with_ip6) {
            sprintf(instance_ip6, "instance6%d", input[i].intf_id);
            if (ecmp) {
                AddActiveActiveInstanceIp(instance_ip6, input[i].intf_id,
                                          input[i].ip6addr);
            } else {
                AddInstanceIp(instance_ip6, input[i].intf_id, input[i].ip6addr);
            }
        }
        if (!l2_vn) {
            AddLink("virtual-network", vn_name, "routing-instance", vrf_name);
            client->WaitForIdle();
        }
        AddLink("virtual-machine-interface", input[i].name, "virtual-machine",
                vm_name);
        AddLink("virtual-machine-interface", input[i].name,
                "virtual-network", vn_name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "routing-instance", vrf_name,
                "virtual-machine-interface-routing-instance");
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "virtual-machine-interface", input[i].name,
                "virtual-machine-interface-routing-instance");
        if (with_ip) {
            AddLink("instance-ip", instance_ip,
                    "virtual-machine-interface", input[i].name);
        }
        if (with_ip6) {
            AddLink("instance-ip", instance_ip6,
                    "virtual-machine-interface", input[i].name);
        }

        // Add VMI --> Project link
        AddLink("virtual-machine-interface", input[i].name,
                "project", proj_name);

        if (acl_id) {
            AddLink("virtual-network", vn_name,
                    "access-control-list", acl_name);
        }
    }
}

void CreateCustomerVMI(struct PortInfo *input, int count, int acl_id,
                     const char* proj, const char *vn, const char *vrf,
                     const char *vm_interface_attr = NULL,
                     bool vn_admin_state = true) {
    CreateCustomerVMIInternal(input, count, acl_id, proj, vn, vrf,
                            vm_interface_attr, false, true, false,
                            vn_admin_state, false, true);
}

void DeleteCustomerVMI(struct PortInfo *input, int count,
                     bool del_vn, int acl_id,
                     const char* proj, const char *vn, const char *vrf,
                     bool with_ip=true, bool with_ip6=false) {
    char vn_name[MAX_TESTNAME_LEN];
    char vm_name[MAX_TESTNAME_LEN];
    char vrf_name[MAX_TESTNAME_LEN];
    char acl_name[MAX_TESTNAME_LEN];
    char proj_name[MAX_TESTNAME_LEN];
    char instance_ip[MAX_TESTNAME_LEN];
    char instance_ip6[MAX_TESTNAME_LEN];

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
    }

    if (proj)
        strncpy(proj_name, proj, MAX_TESTNAME_LEN);
    else
        sprintf(proj_name, "default-domain:default-project");

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "default-domain:default-project:vn%d",
                    input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "default-domain:default-project:vn%d:vn%d",
                    input[i].vn_id, input[i].vn_id);

        sprintf(vm_name, "vm%d", input[i].vm_id);
        sprintf(instance_ip, "instance%d", input[i].intf_id);
        boost::system::error_code ec;
        DelLink("virtual-machine-interface-routing-instance", input[i].name,
                "routing-instance", vrf_name);
        DelLink("virtual-machine-interface-routing-instance", input[i].name,
                "virtual-machine-interface", input[i].name);
        DelLink("virtual-machine", vm_name, "virtual-machine-interface",
                input[i].name);
        DelLink("virtual-machine-interface", input[i].name, "instance-ip",
                instance_ip);
        DelLink("virtual-network", vn_name, "virtual-machine-interface",
                input[i].name);
        DelLink("virtual-machine-interface", input[i].name, "project",
                proj_name);

        if (with_ip6) {
            sprintf(instance_ip6, "instance6%d", input[i].intf_id);
            DelLink("virtual-machine-interface", input[i].name, "instance-ip",
                    instance_ip6);
            DelInstanceIp(instance_ip6);
        }
        DelNode("virtual-machine-interface", input[i].name);
        DelNode("virtual-machine-interface-routing-instance", input[i].name);
        IntfCfgDel(input, i);

        DelNode("virtual-machine", vm_name);
        if (with_ip) {
            DelInstanceIp(instance_ip);
        }
    }

    if (del_vn) {
        for (int i = 0; i < count; i++) {
            int j = 0;
            for (; j < i; j++) {
                if (input[i].vn_id == input[j].vn_id) {
                    break;
                }
            }

            // Ignore duplicate deletes
            if (j < i) {
                continue;
            }
            if (vn)
                sprintf(vn_name, "%s", vn);
            else
                sprintf(vn_name, "default-domain:default-project:vn%d",
                        input[i].vn_id);
            if (vrf)
                sprintf(vrf_name, "%s", vrf);
            else
                sprintf(vrf_name, "default-domain:default-project:vn%d:vn%d",
                        input[i].vn_id, input[i].vn_id);
            DelLink("virtual-network", vn_name, "routing-instance", vrf_name);
            if (acl_id) {
                DelLink("virtual-network", vn_name,
                        "access-control-list", acl_name);
            }

            DelNode("virtual-network", vn_name);
            DelNode("routing-instance", vrf_name);
        }
        // Delete project if all VNs are deleted
        DelNode("project", proj_name);
    }

    if (acl_id) {
        DelNode("access-control-list", acl_name);
    }

    if (client->agent_init()->ksync_enable()) {
        DeleteTapIntf(tap_fd, count);
    }
}

void CreateHBFVMI(struct PortInfo *input, const char* hbf_name,
                     const char* proj, const char *vn, const char *vrf,
                     bool right) {
    char vm_name[MAX_TESTNAME_LEN];

    if (client->agent_init()->ksync_enable()) {
        CreateTapInterfaces("vnet", 1, tap_fd);
    }

    // Add project and HBS objects
    AddNode("project", proj, 1);
    AddNode("host-based-service", hbf_name, 1);
    AddLink("project", proj, "host-based-service", hbf_name);

    sprintf(vm_name, "vm%d", input->vm_id);

    AddVn(vn, input->vn_id, true);
    AddVrf(vrf);

    AddVm(vm_name, input->vm_id);
    AddVmPortVrf(input->name, "", 0);

    IntfCfgAdd(input, 0);
    AddPortWithMac(input->name, input->intf_id,
                       input->mac, NULL);

    AddLink("virtual-network", vn, "routing-instance", vrf);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", input->name, "virtual-machine",
                vm_name);
    AddLink("virtual-machine-interface", input->name,
                "virtual-network", vn);
    AddLink("virtual-machine-interface-routing-instance", input->name,
                "routing-instance", vrf,
                "virtual-machine-interface-routing-instance");
    AddLink("virtual-machine-interface-routing-instance", input->name,
            "virtual-machine-interface", input->name,
            "virtual-machine-interface-routing-instance");

    // VMI --> Project
    AddLink("virtual-machine-interface", input->name,
                "project", proj);
    // Add HBSVN node
    std::stringstream hbsvn_name;
    std::stringstream vntype;
    hbsvn_name << "attr(" << hbf_name << "," << vn << ")";
    vntype << "<virtual-network-type>" <<
        (right ? "right" : "left") << "</virtual-network-type>";

    AddLinkNode("host-based-service-virtual-network",
                 hbsvn_name.str().c_str(), vntype.str().c_str());

    // HBS --> HBSVN
    AddLink("host-based-service", hbf_name,
            "host-based-service-virtual-network", hbsvn_name.str().c_str(),
            "host-based-service-virtual-network");

    // HBSVN --> VN
    AddLink("host-based-service-virtual-network", hbsvn_name.str().c_str(),
            "virtual-network", vn);

    // VN --> VMI
    AddLink("virtual-network", vn, "virtual-network-interface", input->name);
}

void DeleteHBFVMI(struct PortInfo *input, const char* hbf_name,
                     const char* proj, const char *vn, const char *vrf,
                     bool right, bool del_hbs=false) {
    char vm_name[MAX_TESTNAME_LEN];


    sprintf(vm_name, "vm%d", input->vm_id);

    DelLink("virtual-network", vn, "routing-instance", vrf);
    client->WaitForIdle();

    DelLink("virtual-machine-interface", input->name, "virtual-machine",
                vm_name);
    DelLink("virtual-machine-interface", input->name,
                "virtual-network", vn);
    DelLink("virtual-machine-interface-routing-instance", input->name,
                "routing-instance", vrf,
                "virtual-machine-interface-routing-instance");
    DelLink("virtual-machine-interface-routing-instance", input->name,
            "virtual-machine-interface", input->name,
            "virtual-machine-interface-routing-instance");

    // VMI --> Project
    DelLink("virtual-machine-interface", input->name,
                "project", proj);
    // Delete all links and HBSVN node
    std::stringstream hbsvn_name;
    std::stringstream vntype;
    hbsvn_name << "attr(" << hbf_name << "," << vn << ")";
    vntype << "<virtual-network-type>" <<
        (right ? "right" : "left") << "</virtual-network-type>";

    // HBS --> HBSVN
    DelLink("host-based-service", hbf_name,
            "host-based-service-virtual-network", hbsvn_name.str().c_str(),
            "host-based-service-virtual-network");

    // HBSVN --> VN
    DelLink("host-based-service-virtual-network", hbsvn_name.str().c_str(),
            "virtual-network", vn);

    // VN --> VMI
    DelLink("virtual-network", vn, "virtual-network-interface", input->name);

    DelNode("host-based-service-virtual-network",
                 hbsvn_name.str().c_str());


    DelPort(input->name);
    IntfCfgDel(input, 0);
    DelVmPortVrf(input->name);
    DelVm(vm_name);
    DelVrf(vrf);
    DelVn(vn);

    // Delete project and HBS objects
    if (del_hbs) {
       DelLink("project", proj, "host-based-service", hbf_name);
       DelNode("host-based-service", hbf_name);
       DelNode("project", proj);
    }

    if (client->agent_init()->ksync_enable()) {
        CreateTapInterfaces("vnet", 1, tap_fd);
    }

}

// Customer VRF exists before HBF interfaces are added
// Customer VRF is deleted before HBF interfaces are deleted
TEST_F(HbfTest, basic_1) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3}
    };

    // Save initial vrouter vrf count
    TestVrfKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    client->WaitForIdle();
    int init_vrf_count = TestKStateBase::fetched_count_;

    // Create a customer VMI
    client->Reset();
    client->WaitForIdle();
    CreateCustomerVMI(input1, 1, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(1));
    client->Reset();

    // Find the customer VRF
    VrfEntry *vrf;
    VrfKey vkey("default-domain:default-project:vn1:vn1");
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf != NULL);
    // Verify that VrfKSyncEntry objects are created for customer vrf
    VrfKSyncObject *vrf_ksync_obj =
        Agent::GetInstance()->ksync()->vrf_ksync_obj();
    EXPECT_TRUE(vrf_ksync_obj != NULL);
    VrfKSyncEntry *temp_ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->DBToKSyncEntry(vrf));
    VrfKSyncEntry *ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->Find(temp_ksync_entry));
    delete temp_ksync_entry;
    EXPECT_TRUE(ksync_entry != NULL);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_entry->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(3));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);
    EXPECT_TRUE(hbfl->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

    // Customer vrf and ksync entries should have the
    // hbf right and left interfaces indices updated
    EXPECT_TRUE(vrf->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(vrf->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Verify from kstate that vRouter is programmed
    TestVrfKState::Init(vrf->vrf_id(), true, init_vrf_count+3,
                        hbfr->id(), hbfl->id());
    client->WaitForIdle();
    client->KStateResponseWait(1);
    client->WaitForIdle();

    // Delete the customer VMI
    DeleteCustomerVMI(input1, 1, true, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // VRf should be gone
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    // Verify from kstate that vrf is withdrawn from vrouter
    TestVrfKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    client->WaitForIdle();
    int end_vrf_count = TestKStateBase::fetched_count_;
    EXPECT_TRUE(end_vrf_count == (init_vrf_count+2));

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(2));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));

    client->Reset();
}

// Customer VRF exists before HBF interfaces are added
// HBF interfaces are deleted before customer VRF
TEST_F(HbfTest, basic_2) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3}
    };

    // Create a customer VMI
    client->Reset();
    client->WaitForIdle();
    CreateCustomerVMI(input1, 1, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(1));
    client->Reset();

    // Find the customer VRF
    VrfEntry *vrf;
    VrfKey vkey("default-domain:default-project:vn1:vn1");
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf != NULL);
    // Verify that VrfKSyncEntry objects are created for customer vrf
    VrfKSyncObject *vrf_ksync_obj =
        Agent::GetInstance()->ksync()->vrf_ksync_obj();
    EXPECT_TRUE(vrf_ksync_obj != NULL);
    VrfKSyncEntry *temp_ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->DBToKSyncEntry(vrf));
    VrfKSyncEntry *ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->Find(temp_ksync_entry));
    delete temp_ksync_entry;
    EXPECT_TRUE(ksync_entry != NULL);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_entry->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(3));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);
    EXPECT_TRUE(hbfl->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(2));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));

    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Delete the customer VMI
    DeleteCustomerVMI(input1, 1, true, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));

    // Customer VRf should be gone
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    client->Reset();
}

// HBF interfaces exist before customer vrf is added
// Customer VRF is deleted before HBF interfaces are deleted
TEST_F(HbfTest, basic_3) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3}
    };

    client->Reset();
    client->WaitForIdle();

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(3));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);

    // Create customer VMI
    CreateCustomerVMI(input1, 1, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(1));
    client->Reset();

    // Find the customer VRF
    VrfEntry *vrf;
    VrfKey vkey("default-domain:default-project:vn1:vn1");
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf != NULL);
    // Verify that VrfKSyncEntry  objects are created for customer vrf
    VrfKSyncObject *vrf_ksync_obj =
        Agent::GetInstance()->ksync()->vrf_ksync_obj();
    EXPECT_TRUE(vrf_ksync_obj != NULL);
    VrfKSyncEntry *temp_ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->DBToKSyncEntry(vrf));
    VrfKSyncEntry *ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->Find(temp_ksync_entry));
    delete temp_ksync_entry;
    EXPECT_TRUE(ksync_entry != NULL);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Customer vrf and ksync entries should have the
    // hbf right and left interfaces indices updated
    EXPECT_TRUE(vrf->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(vrf->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());

    // Delete the customer VMI
    DeleteCustomerVMI(input1, 1, true, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // VRf should be gone
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(2));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));

    client->Reset();
}

// HBF interfaces exist before customer vrf is added
// HBF interfaces are deleted before customer VRF
TEST_F(HbfTest, basic_4) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3}
    };

    client->Reset();
    client->WaitForIdle();

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(3));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);
    EXPECT_TRUE(hbfl->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

    // Create customer VMI
    CreateCustomerVMI(input1, 1, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(1));
    client->Reset();

    // Find the customer VRF
    VrfEntry *vrf;
    VrfKey vkey("default-domain:default-project:vn1:vn1");
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf != NULL);
    EXPECT_TRUE(vrf->vn() != NULL);
    // Verify that VrfKSyncEntry  objects are created for customer vrf
    VrfKSyncObject *vrf_ksync_obj =
        Agent::GetInstance()->ksync()->vrf_ksync_obj();
    EXPECT_TRUE(vrf_ksync_obj != NULL);
    VrfKSyncEntry *temp_ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->DBToKSyncEntry(vrf));
    VrfKSyncEntry *ksync_entry =
        static_cast<VrfKSyncEntry *>(vrf_ksync_obj->Find(temp_ksync_entry));
    delete temp_ksync_entry;
    EXPECT_TRUE(ksync_entry != NULL);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Customer vrf and ksync entries should have the
    // hbf right and left interfaces indices updated
    EXPECT_TRUE(vrf->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(vrf->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(2));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Delete the customer VMI
    DeleteCustomerVMI(input1, 1, true, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // VRf should be gone
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    client->Reset();
}

// Multiple customer VRFs under the same project
// Customer VRF exists before HBF interfaces are added
// Customer VRF is deleted before HBF interfaces are deleted
TEST_F(HbfTest, basic_5) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 3, "2.2.2.2", "00:00:00:01:01:03", 3, 3}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 4, "3.3.3.3", "00:00:00:01:01:04", 4, 4}
    };

    int port_count = sizeof(input1)/sizeof(input1[0]);

    // Create customer VMIs
    client->Reset();
    client->WaitForIdle();
    CreateCustomerVMI(input1, port_count, 0, NULL, NULL, NULL);
    client->WaitForIdle();
    for (int i = 0; i < port_count; i++) {
        EXPECT_TRUE(VmPortActive(input1, i));
        EXPECT_TRUE(VmPortFind(i+1));
    }

    VrfEntry *vrf[sizeof(input1)];
    VrfKSyncEntry *ksync_entry[sizeof(input1)];
    // Find the customer VRFs
    for (int i = 0; i < port_count; i++) {
        std::stringstream ss;
        ss << "default-domain:default-project:vn" << input1[i].vn_id
            << ":vn" << input1[i].vn_id;
        VrfKey vkey(ss.str().c_str());
        vrf[i] = static_cast<VrfEntry *>
            (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
        EXPECT_TRUE(vrf[i] != NULL);
        EXPECT_TRUE(vrf[i]->vn() != NULL);

        // Verify that VrfKSyncEntry objects are created for customer vrf
        VrfKSyncObject *vrf_ksync_obj =
            Agent::GetInstance()->ksync()->vrf_ksync_obj();
        EXPECT_TRUE(vrf_ksync_obj != NULL);
        VrfKSyncEntry *temp_ksync_entry = static_cast<VrfKSyncEntry *>
            (vrf_ksync_obj->DBToKSyncEntry(vrf[i]));
        ksync_entry[i] = static_cast<VrfKSyncEntry *>
            (vrf_ksync_obj->Find(temp_ksync_entry));
        delete temp_ksync_entry;
        EXPECT_TRUE(ksync_entry[i] != NULL);
        EXPECT_TRUE(ksync_entry[i]->GetState() == KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_entry[i]->hbf_lintf() == Interface::kInvalidIndex);
        EXPECT_TRUE(ksync_entry[i]->hbf_rintf() == Interface::kInvalidIndex);
    }

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(3));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(4));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(4), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);
    EXPECT_TRUE(hbfl->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

    // Customer vrfs and ksync entries should have the
    // hbf right and left interfaces indices updated
    for (int i = 0; i < port_count; i++) {
        EXPECT_TRUE(vrf[i]->hbf_rintf() == hbfr->id());
        EXPECT_TRUE(vrf[i]->hbf_lintf() == hbfl->id());
        EXPECT_TRUE(ksync_entry[i]->hbf_rintf() == hbfr->id());
        EXPECT_TRUE(ksync_entry[i]->hbf_lintf() == hbfl->id());
        EXPECT_TRUE(ksync_entry[i]->GetState() == KSyncEntry::IN_SYNC);
    }

    // Delete the customer VMI
    DeleteCustomerVMI(input1, port_count, true, 0, NULL, NULL, NULL);
    client->WaitForIdle();

    for (int i = 0; i < port_count; i++) {
        EXPECT_FALSE(VmPortFind(i+1));

        VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(i+1), "");
        WAIT_FOR(100, 1000,
                 (Agent::GetInstance()->interface_table()->Find(&key, true)
                == NULL));

        // VRf should be gone
        std::stringstream ss;
        ss << "default-domain:default-project:vn" << input1[i].vn_id
            << ":vn" << input1[i].vn_id;
        VrfKey vkey(ss.str().c_str());
        VrfEntry* vrf = static_cast<VrfEntry *>
            (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
        EXPECT_TRUE(vrf == NULL);
    }

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(4));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(4), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));

    client->Reset();
}

// HBF interfaces exist before customer vrf is added
// HBF interfaces are deleted and added
TEST_F(HbfTest, basic_6) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    struct PortInfo input2[] = {
        {"hbf-r", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2}
    };
    struct PortInfo input3[] = {
        {"hbf-l", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3}
    };

    client->Reset();
    client->WaitForIdle();

    // Create hbf right interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    VmInterface *hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Create HBF left interface
    CreateHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l", false);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input3, 0));
    EXPECT_TRUE(VmPortFind(3));
    VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    VmInterface *hbfl = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key2, true));
    EXPECT_TRUE(hbfl != NULL);
    EXPECT_TRUE(hbfl->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

    // Create customer VMI
    CreateCustomerVMI(input1, 1, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(1));
    client->Reset();

    // Find the customer VRF
    VrfEntry *vrf;
    VrfKey vkey("default-domain:default-project:vn1:vn1");
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf != NULL);
    EXPECT_TRUE(vrf->vn() != NULL);
    // Verify that VrfKSyncEntry  objects are created for customer vrf
    VrfKSyncObject *vrf_ksync_obj =
        Agent::GetInstance()->ksync()->vrf_ksync_obj();
    EXPECT_TRUE(vrf_ksync_obj != NULL);
    VrfKSyncEntry *temp_ksync_entry = static_cast<VrfKSyncEntry *>
        (vrf_ksync_obj->DBToKSyncEntry(vrf));
    VrfKSyncEntry *ksync_entry = static_cast<VrfKSyncEntry *>
        (vrf_ksync_obj->Find(temp_ksync_entry));
    delete temp_ksync_entry;
    EXPECT_TRUE(ksync_entry != NULL);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Customer vrf and ksync entries should have the
    // hbf right and left interfaces indices updated
    EXPECT_TRUE(vrf->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(vrf->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(2));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE, MakeUuid(2), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == hbfl->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Add back right HBF interface
    CreateHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r", true);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(VmPortFind(2));
    DoInterfaceSandesh("");
    hbfr = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->Find(&key1, true));
    EXPECT_TRUE(hbfr != NULL);
    EXPECT_TRUE(hbfr->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

    // Customer vrf and ksync entries should have the
    // hbf right interface indices updated
    EXPECT_TRUE(vrf->hbf_rintf() == hbfr->id());
    EXPECT_TRUE(ksync_entry->hbf_rintf() == hbfr->id());

    // Delete HBF right interface
    DeleteHBFVMI(input2, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-r",
                 "default-domain:default-project:hbf-r:hbf-r",
                  true, false);
    client->WaitForIdle();

    // Delete HBF left interface
    DeleteHBFVMI(input3, "default-domain:default-project:hbf",
                 "default-domain:default-project",
                 "default-domain:default-project:hbf-l",
                 "default-domain:default-project:hbf-l:hbf-l",
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(3));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE, MakeUuid(3), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry->GetState() == KSyncEntry::IN_SYNC);

    // Delete the customer VMI
    DeleteCustomerVMI(input1, 1, true, 0, "default-domain:default-project",
                      "default-domain:default-project:vn1",
                      "default-domain:default-project:vn1:vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE, MakeUuid(1), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // VRf should be gone
    vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    client->Reset();
}

// Two projects, 2 HBS objects
// HBF interfaces exist before customer vrf is added
// HBF interfaces are deleted before customer VRF
TEST_F(HbfTest, basic_7) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 4, "4.4.4.4", "00:00:00:01:01:04", 4, 4}
    };
    struct PortInfo input2[] = {
        {"hbf-r1", 2, "2.2.2.2", "00:00:00:01:01:02", 2, 2},
        {"hbf-r2", 5, "5.5.5.5", "00:00:00:01:01:05", 5, 5}
    };
    struct PortInfo input3[] = {
        {"hbf-l1", 3, "3.3.3.3", "00:00:00:01:01:03", 3, 3},
        {"hbf-l2", 6, "6.6.6.6", "00:00:00:01:01:06", 6, 6}
    };

    client->Reset();
    client->WaitForIdle();

    int port_count = sizeof(input1)/sizeof(input1[0]);
    std::stringstream proj[port_count], hbf[port_count],
       rvn[port_count], lvn[port_count], lvrf[port_count],
       rvrf[port_count], cvn[port_count], cvrf[port_count];
    VrfKSyncEntry *ksync_entry[port_count];
    VmInterface *hbfr[port_count];
    VmInterface *hbfl[port_count];
    VrfEntry *vrf[port_count];
    for (int i = 0; i < port_count; i++) {
        proj[i] << "default-domain:default-project" << i+1;
        hbf[i] << proj[i].str() << ":hbf" << i+1;
        lvn[i] << proj[i].str() << ":hbf-l" << i+1;
        rvn[i] << proj[i].str() << ":hbf-r" << i+1;
        lvrf[i] << proj[i].str() << ":hbf-l" << i+1 << ":hbf-l" << i+1;
        rvrf[i] << proj[i].str() << ":hbf-r" << i+1 << ":hbf-r" << i+1;
        cvn[i] << proj[i].str() << ":vn" << i+1;
        cvrf[i] << proj[i].str() << ":vn" << i+1 << ":vn" << i+1;
    }

    for (int i = 0; i < port_count; i++) {
        // Create hbf right interfaces
        CreateHBFVMI(&input2[i], hbf[i].str().c_str(), proj[i].str().c_str(),
                 rvn[i].str().c_str(), rvrf[i].str().c_str(), true);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(&input2[i], 0));
        EXPECT_TRUE(VmPortFind(input2[i].intf_id));
        DoInterfaceSandesh("");
        VmInterfaceKey key1(AgentKey::ADD_DEL_CHANGE,
                            MakeUuid(input2[i].intf_id), "");
        hbfr[i] = static_cast<VmInterface *>
            (Agent::GetInstance()->interface_table()->Find(&key1, true));
        EXPECT_TRUE(hbfr[i] != NULL);
        EXPECT_TRUE(hbfr[i]->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);

        // Create HBF left interfaces
        CreateHBFVMI(&input3[i], hbf[i].str().c_str(), proj[i].str().c_str(),
                 lvn[i].str().c_str(), lvrf[i].str().c_str(), false);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(&input3[i], 0));
        EXPECT_TRUE(VmPortFind(input3[i].intf_id));
        VmInterfaceKey key2(AgentKey::ADD_DEL_CHANGE, MakeUuid(input3[i].intf_id), "");
        hbfl[i] = static_cast<VmInterface *>
            (Agent::GetInstance()->interface_table()->Find(&key2, true));
        EXPECT_TRUE(hbfl[i] != NULL);
        EXPECT_TRUE(hbfl[i]->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);

        // Create customer VMI
        CreateCustomerVMI(&input1[i], 1, 0, proj[i].str().c_str(), cvn[i].str().c_str(),
                          cvrf[i].str().c_str());
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(&input1[i], 0));
        EXPECT_TRUE(VmPortFind(input1[i].intf_id));
        client->Reset();

        // Find the customer VRF
        VrfKey vkey(cvrf[i].str().c_str());
        vrf[i] = static_cast<VrfEntry *>
            (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
        EXPECT_TRUE(vrf[i] != NULL);
        EXPECT_TRUE(vrf[i]->vn() != NULL);
        // Verify that VrfKSyncEntry  objects are created for customer vrf
        VrfKSyncObject *vrf_ksync_obj = Agent::GetInstance()->ksync()->vrf_ksync_obj();
        EXPECT_TRUE(vrf_ksync_obj != NULL);
        VrfKSyncEntry *temp_ksync_entry =
            static_cast<VrfKSyncEntry *>(vrf_ksync_obj->DBToKSyncEntry(vrf[i]));
        ksync_entry[i] =
            static_cast<VrfKSyncEntry *>(vrf_ksync_obj->Find(temp_ksync_entry));
        delete temp_ksync_entry;
        EXPECT_TRUE(ksync_entry[i] != NULL);
        EXPECT_TRUE(ksync_entry[i]->GetState() == KSyncEntry::IN_SYNC);

        // Customer vrf and ksync entries should have the
        // hbf right and left interfaces indices updated
        EXPECT_TRUE(vrf[i]->hbf_rintf() == hbfr[i]->id());
        EXPECT_TRUE(vrf[i]->hbf_lintf() == hbfl[i]->id());
        EXPECT_TRUE(ksync_entry[i]->hbf_rintf() == hbfr[i]->id());
        EXPECT_TRUE(ksync_entry[i]->hbf_lintf() == hbfl[i]->id());
    }

    for (int i = 0; i < port_count; i++) {
    // Delete HBF right interfaces
    DeleteHBFVMI(&input2[i], hbf[i].str().c_str(), proj[i].str().c_str(),
                  rvn[i].str().c_str(), rvrf[i].str().c_str(),
                  true, false);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input2[i].intf_id));
    VmInterfaceKey key4(AgentKey::ADD_DEL_CHANGE,
                        MakeUuid(input2[i].intf_id), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key4, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry[i]->hbf_lintf() == hbfl[i]->id());
    EXPECT_TRUE(ksync_entry[i]->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry[i]->GetState() == KSyncEntry::IN_SYNC);

    // Delete HBF left interface
    DeleteHBFVMI(&input3[i], hbf[i].str().c_str(), proj[i].str().c_str(),
                  lvn[i].str().c_str(), lvrf[i].str().c_str(),
                  false, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input3[i].intf_id));
    VmInterfaceKey key5(AgentKey::ADD_DEL_CHANGE,
                        MakeUuid(input3[i].intf_id), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key5, true)
                == NULL));
    // Verify VRf and Ksync entries are updated
    EXPECT_TRUE(ksync_entry[i]->hbf_lintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry[i]->hbf_rintf() == Interface::kInvalidIndex);
    EXPECT_TRUE(ksync_entry[i]->GetState() == KSyncEntry::IN_SYNC);

    // Delete the customer VMI
    DeleteCustomerVMI(&input1[i], 1, true, 0, proj[i].str().c_str(),
                      cvn[i].str().c_str(), cvrf[i].str().c_str());
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input1[i].intf_id));
    VmInterfaceKey key3(AgentKey::ADD_DEL_CHANGE,
                        MakeUuid(input1[i].intf_id), "");
    WAIT_FOR(100, 1000,
             (Agent::GetInstance()->interface_table()->Find(&key3, true)
                == NULL));

    // VRf should be gone
    VrfKey vkey(cvrf[i].str().c_str());
    VrfEntry* vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vkey));
    EXPECT_TRUE(vrf == NULL);

    client->Reset();
    }
}

int main(int argc, char **argv) {
    GETUSERARGS();

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
