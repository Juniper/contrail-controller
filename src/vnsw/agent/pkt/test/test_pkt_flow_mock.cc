/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_setup.h"
#include "ksync/ksync_sock_user.h"
#include "cmn/agent_cmn.h"

#include <boost/program_options.hpp>

using boost::optional;
namespace opt = boost::program_options;

char setup_file[1024];
char config_file[1024];

void RouterIdDepInit(Agent *agent) {
}

namespace {
class AclFlowTest : public ::testing::Test {
protected:
};

static void TxIpPacket(int ifindex, const char *sip, const char *dip,
			  int proto, int hash_id) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, proto);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}


bool TrafficActionCheck(TrafficAction::Action act,
                       int32_t vr_flow_action) {
    switch (act) {
        case TrafficAction::DENY:
            return ((vr_flow_action & 0x1) == VR_FLOW_ACTION_DROP) ? true : false;
        case TrafficAction::PASS:
            return (vr_flow_action & VR_FLOW_ACTION_FORWARD) ? true : false;
        default:
            return false;
    }
    return false;
}

void ProcessExceptionPackets() {
    std::vector<ExceptionPacket *>::iterator it;
    int hash_id = 10;
    for (it = excep_p_l.begin(); it != excep_p_l.end(); ++it) {
         ExceptionPacket *ep = *it;
	 Vn *vn = FindVn(ep->vn);
	 if (vn == NULL) {
	     LOG(ERROR, "Vn doesn't exist");
	     return;
	 }
	 Vm *vm = FindVm(*vn, ep->vm);
	 if (vm == NULL) {
	     LOG(ERROR, "Vm:" << ep->vm << " doesn't exist in the Vn:" << vn->name);
	     return;
	 }
	 
	 VmInterface *intf = VmInterfaceGet(vm->pinfo.intf_id);
	 if (intf == NULL) {
	   LOG(ERROR, "Interface is not found");
	   return;
	 }
	 LOG(DEBUG, "Interface ID:" << intf->id());
	 TxIpPacket(intf->id(), ep->sip.c_str(), ep->dip.c_str(), 
	            strtoul((ep->proto).c_str(), NULL, 0), hash_id);
         client->WaitForIdle();
         AclTable *table = Agent::GetInstance()->acl_table();
         KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
         KSyncSockTypeMap::ksync_map_flow::iterator ksit;
         EXPECT_TRUE((ksit = sock->flow_map.find(hash_id)) != sock->flow_map.end());
         vr_flow_req vr_flow = ksit->second;
         TrafficAction::Action act = table->ConvertActionString(ep->act);
         EXPECT_TRUE(TrafficActionCheck(act, vr_flow.get_fr_action()));
         EXPECT_EQ(VR_FLOW_FLAG_ACTIVE, vr_flow.get_fr_flags());
         EXPECT_EQ(hash_id, vr_flow.get_fr_index());
         hash_id++;
    }
}

void CreateNodeNetwork(Vn &vn) {
    Acl *acl = FindAcl(vn.acl_id);
    EXPECT_TRUE(acl != NULL);
    client->Reset();
    AddVn(vn.name.c_str(), vn.id);
    AddVrf(vn.vrf.c_str());
    AddLink("virtual-network", vn.name.c_str(), "routing-instance", vn.vrf.c_str());
    AddLink("virtual-network", vn.name.c_str(), "access-control-list", acl->name.c_str()); 

    std::vector<Vm *>::iterator iter;
    for (iter = vn.vm_l.begin(); iter != vn.vm_l.end();
	 ++iter) {
         Vm *vm = *iter;
	 AddVm(vm->name.c_str(), vm->id);

     CreateVmportEnv(&(vm->pinfo), 1);
    }
    client->WaitForIdle();
    for (iter = vn.vm_l.begin(); iter != vn.vm_l.end();
	 ++iter) {
         Vm *vm = *iter;
         EXPECT_TRUE(VmPortActive(&(vm->pinfo), 0));
         EXPECT_TRUE(VmPortPolicyEnable(&(vm->pinfo), 0));
         Ip4Address ip = Ip4Address::from_string(vm->pinfo.addr);
         EXPECT_TRUE(RouteFind(vn.vrf, ip, 32));
    }
}

void CreateNodeNetworks() {
    std::vector<Vn *>::iterator iter;
    for (iter = vn_l.begin(); iter !=vn_l.end(); ++iter) {
         Vn *vn = *iter;
	 CreateNodeNetwork(*vn);
    }      
}

void LoadAcl() {
    pugi::xml_document xdoc;
    xdoc.load_file("data.xml");
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc.first_child(), 0);
}
  
TEST_F(AclFlowTest, Setup) {
    string str;
    if (setup_file[0] == '\0') {
        ReadSetupFile(NULL);
    } else {
        ReadSetupFile(setup_file);
    }
    ConstructAclXmlDoc();
    LoadAcl();
    client->Reset();
    CreateNodeNetworks();
    ProcessExceptionPackets();
}

} //namespace

int main (int argc, char **argv) {
    setup_file[0] = '\0';
    config_file[0] = '\0';
    opt::options_description desc("Command line options");
    desc.add_options()
            ("help", "help message")
            ("setup-file", opt::value<string>(), "Test setup file")
            ("config-file", opt::value<string>(), "Config file");
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << desc << endl;
        exit(0);
    }
    if (var_map.count("setup-file")) {
       snprintf(setup_file, sizeof(setup_file), "%s",
                var_map["setup-file"].as<string>().c_str());
    }
    LOG(DEBUG, "Setup File:" << setup_file);
    if (var_map.count("config-file")) {
       snprintf(config_file, sizeof(config_file), "%s",
                var_map["config-file"].as<string>().c_str());
    } else {
        strcpy(config_file, DEFAULT_VNSW_CONFIG_FILE);
    }
    LOG(DEBUG, "Config File:" << config_file);

    client = TestInit(config_file, false, true, false, true);
	Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
