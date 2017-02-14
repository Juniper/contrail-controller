/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test/test_init.h"
#include "oper/ecmp_load_balance.h"
#include "oper/mirror_table.h"
#include "oper/physical_device_vn.h"
#include "ksync/ksync_sock_user.h"
#include "uve/test/vn_uve_table_test.h"
#include "uve/agent_uve_stats.h"
#include <cfg/cfg_types.h>
#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>
#include <controller/controller_ifmap.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/mpls_index.h>
#define MAX_TESTNAME_LEN 80

using namespace std;

BgpPeer *bgp_peer_;
TestClient *client;
#define MAX_VNET 10
int test_tap_fd[MAX_VNET];

TestTaskHold::HoldTask::HoldTask(TestTaskHold *hold_entry) :
    Task(hold_entry->task_id_, hold_entry->task_instance_),
    hold_entry_(hold_entry) {
}

bool TestTaskHold::HoldTask::Run() {
    hold_entry_->task_held_ = true;
    while (hold_entry_->task_held_ == true) {
        usleep(2000);
    }
    hold_entry_->task_held_ = true;
    return true;
}

TestTaskHold::TestTaskHold(int task_id, int task_instance) :
    task_id_(task_id), task_instance_(task_instance) {
    task_held_ = false;
    HoldTask *task_entry = new HoldTask(this);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task_entry);
    while (task_held_ != true) {
        usleep(200);
    }
}

TestTaskHold::~TestTaskHold() {
    task_held_ = false;
    while (task_held_ != true) {
        usleep(200);
    }
}

uuid MakeUuid(int id) {
    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", id);
    boost::uuids::uuid u1 = StringToUuid(std::string(str));

    return u1;
}

void DelXmlHdr(char *buff, int &len) {
    len = 0;
    sprintf(buff + len, "<?xml version=\"1.0\"?>\n"
                        "    <config>\n"
                        "         <delete>\n");
    len = strlen(buff);
}

void DelXmlTail(char *buff, int &len) {
    sprintf(buff + len, "        </delete>\n"
                        "    </config>\n");
    len = strlen(buff);
}

void AddXmlHdr(char *buff, int &len) {
    len = 0;
    sprintf(buff + len, "<?xml version=\"1.0\"?>\n"
                        "    <config>\n"
                        "         <update>\n");
    len = strlen(buff);
}

void AddXmlTail(char *buff, int &len) {
    sprintf(buff + len, "        </update>\n"
                        "    </config>\n");
    len = strlen(buff);
}

static bool CheckLink(const string &node1, const string &node2,
               const string &match1, const string &match2) {
    if (node1 == match1 && node2 == match2)
        return true;
    if (node1 == match2 && node2 == match1)
        return true;

    return false;
}

typedef std::map<string, string> LinkToMetadata;
LinkToMetadata link_to_metadata_;

static void AddLinkToMetadata(const char *node1, const char *node2,
                              const char *metadata = NULL) {
    char buff[128];
    if (metadata == NULL)
        sprintf(buff, "%s-%s", node1, node2);
    else
        strcpy(buff, metadata);

    link_to_metadata_.insert(make_pair(string(node1) + "-" + string(node2),
                                       buff));
    link_to_metadata_.insert(make_pair(string(node2) + "-" + string(node1),
                                       buff));
}

static void BuildLinkToMetadata() {
    if (link_to_metadata_.size() != 0)
        return;

    AddLinkToMetadata("virtual-machine-interface", "virtual-network");
    AddLinkToMetadata("virtual-machine-interface", "virtual-machine");
    AddLinkToMetadata("virtual-machine-interface", "security-group");
    AddLinkToMetadata("virtual-machine-interface",
                      "virtual-machine-interface-routing-instance",
                      "virtual-machine-interface-routing-instance");
    AddLinkToMetadata("virtual-machine-interface", "interface-route-table",
                      "virtual-machine-interface-route-table");
    AddLinkToMetadata("virtual-machine-interface",
                      "virtual-machine-interface-bridge-domain",
                      "virtual-machine-interface-bridge-domain");
    AddLinkToMetadata("instance-ip", "virtual-machine-interface");
    AddLinkToMetadata("instance-ip", "virtual-network");
    AddLinkToMetadata("instance-ip", "floating-ip");
    AddLinkToMetadata("virtual-machine-interface", "virtual-machine-interface",
                      "virtual-machine-interface-sub-interface");

    AddLinkToMetadata("virtual-network", "routing-instance");
    AddLinkToMetadata("virtual-network", "access-control-list");
    AddLinkToMetadata("virtual-network", "floating-ip-pool");
    AddLinkToMetadata("virtual-network", "alias-ip-pool");
    AddLinkToMetadata("virtual-network", "virtual-network-network-ipam",
                      "virtual-network-network-ipam");
    AddLinkToMetadata("virtual-network-network-ipam", "network-ipam",
                      "virtual-network-network-ipam");

    AddLinkToMetadata("security-group", "access-control-list");

    AddLinkToMetadata("routing-instance",
                      "virtual-machine-interface-routing-instance",
                      "virtual-machine-interface-routing-instance");

    AddLinkToMetadata("physical-router", "physical-interface");
    AddLinkToMetadata("physical-router", "logical-interface");
    AddLinkToMetadata("physical-interface", "logical-interface");
    AddLinkToMetadata("logical-interface", "virtual-machine-interface");

    AddLinkToMetadata("floating-ip-pool", "floating-ip");
    AddLinkToMetadata("floating-ip", "virtual-machine-interface");

    AddLinkToMetadata("alias-ip-pool", "alias-ip");
    AddLinkToMetadata("alias-ip", "virtual-machine-interface");

    AddLinkToMetadata("subnet", "virtual-machine-interface");
    AddLinkToMetadata("virtual-router", "virtual-machine");
    AddLinkToMetadata("virtual-machine-interface", "bgp-as-a-service");
    AddLinkToMetadata("bgp-router", "bgp-as-a-service");
    AddLinkToMetadata("bgp-router", "routing-instance");
    AddLinkToMetadata("virtual-network", "qos-config");
    AddLinkToMetadata("virtual-machine-interface", "qos-config");
    AddLinkToMetadata("global-qos-config", "qos-config");
    AddLinkToMetadata("forwarding-class", "qos-queue");
    AddLinkToMetadata("network-ipam", "virtual-DNS");
    AddLinkToMetadata("virtual-machine-interface", "service-health-check", "service-port-health-check");

    AddLinkToMetadata("bridge-domain",
                      "virtual-machine-interface-bridge-domain",
                      "virtual-machine-interface-bridge-domain");
    AddLinkToMetadata("virtual-network", "bridge-domain");
}

string GetMetadata(const char *node1, const char *node2,
                   const char *mdata) {
    BuildLinkToMetadata();

    if (mdata != NULL)
        return mdata;

    LinkToMetadata::iterator it = link_to_metadata_.find(string(node1) + "-" +
                                                         string(node2));
    if (it == link_to_metadata_.end()) {
        cout << "Error finding metadata for link between "
            << node1 << " and " << node2 << endl;
        // Metadata not found for the link.
        // Please populate link-to-metadata entry in BuildLinkToMetadata
        assert(0);
    }

    return it->second;
}

void AddLinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2,
                   const char *name2, const char *mdata) {
    sprintf(buff + len,
            "       <link>\n"
            "           <node type=\"%s\">\n"
            "               <name>%s</name>\n"
            "           </node>\n"
            "           <node type=\"%s\">\n"
            "               <name>%s</name>\n"
            "           </node>\n"
            "           <metadata type=\"%s\"/>\n"
            "       </link>\n", node_name1, name1, node_name2, name2,
            GetMetadata(node_name1, node_name2, mdata).c_str());

    len = strlen(buff);
}

void DelLinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2,
                   const char *name2) {
    sprintf(buff + len,
            "       <link>\n"
            "           <node type=\"%s\">\n"
            "               <name>%s</name>\n"
            "           </node>\n"
            "           <node type=\"%s\">\n"
            "               <name>%s</name>\n"
            "           </node>\n"
            "           <metadata type=\"%s\"/>\n"
            "       </link>\n", node_name1, name1, node_name2, name2, 
            GetMetadata(node_name1, node_name2).c_str());
    len = strlen(buff);
}

void AddNodeStringWithoutUuid(char *buff, int &len, const char
        *node_name, const char *name) {
    sprintf(buff + len,
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "       </node>\n", node_name, name);
    len = strlen(buff);
}

void AddNodeString(char *buff, int &len, const char *node_name,
                   const char *name, int id, const char *attr,
                   bool admin_state) {
    sprintf(buff + len,
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "               <enable>%s</enable>\n"
            "           </id-perms>\n"
            "           %s\n"
            "       </node>\n", node_name, name, id,
                                (admin_state == true) ? "true" : "false", attr);
    len = strlen(buff);
}

void AddNodeString(char *buff, int &len, const char *node_name,
                   const char *name, int id) {
    sprintf(buff + len,
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "               <enable>true</enable>\n"
            "           </id-perms>\n"
            "       </node>\n", node_name, name, id);
    len = strlen(buff);
}

void AddNodeString(char *buff, int &len, const char *node_name,
                   const char *name, int id, bool enable) {
    char status_str[10];
    if (enable) {
        strcpy(status_str, "true");
    } else {
        strcpy(status_str, "false");
    }

    sprintf(buff + len,
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "               <enable>%s</enable>\n"
            "           </id-perms>\n"
            "       </node>\n", node_name, name, id, status_str);
    len = strlen(buff);
}

void AddNodeString(char *buff, int &len, const char *nodename, const char *name,
                   IpamInfo *ipam, int count,
                   const std::vector<std::string> *vm_host_routes,
                   const char *add_subnet_tags) {
    std::stringstream str;
    str << "       <node type=\"" << nodename << "\">\n";
    str << "           <name>" << name << "</name>\n";
    str << "           <value>\n";
    for (int i = 0; i < count; i++) {
        std::string dhcp_enable = (ipam[i].dhcp_enable == true) ? "true" : "false";
        str << "               <ipam-subnets>\n";
        str << "                   <subnet>\n";
        str << "                       <ip-prefix>" << ipam[i].ip_prefix << "</ip-prefix>\n";
        str << "                       <ip-prefix-len>" << ipam[i].plen << "</ip-prefix-len>\n";
        str << "                   </subnet>\n";
        str << "                   <default-gateway>" << ipam[i].gw << "</default-gateway>\n";
        str << "                   <dns-server-address>" << ipam[i].gw << "</dns-server-address>\n";
        str << "                   <enable-dhcp>" << dhcp_enable << "</enable-dhcp>\n";
        str << "                   <alloc-unit>" << ipam[i].alloc_unit << "</alloc-unit>\n";
        if (add_subnet_tags)
            str <<                 add_subnet_tags << "\n";
        str << "               </ipam-subnets>\n";
    }
    if (vm_host_routes && vm_host_routes->size()) {
        str << "               <host-routes>\n";
        for (unsigned int i = 0; i < vm_host_routes->size(); i++) {
            std::string prefix, nexthop;
            std::vector<std::string> tokens;
            boost::split(tokens, (*vm_host_routes)[i], boost::is_any_of(" \t"));
            vector<string>::iterator it = tokens.begin();
            if (it != tokens.end()) {
                prefix = *it;
                it++;
            }
            if (it != tokens.end()) {
                nexthop = *it;
            }
            str << "                   <route>\n";
            str << "                       <prefix>" << prefix << "</prefix>\n";
            str << "                       <next-hop>" << nexthop << "</next-hop>\n";
            str << "                       <next-hop-type />\n";
            str << "                   </route>\n";
        }
        str << "               </host-routes>\n";
    }
    str << "           </value>\n";
    str << "       </node>\n";
    std::string final_str = str.str();
    memcpy(buff + len, final_str.data(), final_str.size());
    len += final_str.size();
}

void AddLinkNodeString(char *buff, int &len, const char *nodename, const char *name,
                       const char *attr) {
    std::stringstream str;
    str << "       <node type=\"" << nodename << "\">\n";
    str << "           <name>" << name << "</name>\n";
    str << "           <value>\n";
    str << "               " << attr << "\n";
    str << "           </value>\n";
    str << "       </node>\n";
    std::string final_str = str.str();
    memcpy(buff + len, final_str.data(), final_str.size());
    len += final_str.size();
}

void AddVmPortVrfNodeString(char *buff, int &len, const char *name, int id) {
    AddLinkNodeString(buff, len, "virtual-machine-interface-routing-instance",
                  name, "<direction>both</direction>");
                  //<vlan-tag>1</vlan-tag> <service-chain-address></service-chain-address>");
}

void DelNodeString(char *buff, int &len, const char *node_name,
                   const char *name) {
    sprintf(buff + len,
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "       </node>\n", node_name, name);

    len = strlen(buff);
}

void ApplyXmlString(const char *buff) {
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    //cout << buff << endl;
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddLink(const char *node_name1, const char *name1,
             const char *node_name2, const char *name2, const char *mdata) {
    char buff[1024];
    int len = 0;
 
    AddXmlHdr(buff, len);
    AddLinkString(buff, len, node_name1, name1, node_name2, name2, mdata);
    AddXmlTail(buff, len);
    //LOG(DEBUG, buff);
    ApplyXmlString(buff);
    return;
}

void DelLink(const char *node_name1, const char *name1,
             const char *node_name2, const char *name2) {
    char buff[1024];
    int len = 0;

    DelXmlHdr(buff, len);
    DelLinkString(buff, len, node_name1, name1, node_name2, name2);
    DelXmlTail(buff, len);
    //LOG(DEBUG, buff);
    ApplyXmlString(buff);
    return;
}

void AddNode(const char *node_name, const char *name, int id) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

void AddNodeByStatus(const char *node_name, const char *name, int id, bool status) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id, status);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

// admin_state is true by default
void AddNode(const char *node_name, const char *name, int id,
                    const char *attr, bool admin_state) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    if (id)
        AddNodeString(buff, len, node_name, name, id, attr, admin_state);
    else
        AddNodeStringWithoutUuid(buff, len, node_name, name);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

// admin_state is true by default
void AddNode(Agent *agent, const char *node_name, const char *name, int id,
             const char *attr, bool admin_state) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id, attr, admin_state);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

void AddLinkNode(const char *node_name, const char *name, const char *attr) {
    char buff[1024];
    int len = 0;

    AddXmlHdr(buff, len);
    AddLinkNodeString(buff, len, node_name, name, attr);
    AddXmlTail(buff, len);
    LOG(DEBUG, buff);
    ApplyXmlString(buff);
    return;
}

void DelNode(const char *node_name, const char *name) {
    char buff[1024];
    int len = 0;

    DelXmlHdr(buff, len);
    DelNodeString(buff, len, node_name, name);
    DelXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

void DelNode(Agent *agent, const char *node_name, const char *name) {
    char buff[1024];
    int len = 0;

    DelXmlHdr(buff, len);
    DelNodeString(buff, len, node_name, name);
    DelXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

uint32_t PortSubscribeSize(Agent *agent) {
    return agent->port_ipc_handler()->port_subscribe_table()->Size();
}

bool PortSubscribe(VmiSubscribeEntry *entry) {
    string json;
    Agent *agent = Agent::GetInstance();
    agent->port_ipc_handler()->MakeVmiUuidJson(entry, json, false);
    string err;
    return agent->port_ipc_handler()->AddPortFromJson(json, false, err, false);
}

bool PortSubscribe(const std::string &ifname,
                   const boost::uuids::uuid &vmi_uuid,
                   const boost::uuids::uuid vm_uuid,
                   const std::string &vm_name,
                   const boost::uuids::uuid &vn_uuid,
                   const boost::uuids::uuid &project_uuid,
                   const Ip4Address &ip4_addr, const Ip6Address &ip6_addr,
                   const std::string &mac_addr) {
    VmiSubscribeEntry entry(VmiSubscribeEntry::VMPORT, ifname, 0,
                            vmi_uuid, vm_uuid, vm_name, vn_uuid, project_uuid,
                            ip4_addr, ip6_addr, mac_addr,
                            VmInterface::kInvalidVlanId,
                            VmInterface::kInvalidVlanId);
    return PortSubscribe(&entry);
}

void PortUnSubscribe(const boost::uuids::uuid &u) {
    Agent *agent = Agent::GetInstance();
    string err;
    agent->port_ipc_handler()->DeleteVmiUuidEntry(u, err);
}

void IntfSyncMsg(PortInfo *input, int id) {
    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                             MakeUuid(input[id].intf_id), "");
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
    usleep(1000);
}

void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, uint16_t vlan,
                const string ip6addr, int project_id) {
    boost::system::error_code ec;
    Ip4Address ip = Ip4Address::from_string(ipaddr, ec);
    char vm_name[MAX_TESTNAME_LEN];
    sprintf(vm_name, "vm%d", vm_id);
    Ip6Address ip6 = Ip6Address();
    if (!ip6addr.empty()) {
        ip6 = Ip6Address::from_string(ip6addr, ec);
    }

    VmiSubscribeEntry entry(PortSubscribeEntry::VMPORT, name, 0,
                            MakeUuid(intf_id), MakeUuid(vm_id), vm_name,
                            MakeUuid(vn_id), MakeUuid(project_id), ip, ip6,
                            mac, vlan, vlan);
    string json;
    Agent *agent = Agent::GetInstance();
    agent->port_ipc_handler()->MakeVmiUuidJson(&entry, json, false);
    string err;
    agent->port_ipc_handler()->AddPortFromJson(json, false, err, false);
    client->WaitForIdle();
}

void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac,
                const string ip6addr) {
    IntfCfgAdd(intf_id, name, ipaddr, vm_id, vn_id, mac,
               VmInterface::kInvalidVlanId, ip6addr);
}

void IntfCfgAdd(PortInfo *input, int id) {
    IntfCfgAdd(input[id].intf_id, input[id].name, input[id].addr,
  	       input[id].vm_id, input[id].vn_id, input[id].mac,
	       input[id].ip6addr);
}

void IntfCfgAddThrift(PortInfo *input, int id) {
    IntfCfgAdd(input, id);
}

void IntfCfgDelNoWait(int id) {
    Agent *agent = Agent::GetInstance();
    string err;
    agent->port_ipc_handler()->DeleteVmiUuidEntry(MakeUuid(id), err);
}

void IntfCfgDel(int id) {
    IntfCfgDelNoWait(id);
    client->WaitForIdle();
}

void IntfCfgDel(PortInfo *input, int id) {
    IntfCfgDel(input[id].intf_id);
}

bool VrfFind(const char *name, bool ret_del) {
    VrfEntry *vrf;
    VrfKey key(name);
    vrf = static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->Find(&key, ret_del));
    return (vrf != NULL);
}

bool VrfFind(const char *name) {
    VrfEntry *vrf;
    VrfKey key(name);
    vrf = static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->FindActiveEntry(&key));
    return (vrf != NULL);
}

VrfEntry *VrfGet(const char *name, bool ret_del) {
    Agent *agent = Agent::GetInstance();
    VrfKey key(name);
    VrfEntry *vrf =
        static_cast<VrfEntry *>(agent->vrf_table()->Find(&key, ret_del));
    if (vrf && (ret_del == false && vrf->IsDeleted()))
        vrf = NULL;

    return vrf;
}

uint32_t GetVrfId(const char *name) {
    VrfEntry *vrf = VrfGet(name, false);
    return vrf->vrf_id();
}

VrfEntry *VrfGet(size_t index) {
    return static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindVrfFromId(index));
}

bool VnFind(int id) {
    VnEntry *vn;
    VnKey key(MakeUuid(id));
    vn = static_cast<VnEntry *>(Agent::GetInstance()->vn_table()->FindActiveEntry(&key));
    return (vn != NULL);
}

VnEntry *VnGet(int id) {
    VnKey key(MakeUuid(id));
    return static_cast<VnEntry *>(Agent::GetInstance()->vn_table()->FindActiveEntry(&key));
}

bool VxlanFind(int id) {
    VxLanId *vxlan;
    VxLanIdKey key(id);
    vxlan = static_cast<VxLanId *>(Agent::GetInstance()->vxlan_table()->FindActiveEntry(&key));
    return (vxlan != NULL);
}

VxLanId *VxlanGet(int id) {
    VxLanIdKey key(id);
    return static_cast<VxLanId *>(Agent::GetInstance()->vxlan_table()->FindActiveEntry(&key));
}

bool AclFind(int id) {
    AclDBEntry *acl;
    AclKey key(MakeUuid(id));
    acl = static_cast<AclDBEntry *>(Agent::GetInstance()->acl_table()->FindActiveEntry(&key));
    return (acl != NULL);
}

AclDBEntry *AclGet(int id) {
    AclKey key(MakeUuid(id));
    return static_cast<AclDBEntry *>(Agent::GetInstance()->acl_table()->FindActiveEntry(&key));
}

VmEntry *VmGet(int id) {
    VmKey key(MakeUuid(id));
    return static_cast<VmEntry *>(Agent::GetInstance()->vm_table()->FindActiveEntry(&key));
}

bool VmFind(int id) {
    VmEntry *vm;
    VmKey key(MakeUuid(id));
    vm = static_cast<VmEntry *>(Agent::GetInstance()->vm_table()->FindActiveEntry(&key));
    return (vm != NULL);
}

bool VmPortFind(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    return (intf != NULL ? !intf->IsDeleted() : false);
}

bool VmPortFindRetDel(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->Find(&key, true));
    return (intf != NULL);
}

uint32_t VmPortGetId(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf) {
        return intf->id();
    }

    return 0;
}

std::string VmPortGetAnalyzerName(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf) {
        return intf->GetAnalyzer();
    }
    return std::string();
}

Interface::MirrorDirection VmPortGetMirrorDirection(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf) {
        return intf->mirror_direction();
    }
    return Interface::UNKNOWN;
}

bool VmPortFind(PortInfo *input, int id) {
    return VmPortFind(input[id].intf_id);
}

bool VmPortL2Active(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->l2_active() == true);
}

bool VmPortL2Active(PortInfo *input, int id) {
    return VmPortL2Active(input[id].intf_id);
}

bool VmPortActive(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->ipv4_active() == true);
}

bool VmPortActive(PortInfo *input, int id) {
    return VmPortActive(input[id].intf_id);
}

bool VmPortV6Active(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->ipv6_active() == true);
}

bool VmPortV6Active(PortInfo *input, int id) {
    return VmPortV6Active(input[id].intf_id);
}

bool VmPortPolicyEnabled(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->policy_enabled());
}

bool VmPortPolicyEnabled(PortInfo *input, int id) {
    return VmPortPolicyEnabled(input[id].intf_id);
}

InetInterface *InetInterfaceGet(const char *ifname) {
    InetInterfaceKey key(ifname);
    return static_cast<InetInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
}

Interface *VmPortGet(int id) {
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    return static_cast<Interface *>(Agent::GetInstance()->interface_table()->Find(&key, false));
}

bool VmPortFloatingIpCount(int id, unsigned int count) {
    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(id));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL)
        return false;

    EXPECT_EQ(intf->floating_ip_list().list_.size(), count);
    if (intf->floating_ip_list().list_.size() != count)
        return false;

    return true;
}

bool QosConfigFind(uint32_t id) {
    AgentQosConfigKey key(MakeUuid(id));

    if (Agent::GetInstance()->qos_config_table()->FindActiveEntry(&key) == NULL) {
        return false;
    }
    return true;
}

const AgentQosConfig*
QosConfigGetByIndex(uint32_t id) {
    return Agent::GetInstance()->qos_config_table()->FindByIndex(id);
}

const AgentQosConfig*
QosConfigGet(uint32_t id) {
    AgentQosConfigKey key(MakeUuid(id));

    return static_cast<AgentQosConfig *>(Agent::GetInstance()->
            qos_config_table()->FindActiveEntry(&key));
}

bool ForwardingClassFind(uint32_t id) {
    ForwardingClassKey key(MakeUuid(id));
    if (Agent::GetInstance()->forwarding_class_table()->
            FindActiveEntry(&key) == NULL) {
        return false;
    }
    return true;
}

ForwardingClass*
ForwardingClassGet(uint32_t id) {
    ForwardingClassKey key(MakeUuid(id));

    return static_cast<ForwardingClass *>(Agent::GetInstance()->
            forwarding_class_table()->FindActiveEntry(&key));
}

bool VmPortAliasIpCount(int id, unsigned int count) {
    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(id));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL)
        return false;

    EXPECT_EQ(intf->alias_ip_list().list_.size(), count);
    if (intf->alias_ip_list().list_.size() != count)
        return false;

    return true;
}

bool VmPortGetStats(PortInfo *input, int id, uint32_t & bytes, uint32_t & pkts) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    AgentUveStats *uve = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    StatsManager *sm  = uve->stats_manager();
    const StatsManager::InterfaceStats *st = sm->GetInterfaceStats(intf);
    if (st == NULL)
        return false;

    bytes = st->in_bytes;
    pkts = st->in_pkts;
    return true;
}

bool VrfStatsMatch(int vrf_id, std::string vrf_name, bool stats_match,
                   const vr_vrf_stats_req &req) {
    AgentUveStats *uve = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    StatsManager *sm  = uve->stats_manager();
    const StatsManager::VrfStats *st = sm->GetVrfStats(vrf_id);
    if (st == NULL) {
        return false;
    }

    if (vrf_name.compare(st->name) != 0) {
        return false;
    }

    if (!stats_match) {
        return true;
    }

    if (st->discards == (uint64_t)req.get_vsr_discards() &&
        st->resolves == (uint64_t)req.get_vsr_resolves() &&
        st->receives == (uint64_t)req.get_vsr_receives() &&
        st->udp_tunnels == (uint64_t)req.get_vsr_udp_tunnels() &&
        st->udp_mpls_tunnels == (uint64_t)req.get_vsr_udp_mpls_tunnels() &&
        st->gre_mpls_tunnels == (uint64_t)req.get_vsr_gre_mpls_tunnels() &&
        st->ecmp_composites == (uint64_t)req.get_vsr_ecmp_composites() &&
        st->l2_mcast_composites == (uint64_t)req.get_vsr_l2_mcast_composites() &&
        st->fabric_composites == (uint64_t)req.get_vsr_fabric_composites() &&
        st->l2_encaps == (uint64_t)req.get_vsr_l2_encaps() &&
        st->encaps == (uint64_t)req.get_vsr_encaps() &&
        st->gros == (uint64_t)req.get_vsr_gros() &&
        st->diags == (uint64_t)req.get_vsr_diags() &&
        st->encap_composites == (uint64_t)req.get_vsr_encap_composites() &&
        st->evpn_composites == (uint64_t)req.get_vsr_evpn_composites() &&
        st->vrf_translates == (uint64_t)req.get_vsr_vrf_translates() &&
        st->vxlan_tunnels == (uint64_t)req.get_vsr_vxlan_tunnels() &&
        st->arp_virtual_proxy == (uint64_t)req.get_vsr_arp_virtual_proxy() &&
        st->arp_virtual_stitch == (uint64_t)req.get_vsr_arp_virtual_stitch() &&
        st->arp_virtual_flood == (uint64_t)req.get_vsr_arp_virtual_flood() &&
        st->arp_physical_stitch == (uint64_t)req.get_vsr_arp_physical_stitch() &&
        st->arp_tor_proxy == (uint64_t)req.get_vsr_arp_tor_proxy() &&
        st->arp_physical_flood == (uint64_t)req.get_vsr_arp_physical_flood() &&
        st->l2_receives == (uint64_t)req.get_vsr_l2_receives() &&
        st->uuc_floods == (uint64_t)req.get_vsr_uuc_floods()) {
        return true;
    }
    return false;
}

bool VrfStatsMatchPrev(int vrf_id, const vr_vrf_stats_req &req) {
    AgentUveStats *uve = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    StatsManager *sm  = uve->stats_manager();
    const StatsManager::VrfStats *st = sm->GetVrfStats(vrf_id);
    if (st == NULL) {
        return false;
    }

    if (st->prev_discards == (uint64_t)req.get_vsr_discards() &&
        st->prev_resolves == (uint64_t)req.get_vsr_resolves() &&
        st->prev_receives == (uint64_t)req.get_vsr_receives() &&
        st->prev_udp_tunnels == (uint64_t)req.get_vsr_udp_tunnels() &&
        st->prev_udp_mpls_tunnels == (uint64_t)req.get_vsr_udp_mpls_tunnels() &&
        st->prev_gre_mpls_tunnels == (uint64_t)req.get_vsr_gre_mpls_tunnels() &&
        st->prev_ecmp_composites == (uint64_t)req.get_vsr_ecmp_composites() &&
        st->prev_l2_mcast_composites ==
            (uint64_t)req.get_vsr_l2_mcast_composites() &&
        st->prev_fabric_composites ==
            (uint64_t)req.get_vsr_fabric_composites() &&
        st->prev_l2_encaps == (uint64_t)req.get_vsr_l2_encaps() &&
        st->prev_encaps == (uint64_t)req.get_vsr_encaps() &&
        st->prev_gros == (uint64_t)req.get_vsr_gros() &&
        st->prev_diags == (uint64_t)req.get_vsr_diags() &&
        st->prev_encap_composites == (uint64_t)req.get_vsr_encap_composites() &&
        st->prev_evpn_composites == (uint64_t)req.get_vsr_evpn_composites() &&
        st->prev_vrf_translates == (uint64_t)req.get_vsr_vrf_translates() &&
        st->prev_vxlan_tunnels == (uint64_t)req.get_vsr_vxlan_tunnels() &&
        st->prev_arp_virtual_proxy ==
            (uint64_t)req.get_vsr_arp_virtual_proxy() &&
        st->prev_arp_virtual_stitch ==
            (uint64_t)req.get_vsr_arp_virtual_stitch() &&
        st->prev_arp_virtual_flood ==
            (uint64_t)req.get_vsr_arp_virtual_flood() &&
        st->prev_arp_physical_stitch ==
            (uint64_t)req.get_vsr_arp_physical_stitch() &&
        st->prev_arp_tor_proxy == (uint64_t)req.get_vsr_arp_tor_proxy() &&
        st->prev_arp_physical_flood ==
            (uint64_t)req.get_vsr_arp_physical_flood() &&
        st->prev_l2_receives == (uint64_t)req.get_vsr_l2_receives() &&
        st->prev_uuc_floods == (uint64_t)req.get_vsr_uuc_floods()) {
        return true;
    }
    return false;
}

bool VmPortStats(PortInfo *input, int id, uint32_t bytes, uint32_t pkts) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    AgentUveStats *uve = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    StatsManager *sm  = uve->stats_manager();
    const StatsManager::InterfaceStats *st = sm->GetInterfaceStats(intf);
    if (st == NULL)
        return false;

    if (st->in_pkts == pkts && st->in_bytes == bytes)
        return true;
    return false;
}

bool VmPortStatsMatch(Interface *intf, uint32_t ibytes, uint32_t ipkts,
                      uint32_t obytes, uint32_t opkts) {
    AgentUveStats *uve = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    StatsManager *sm  = uve->stats_manager();
    const StatsManager::InterfaceStats *st = sm->GetInterfaceStats(intf);
    EXPECT_TRUE(st != NULL);
    if (st == NULL)
        return false;

    if (st->in_pkts == ipkts && st->in_bytes == ibytes &&
        st->out_pkts == opkts && st->out_bytes == obytes) {
        return true;
    }
    LOG(DEBUG, "ipkts " << st->in_pkts << " ibytes " << st->in_bytes <<
               " opkts " << st->out_pkts << " obytes " << st->out_bytes);
    return false;
}

bool VmPortL2Inactive(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;
    return (intf->l2_active() == false);
}

bool VmPortL2Inactive(PortInfo *input, int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->l2_active() == false);
}

bool VmPortInactive(int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;
    return (intf->ipv4_active() == false);
}

bool VmPortInactive(PortInfo *input, int id) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->ipv4_active() == false);
}

PhysicalInterface *EthInterfaceGet(const char *name) {
    PhysicalInterface *intf;
    PhysicalInterfaceKey key(name);
    intf=static_cast<PhysicalInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    return intf;
}

VmInterface *VmInterfaceGet(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    return intf;
}

bool VmPortPolicyEnable(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->policy_enabled() == true);
}

bool VmPortPolicyEnable(PortInfo *input, int id) {
    return VmPortPolicyEnable(input[id].intf_id);
}

bool VmPortPolicyDisable(int id) {
    VmInterface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(id), "");
    intf = static_cast<VmInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    return (intf->policy_enabled() == false);
}
bool VmPortPolicyDisable(PortInfo *input, int id) {
    return VmPortPolicyDisable(input[id].intf_id);
}

bool DBTableFind(const string &table_name) {
   return (Agent::GetInstance()->db()->FindTable(table_name) != NULL);
}

void VnAddReq(int id, const char *name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, nil_uuid(),
                                              name, ipam, vn_ipam_data, id,
                                              (id + 100), true, true, false,
                                              false, false, false);
    usleep(1000);
}

void VnAddReq(int id, const char *name, int acl_id) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name,
                                              MakeUuid(acl_id),
                                              name, ipam, vn_ipam_data, id,
                                              (id + 100), true, true, false,
                                              false, false, false);
    usleep(1000);
}

void VnAddReq(int id, const char *name, int acl_id, const char *vrf_name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name,
                                              MakeUuid(acl_id), vrf_name, ipam,
                                              vn_ipam_data, id, (id + 100),
                                              true, true, false,
                                              false, false, false);
    usleep(1000);
}

void VnAddReq(int id, const char *name, const char *vrf_name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, nil_uuid(),
                                              vrf_name, ipam, vn_ipam_data, id,
                                              (id + 100), true, true, false,
                                              false, false, false);
    usleep(1000);
}

void VnVxlanAddReq(int id, const char *name, uint32_t vxlan_id) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, nil_uuid(),
                                              name, ipam, vn_ipam_data, id,
                                              vxlan_id, true, true, false,
                                              false, false, false);
}

void VnDelReq(int id) {
    Agent::GetInstance()->vn_table()->DelVn(MakeUuid(id));
    usleep(1000);
}

void VrfAddReq(const char *name) {
    Agent::GetInstance()->vrf_table()->CreateVrfReq(name);
    usleep(1000);
}

void VrfAddReq(const char *name, const boost::uuids::uuid &vn_uuid) {
    Agent::GetInstance()->vrf_table()->CreateVrfReq(name, vn_uuid);
    usleep(1000);
}

void VrfDelReq(const char *name) {
    Agent::GetInstance()->vrf_table()->DeleteVrfReq(name);
    usleep(1000);
}

void VmAddReq(int id) {
    char vm_name[80];
    VmKey *key = new VmKey(MakeUuid(id));
    VmData::SGUuidList sg_list(0);
    sprintf(vm_name, "vm%d", id);
    VmData *data = new VmData(Agent::GetInstance(), NULL, string(vm_name),
                              sg_list);
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);

    Agent::GetInstance()->vm_table()->Enqueue(&req);
    usleep(1000);
}

void VmDelReq(int id) {
    VmKey *key = new VmKey(MakeUuid(id));
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);

    Agent::GetInstance()->vm_table()->Enqueue(&req);
    usleep(1000);
}

void AclAddReq(int id) {
    AclSpec *acl_spec = new AclSpec();
    AclEntrySpec *entry_spec = new AclEntrySpec();
    entry_spec->id = id;
    entry_spec->terminal = false;
    acl_spec->acl_entry_specs_.push_back(*entry_spec);
    delete entry_spec;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    AclKey *key = new AclKey(MakeUuid(id));
    req.key.reset(key);

    AclData *data = new AclData(Agent::GetInstance(), NULL, *acl_spec);
    req.data.reset(data);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
    delete acl_spec;
    usleep(1000);
}

void AclDelReq(int id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    AclKey *key = new AclKey(MakeUuid(id));
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
    usleep(1000);
}

void AclAddReq(int id, int ace_id, bool drop) {
    AclSpec *acl_spec = new AclSpec();
    acl_spec->acl_id = MakeUuid(id);

    AclEntrySpec *ae_spec = new AclEntrySpec();
    ae_spec->id = ace_id;
    ae_spec->terminal = true;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    if (drop) {
        action.simple_action = TrafficAction::DENY;
    } else {
        action.simple_action = TrafficAction::PASS;
    }
    ae_spec->action_l.push_back(action);
    acl_spec->acl_entry_specs_.push_back(*ae_spec);
    delete ae_spec;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    AclKey *key = new AclKey(MakeUuid(id));
    AclData *data = new AclData(Agent::GetInstance(), NULL, *acl_spec);

    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
    delete acl_spec;
    usleep(1000);
}

void DeleteRoute(const char *vrf, const char *ip) {
    Ip4Address addr = Ip4Address::from_string(ip);
    Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(NULL,
                                            vrf, addr, 32, NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
}

void DeleteRoute(const char *vrf, const char *ip, uint8_t plen,
                 const Peer *peer) {
    Ip4Address addr = Ip4Address::from_string(ip);
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    if (bgp_peer == NULL) {
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            DeleteReq(peer, vrf, addr, plen, NULL);
    } else {
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            DeleteReq(peer, vrf, addr, plen,
                      new ControllerVmRoute(bgp_peer));
    }
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind(vrf, addr, 32) == false));
}

bool RouteFind(const string &vrf_name, const Ip4Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    if (!vrf->GetInet4UnicastRouteTable()) {
        return false;
    }

    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (vrf->GetInet4UnicastRouteTable()->FindActiveEntry(&key));
    return (route != NULL);
}

bool RouteFind(const string &vrf_name, const string &addr, int plen) {
    return RouteFind(vrf_name, Ip4Address::from_string(addr), plen);
}

bool RouteFindV6(const string &vrf_name, const Ip6Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (vrf->GetInet6UnicastRouteTable()->FindActiveEntry(&key));
    return (route != NULL);
}

bool RouteFindV6(const string &vrf_name, const string &addr, int plen) {
    return RouteFindV6(vrf_name, Ip6Address::from_string(addr), plen);
}

bool L2RouteFind(const string &vrf_name, const MacAddress &mac,
                 const IpAddress &ip_addr) {
    if (Agent::GetInstance()->vrf_table() == NULL) {
        return false;
    }
    BridgeAgentRouteTable *bridge_table =
        static_cast<BridgeAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->GetBridgeRouteTable(vrf_name));
    if (bridge_table == NULL)
        return false;
    BridgeRouteEntry *route = bridge_table->FindRoute(mac);
    return (route != NULL);
}

bool L2RouteFind(const string &vrf_name, const MacAddress &mac) {
    return L2RouteFind(vrf_name, mac, IpAddress());
}

bool MCRouteFind(const string &vrf_name, const Ip4Address &grp_addr,
                 const Ip4Address &src_addr) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    Inet4MulticastRouteKey key(vrf_name, src_addr, grp_addr);
    Inet4MulticastRouteEntry *route =
        static_cast<Inet4MulticastRouteEntry *>
        (static_cast<Inet4MulticastAgentRouteTable *>(vrf->
             GetInet4MulticastRouteTable())->FindActiveEntry(&key));
    return (route != NULL);
}

bool MCRouteFind(const string &vrf_name, const string &grp_addr,
                 const string &src_addr) {
    return MCRouteFind(vrf_name, Ip4Address::from_string(grp_addr),
                       Ip4Address::from_string(src_addr));
}

bool MCRouteFind(const string &vrf_name, const Ip4Address &grp_addr) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    Inet4MulticastRouteKey key(vrf_name, grp_addr);
    Inet4MulticastRouteEntry *route =
        static_cast<Inet4MulticastRouteEntry *>
        (static_cast<Inet4MulticastAgentRouteTable *>(vrf->
             GetInet4MulticastRouteTable())->FindActiveEntry(&key));
    return (route != NULL);
}

bool MCRouteFind(const string &vrf_name, const string &grp_addr) {
    return MCRouteFind(vrf_name, Ip4Address::from_string(grp_addr));

}

BridgeRouteEntry *L2RouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr) {
    Agent *agent = Agent::GetInstance();
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    BridgeRouteKey key(agent->local_vm_peer(), vrf_name, mac);
    BridgeRouteEntry *route =
        static_cast<BridgeRouteEntry *>
        (static_cast<BridgeAgentRouteTable *>(vrf->
             GetBridgeRouteTable())->FindActiveEntry(&key));
    return route;
}

BridgeRouteEntry *L2RouteGet(const string &vrf_name,
                             const MacAddress &mac) {
    return L2RouteGet(vrf_name, mac, IpAddress());
}

const NextHop* L2RouteToNextHop(const string &vrf, const MacAddress &mac) {
    BridgeRouteEntry* rt = L2RouteGet(vrf, mac);
    if (rt == NULL)
        return NULL;

    return rt->GetActiveNextHop();
}

EvpnRouteEntry *EvpnRouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr, uint32_t ethernet_tag) {
    Agent *agent = Agent::GetInstance();
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    EvpnRouteKey key(agent->local_vm_peer(), vrf_name, mac, ip_addr,
                     ethernet_tag);
    EvpnRouteEntry *route =
        static_cast<EvpnRouteEntry *>
        (static_cast<EvpnAgentRouteTable *>
         (vrf->GetEvpnRouteTable())->FindActiveEntry(&key));
    return route;
}

InetUnicastRouteEntry* RouteGet(const string &vrf_name, const Ip4Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (vrf->GetInet4UnicastRouteTable()->FindActiveEntry(&key));
    return route;
}

InetUnicastRouteEntry* RouteGetV6(const string &vrf_name, const Ip6Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (vrf->GetInet6UnicastRouteTable()->FindActiveEntry(&key));
    return route;
}

const NextHop* RouteToNextHop(const string &vrf_name, const Ip4Address &addr,
                              int plen) {
    InetUnicastRouteEntry* rt = RouteGet(vrf_name, addr, plen);
    if (rt == NULL)
        return NULL;

    return rt->GetActiveNextHop();
}

Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const Ip4Address &grp_addr) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Inet4MulticastRouteKey key(vrf_name, grp_addr);
    Inet4MulticastRouteEntry *route =
        static_cast<Inet4MulticastRouteEntry *>
        (static_cast<Inet4MulticastAgentRouteTable *>(vrf->
             GetInet4MulticastRouteTable())->FindActiveEntry(&key));
    return route;
}

Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const string &grp_addr) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Inet4MulticastRouteKey key(vrf_name, Ip4Address::from_string(grp_addr));
    Inet4MulticastRouteEntry *route =
        static_cast<Inet4MulticastRouteEntry *>
        (static_cast<Inet4MulticastAgentRouteTable *>(vrf->
             GetInet4MulticastRouteTable())->FindActiveEntry(&key));
    return route;
}

bool TunnelNHFind(const Ip4Address &server_ip, bool policy, TunnelType::Type type) {
    TunnelNHKey key(Agent::GetInstance()->fabric_vrf_name(),
                    Agent::GetInstance()->router_id(), server_ip,
                    policy, type);
    NextHop *nh = static_cast<NextHop *>(Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key));
    return (nh != NULL);
}

NextHop *ReceiveNHGet(NextHopTable *table, const char *ifname, bool policy) {
    ReceiveNHKey key(new InetInterfaceKey(ifname), policy);
    return static_cast<NextHop *> (table->FindActiveEntry(&key));
}

bool TunnelNHFind(const Ip4Address &server_ip) {
    return TunnelNHFind(server_ip, false, TunnelType::MPLS_GRE);
}

bool VlanNhFind(int id, uint16_t tag) {
    VlanNHKey key(MakeUuid(id), tag);
    NextHop *nh = static_cast<NextHop *>(Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key));
    return (nh != NULL);
}

bool BridgeTunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const IpAddress &vm_addr, uint8_t plen, bool leaf) {
    VnListType vn_list;
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, server_ip,
                              bmap, label, vn_list, SecurityGroupList(),
                              PathPreference(), false, EcmpLoadBalance(),
                              leaf);
    EvpnAgentRouteTable::AddRemoteVmRouteReq(peer, vm_vrf, remote_vm_mac,
                                        vm_addr, 0, data);
    return true;
}

bool BridgeTunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const char *server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const char *vm_addr, uint8_t plen, bool leaf) {
    boost::system::error_code ec;
    BridgeTunnelRouteAdd(peer, vm_vrf, bmap,
                        Ip4Address::from_string(server_ip, ec), label, remote_vm_mac,
                        IpAddress::from_string(vm_addr, ec), plen, leaf);
}

bool EcmpTunnelRouteAdd(const BgpPeer *peer, const string &vrf_name,
                        const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const PathPreference &path_preference) {
    COMPOSITETYPE type = Composite::ECMP;
    if (local_ecmp) {
        type = Composite::LOCAL_ECMP;
    }
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false,
                                        comp_nh_list, vrf_name));
    nh_req.data.reset(new CompositeNHData());

    VnListType vn_list;
    vn_list.insert(vn_name);
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(peer, vm_ip, plen, vn_list, -1, false, vrf_name,
                                sg, path_preference, TunnelType::MplsType(),
                                EcmpLoadBalance(), nh_req);
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vrf_name, vm_ip, plen, data);
}

bool EcmpTunnelRouteAdd(const BgpPeer *peer, const string &vrf_name,
                        const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const PathPreference &path_preference,
                       EcmpLoadBalance &ecmp_load_balance) {
    COMPOSITETYPE type = Composite::ECMP;
    if (local_ecmp) {
        type = Composite::LOCAL_ECMP;
    }
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false,
                                        comp_nh_list, vrf_name));
    nh_req.data.reset(new CompositeNHData());

    VnListType vn_list;
    vn_list.insert(vn_name);
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(peer, vm_ip, plen, vn_list, -1, false, vrf_name,
                                sg, path_preference, TunnelType::MplsType(),
                                ecmp_load_balance, nh_req);
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vrf_name, vm_ip, plen, data);
}

bool Inet6TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf, const Ip6Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference) {
    VnListType vn_list;
    vn_list.insert(dest_vn_name);
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, server_ip,
                              bmap, label, vn_list, sg,
                              path_preference, false, EcmpLoadBalance(),
                              false);
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vm_vrf,
                                        vm_addr, plen, data);
    return true;
}

bool EcmpTunnelRouteAdd(Agent *agent, const BgpPeer *peer, const string &vrf,
                        const string &prefix, uint8_t plen,
                        const string &remote_server_1, uint32_t label1,
                        const string &remote_server_2, uint32_t label2,
                        const string &vn) {
    Ip4Address remote_server_ip1 = Ip4Address::from_string(remote_server_1);
    Ip4Address remote_server_ip2 = Ip4Address::from_string(remote_server_2);
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(label1,
                                                  agent->fabric_vrf_name(),
                                                  agent->router_id(),
                                                  remote_server_ip1, false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyPtr nh_data2(new ComponentNHKey(label2,
                                                  agent->fabric_vrf_name(),
                                                  agent->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(peer, vrf, Ip4Address::from_string(prefix), plen,
                       comp_nh_list, false, vn, sg_id_list, PathPreference());
    client->WaitForIdle();
}

bool Inet4TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf, const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference) {
    VnListType vn_list;
    vn_list.insert(dest_vn_name);
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, server_ip,
                              bmap, label, vn_list, sg,
                              path_preference, false, EcmpLoadBalance(),
                              false);
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vm_vrf,
                                        vm_addr, plen, data);
    return true;
}

bool Inet4TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf, char *vm_addr,
                         uint8_t plen, char *server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference) {
    boost::system::error_code ec;
    Inet4TunnelRouteAdd(peer, vm_vrf, Ip4Address::from_string(vm_addr, ec), plen,
                        Ip4Address::from_string(server_ip, ec), bmap, label,
                        dest_vn_name, sg, path_preference);
}

bool TunnelRouteAdd(const char *server, const char *vmip, const char *vm_vrf,
                    int label, const char *vn, TunnelType::TypeBmap bmap) {
    boost::system::error_code ec;
    VnListType vn_list;
    if (vn) vn_list.insert(vn);
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(bgp_peer_,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, Ip4Address::from_string(server, ec),
                              TunnelType::AllType(), label, vn_list,
                              SecurityGroupList(), PathPreference(), false,
                              EcmpLoadBalance(), false);
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, vm_vrf,
                                        Ip4Address::from_string(vmip, ec),
                                        32, data);
    return true;
}

bool TunnelRouteAdd(const char *server, const char *vmip, const char *vm_vrf,
                    int label, const char *vn) {
    return TunnelRouteAdd(server, vmip, vm_vrf, label, vn,
                          TunnelType::AllType());
}

bool AddArp(const char *ip, const char *mac_str, const char *ifname) {
    MacAddress mac(mac_str);
    Interface *intf;
    PhysicalInterfaceKey key(ifname);
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    boost::system::error_code ec;
    VnListType vn_list;
    InetUnicastAgentRouteTable::ArpRoute(DBRequest::DB_ENTRY_ADD_CHANGE,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Ip4Address::from_string(ip, ec), mac,
                              Agent::GetInstance()->fabric_vrf_name(),
                              *intf, true, 32, false, vn_list, SecurityGroupList());

    return true;
}

bool DelArp(const string &ip, const char *mac_str, const string &ifname) {
    MacAddress mac(mac_str);
    Interface *intf;
    PhysicalInterfaceKey key(ifname);
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    boost::system::error_code ec;
    VnListType vn_list;
    InetUnicastAgentRouteTable::ArpRoute(DBRequest::DB_ENTRY_DELETE,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Ip4Address::from_string(ip, ec),
                              mac, Agent::GetInstance()->fabric_vrf_name(), *intf,
                              false, 32, false, vn_list, SecurityGroupList());
    return true;
}

void AddVm(const char *name, int id) {
    AddNode("virtual-machine", name, id);
}

void DelVm(const char *name) {
    DelNode("virtual-machine", name);
}

void AddVrf(const char *name, int id, bool default_ri) {
    std::stringstream str;
    str << "    <routing-instance-is-default>" << default_ri << "</routing-instance-is-default>" << endl;
    char buff[10240];
    int len = 0;
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "routing-instance", name, id, str.str().c_str());
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

void DelVrf(const char *name) {
    DelNode("routing-instance", name);
}

void AddBridgeDomain(const char *name, uint32_t id, uint32_t isid,
                     bool mac_learning) {
    std::stringstream str;
    str << "<isid>" << isid << "</isid>" << endl;
    str<< "<mac-learning-enabled>" << mac_learning << "</mac-learning-enabled>";

    char buff[10240];
    int len = 0;
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "bridge-domain", name, id, str.str().c_str());
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
}

void ModifyForwardingModeVn(const string &name, int id, const string &fw_mode) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>" << fw_mode << "</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name.c_str(), id, str.str().c_str());
}

void AddL3Vn(const char *name, int id) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <forwarding-mode>l3</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str());
}

void AddL2L3Vn(const char *name, int id, bool admin_state) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>l2_l3</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str(), admin_state);
}

void AddL2Vn(const char *name, int id) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>l2</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str());
}

// default admin_state is true
void AddVn(const char *name, int id, bool admin_state) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <rpf>enable</rpf>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str(), admin_state);
}

void AddVn(const char *name, int id, int vxlan_id, bool admin_state) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <vxlan-network-identifier>" << vxlan_id << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>l2_l3</forwarding-mode>" << endl;
    str << "    <rpf>enable</rpf>" << endl;
    str << "</virtual-network-properties>" << endl;
    str << "<virtual-network-network-id>" << vxlan_id << "</virtual-network-network-id>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str(), admin_state);
}


void DelVn(const char *name) {
    DelNode("virtual-network", name);
}

void AddSriovPort(const char *name, int id) {
    std::stringstream str;
    str << "<virtual-machine-interface-mac-addresses>" << endl;
    str << "    <mac-address>00:00:00:00:00:" << id << "</mac-address>"
        << endl;
    str << "</virtual-machine-interface-mac-addresses>" << endl;
    str << "<display-name> " << name << "</display-name>" << endl;

    //vnic type as direct makes the port sriov
    str << "<virtual-machine-interface-bindings>";
    str << "<key-value-pair>";
    str << "<key>vnic_type</key>";
    str << "<value>direct</value>";
    str << "</key-value-pair>";
    str << "</virtual-machine-interface-bindings>";

    char buff[4096];
    strcpy(buff, str.str().c_str());
    AddNode("virtual-machine-interface", name, id, buff);

}

void AddPort(const char *name, int id, const char *attr) {
    std::stringstream str;
    str << "<virtual-machine-interface-mac-addresses>" << endl;
    str << "    <mac-address>00:00:00:00:00:" << id << "</mac-address>"
        << endl;
    str << "</virtual-machine-interface-mac-addresses>" << endl;
    str << "<display-name> " << name << "</display-name>" << endl;

    char buff[4096];
    strcpy(buff, str.str().c_str());
    AddNode("virtual-machine-interface", name, id, buff);
}

void AddPortWithMac(const char *name, int id, const char *mac,
                    const char *attr) {
    std::stringstream str;
    str << "<virtual-machine-interface-mac-addresses>" << endl;
    str << "    <mac-address>" << mac << "</mac-address>"
        << endl;
    str << "</virtual-machine-interface-mac-addresses>" << endl;

    char buff[4096];
    strcpy(buff, str.str().c_str());
    if (attr != NULL)
        strcat(buff, attr);
    AddNode("virtual-machine-interface", name, id, buff);
}

void AddPortByStatus(const char *name, int id, bool admin_status) {
    AddNodeByStatus("virtual-machine-interface", name, id, admin_status);
}

void DelPort(const char *name) {
    DelNode("virtual-machine-interface", name);
}

void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *rt,
                            int count) {
    std::ostringstream o_str;

    for (int i = 0; i < count; i++) {
        o_str << "<route>\n"
              << "<prefix>" << rt->addr_.to_string()
              << "/" << rt->plen_ << "</prefix>\n"
              << "<next-hop>\" \"</next-hop>\n"
              << "<next-hop-type>\" \"</next-hop-type>\n"
              << "</route>\n";
        rt++;
    }

    char buff[10240];
    sprintf(buff, "<interface-route-table-routes>\n"
                  "%s"
                  "</interface-route-table-routes>\n", o_str.str().c_str());
    AddNode("interface-route-table", name, id, buff);
}

void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *rt,
                            int count, const char *nexthop) {
    std::ostringstream o_str;

    for (int i = 0; i < count; i++) {
        o_str << "<route>\n"
              << "<prefix>" << rt->addr_.to_string()
              << "/" << rt->plen_ << "</prefix>\n"
              << "<next-hop>" << nexthop << "</next-hop>\n"
              << "<next-hop-type>\" \"</next-hop-type>\n"
              << "</route>\n";
        rt++;
    }

    char buff[10240];
    sprintf(buff, "<interface-route-table-routes>\n"
                  "%s"
                  "</interface-route-table-routes>\n", o_str.str().c_str());
    AddNode("interface-route-table", name, id, buff);
}

void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *rt,
                            int count, const char *nexthop,
                            const std::vector<std::string> &communities) {
    std::ostringstream o_str;

    for (int i = 0; i < count; i++) {
        o_str << "<route>\n"
              << "<prefix>" << rt->addr_.to_string()
              << "/" << rt->plen_ << "</prefix>\n";
        if (nexthop) {
            o_str << "<next-hop>" << nexthop << "</next-hop>\n";
        }
        o_str << "<next-hop-type>\" \"</next-hop-type>\n";
        o_str << "<community-attributes>\n";
        BOOST_FOREACH(string community, communities) {
            o_str << "<community-attribute>" 
                  << community 
                  << "</community-attribute>\n";
        }
        o_str << "</community-attributes>\n";
        o_str << "</route>\n";
        rt++;
    }

    char buff[10240];
    sprintf(buff, "<interface-route-table-routes>\n"
                  "%s"
                  "</interface-route-table-routes>\n", o_str.str().c_str());
    AddNode("interface-route-table", name, id, buff);
}

void AddInterfaceRouteTableV6(const char *name, int id, TestIp6Prefix *rt,
                              int count) {
    std::ostringstream o_str;

    for (int i = 0; i < count; i++) {
        o_str << "<route>\n"
              << "<prefix>" << rt->addr_.to_string()
              << "/" << rt->plen_ << "</prefix>\n"
              << "<next-hop>\" \"</next-hop>\n"
              << "<next-hop-type>\" \"</next-hop-type>\n"
              << "</route>\n";
        rt++;
    }

    char buff[10240];
    sprintf(buff, "<interface-route-table-routes>\n"
                  "%s"
                  "</interface-route-table-routes>\n", o_str.str().c_str());
    AddNode("interface-route-table", name, id, buff);
}

void StartAcl(string *str, const char *name, int id) {
    char buff[1024];

    sprintf(buff,
            "<?xml version=\"1.0\"?>\n"
            "<config>\n"
            "   <update>\n"
            "       <node type=\"access-control-list\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "           </id-perms>\n"
            "           <access-control-list-entries>\n",
            name, id);
    *str = string(buff);
    return;
}

void EndAcl(string *str) {
    char buff[512];
    sprintf(buff,
            "           </access-control-list-entries>\n"
            "       </node>\n"
            "   </update>\n"
            "</config>\n");
    string s(buff);
    *str += s;
    return;
}

void AddAceEntry(string *str, const char *src_vn, const char *dst_vn,
                 const char *proto, uint16_t sport_start, uint16_t sport_end,
                 uint16_t dport_start, uint16_t dport_end,
                 const char *action, const std::string &vrf_assign,
                 const std::string &mirror_ip, const std::string &qos_action) {
    char buff[2048];

    std::ostringstream mirror;

    if (mirror_ip != "") {
        mirror << "<mirror-to>";
        mirror << "<analyzer-name>mirror-1</analyzer-name>";
        mirror << "<analyzer-ip-address>" << mirror_ip << "</analyzer-ip-address>";
        mirror << "<routing-instance>" << "" << "</routing-instance>";
        mirror << "<udp-port>" << "8159" << "</udp-port>";
        mirror << "</mirror-to>";
    } else {
        mirror << "";
    }

    sprintf(buff,
            "                <acl-rule>\n"
            "                    <match-condition>\n"
            "                        <protocol>%s</protocol>\n"
            "                        <src-address>\n"
            "                            <virtual-network>%s</virtual-network>\n"
            "                        </src-address>\n"
            "                        <src-port>\n"
            "                            <start-port>%d</start-port>\n"
            "                            <end-port>%d</end-port>\n"
            "                        </src-port>\n"
            "                        <dst-address>\n"
            "                            <virtual-network>%s</virtual-network>\n"
            "                        </dst-address>\n"
            "                        <dst-port>\n"
            "                            <start-port>%d</start-port>\n"
            "                            <end-port>%d</end-port>\n"
            "                        </dst-port>\n"
            "                    </match-condition>\n"
            "                    <action-list>\n"
            "                        <simple-action>%s</simple-action>\n"
            "                        %s\n"
            "                        <assign-routing-instance>%s</assign-routing-instance>\n"
            "                        <qos-action>%s</qos-action>\n"
            "                    </action-list>\n"
            "                </acl-rule>\n",
        proto, src_vn, sport_start, sport_end, dst_vn, dport_start, dport_end,
        action, mirror.str().c_str(), vrf_assign.c_str(), qos_action.c_str());
    string s(buff);
    *str += s;
    return;
}

static string AddAclXmlString(const char *node_name, const char *name, int id,
                              const char *src_vn, const char *dest_vn,
                              const char *action, const std::string &vrf_assign,
                              const std::string &mirror_ip,
                              const std::string &qos_action) {
    string str;
    StartAcl(&str, name, id);
    AddAceEntry(&str, src_vn, dest_vn, "any", 10, 20, 10, 20, action,
                vrf_assign, mirror_ip, qos_action);
    AddAceEntry(&str, dest_vn, src_vn, "any", 10, 20, 10, 20, action,
                vrf_assign, mirror_ip, qos_action);
    EndAcl(&str);
    return str;
}

void AddAcl(const char *name, int id) {
    AddNode("access-control-list", name, id);
}

void DelAcl(const char *name) {
    DelNode("access-control-list", name);
}

void AddAcl(const char *name, int id, const char *src_vn, const char *dest_vn,
            const char *action) {
    std::string s = AddAclXmlString("access-control-list", name, id,
                                    src_vn, dest_vn, action, "", "", "");
    ApplyXmlString(s.c_str());
}

void AddVrfAssignNetworkAcl(const char *name, int id, const char *src_vn,
                            const char *dest_vn, const char *action,
                            std::string vrf_name) {
    std::string s = AddAclXmlString("access-control-list", name, id,
                                    src_vn, dest_vn, action, vrf_name, "", "");
    ApplyXmlString(s.c_str());
}

void AddQosAcl(const char *name, int id, const char *src_vn,
               const char *dest_vn, const char *action,
               std::string qos_config) {
    std::string s = AddAclXmlString("access-control-list", name, id,
                                     src_vn, dest_vn, action, "", "", qos_config);
    ApplyXmlString(s.c_str());
}

void AddMirrorAcl(const char *name, int id, const char *src_vn,
                  const char *dest_vn, const char *action,
                  std::string mirror_ip) {
    std::string s = AddAclXmlString("access-control-list", name, id,
            src_vn, dest_vn, action, "", mirror_ip, "");
    ApplyXmlString(s.c_str());
}

void DelOperDBAcl(int id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    AclSpec acl_spec;
    AclKey *key = new AclKey(MakeUuid(id));
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
}

void AddSg(const char *name, int id, int sg_id) {
    char buff[128];
    sprintf(buff, "<security-group-id>%d</security-group-id>", sg_id);
    AddNode("security-group", name, id, buff);
}

void AddFloatingIp(const char *name, int id, const char *addr,
                   const char *fixed_ip, const char *direction,
                   bool port_map_enable,
                   uint16_t port_map1, uint16_t port_map2, uint16_t port_map3,
                   uint16_t port_map4) {
    uint16_t port_map[4] = { port_map1, port_map2, port_map3, port_map4 };
    ostringstream str;
    str << "<floating-ip-address>" << addr << "</floating-ip-address>" << endl;
    str << "<floating-ip-fixed-ip-address>" << fixed_ip <<
           "</floating-ip-fixed-ip-address>" << endl;
    if (direction != NULL) {
        str << "<floating-ip-traffic-direction>" << direction
            << "</floating-ip-traffic-direction>" << endl;
    }

    str << "<floating-ip-port-mappings-enable>";
    if (port_map_enable)
        str << "true";
    else
        str << "false";
    str << "</floating-ip-port-mappings-enable>" << endl;
    str << "<floating-ip-port-mappings>" << endl;
    for (int i = 0; i < 4; i++) {
        if (port_map[i]) {
            str << "    <port-mappings>" << endl;
            str << "        <protocol>tcp</protocol>" << endl;
            str << "        <src-port>" << port_map[i] << "</src-port>" << endl;
            str << "        <dst-port>" << port_map[i] + 1000 << "</dst-port>"
                << endl;
            str << "    </port-mappings>" << endl;
            str << "    <port-mappings>" << endl;
            str << "        <protocol>udp</protocol>" << endl;
            str << "        <src-port>" << port_map[i] << "</src-port>" << endl;
            str << "        <dst-port>" << port_map[i] + 1000 << "</dst-port>"
                << endl;
            str << "    </port-mappings>" << endl;
        }
    }
    str << "</floating-ip-port-mappings>";

    AddNode("floating-ip", name, id, str.str().c_str());
}

void DelFloatingIp(const char *name) {
    DelNode("floating-ip", name);
}

void AddFloatingIpPool(const char *name, int id) {
    AddNode("floating-ip-pool", name, id);
}

void DelFloatingIpPool(const char *name) {
    DelNode("floating-ip-pool", name);
}

void AddAliasIp(const char *name, int id, const char *addr) {
    char buff[128];

    sprintf(buff, "<alias-ip-address>%s</alias-ip-address>", addr);
    AddNode("alias-ip", name, id, buff);
}

void DelAliasIp(const char *name) {
    DelNode("alias-ip", name);
}

void AddAliasIpPool(const char *name, int id) {
    AddNode("alias-ip-pool", name, id);
}

void DelAliasIpPool(const char *name) {
    DelNode("alias-ip-pool", name);
}

void AddInstanceIp(const char *name, int id, const char *addr) {
    char buf[256];

    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>"
                 "<instance-ip-mode>active-backup</instance-ip-mode>", addr);
    AddNode("instance-ip", name, id, buf);
}

void AddActiveActiveInstanceIp(const char *name, int id, const char *addr) {
    char buf[128];
    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>"
                 "<instance-ip-mode>active-active</instance-ip-mode>", addr);
    AddNode("instance-ip", name, id, buf);
}

void AddHealthCheckServiceInstanceIp(const char *name, int id,
                                     const char *addr) {
    char buf[256];

    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>"
                 "<service-health-check-ip>true</service-health-check-ip>"
                 "<instance-ip-mode>active-backup</instance-ip-mode>", addr);
    AddNode("instance-ip", name, id, buf);
}

void AddServiceInstanceIp(const char *name, int id, const char *addr, bool ecmp,
                          const char *tracking_ip) {
    char buf[512];
    char mode[256];

    if (ecmp) {
         sprintf(mode, "active-active");
    } else {
        sprintf(mode, "active-backup");
    }

    char tracking_ip_buf[256] = "0.0.0.0";
    if (tracking_ip) {
        sprintf(tracking_ip_buf, "%s", tracking_ip);
    }

    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>"
                 "<service-instance-ip>true</service-instance-ip>"
                 "<instance-ip-mode>%s</instance-ip-mode>"
                 "<secondary-ip-tracking-ip>"
                 "    <ip-prefix>%s</ip-prefix>"
                 "    <ip-prefix-len>32</ip-prefix-len>"
                 "</secondary-ip-tracking-ip>", addr, mode,
                 tracking_ip_buf);
    AddNode("instance-ip", name, id, buf);
}

void DelInstanceIp(const char *name) {
    DelNode("instance-ip", name);
}

void AddSubnetType(const char *name, int id, const char *addr, uint8_t plen) {

    char buf[1024];
    sprintf(buf, "<subnet-ip-prefix>\n"
                 "<ip-prefix>%s</ip-prefix>\n"
                 "<ip-prefix-len>%d</ip-prefix-len>\n"
                 "</subnet-ip-prefix>", addr, plen);
    AddNode("subnet", name, id, buf);
}

void AddPhysicalDevice(const char *name, int id) {
    char buf[1024];
    sprintf(buf, "<physical-router-vendor-name>Juniper</physical-router-vendor-name>"
                 "<display-name>%s</display-name>", name);
    AddNode("physical-router", name, id, buf);
}

void DeletePhysicalDevice(const char *name) {
    DelNode("physical-router", name);
}

void AddPhysicalInterface(const char *name, int id, const char* display_name) {
    char buf[128];
    sprintf(buf, "<user-visible>Juniper</user-visible>"
                 "<display-name>%s</display-name>", display_name);
    AddNode("physical-interface", name, id, buf);
}

void DeletePhysicalInterface(const char *name) {
    DelNode("physical-interface", name);
}

void AddLogicalInterface(const char *name, int id, const char* display_name, int vlan) {
    char buf[1024];
    sprintf(buf, "<logical-interface-vlan-tag>%d</logical-interface-vlan-tag>"
                 "<logical-interface-type>l2</logical-interface-type>"
                 "<user-visible>Juniper</user-visible>"
                 "<display-name>%s</display-name>", vlan, display_name);
    AddNode("logical-interface", name, id, buf);
}

void DeleteLogicalInterface(const char *name) {
    DelNode("logical-interface", name);
}

void AddVmPortVrf(const char *name, const string &ip, uint16_t tag,
                  const string &v6_ip) {
    char buff[1024];
    int len = 0;

    len += sprintf(buff + len,   "<direction>both</direction>");
    len += sprintf(buff + len,   "<vlan-tag>%d</vlan-tag>", tag);
    len += sprintf(buff + len,   "<src-mac>02:00:00:00:00:02</src-mac>");
    len += sprintf(buff + len,   "<dst-mac>02:00:00:00:00:01</dst-mac>");
    len += sprintf(buff + len,
                   "<service-chain-address>%s</service-chain-address>",
                   ip.c_str());
    if (!v6_ip.empty()) {
        len += sprintf(buff + len,
                   "<ipv6-service-chain-address>%s</ipv6-service-chain-address>",
                    v6_ip.c_str());
    }
    AddLinkNode("virtual-machine-interface-routing-instance", name, buff);
}

void DelVmPortVrf(const char *name) {
    DelNode("virtual-machine-interface-routing-instance", name);
}

void AddVmportBridgeDomain(const char *name, uint32_t vlan_tag) {
    std::stringstream str;
    str << "<vlan-tag>" << vlan_tag << "</vlan-tag>";

    AddLinkNode("virtual-machine-interface-bridge-domain", name,
                str.str().c_str());
}

void AddIPAM(const char *name, IpamInfo *ipam, int ipam_size, const char *ipam_attr,
             const char *vdns_name, const std::vector<std::string> *vm_host_routes,
             const char *add_subnet_tags) {
    char buff[10240];
    char node_name[128];
    char ipam_name[128];
    int len = 0;

    sprintf(node_name, "default-network-ipam,%s", name);
    sprintf(ipam_name, "default-network-ipam");
    AddXmlHdr(buff, len);
    if (ipam_attr) {
        AddNodeString(buff, len, "network-ipam", ipam_name, 1, ipam_attr);
    } else {
        AddNodeString(buff, len, "network-ipam", ipam_name, 1);
    }
    AddNodeString(buff, len, "virtual-network-network-ipam", node_name,
                            ipam, ipam_size, vm_host_routes, add_subnet_tags);
    AddLinkString(buff, len, "virtual-network", name,
                  "virtual-network-network-ipam", node_name,
                  "virtual-network-network-ipam");
    AddLinkString(buff, len, "network-ipam", ipam_name,
                  "virtual-network-network-ipam", node_name,
                  "virtual-network-network-ipam");
    if (vdns_name) {
        AddLinkString(buff, len, "network-ipam", ipam_name,
                      "virtual-DNS", vdns_name, "virtual-DNS");
    }
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
}

void DelIPAM(const char *name, const char *vdns_name) {
    char buff[2048];
    char node_name[128];
    char ipam_name[128];
    int len = 0;

    sprintf(node_name, "default-network-ipam,%s", name);
    sprintf(ipam_name, "default-network-ipam");
    DelXmlHdr(buff, len);
    DelLinkString(buff, len, "virtual-network", name,
                 "virtual-network-network-ipam", node_name);
    DelLinkString(buff, len, "network-ipam", ipam_name,
                 "virtual-network-network-ipam", node_name);
    if (vdns_name) {
        DelLinkString(buff, len, "network-ipam", ipam_name,
                      "virtual-DNS", vdns_name);
    }
    DelNodeString(buff, len, "virtual-network-network-ipam", node_name);
    DelNodeString(buff, len, "network-ipam", ipam_name);
    DelXmlTail(buff, len);
    ApplyXmlString(buff);
    return;
}

void AddVDNS(const char *vdns_name, const char *vdns_attr) {
    char buff[4096];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-DNS", vdns_name, 1, vdns_attr);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
}

void DelVDNS(const char *vdns_name) {
    char buff[2048];
    int len = 0;

    DelXmlHdr(buff, len);
    DelNodeString(buff, len, "virtual-DNS", vdns_name);
    DelXmlTail(buff, len);
    ApplyXmlString(buff);
}

void AddLinkLocalConfig(const TestLinkLocalService *services, int count) {
    std::stringstream global_config;
    global_config << "<linklocal-services>\n";
    for (int i = 0; i < count; ++i) {
        global_config << "<linklocal-service-entry>\n";
        global_config << "<linklocal-service-name>";
        global_config << services[i].linklocal_name;
        global_config << "</linklocal-service-name>\n";
        global_config << "<linklocal-service-ip>";
        global_config << services[i].linklocal_ip;
        global_config << "</linklocal-service-ip>\n";
        global_config << "<linklocal-service-port>";
        global_config << services[i].linklocal_port;
        global_config << "</linklocal-service-port>\n";
        global_config << "<ip-fabric-DNS-service-name>";
        global_config << services[i].fabric_dns_name;
        global_config << "</ip-fabric-DNS-service-name>\n";
        for (uint32_t j = 0; j < services[i].fabric_ip.size(); ++j) {
            global_config << "<ip-fabric-service-ip>";
            global_config << services[i].fabric_ip[j];
            global_config << "</ip-fabric-service-ip>\n";
        }
        global_config << "<ip-fabric-service-port>";
        global_config << services[i].fabric_port;
        global_config << "</ip-fabric-service-port>\n";
        global_config << "</linklocal-service-entry>\n";
    }
    global_config << "</linklocal-services>";

    char buf[8192];
    int len = 0;
    memset(buf, 0, 8192);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "global-vrouter-config",
                  "default-global-system-config:default-global-vrouter-config",
                  1024, global_config.str().c_str());
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

void DelLinkLocalConfig() {
    char buf[4096];
    int len = 0;
    memset(buf, 0, 4096);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "global-vrouter-config",
                  "default-global-system-config:default-global-vrouter-config",
                  1024, "");
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

void DeleteGlobalVrouterConfig() {
    char buf[4096];
    int len = 0;
    memset(buf, 0, 4096);
    DelXmlHdr(buf, len);
    DelNodeString(buf, len, "global-vrouter-config",
                  "default-global-system-config:default-global-vrouter-config");
    DelXmlTail(buf, len);
    ApplyXmlString(buf);
}

void VxLanNetworkIdentifierMode(bool config) {
    std::stringstream str;
    if (config) {
        str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    } else {
        str << "<vxlan-network-identifier-mode>automatic</vxlan-network-identifier-mode>" << endl;
    }
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
}

void GlobalForwardingMode(std::string mode) {
    std::stringstream str;
    str << "<forwarding-mode>" << mode << "</forwarding-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
}

void AddEncapList(const char *encap1, const char *encap2, const char *encap3) {
    std::stringstream str;
    str << "<encapsulation-priorities>" << endl;
    if (encap1) {
        str << "    <encapsulation>" << encap1 << "</encapsulation>";
    }

    if (encap2) {
        str << "    <encapsulation>" << encap2 << "</encapsulation>";
    }

    if (encap3) {
        str << "    <encapsulation>" << encap3 << "</encapsulation>";
    }
    str << "</encapsulation-priorities>";

    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
}

void AddEncapList(Agent *agent, const char *encap1, const char *encap2,
                  const char *encap3) {
    std::stringstream str;
    str << "<encapsulation-priorities>" << endl;
    if (encap1) {
        str << "    <encapsulation>" << encap1 << "</encapsulation>";
    }

    if (encap2) {
        str << "    <encapsulation>" << encap2 << "</encapsulation>";
    }

    if (encap3) {
        str << "    <encapsulation>" << encap3 << "</encapsulation>";
    }
    str << "</encapsulation-priorities>";

    AddNode(agent, "global-vrouter-config", "vrouter-config", 1,
            str.str().c_str());
}

void DelEncapList() {
    DelNode("global-vrouter-config", "vrouter-config");
}

void DelEncapList(Agent *agent) {
    DelNode(agent, "global-vrouter-config", "vrouter-config");
}

void DelHealthCheckService(const char *name) {
    DelNode("service-health-check", name);
}

void AddHealthCheckService(const char *name, int id,
                           const char *url_path,
                           const char *monitor_type) {
    char buf[1024];

    sprintf(buf, "<service-health-check-properties>"
                 "    <enabled>false</enabled>"
                 "    <health-check-type>end-to-end</health-check-type>"
                 "    <monitor-type>%s</monitor-type>"
                 "    <delay>5</delay>"
                 "    <timeout>5</timeout>"
                 "    <max-retries>3</max-retries>"
                 "    <http-method></http-method>"
                 "    <url-path>%s</url-path>"
                 "    <expected-codes></expected-codes>"
                 "</service-health-check-properties>", monitor_type, url_path);
    AddNode("service-health-check", name, id, buf);
}

void send_icmp(int fd, uint8_t smac, uint8_t dmac, uint32_t sip, uint32_t dip) {
    uint8_t dummy_dmac[6], dummy_smac[6];
	memset(dummy_dmac, 0, sizeof(dummy_dmac));
	memset(dummy_smac, 0, sizeof(dummy_dmac));
	dummy_dmac[5] = dmac;
	dummy_smac[5] = smac;

    IcmpPacket icmp(dummy_smac, dummy_dmac, sip, dip);
    int ret = write(fd, icmp.GetPacket(), sizeof(icmp_packet));
    LOG(DEBUG, "Written " << ret << " bytes to fd " << fd);
    if (ret < 0)
	{
	    LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
	        "> writing");
	}
    usleep(3*1000);
}

uint32_t GetFlowKeyNH(int id) {
    uuid intf_uuid = MakeUuid(id);
    VmInterfaceKey key(AgentKey::RESYNC, intf_uuid, "");
    const Interface *intf = static_cast<const Interface *>(
            Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    return intf->flow_key_nh()->id();
}

uint32_t GetFlowkeyNH(char *name) {
    InetInterface *intf = InetInterfaceGet(name);
    return intf->flow_key_nh()->id();
}

bool FlowStats(FlowIp *input, int id, uint32_t bytes, uint32_t pkts) {
    Agent *agent = Agent::GetInstance();
    VrfEntry *vrf;
    VrfKey vrf_key(input[id].vrf);
    vrf = static_cast<VrfEntry *>(agent->vrf_table()->FindActiveEntry(&vrf_key));
    LOG(DEBUG, "Vrf id for " << input[id].vrf << " is " << vrf->vrf_id());

    FlowKey key;

    key.src_port = 0;
    key.dst_port = 0;
    key.nh = id;
    key.src_addr = IpAddress(Ip4Address(input[id].sip));
    key.dst_addr = IpAddress(Ip4Address(input[id].dip));
    key.protocol = IPPROTO_ICMP;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowEntry *fe = agent->pkt()->get_flow_proto()->Find(key, 0);
    if (fe == NULL) {
        LOG(DEBUG, "Flow not found");
        return false;
    }
    FlowStatsCollector *fec = fe->fsc();
    if (fec == NULL) {
        return false;
    }
    FlowExportInfo *info = fec->FindFlowExportInfo(fe);

    if (info) {
        LOG(DEBUG, " bytes " << info->bytes() << " pkts " << info->packets());
        if (info->bytes() == bytes && info->packets() == pkts) {
            return true;
        }
    }

    return false;
}

void AddVmPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid) {
    struct PortInfo input[] = {
        {"", 0, "", "", 0, 0 }
    };

    strcpy(input[0].name, vmi);
    input[0].intf_id = intf_id;
    strcpy(input[0].addr, ip);
    strcpy(input[0].mac, mac);
    input[0].vn_id = vn_uuid;
    input[0].vm_id = vm_uuid;

    AddVn(vn, vn_uuid);
    AddVrf(vrf);
    AddVm(vm, vm_uuid);
    AddPort(vmi, intf_id);
    AddVmPortVrf(vmi, "", 0);
    IntfCfgAdd(input, 0);
    AddInstanceIp(instance_ip, instance_uuid, ip);
    AddLink("virtual-machine-interface", vmi, "virtual-network", vn);
    AddLink("virtual-network", vn, "routing-instance", vrf);
    AddLink("virtual-machine", vm, "virtual-machine-interface", vmi);
    AddLink("virtual-machine-interface-routing-instance", vmi,
            "routing-instance", vrf);
    AddLink("virtual-machine-interface-routing-instance", vmi,
            "virtual-machine-interface", vmi);
    AddLink("virtual-machine-interface", vmi, "instance-ip", instance_ip);
    client->WaitForIdle();
}

void DelVmPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid) {
    struct PortInfo input[] = {
        {"", 0, "", "", 0, 0 }
    };

    strcpy(input[0].name, vmi);
    input[0].intf_id = intf_id;
    strcpy(input[0].addr, ip);
    strcpy(input[0].mac, mac);
    input[0].vn_id = vn_uuid;
    input[0].vm_id = vm_uuid;

    DelLink("virtual-machine-interface", vmi, "virtual-network", vm);
    DelLink("virtual-network", vn, "routing-instance", vrf);
    DelLink("virtual-machine", vm, "virtual-machine-interface", vmi);
    DelLink("virtual-machine-interface-routing-instance", vmi,
            "routing-instance", vrf);
    DelLink("virtual-machine-interface-routing-instance", vmi,
            "virtual-machine-interface", vmi);
    DelLink("virtual-machine-interface", vmi,
            "instance-ip", instance_ip);
    DelVn(vn);
    DelVrf(vrf);
    DelVm(vm);
    DelPort(vmi);
    DelVmPortVrf(vmi);
    DelInstanceIp(instance_ip);
    IntfCfgDel(input, 0);
    DelNode("virtual-machine-interface", vmi);
    DelNode("virtual-machine-interface-routing-instance", vmi);
    client->WaitForIdle();
}

void DeleteVmportFIpEnv(struct PortInfo *input, int count, int del_vn, int acl_id,
                        const char *vn, const char *vrf) {
    char vn_name[80];
    char vm_name[80];
    char vrf_name[80];
    char acl_name[80];
    char instance_ip[80];

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
    }

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "default-project:vn%d", input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "default-project:vn%d:vn%d", input[i].vn_id,
                    input[i].vn_id);
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
        DelNode("virtual-machine-interface", input[i].name);
        DelNode("virtual-machine-interface-routing-instance", input[i].name);
        IntfCfgDel(input, i);

        DelNode("virtual-machine", vm_name);
    }

    if (del_vn) {
        for (int i = 0; i < count; i++) {
            int j = 0;
            for (; j < i; j++) {
                if (input[i].vn_id == input[j].vn_id) {
                    break;
                }
            }

            if (j < i) {
                break;
            }
            if (vn)
                sprintf(vn_name, "%s", vn);
            else
                sprintf(vn_name, "default-project:vn%d", input[i].vn_id);
            if (vrf)
                sprintf(vrf_name, "%s", vrf);
            else
                sprintf(vrf_name, "default-project:vn%d:vn%d", input[i].vn_id,
                        input[i].vn_id);
            sprintf(vm_name, "vm%d", input[i].vm_id);
            DelLink("virtual-network", vn_name, "routing-instance", vrf_name);
            if (acl_id) {
                DelLink("virtual-network", vn_name, "access-control-list", acl_name);
            }

            DelNode("virtual-network", vn_name);
            DelNode("routing-instance", vrf_name);
        }
    }

    if (acl_id) {
        DelNode("access-control-list", acl_name);
    }
}

void DeleteVmportEnv(struct PortInfo *input, int count, int del_vn, int acl_id,
                     const char *vn, const char *vrf, bool with_ip,
                     bool with_ip6) {
    char vn_name[80];
    char vm_name[80];
    char vrf_name[80];
    char acl_name[80];
    char instance_ip[80];
    char instance_ip6[80];

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
    }

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "vn%d", input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "vrf%d", input[i].vn_id);
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
                sprintf(vn_name, "vn%d", input[i].vn_id);
            if (vrf)
                sprintf(vrf_name, "%s", vrf);
            else
                sprintf(vrf_name, "vrf%d", input[i].vn_id);
            DelLink("virtual-network", vn_name, "routing-instance", vrf_name);
            if (acl_id) {
                DelLink("virtual-network", vn_name, "access-control-list", acl_name);
            }

            DelNode("virtual-network", vn_name);
            DelNode("routing-instance", vrf_name);
        }
    }

    if (acl_id) {
        DelNode("access-control-list", acl_name);
    }

    if (client->agent_init()->ksync_enable()) {
        DeleteTapIntf(test_tap_fd, count);
    }
}

void CreateVmportFIpEnv(struct PortInfo *input, int count, int acl_id,
                        const char *vn, const char *vrf) {
    char vn_name[MAX_TESTNAME_LEN];
    char vm_name[MAX_TESTNAME_LEN];
    char vrf_name[MAX_TESTNAME_LEN];
    char acl_name[MAX_TESTNAME_LEN];
    char instance_ip[MAX_TESTNAME_LEN];

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
        AddAcl(acl_name, acl_id);
    }

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "default-project:vn%d", input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "default-project:vn%d:vn%d", input[i].vn_id,
                    input[i].vn_id);
        sprintf(vm_name, "vm%d", input[i].vm_id);
        sprintf(instance_ip, "instance%d", input[i].intf_id);
        AddVn(vn_name, input[i].vn_id);
        AddVrf(vrf_name);
        AddVm(vm_name, input[i].vm_id);
        AddVmPortVrf(input[i].name, "", 0);

        //AddNode("virtual-machine-interface-routing-instance", input[i].name,
        //        input[i].intf_id);
        IntfCfgAddThrift(input, i);
        AddPort(input[i].name, input[i].intf_id);
        AddActiveActiveInstanceIp(instance_ip, input[i].intf_id, input[i].addr);
        AddLink("virtual-network", vn_name, "routing-instance", vrf_name);
        AddLink("virtual-machine-interface", input[i].name, "virtual-machine", vm_name);
        AddLink("virtual-machine-interface", input[i].name, "virtual-network",
                vn_name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "routing-instance", vrf_name, "virtual-machine-interface-routing-instance");
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "virtual-machine-interface", input[i].name, "virtual-machine-interface-routing-instance");
        AddLink("virtual-machine-interface", input[i].name,
                "instance-ip", instance_ip);
        AddLink("instance-ip", instance_ip, "virtual-machine-interface",
                input[i].name);

        if (acl_id) {
            AddLink("virtual-network", vn_name, "access-control-list", acl_name);
        }
    }
}

void CreateVmportEnvInternal(struct PortInfo *input, int count, int acl_id,
                     const char *vn, const char *vrf,
                     const char *vm_interface_attr,
                     bool l2_vn, bool with_ip, bool ecmp,
                     bool vn_admin_state, bool with_ip6, bool send_nova_msg) {
    char vn_name[MAX_TESTNAME_LEN];
    char vm_name[MAX_TESTNAME_LEN];
    char vrf_name[MAX_TESTNAME_LEN];
    char acl_name[MAX_TESTNAME_LEN];
    char instance_ip[MAX_TESTNAME_LEN];
    char instance_ip6[MAX_TESTNAME_LEN];

    if (client->agent_init()->ksync_enable()) {
        CreateTapInterfaces("vnet", count, test_tap_fd);
    }

    if (acl_id) {
        sprintf(acl_name, "acl%d", acl_id);
        AddAcl(acl_name, acl_id);
    }

    for (int i = 0; i < count; i++) {
        if (vn)
            strncpy(vn_name, vn, MAX_TESTNAME_LEN);
        else
            sprintf(vn_name, "vn%d", input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "vrf%d", input[i].vn_id);
        sprintf(vm_name, "vm%d", input[i].vm_id);
        sprintf(instance_ip, "instance%d", input[i].intf_id);
        if (!l2_vn) {
            AddVn(vn_name, input[i].vn_id, vn_admin_state);
            AddVrf(vrf_name);
        }
        AddVm(vm_name, input[i].vm_id);
        AddVmPortVrf(input[i].name, "", 0);

        //AddNode("virtual-machine-interface-routing-instance", input[i].name,
        //        input[i].intf_id);
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

        if (acl_id) {
            AddLink("virtual-network", vn_name, "access-control-list", acl_name);
        }
    }
}

void CreateVmportEnvWithoutIp(struct PortInfo *input, int count, int acl_id,
                              const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false, false,
                            false, true);
}

void CreateVmportEnv(struct PortInfo *input, int count, int acl_id,
                     const char *vn, const char *vrf,
                     const char *vm_interface_attr,
                     bool vn_admin_state) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf,
                            vm_interface_attr, false, true, false,
                            vn_admin_state, false, true);
}

void CreateL2VmportEnv(struct PortInfo *input, int count, int acl_id,
                     const char *vn, const char *vrf) {
    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    TestClient::WaitForIdle();
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, true,
                            true, false, true);
}

void CreateL3VmportEnv(struct PortInfo *input, int count, int acl_id,
                       const char *vn, const char *vrf) {
    AddL3Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    TestClient::WaitForIdle();
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, true,
                            true, false, true);
}

void CreateVmportWithEcmp(struct PortInfo *input, int count, int acl_id,
                          const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false,
                            true, true, true);
}

void CreateVmportWithoutNova(struct PortInfo *input, int count, int acl_id,
                          const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false,
                            true, true, true, false, false);
}

void CreateV6VmportEnv(struct PortInfo *input, int count, int acl_id,
                       const char *vn, const char *vrf, bool with_v4_ip) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false,
                            with_v4_ip, false, true, true);
}

void CreateV6VmportWithEcmp(struct PortInfo *input, int count, int acl_id,
                       const char *vn, const char *vrf, bool with_v4_ip) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false,
                            with_v4_ip, true, true, true);
}

void FlushFlowTable() {
    Agent::GetInstance()->pkt()->get_flow_proto()->FlushFlows();
    TestClient::WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->get_flow_proto()->FlowCount());
}

static bool FlowDeleteTrigger(FlowKey key) {
    FlowTable *table =
        Agent::GetInstance()->pkt()->get_flow_proto()->GetTable(0);
    if (table->Find(key) == NULL) {
        return true;
    }

    table->Delete(key, true);
    return true;
}

bool FlowDelete(const string &vrf_name, const char *sip, const char *dip,
                uint8_t proto, uint16_t sport, uint16_t dport, int nh_id) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src_addr = IpAddress::from_string(sip);
    key.dst_addr = IpAddress::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    if (proto == IPPROTO_ICMPV6) {
        key.dst_port = ICMP6_ECHO_REPLY;
    }
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    if (Agent::GetInstance()->pkt()->get_flow_proto()->Find(key, 0) == NULL) {
        return false;
    }

    int task_id = TaskScheduler::GetInstance()->GetTaskId(kTaskFlowEvent);
    std::auto_ptr<TaskTrigger> trigger_
        (new TaskTrigger(boost::bind(FlowDeleteTrigger, key), task_id, 0));
    trigger_->Set();
    client->WaitForIdle();
    return true;
}

bool FlowFail(int vrf_id, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, int nh_id) {
    FlowKey key;
    key.nh = nh_id;
    key.src_addr = IpAddress::from_string(sip);
    key.dst_addr = IpAddress::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;
    FlowProto *fp = Agent::GetInstance()->pkt()->get_flow_proto();

    WAIT_FOR(1000, 1000, (fp->Find(key, 0) == false));
    FlowEntry *fe = fp->Find(key, 0);
    if (fe == NULL) {
        return true;
    }
    if (fe->deleted()) {
        return true;
    }
    return false;
}

bool FlowFail(const string &vrf_name, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, int nh_id) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    return FlowFail(vrf->vrf_id(), sip, dip, proto, sport, dport, nh_id);
}

bool FlowGetNat(const string &vrf_name, const char *sip, const char *dip,
                uint8_t proto, uint16_t sport, uint16_t dport,
                std::string svn, std::string dvn, uint32_t hash_id,
                const char *nat_vrf, const char *nat_sip,
                const char *nat_dip, uint16_t nat_sport, int16_t nat_dport,
                int nh_id, int nat_nh_id) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src_addr = IpAddress::from_string(sip);
    key.dst_addr = IpAddress::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowTable *table =
        Agent::GetInstance()->pkt()->get_flow_proto()->GetTable(0);
    FlowEntry *entry = table->Find(key);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow));
    if (!entry->is_flags_set(FlowEntry::NatFlow)) {
        return false;
    }

    EXPECT_TRUE(VnMatch(entry->data().source_vn_list, svn));
    if (!VnMatch(entry->data().source_vn_list, svn)) {
        return false;
    }

    EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, dvn));
    if (!VnMatch(entry->data().dest_vn_list, dvn)) {
        return false;
    }

    if ((int)hash_id >= 0) {
        EXPECT_TRUE(entry->flow_handle() == hash_id);
        if (entry->flow_handle() != hash_id) {
            return false;
        }
    }

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(nat_vrf);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    key.nh = nat_nh_id;
    key.src_addr = IpAddress::from_string(nat_dip);
    key.dst_addr = IpAddress::from_string(nat_sip);
    key.src_port = nat_dport;
    key.dst_port = nat_sport;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;
    FlowEntry * rentry = table->Find(key);
    EXPECT_TRUE(rentry != NULL);
    if (rentry == NULL) {
        return false;
    }

    EXPECT_TRUE(rentry->is_flags_set(FlowEntry::NatFlow));
    if (!rentry->is_flags_set(FlowEntry::NatFlow)) {
        return false;
    }

    EXPECT_TRUE(entry->reverse_flow_entry() == rentry);
    EXPECT_TRUE(rentry->reverse_flow_entry() == entry);
    if (entry->reverse_flow_entry() != rentry)
        return false;
    if (rentry->reverse_flow_entry() != entry)
        return false;

    return true;
}

FlowEntry* FlowGet(std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport, int nh_id,
                   uint32_t flow_handle) {
    FlowKey key;
    key.nh = nh_id;
    key.src_addr = IpAddress::from_string(sip);
    key.dst_addr = IpAddress::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowTable *table =
        Agent::GetInstance()->pkt()->get_flow_proto()->GetFlowTable(key,
                                                                    flow_handle);
    if (table == NULL) {
        return NULL;
    }
    return table->Find(key);
}

FlowEntry* FlowGet(int vrf_id, std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport, int nh_id) {
    FlowKey key;
    key.nh = nh_id;
    key.src_addr = IpAddress::from_string(sip);
    key.dst_addr = IpAddress::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    return Agent::GetInstance()->pkt()->get_flow_proto()->Find(key, 0);
}

FlowEntry* FlowGet(int nh_id, std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport) {
    return FlowGet(0, sip, dip, proto, sport, dport, nh_id);
}

bool FlowGet(int vrf_id, const char *sip, const char *dip, uint8_t proto,
             uint16_t sport, uint16_t dport, bool short_flow, int hash_id,
             int reverse_hash_id, int nh_id, int reverse_nh_id) {
    FlowKey key;
    key.nh = nh_id;
    key.src_addr = Ip4Address::from_string(sip);
    key.dst_addr = Ip4Address::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowEntry *entry = Agent::GetInstance()->pkt()->get_flow_proto()->Find(key,
                                                                           0);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    if (hash_id >= 0) {
        EXPECT_EQ(entry->flow_handle(), (uint32_t)hash_id);
        if (entry->flow_handle() != (uint32_t)hash_id) {
            return false;
        }
    }

    EXPECT_EQ(entry->is_flags_set(FlowEntry::ShortFlow), short_flow);
    if (entry->is_flags_set(FlowEntry::ShortFlow) != short_flow) {
        return false;
    }

    if (reverse_hash_id == 0) {
        bool ret = true;
        FlowEntry *rev = entry->reverse_flow_entry();

        EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == false);
        if (entry->is_flags_set(FlowEntry::NatFlow) == true)
            ret = false;

        if ((uint32_t)reverse_nh_id != rev->key().nh)
            ret = false;

        EXPECT_EQ(entry->key().protocol, rev->key().protocol);
        if (entry->key().protocol != rev->key().protocol)
            ret = false;

        EXPECT_EQ(entry->key().src_addr, rev->key().dst_addr);
        if (entry->key().src_addr != rev->key().dst_addr)
            ret = false;

        EXPECT_EQ(entry->key().dst_addr, rev->key().src_addr);
        if (entry->key().dst_addr != rev->key().src_addr)
            ret = false;

        return ret;
    }

    if (reverse_hash_id > 0) {
        FlowEntry *rev = entry->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);
        if (rev == NULL) {
            return false;
        }

        EXPECT_EQ(rev->flow_handle(), (uint32_t)reverse_hash_id);
        if (rev->flow_handle() != (uint32_t)reverse_hash_id) {
            return false;
        }
    }

    return true;
}

bool FlowGet(const string &vrf_name, const char *sip, const char *dip,
             uint8_t proto, uint16_t sport, uint16_t dport, bool rflow,
             std::string svn, std::string dvn, uint32_t hash_id, int nh_id,
             int rev_nh_id) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src_addr = Ip4Address::from_string(sip);
    key.dst_addr = Ip4Address::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowTable *table =
        Agent::GetInstance()->pkt()->get_flow_proto()->GetFlowTable(key, 0);
    FlowEntry *entry = table->Find(key);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_TRUE(VnMatch(entry->data().source_vn_list, svn));
    if (!VnMatch(entry->data().source_vn_list, svn)) {
        return false;
    }

    EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, dvn));
    if (!VnMatch(entry->data().dest_vn_list, dvn)) {
        return false;
    }

    if (hash_id >= 0) {
        EXPECT_EQ(entry->flow_handle(), hash_id);
        if (entry->flow_handle() != hash_id) {
            return false;
        }
    }

    if (rflow) {
        key.nh = rev_nh_id;
        key.src_addr = Ip4Address::from_string(dip);
        key.dst_addr = Ip4Address::from_string(sip);
        key.src_port = dport;
        key.dst_port = sport;
        key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;
        FlowEntry * rentry = table->Find(key);
        EXPECT_TRUE(rentry != NULL);
        if (rentry == NULL) {
            return false;
        }
        EXPECT_TRUE(entry->reverse_flow_entry() == rentry);
        EXPECT_TRUE(rentry->reverse_flow_entry() == entry);
        if (entry->reverse_flow_entry() != rentry)
            return false;
        if (rentry->reverse_flow_entry() != entry)
            return false;
    } else {
        bool ret = true;
        FlowEntry *rev = entry->reverse_flow_entry();

        EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == false);
        if (entry->is_flags_set(FlowEntry::NatFlow) == true)
            ret = false;

        if (rev_nh_id != -1) {
            EXPECT_EQ(rev_nh_id, rev->key().nh);
            ret = false;
        } else {
            //EXPECT_EQ((uint32_t) rflow_vrf, rev->key().vrf);
        }

        EXPECT_EQ(entry->key().protocol, rev->key().protocol);
        if (entry->key().protocol != rev->key().protocol)
            ret = false;

        EXPECT_EQ(entry->key().src_addr, rev->key().dst_addr);
        if (entry->key().src_addr != rev->key().dst_addr)
            ret = false;

        EXPECT_EQ(entry->key().dst_addr, rev->key().src_addr);
        if (entry->key().dst_addr != rev->key().src_addr)
            ret = false;

        return ret;
    }
    return true;
}

bool FlowGet(const string &vrf_name, const char *sip, const char *dip,
             uint8_t proto, uint16_t sport, uint16_t dport, bool rflow,
             std::string svn, std::string dvn, uint32_t hash_id, bool fwd,
             bool nat, int nh_id, int rev_nh_id) {

    bool flow_fwd = false;
    bool ret = FlowGet(vrf_name, sip, dip, proto, sport, dport, rflow,
                       svn, dvn, hash_id, nh_id, rev_nh_id);

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src_addr = Ip4Address::from_string(sip);
    key.dst_addr = Ip4Address::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowEntry *entry = Agent::GetInstance()->pkt()->get_flow_proto()->Find(key,
                                                                           0);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_EQ(entry->is_flags_set(FlowEntry::NatFlow), nat);
    if (entry->is_flags_set(FlowEntry::NatFlow) != nat) {
        ret = false;
    }

    if (entry->match_p().action_info.action & (1 << SimpleAction::PASS)) {
        flow_fwd = true;
    }

    if (entry->IsShortFlow()) {
        flow_fwd = false;
    }

    EXPECT_EQ(flow_fwd, fwd);
    if (flow_fwd != fwd) {
        ret = false;
    }

    return ret;
}

bool FlowStatsMatch(const string &vrf_name, const char *sip,
                    const char *dip, uint8_t proto, uint16_t sport,
                    uint16_t dport, uint64_t pkts, uint64_t bytes, int nh_id,
                    uint32_t flow_handle) {
    Agent *agent = Agent::GetInstance();
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src_addr = Ip4Address::from_string(sip);
    key.dst_addr = Ip4Address::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowEntry *fe = NULL;
    if (flow_handle != FlowEntry::kInvalidFlowHandle) {
        FlowTable *table = agent->pkt()->get_flow_proto()->GetFlowTable(key,
                                                                        flow_handle);
        if (table == NULL) {
            return NULL;
        }
        fe = table->Find(key);
    } else {
        fe = agent->pkt()->get_flow_proto()->Find(key, 0);
    }
    EXPECT_TRUE(fe != NULL);
    if (fe == NULL) {
        return false;
    }
    FlowStatsCollector *fec = fe->fsc();
    if (fec == NULL) {
        return false;
    }
    FlowExportInfo *info = fec->FindFlowExportInfo(fe);
    if (info) {
        LOG(DEBUG, " bytes " << info->bytes() << " pkts " << info->packets());
        if (info->bytes() == bytes && info->packets() == pkts) {
            return true;
        }
    }

    return false;
}

bool FindFlow(const string &vrf_name, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, bool nat,
              const string &nat_vrf_name, const char *nat_sip,
              const char *nat_dip, uint16_t nat_sport, uint16_t nat_dport,
              int fwd_nh_id, int rev_nh_id) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = fwd_nh_id;
    key.src_addr = Ip4Address::from_string(sip);
    key.dst_addr = Ip4Address::from_string(dip);
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowTable *table =
        Agent::GetInstance()->pkt()->get_flow_proto()->GetFlowTable(key, 0);
    FlowEntry *entry = table->Find(key);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    if (nat == false) {
        EXPECT_TRUE(entry->reverse_flow_entry() == NULL);
        if (entry->reverse_flow_entry() == NULL)
            return true;

        return false;
    }

    VrfEntry *nat_vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(nat_vrf_name);
    EXPECT_TRUE(nat_vrf != NULL);
    if (nat_vrf == NULL)
        return false;

    key.nh = rev_nh_id;
    key.src_addr = Ip4Address::from_string(nat_dip);
    key.dst_addr = Ip4Address::from_string(nat_sip);
    key.src_port = nat_dport;
    key.dst_port = nat_sport;
    key.protocol = proto;
    key.family = key.src_addr.is_v4() ? Address::INET : Address::INET6;

    FlowEntry *nat_entry = table->Find(key);
    EXPECT_TRUE(nat_entry != NULL);
    if (nat_entry == NULL) {
        return false;
    }

    EXPECT_EQ(entry->reverse_flow_entry(), nat_entry);
    EXPECT_EQ(nat_entry->reverse_flow_entry(), entry);

    if ((entry->reverse_flow_entry() == nat_entry) &&
        (nat_entry->reverse_flow_entry() == entry)) {
        return true;
    }

    return false;
}

PktGen *TxTcpPacketUtil(int ifindex, const char *sip, const char *dip,
                        int sport, int dport, uint32_t hash_idx) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_idx);
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, false, 64);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
    return NULL;
}

PktGen *TxIpPacketUtil(int ifindex, const char *sip, const char *dip,
                       int proto, int hash_id) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id);
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1)
        pkt->AddIcmpHdr();

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
    return NULL;
}

const NextHop* MplsToNextHop(uint32_t label) {
    MplsLabel *mpls = Agent::GetInstance()->mpls_table()->FindMplsLabel(label);
    if (mpls) {
        return mpls->nexthop();
    }
    return NULL;
}

int MplsToVrfId(int label) {
    int vrf = 0;
    MplsLabel *mpls = Agent::GetInstance()->mpls_table()->FindMplsLabel(label);
    if (mpls) {
        const NextHop *nh = mpls->nexthop();
        if (nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *nh1 = static_cast<const InterfaceNH *>(nh);
            const VmInterface *intf =
                static_cast<const VmInterface *>(nh1->GetInterface());
            if (intf && intf->vrf()) {
                vrf = intf->vrf()->vrf_id();
            }
        } else if (nh->GetType() == NextHop::COMPOSITE) {
            const CompositeNH *nh1 = static_cast<const CompositeNH *>(nh);
            vrf = nh1->vrf()->vrf_id();
        } else if (nh->GetType() == NextHop::VLAN) {
            const VlanNH *nh1 = static_cast<const VlanNH *>(nh);
            const VmInterface *intf =
                static_cast<const VmInterface *>(nh1->GetInterface());
            if (intf && intf->GetServiceVlanVrf(nh1->GetVlanTag())) {
                vrf = intf->GetServiceVlanVrf(nh1->GetVlanTag())->vrf_id();
            }
        } else if (nh->GetType() == NextHop::VRF) {
            const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);
            vrf = vrf_nh->GetVrf()->vrf_id();
        } else {
            assert(0);
        }
    }
    return vrf;
}

uint32_t GetInterfaceLabel(int uuid, bool l3) {
}

PktGen *TxMplsPacketUtil(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label,
                            const char *sip, const char *dip,
                            int proto, int hash_idx) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_idx, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1)
        pkt->AddIcmpHdr();

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;

    return NULL;
}

PktGen *TxMplsTcpPacketUtil(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label,
                            const char *sip, const char *dip,
                            int sport, int dport, int hash_idx) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_idx, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:5E:00:01:00", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, false, 64);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
    return NULL;
}

bool RouterIdMatch(Ip4Address rid2) {
    Ip4Address rid1 = Agent::GetInstance()->router_id();
    int ret = rid1.to_string().compare(rid2.to_string());
    EXPECT_TRUE(ret == 0);
    if (ret) {
        return false;
    }
    return true;
}

bool ResolvRouteFind(const string &vrf_name, const Ip4Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL) {
        LOG(DEBUG, "VRF not found " << vrf_name);
        return false;
    }

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    InetUnicastRouteEntry *route =
        static_cast<InetUnicastRouteEntry *>
        (static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    if (route == NULL) {
        LOG(DEBUG, "Resolve route not found");
        return false;
    }

    const NextHop *nh = route->GetActiveNextHop();
    if (nh == NULL) {
        LOG(DEBUG, "NH not found for Resolve route");
        return false;
    }

    EXPECT_TRUE(nh->GetType() == NextHop::RESOLVE);
    if (nh->GetType() != NextHop::RESOLVE) {
        LOG(DEBUG, "NH type is not resolve " << nh->GetType());
        return false;
    }
    return true;
}

bool VhostRecvRouteFind(const string &vrf_name, const Ip4Address &addr,
                        int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL) {
        LOG(DEBUG, "VRF not found " << vrf_name);
        return false;
    }

    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);
    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    if (route == NULL) {
        LOG(DEBUG, "Vhost Receive route not found");
        return false;
    }

    const NextHop *nh = route->GetActiveNextHop();
    if (nh == NULL) {
        LOG(DEBUG, "NH not found for Vhost Receive route");
        return false;
    }

    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);
    if (nh->GetType() != NextHop::RECEIVE) {
        LOG(DEBUG, "NH type is not receive " << nh->GetType());
        return false;
    }

    return true;
}

uint32_t PathCount(const string vrf_name, const Ip4Address &addr, int plen) {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    const Agent *agent = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    InetUnicastRouteKey key(agent->local_vm_peer(), vrf_name, addr, plen);

    InetUnicastRouteEntry* route =
        static_cast<InetUnicastRouteEntry *>
        (static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    if (route == NULL) {
        return 0;
    }

    return route->GetPathList().size();
}

bool FindVxLanId(const Agent *agent, uint32_t vxlan_id) {
    return (GetVxLan(agent, vxlan_id) != NULL);
}

VxLanId* GetVxLan(const Agent *agent, uint32_t vxlan_id) {
    VxLanIdKey vxlan_id_key(vxlan_id);
    VxLanId *vxlan_id_entry =
        static_cast<VxLanId *>(agent->vxlan_table()->FindActiveEntry(&vxlan_id_key));
    return vxlan_id_entry;
}

bool FindMplsLabel(uint32_t label) {
    MplsLabelKey key(label);
    MplsLabel *mpls = static_cast<MplsLabel *>(Agent::GetInstance()->mpls_table()->FindActiveEntry(&key));
    return (mpls != NULL);
}

MplsLabel* GetActiveLabel(uint32_t label) {
    MplsLabelKey key(label);
    return static_cast<MplsLabel *>(Agent::GetInstance()->mpls_table()->FindActiveEntry(&key));
}

bool FindNH(NextHopKey *key) {
    return (Agent::GetInstance()->nexthop_table()->FindActiveEntry(key) != NULL);
}

NextHop *GetNH(NextHopKey *key) {
    return static_cast<NextHop *>
        (Agent::GetInstance()->nexthop_table()->FindActiveEntry(key));
}

bool VmPortServiceVlanCount(int id, unsigned int count) {
    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(id));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL) {
        return false;
    }

    EXPECT_EQ(intf->service_vlan_list().list_.size(), count);
    if (intf->service_vlan_list().list_.size() != count) {
        return false;
    }
    return true;
}

BgpPeer *CreateBgpPeer(std::string addr, std::string name) {
    boost::system::error_code ec;
    Ip4Address ip = Ip4Address::from_string(addr, ec);
    return (CreateBgpPeer(ip, name));
}

BgpPeer *CreateBgpPeer(const Ip4Address &addr, std::string name) {
    XmppChannelMock *xmpp_channel = new XmppChannelMock();
    AgentXmppChannel *channel;
    Agent::GetInstance()->set_controller_ifmap_xmpp_server(addr.to_string(), 1);

    channel = new AgentXmppChannel(Agent::GetInstance(),
                                   "XMPP Server", "", 1);
    channel->RegisterXmppChannel(xmpp_channel);
    Agent::GetInstance()->set_controller_xmpp_channel(channel, 1);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel, xmps::READY);
    client->WaitForIdle();
    return channel->bgp_peer_id();
}

static bool ControllerCleanupTrigger() {
    Agent::GetInstance()->controller()->Cleanup();
    return true;
}

void FireAllControllerTimers(Agent *agent, AgentXmppChannel *channel) {
    AgentIfMapXmppChannel::NewSeqNumber();
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        if (agent->ifmap_xmpp_channel(count)) {
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                config_cleanup_timer()->sequence_number_ =
                AgentIfMapXmppChannel::GetSeqNumber();
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                end_of_config_timer()->controller_timer_->Fire();
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                config_cleanup_timer()->TimerExpirationDone();
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                end_of_config_timer()->TimerExpirationDone();
        }
    }
    Agent::GetInstance()->ifmap_stale_cleaner()->
        StaleTimeout(AgentIfMapXmppChannel::GetSeqNumber());

    TaskScheduler::GetInstance()->Stop();
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        if (agent->ifmap_xmpp_channel(count)) {
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                config_cleanup_timer()->controller_timer_->Fire();
            if (channel) {
                channel->end_of_rib_tx_timer()->controller_timer_->Fire();
                channel->end_of_rib_rx_timer()->controller_timer_->Fire();
            }
            Agent::GetInstance()->ifmap_xmpp_channel(count)->
                end_of_config_timer()->controller_timer_->Fire();
        }
    }
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

void DeleteBgpPeer(Peer *peer) {
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);
    AgentXmppChannel *channel = NULL;
    FireAllControllerTimers(Agent::GetInstance(), channel);
    if (bgp_peer) {
        channel = bgp_peer->GetAgentXmppChannel();
        //Increment sequence number to clear config
        Agent::GetInstance()->controller()->
            DisConnectControllerIfmapServer(channel->GetXmppServerIdx());
        Agent::GetInstance()->controller()->FlushTimedOutChannels(channel->
                                            GetXmppServerIdx());
    }
    client->WaitForIdle();
    int task_id = TaskScheduler::GetInstance()->GetTaskId("Agent::ControllerXmpp");
    std::auto_ptr<TaskTrigger> trigger_
        (new TaskTrigger(boost::bind(ControllerCleanupTrigger), task_id, 0));
    trigger_->Set();
    client->WaitForIdle();
    Agent::GetInstance()->reset_controller_xmpp_channel(0);
    Agent::GetInstance()->reset_controller_xmpp_channel(1);
}

void FillEvpnNextHop(BgpPeer *peer, std::string vrf_name,
                     uint32_t label, uint32_t bmap) {
    TunnelOlist evpn_olist_map;
    evpn_olist_map.push_back(OlistTunnelEntry(nil_uuid(), label,
                                              IpAddress::from_string("8.8.8.8").to_v4(),
                                              bmap));
    Agent::GetInstance()->oper_db()->multicast()->
        ModifyEvpnMembers(peer, vrf_name,
                          evpn_olist_map, 0);
    client->WaitForIdle();
}

void FlushEvpnNextHop(BgpPeer *peer, std::string vrf_name,
                      uint32_t tag) {
    TunnelOlist evpn_olist_map;
    Agent::GetInstance()->oper_db()->multicast()->
        ModifyEvpnMembers(peer, vrf_name,
                          evpn_olist_map, tag,
                          ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
}

BridgeRouteEntry *GetL2FloodRoute(const std::string &vrf_name) {
    MacAddress broadcast_mac("ff:ff:ff:ff:ff:ff");
    BridgeRouteEntry *rt = L2RouteGet("vrf1", broadcast_mac);
    return rt;
}

PhysicalDevice *PhysicalDeviceGet(int id) {
    PhysicalDevice *pd;
    PhysicalDeviceKey key(MakeUuid(id));
    pd = static_cast<PhysicalDevice *>(Agent::GetInstance()->
            physical_device_table()->FindActiveEntry(&key));
    return pd;
}

PhysicalInterface *PhysicalInterfaceGet(const std::string &name) {
    PhysicalInterface *intf;
    PhysicalInterfaceKey key(name);
    intf = static_cast<PhysicalInterface *>(Agent::GetInstance()->
            interface_table()->FindActiveEntry(&key));
    return intf;
}

LogicalInterface *LogicalInterfaceGet(int id, const std::string &name) {
    LogicalInterface *intf;
    VlanLogicalInterfaceKey key(MakeUuid(id), name);
    intf = static_cast<LogicalInterface *>(Agent::GetInstance()->
            interface_table()->FindActiveEntry(&key));
    return intf;
}

void EnableRpf(const std::string &vn_name, int vn_id) {
    std::ostringstream buf;
    buf << "<virtual-network-properties>";
    buf << "<rpf>";
    buf << "enable";
    buf << "</rpf>";
    buf << "</virtual-network-properties>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-network", vn_name.c_str(), vn_id, cbuf);
    client->WaitForIdle();
}

void DisableRpf(const std::string &vn_name, int vn_id) {
    std::ostringstream buf;
    buf << "<virtual-network-properties>";
    buf << "<rpf>";
    buf << "disable";
    buf << "</rpf>";
    buf << "</virtual-network-properties>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-network", vn_name.c_str(), vn_id, cbuf);
    client->WaitForIdle();
}

void EnableUnknownBroadcast(const std::string &vn_name, int vn_id) {
    std::ostringstream buf;
    buf << "<virtual-network-properties>";
    buf << "<network-id>" << vn_id << "</network-id>" << endl;
    buf << "<vxlan-network-identifier>" << (vn_id+100) <<
           "</vxlan-network-identifier>" << endl;
    buf << "</virtual-network-properties>";
    buf << "<flood-unknown-unicast>";
    buf << "true";
    buf << "</flood-unknown-unicast>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-network", vn_name.c_str(), vn_id, cbuf);
    client->WaitForIdle();
}

void DisableUnknownBroadcast(const std::string &vn_name, int vn_id) {
    std::ostringstream buf;
    buf << "<virtual-network-properties>";
    buf << "<network-id>" << vn_id << "</network-id>" << endl;
    buf << "<vxlan-network-identifier>" << (vn_id+100) <<
           "</vxlan-network-identifier>" << endl;
    buf << "</virtual-network-properties>";
    buf << "<flood-unknown-unicast>";
    buf << "false";
    buf << "</flood-unknown-unicast>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-network", vn_name.c_str(), vn_id, cbuf);
    client->WaitForIdle();
}

void AddInterfaceVrfAssignRule(const char *intf_name, int intf_id,
                               const char *sip, const char *dip, int proto,
                               const char *vrf, const char *ignore_acl) {
        char buf[3000];
        sprintf(buf,
                "    <vrf-assign-table>\n"
                "        <vrf-assign-rule>\n"
                "            <match-condition>\n"
                "                 <protocol>\n"
                "                     %d\n"
                "                 </protocol>\n"
                "                 <src-address>\n"
                "                     <subnet>\n"
                "                        <ip-prefix>\n"
                "                         %s\n"
                "                        </ip-prefix>\n"
                "                        <ip-prefix-len>\n"
                "                         24\n"
                "                        </ip-prefix-len>\n"
                "                     </subnet>\n"
                "                 </src-address>\n"
                "                 <src-port>\n"
                "                     <start-port>\n"
                "                         %d\n"
                "                     </start-port>\n"
                "                     <end-port>\n"
                "                         %d\n"
                "                     </end-port>\n"
                "                 </src-port>\n"
                "                 <dst-address>\n"
                "                     <subnet>\n"
                "                        <ip-prefix>\n"
                "                         %s\n"
                "                        </ip-prefix>\n"
                "                        <ip-prefix-len>\n"
                "                         24\n"
                "                        </ip-prefix-len>\n"
                "                     </subnet>\n"
                "                 </dst-address>\n"
                "                 <dst-port>\n"
                "                     <start-port>\n"
                "                        %d\n"
                "                     </start-port>\n"
                "                     <end-port>\n"
                "                        %d\n"
                "                     </end-port>\n"
                "                 </dst-port>\n"
                "             </match-condition>\n"
                "             <vlan-tag>0</vlan-tag>\n"
                "             <routing-instance>%s</routing-instance>\n"
                "             <ignore-acl>%s</ignore-acl>\n"
                "         </vrf-assign-rule>\n"
                "    </vrf-assign-table>\n",
        proto, sip, 0, 65535, dip, 0, 65535, vrf,
        ignore_acl);
        AddNode("virtual-machine-interface", intf_name, intf_id, buf);
        client->WaitForIdle();
}

void AddPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalDeviceVnKey(MakeUuid(dev_id),
                MakeUuid(vn_id)));
    agent->physical_device_vn_table()->Enqueue(&req);
    PhysicalDeviceVn key(MakeUuid(dev_id), MakeUuid(vn_id));
    if (validate) {
        WAIT_FOR(100, 10000,
                (agent->physical_device_vn_table()->Find(&key, false) != NULL));
    }
}

void DelPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PhysicalDeviceVnKey(MakeUuid(dev_id),
                MakeUuid(vn_id)));
    agent->physical_device_vn_table()->Enqueue(&req);
    PhysicalDeviceVn key(MakeUuid(dev_id), MakeUuid(vn_id));
    if (validate) {
        WAIT_FOR(100, 10000,
                (agent->physical_device_vn_table()->Find(&key, true) == NULL));
    }
}

void AddStaticPreference(std::string intf_name, int intf_id,
                         uint32_t value) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-properties>";
    buf << "<local-preference>";
    buf << value;
    buf << "</local-preference>";
    buf << "</virtual-machine-interface-properties>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(),
            intf_id, cbuf);
    client->WaitForIdle();
}

bool VnMatch(VnListType &vn_list, std::string &vn) {
    if (vn == "" || vn == unknown_vn_) {
        return true;
    }

    for (VnListType::iterator it = vn_list.begin();
         it != vn_list.end(); ++it) {
        if (*it == vn)
            return true;
    }
    return false;
}

void AddAddressVrfAssignAcl(const char *intf_name, int intf_id,
                            const char *sip, const char *dip, int proto,
                            int sport_start, int sport_end, int dport_start,
                            int dport_end, const char *vrf,
                            const char *ignore_acl, const char *svc_intf_type) {

    if (svc_intf_type == NULL)
        svc_intf_type = "management";

    char buf[3000];
    sprintf(buf,
            "    <virtual-machine-interface-properties>\n"
            "       <service-interface-type>%s</service-interface-type>\n"
            "    </virtual-machine-interface-properties>\n"
            "    <vrf-assign-table>\n"
            "        <vrf-assign-rule>\n"
            "            <match-condition>\n"
            "                 <protocol>\n"
            "                     %d\n"
            "                 </protocol>\n"
            "                 <src-address>\n"
            "                     <subnet>\n"
            "                        <ip-prefix>\n"
            "                         %s\n"
            "                        </ip-prefix>\n"
            "                        <ip-prefix-len>\n"
            "                         24\n"
            "                        </ip-prefix-len>\n"
            "                     </subnet>\n"
            "                 </src-address>\n"
            "                 <src-port>\n"
            "                     <start-port>\n"
            "                         %d\n"
            "                     </start-port>\n"
            "                     <end-port>\n"
            "                         %d\n"
            "                     </end-port>\n"
            "                 </src-port>\n"
            "                 <dst-address>\n"
            "                     <subnet>\n"
            "                        <ip-prefix>\n"
            "                         %s\n"
            "                        </ip-prefix>\n"
            "                        <ip-prefix-len>\n"
            "                         24\n"
            "                        </ip-prefix-len>\n"
            "                     </subnet>\n"
            "                 </dst-address>\n"
            "                 <dst-port>\n"
            "                     <start-port>\n"
            "                        %d\n"
            "                     </start-port>\n"
            "                     <end-port>\n"
            "                        %d\n"
            "                     </end-port>\n"
            "                 </dst-port>\n"
            "             </match-condition>\n"
            "             <vlan-tag>0</vlan-tag>\n"
            "             <routing-instance>%s</routing-instance>\n"
            "             <ignore-acl>%s</ignore-acl>\n"
            "         </vrf-assign-rule>\n"
            "    </vrf-assign-table>\n",
        svc_intf_type, proto, sip, sport_start, sport_end, dip, dport_start,
        dport_end, vrf, ignore_acl);
    AddNode("virtual-machine-interface", intf_name, intf_id, buf);
    client->WaitForIdle();
}

void SendBgpServiceConfig(const std::string &ip,
                          uint32_t source_port,
                          uint32_t id,
                          const std::string &vmi_name,
                          const std::string &vrf_name,
                          const std::string &bgp_router_type,
                          bool deleted) {
    std::stringstream bgp_router_name;
    bgp_router_name << "bgp-router-" << source_port << "-" << ip;

    std::stringstream str;
    str << "<bgp-router-parameters><identifier>" << ip << "</identifier>"
        "<address>" << ip << "</address>"
        "<source-port>" << source_port << "</source-port>"
        "<router-type>" << bgp_router_type << "</router-type>"
        "</bgp-router-parameters>" << endl;

    std::stringstream str1;
    //Agent does not pick IP from bgpaas-ip-address. So dont populate.
    str1 << "<bgpaas-ip-address></bgpaas-ip-address>" << endl;

    if (deleted) {
        DelLink("bgp-router", bgp_router_name.str().c_str(),
                "bgp-as-a-service", bgp_router_name.str().c_str());
        client->WaitForIdle();
        DelLink("bgp-router", bgp_router_name.str().c_str(),
                "routing-instance", vrf_name.c_str());
        client->WaitForIdle();
        DelLink("virtual-machine-interface", vmi_name.c_str(),
                "bgp-as-a-service", bgp_router_name.str().c_str());
        client->WaitForIdle();
        DelNode("bgp-router", bgp_router_name.str().c_str());
        client->WaitForIdle();
        DelNode("bgp-as-a-service", bgp_router_name.str().c_str());
        client->WaitForIdle();
        return;
    }

    AddNode("bgp-router", bgp_router_name.str().c_str(), id,
            str.str().c_str());
    client->WaitForIdle();
    AddNode("bgp-as-a-service", bgp_router_name.str().c_str(), id,
            str1.str().c_str());
    client->WaitForIdle();
    AddLink("bgp-router", bgp_router_name.str().c_str(),
            "bgp-as-a-service", bgp_router_name.str().c_str());
    client->WaitForIdle();
    AddLink("bgp-router", bgp_router_name.str().c_str(),
            "routing-instance", vrf_name.c_str());
    client->WaitForIdle();
    AddLink("virtual-machine-interface", vmi_name.c_str(),
            "bgp-as-a-service", bgp_router_name.str().c_str());
    client->WaitForIdle();
}

void AddQosConfig(struct TestQosConfigData &data) {
    std::stringstream str;

    str << "<qos-config-type>" << data.type_ << "</qos-config-type>";

    str << "<default-forwarding-class-id>" << data.default_forwarding_class_
        << "</default-forwarding-class-id>";

    if (data.dscp_.size()) {
        str << "<dscp-entries>";
    }

    std::map<uint32_t, uint32_t>::const_iterator it = data.dscp_.begin();
    for (; it != data.dscp_.end(); it++) {
        str << "<qos-id-forwarding-class-pair>";
        str << "<key>" << it->first << "</key>";
        str << "<forwarding-class-id>"<< it->second << "</forwarding-class-id>";
        str << "</qos-id-forwarding-class-pair>";
    }
    if (data.dscp_.size()) {
        str << "</dscp-entries>";
    }

    if (data.vlan_priority_.size()) {
        str << "<vlan-priority-entries>";
    }
    it = data.vlan_priority_.begin();
    for (; it != data.vlan_priority_.end(); it++) {
        str << "<qos-id-forwarding-class-pair>";
        str << "<key>" << it->first << "</key>";
        str << "<forwarding-class-id>"<< it->second << "</forwarding-class-id>";
        str << "</qos-id-forwarding-class-pair>";
    }
    if (data.vlan_priority_.size()) {
        str << "</vlan-priority-entries>";
    }

    if (data.mpls_exp_.size()) {
        str << "<mpls-exp-entries>";
    }
    it = data.mpls_exp_.begin();
    for (; it != data.mpls_exp_.end(); it++) {
        str << "<qos-id-forwarding-class-pair>";
        str << "<key>" << it->first << "</key>";
        str << "<forwarding-class-id>"<< it->second << "</forwarding-class-id>";
        str << "</qos-id-forwarding-class-pair>";
    }
    if (data.mpls_exp_.size()) {
        str << "</mpls-exp-entries>";
    }

    char buf[10000];
    int len = 0;
    memset(buf, 0, 10000);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "qos-config",
            data.name_.c_str(), data.id_, str.str().c_str());
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

void VerifyQosConfig(Agent *agent, struct TestQosConfigData *data) {
    AgentQosConfigKey key(MakeUuid(data->id_));
    AgentQosConfig *qc = static_cast<AgentQosConfig *>(
            agent->qos_config_table()->FindActiveEntry(&key));

    if (data->type_ == "vhost") {
        EXPECT_TRUE(qc->type() == AgentQosConfig::VHOST);
    } else if (data->type_ == "fabric") {
        EXPECT_TRUE(qc->type() == AgentQosConfig::FABRIC);
    } else {
        EXPECT_TRUE(qc->type() == AgentQosConfig::DEFAULT);
    }

    std::map<uint32_t, uint32_t>::const_iterator it = data->dscp_.begin();
    AgentQosConfig::QosIdForwardingClassMap::const_iterator qc_it =
        qc->dscp_map().begin();
    while(it != data->dscp_.end() && qc_it != qc->dscp_map().end()) {
        EXPECT_TRUE(it->first == qc_it->first);
        EXPECT_TRUE(it->second == qc_it->second);
        it++;
        qc_it++;
    }
    EXPECT_TRUE(it == data->dscp_.end());
    EXPECT_TRUE(qc_it == qc->dscp_map().end());

    it = data->vlan_priority_.begin();
    qc_it = qc->vlan_priority_map().begin();
    while(it != data->vlan_priority_.end() &&
            qc_it != qc->vlan_priority_map().end()) {
        EXPECT_TRUE(it->first == qc_it->first);
        EXPECT_TRUE(it->second == qc_it->second);
        it++;
        qc_it++;
    }
    EXPECT_TRUE(it == data->vlan_priority_.end());
    EXPECT_TRUE(qc_it == qc->vlan_priority_map().end());

    it = data->mpls_exp_.begin();
    qc_it = qc->mpls_exp_map().begin();
    while(it != data->mpls_exp_.end() &&
            qc_it != qc->mpls_exp_map().end()) {
        EXPECT_TRUE(it->first == qc_it->first);
        EXPECT_TRUE(it->second == qc_it->second);
        it++;
        qc_it++;
    }
    EXPECT_TRUE(it == data->mpls_exp_.end());
    EXPECT_TRUE(qc_it == qc->mpls_exp_map().end());
    EXPECT_TRUE(qc->default_forwarding_class() ==
                data->default_forwarding_class_);
}

void AddQosQueue(const char *name, uint32_t id, uint32_t qos_queue_id) {

    std::stringstream str;
    str << "<qos-queue-identifier>" << qos_queue_id << "</qos-queue-identifier>";

    char buf[10000];
    int len = 0;
    memset(buf, 0, 10000);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "qos-queue", name, id, str.str().c_str());
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

void AddGlobalConfig(struct TestForwardingClassData *data,
                     uint32_t count) {
    std::stringstream str;

    char qos_name[100];
    char fc_name[100];
    for (uint32_t i = 0; i < count; i++) {
        sprintf(qos_name, "qosqueue%d", data[i].qos_queue_);
        sprintf(fc_name, "fc%d", data[i].id_);

        AddNode("qos-queue", qos_name, data[i].qos_queue_);
        client->WaitForIdle();
        str << "<forwarding-class-id>" << data[i].id_ << "</forwarding-class-id>";
        str << "<forwarding-class-dscp>" << data[i].dscp_ <<
            "</forwarding-class-dscp>";
        str << "<forwarding-class-vlan-priority>" << data[i].vlan_priority_
            << "</forwarding-class-vlan-priority>";
        str << "<forwarding-class-mpls-exp>" << data[i].mpls_exp_
            << "</forwarding-class-mpls-exp>";

        char buf[10000];
        int len = 0;
        memset(buf, 0, 10000);
        AddXmlHdr(buf, len);
        AddNodeString(buf, len, "forwarding-class", fc_name,
                      data[i].id_, str.str().c_str());
        AddXmlTail(buf, len);
        ApplyXmlString(buf);
        AddLink("forwarding-class", fc_name, "qos-queue", qos_name);
    }
}

void DelGlobalConfig(struct TestForwardingClassData *data,
                     uint32_t count) {

    char qos_name[100];
    char fc_name[100];
    for (uint32_t i = 0; i < count; i++) {
        sprintf(qos_name, "qosqueue%d", data[i].qos_queue_);
        sprintf(fc_name, "fc%d", data[i].id_);

        DelLink("forwarding-class", fc_name, "qos-queue", qos_name);
        DelNode("qos-queue", qos_name);
        DelNode("forwarding-class", fc_name);
    }
    client->WaitForIdle();
}

void VerifyForwardingClass(Agent *agent, struct TestForwardingClassData *data,
                           uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        ForwardingClassKey key(MakeUuid(data[i].id_));
        ForwardingClass *fc = static_cast<ForwardingClass *>(
            agent->forwarding_class_table()->FindActiveEntry(&key));
        EXPECT_TRUE(fc->dscp() == data[i].dscp_);
        EXPECT_TRUE(fc->vlan_priority() == data[i].vlan_priority_);
        EXPECT_TRUE(fc->mpls_exp() == data[i].mpls_exp_);
        EXPECT_TRUE(fc->qos_queue_ref()->uuid() == MakeUuid(data[i].qos_queue_));
    }
}

void AddAapWithDisablePolicy(std::string intf_name, int intf_id,
                             std::vector<Ip4Address> aap_list,
                             bool disable_policy) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-allowed-address-pairs>";
    std::vector<Ip4Address>::iterator it = aap_list.begin();
    while (it != aap_list.end()) {
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << it->to_string()<<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac><mac-address>" << "00:00:00:00:00:00"
            << "</mac-address></mac>";
        buf << "<flag>" << "act-stby" << "</flag>";
        buf << "</allowed-address-pair>";
        it++;
    }
    buf << "</virtual-machine-interface-allowed-address-pairs>";
    buf << "<virtual-machine-interface-disable-policy>";
    if (disable_policy) {
        buf << "true";
    } else {
        buf << "false";
    }
    buf << "</virtual-machine-interface-disable-policy>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(), intf_id, cbuf);
    client->WaitForIdle();
}

void AddAap(std::string intf_name, int intf_id,
            std::vector<Ip4Address> aap_list) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-allowed-address-pairs>";
    std::vector<Ip4Address>::iterator it = aap_list.begin();
    while (it != aap_list.end()) {
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << it->to_string()<<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac><mac-address>" << "00:00:00:00:00:00"
            << "</mac-address></mac>";
        buf << "<flag>" << "act-stby" << "</flag>";
        buf << "</allowed-address-pair>";
        it++;
    }
    buf << "</virtual-machine-interface-allowed-address-pairs>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(), intf_id, cbuf);
    client->WaitForIdle();
}

void AddAap(std::string intf_name, int intf_id, Ip4Address ip,
            const std::string &mac) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-allowed-address-pairs>";
    buf << "<allowed-address-pair>";
    buf << "<ip>";
    buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
    buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
    buf << "</ip>";
    buf << "<mac>" << mac << "</mac>";
    buf << "<flag>" << "act-stby" << "</flag>";
    buf << "</allowed-address-pair>";
    buf << "</virtual-machine-interface-allowed-address-pairs>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(),
            intf_id, cbuf);
    client->WaitForIdle();
}

void AddAapWithMacAndDisablePolicy(const std::string &intf_name, int intf_id,
                                   Ip4Address ip, const std::string &mac,
                                   bool disable_policy) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-allowed-address-pairs>";
    buf << "<allowed-address-pair>";
    buf << "<ip>";
    buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
    buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
    buf << "</ip>";
    buf << "<mac>" << mac << "</mac>";
    buf << "<flag>" << "act-stby" << "</flag>";
    buf << "</allowed-address-pair>";
    buf << "</virtual-machine-interface-allowed-address-pairs>";
    buf << "<virtual-machine-interface-disable-policy>";
    if (disable_policy) {
        buf << "true";
    } else {
        buf << "false";
    }
    buf << "</virtual-machine-interface-disable-policy>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(), intf_id, cbuf);
    client->WaitForIdle();
}

void AddEcmpAap(std::string intf_name, int intf_id, Ip4Address ip,
                const std::string &mac) {
    std::ostringstream buf;
    buf << "<virtual-machine-interface-allowed-address-pairs>";
    buf << "<allowed-address-pair>";
    buf << "<ip>";
    buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
    buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
    buf << "</ip>";
    buf << "<mac>" << mac << "</mac>";
    buf << "<address-mode>" << "active-active" << "</address-mode>";
    buf << "</allowed-address-pair>";
    buf << "</virtual-machine-interface-allowed-address-pairs>";
    char cbuf[10000];
    strcpy(cbuf, buf.str().c_str());
    AddNode("virtual-machine-interface", intf_name.c_str(),
            intf_id, cbuf);
    client->WaitForIdle();
}

uint32_t AllocLabel(const char *str) {
    Agent *agent = Agent::GetInstance();
    std::stringstream str_str;
    str_str << str;
    ResourceManager::KeyPtr key(new TestMplsResourceKey(agent->
                                resource_manager(), str_str.str()));
    uint32_t label = ((static_cast<IndexResourceData *>(agent->resource_manager()->
                                      Allocate(key).get()))->index());
    return label;
}

void FreeLabel(uint32_t label) {
    Agent *agent = Agent::GetInstance();
    agent->resource_manager()->Release(Resource::MPLS_INDEX, label);
}

bool BridgeDomainFind(int id) {
    BridgeDomainEntry *bridge_domain;
    BridgeDomainKey key(MakeUuid(id));
    bridge_domain =
        static_cast<BridgeDomainEntry *>(Agent::GetInstance()->
                bridge_domain_table()->FindActiveEntry(&key));
    return (bridge_domain != NULL);
}

BridgeDomainEntry* BridgeDomainGet(int id) {
    BridgeDomainKey key(MakeUuid(id));
    return static_cast<BridgeDomainEntry *>(Agent::GetInstance()->
                           bridge_domain_table()->FindActiveEntry(&key));
}
