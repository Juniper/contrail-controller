/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test/test_init.h"
#include "oper/mirror_table.h"
#include "uve/test/vn_uve_table_test.h"

#define MAX_TESTNAME_LEN 80

using namespace std;

Peer *bgp_peer_;
TestClient *client;
#define MAX_VNET 10
int test_tap_fd[MAX_VNET];

uuid MakeUuid(int id) {
    char str[50];
    boost::uuids::string_generator gen;
    sprintf(str, "000000000000000000000000000000%02x", id);
    boost::uuids::uuid u1 = gen(std::string(str));

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

void AddLinkString(char *buff, int &len, const char *node_name1,
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
            "       </link>\n", node_name1, name1, node_name2, name2);

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
            "       </link>\n", node_name1, name1, node_name2, name2);
    len = strlen(buff);
}

void AddNodeString(char *buff, int &len, const char *node_name,
                   const char *name, int id, const char *attr) {
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
            "           %s\n"
            "       </node>\n", node_name, name, id, attr);
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
        str << "                   <enable-dhcp>" << dhcp_enable << "</enable-dhcp>\n";
        if (add_subnet_tags)
            str <<                 add_subnet_tags << "\n";
        str << "               </ipam-subnets>\n";
    }
    if (vm_host_routes && vm_host_routes->size()) {
        str << "               <host-routes>\n";
        for (unsigned int i = 0; i < vm_host_routes->size(); i++) {
            str << "                   <route>\n";
            str << "                       <prefix>" << (*vm_host_routes)[i] << "</prefix>\n";
            str << "                       <next-hop />\n";
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
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddLink(const char *node_name1, const char *name1,
             const char *node_name2, const char *name2) {
    char buff[1024];
    int len = 0;

    AddXmlHdr(buff, len);
    AddLinkString(buff, len, node_name1, name1, node_name2, name2);
    AddXmlTail(buff, len);
    //LOG(DEBUG, buff);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
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
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddNode(const char *node_name, const char *name, int id) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id);
    AddXmlTail(buff, len);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddNodeByStatus(const char *node_name, const char *name, int id, bool status) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id, status);
    AddXmlTail(buff, len);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddNode(const char *node_name, const char *name, int id, 
                    const char *attr) {
    char buff[10240];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, node_name, name, id, attr);
    AddXmlTail(buff, len);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void AddLinkNode(const char *node_name, const char *name, const char *attr) {
    char buff[1024];
    int len = 0;

    AddXmlHdr(buff, len);
    AddLinkNodeString(buff, len, node_name, name, attr);
    AddXmlTail(buff, len);
    LOG(DEBUG, buff);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
}

void DelNode(const char *node_name, const char *name) {
    char buff[1024];
    int len = 0;

    DelXmlHdr(buff, len);
    DelNodeString(buff, len, node_name, name);
    DelXmlTail(buff, len);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    return;
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
                int project_id) {
    CfgIntKey *key = new CfgIntKey(MakeUuid(intf_id));
    CfgIntData *data = new CfgIntData();
    boost::system::error_code ec;
    IpAddress ip = Ip4Address::from_string(ipaddr, ec);
    char vm_name[MAX_TESTNAME_LEN];
    sprintf(vm_name, "vm%d", vm_id);
    data->Init(MakeUuid(vm_id), MakeUuid(vn_id), MakeUuid(project_id),
               name, ip, mac, vm_name, vlan, 0);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    usleep(1000);
}

void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac) {
    IntfCfgAdd(intf_id, name, ipaddr, vm_id, vn_id, mac,
               VmInterface::kInvalidVlanId);
}

void IntfCfgAdd(PortInfo *input, int id) {
    IntfCfgAdd(input[id].intf_id, input[id].name, input[id].addr,
               input[id].vm_id, input[id].vn_id, input[id].mac);
}

void IntfCfgDel(PortInfo *input, int id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    CfgIntKey *key = new CfgIntKey(MakeUuid(input[id].intf_id));
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    usleep(1000);
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

VrfEntry *VrfGet(const char *name) {
    VrfKey key(name);
    return static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->FindActiveEntry(&key));
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
    return static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
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

bool VmPortGetStats(PortInfo *input, int id, uint32_t & bytes, uint32_t & pkts) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    const AgentStatsCollector::InterfaceStats *st = AgentUve::GetInstance()->agent_stats_collector()->GetInterfaceStats(intf);
    if (st == NULL)
        return false;

    bytes = st->in_bytes;
    pkts = st->in_pkts;
    return true;
}

bool VrfStatsMatch(int vrf_id, std::string vrf_name, bool stats_match,
                   uint64_t discards, uint64_t resolves, uint64_t receives, 
                   uint64_t udp_tunnels, uint64_t udp_mpls_tunnels, 
                   uint64_t gre_mpls_tunnels, uint64_t ecmp_composites, 
                   uint64_t fabric_composites, uint64_t l2_mcast_composites,
                   uint64_t l3_mcast_composites, uint64_t multi_proto_composites,
                   uint64_t encaps, uint64_t l2_encaps) {
    const AgentStatsCollector::VrfStats *st = 
                        Agent::GetInstance()->uve()->agent_stats_collector()->GetVrfStats(vrf_id);
    if (st == NULL) {
        return false;
    }

    if (vrf_name.compare(st->name) != 0) {
        return false;
    }
   
    if (!stats_match) {
        return true;
    }

    if (st->discards == discards && st->resolves == resolves && 
        st->receives == receives && st->udp_tunnels == udp_tunnels && 
        st->udp_mpls_tunnels == udp_mpls_tunnels &&
        st->gre_mpls_tunnels == gre_mpls_tunnels &&
        st->ecmp_composites == ecmp_composites &&
        st->l3_mcast_composites == l3_mcast_composites &&
        st->l2_mcast_composites == l2_mcast_composites &&
        st->fabric_composites == fabric_composites &&
        st->multi_proto_composites == multi_proto_composites &&
        st->l2_encaps == l2_encaps && st->encaps == encaps) {
        return true;
    }
    LOG(DEBUG, "discards " << st->discards << " resolves " << st->resolves <<
        " receives " << st->receives << " udp tunnels " << st->udp_tunnels <<
        " udp mpls tunnels " << st->udp_mpls_tunnels << 
        " udp gre tunnels " << st->gre_mpls_tunnels << 
        " ecmp composites " << st->ecmp_composites << 
        " fabric composites " << st->fabric_composites << 
        " l2 composites " << st->l2_mcast_composites << 
        " l3 composites " << st->l3_mcast_composites << 
        " multi proto composites " << st->multi_proto_composites << 
        " encaps " << st->encaps << " l2 encals " << st->l2_encaps);
    return false;
}

bool VrfStatsMatchPrev(int vrf_id, uint64_t discards, uint64_t resolves, 
                   uint64_t receives, uint64_t udp_tunnels, 
                   uint64_t udp_mpls_tunnels, uint64_t gre_mpls_tunnels, 
                   uint64_t ecmp_composites, uint64_t fabric_composites, 
                   uint64_t l2_mcast_composites, uint64_t l3_mcast_composites, 
                   uint64_t multi_proto_composites, uint64_t encaps, 
                   uint64_t l2_encaps) {
    const AgentStatsCollector::VrfStats *st = 
                        Agent::GetInstance()->uve()->agent_stats_collector()->GetVrfStats(vrf_id);
    if (st == NULL) {
        return false;
    }

    if (st->prev_discards == discards && st->prev_resolves == resolves && 
        st->prev_receives == receives && st->prev_udp_tunnels == udp_tunnels && 
        st->prev_udp_mpls_tunnels == udp_mpls_tunnels &&
        st->prev_gre_mpls_tunnels == gre_mpls_tunnels &&
        st->prev_ecmp_composites == ecmp_composites &&
        st->prev_l3_mcast_composites == l3_mcast_composites &&
        st->prev_l2_mcast_composites == l2_mcast_composites &&
        st->prev_fabric_composites == fabric_composites &&
        st->prev_multi_proto_composites == multi_proto_composites &&
        st->prev_l2_encaps == l2_encaps && st->prev_encaps == encaps) {
        return true;
    }
    LOG(DEBUG, "discards " << st->prev_discards << " resolves " << st->prev_resolves <<
        " receives " << st->prev_receives << " udp tunnels " << st->prev_udp_tunnels <<
        " udp mpls tunnels " << st->prev_udp_mpls_tunnels << 
        " udp gre tunnels " << st->prev_gre_mpls_tunnels << 
        " ecmp composites " << st->prev_ecmp_composites << 
        " fabric composites " << st->prev_fabric_composites << 
        " l2 composites " << st->prev_l2_mcast_composites << 
        " l3 composites " << st->prev_l3_mcast_composites << 
        " multi proto composites " << st->prev_multi_proto_composites << 
        " encaps " << st->prev_encaps << " l2 encals " << st->prev_l2_encaps);
    return false;
}

bool VnStatsMatch(char *vn, uint64_t in_bytes, uint64_t in_pkts,
                  uint64_t out_bytes, uint64_t out_pkts) {
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    const VnUveEntry* entry = vnut->GetVnUveEntry(string(vn));
    if (!entry) {
        LOG(DEBUG, "Vn " << string(vn) << " NOT FOUND");
        return false;
    }
    uint64_t match_in_bytes, match_out_bytes, match_in_pkts, match_out_pkts;
    entry->GetInStats(&match_in_bytes, &match_in_pkts);
    entry->GetOutStats(&match_out_bytes, &match_out_pkts);

    if (match_in_bytes == in_bytes && match_in_pkts == in_pkts &&
        match_out_bytes == out_bytes && match_out_pkts == out_pkts) {
        return true;
    }
    LOG(DEBUG, "in_bytes " << match_in_bytes << " in_tpkts " << match_in_pkts <<
               "out bytes " << match_out_bytes  << " out_tpkts " << match_out_pkts);
    return false;
}

bool VmPortStats(PortInfo *input, int id, uint32_t bytes, uint32_t pkts) {
    Interface *intf;
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(input[id].intf_id),
                       input[id].name);
    intf=static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    if (intf == NULL)
        return false;

    const AgentStatsCollector::InterfaceStats *st = AgentUve::GetInstance()->agent_stats_collector()->GetInterfaceStats(intf);
    if (st == NULL)
        return false;

    if (st->in_pkts == pkts && st->in_bytes == bytes)
        return true;
    return false;
}

bool VmPortStatsMatch(Interface *intf, uint32_t ibytes, uint32_t ipkts,
                      uint32_t obytes, uint32_t opkts) {
    const AgentStatsCollector::InterfaceStats *st = AgentUve::GetInstance()->agent_stats_collector()->GetInterfaceStats(intf);
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

void DeleteTap(int fd) {
    if (ioctl(fd, TUNSETPERSIST, 0) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> making tap interface persistent");
        assert(0);
    }
}

void DeleteTapIntf(const int fd[], int count) {
    for (int i = 0; i < count; i++) {
        DeleteTap(fd[i]);
    }
}

int CreateTap(const char *name) {
    int fd;
    struct ifreq ifr;

    if ((fd = open(TUN_INTF_CLONE_DEV, O_RDWR)) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> opening tap-device");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, name, IF_NAMESIZE);
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> creating " << name << "tap-device");
        assert(0);
    }

    if (ioctl(fd, TUNSETPERSIST, 1) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> making tap interface persistent");
        assert(0);
    }
    return fd;
}

void CreateTapIntf(const char *name, int count) {
    char ifname[IF_NAMESIZE];

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        CreateTap(ifname);
    }
}

void CreateTapInterfaces(const char *name, int count, int *fd) {
    char ifname[IF_NAMESIZE];
    int raw;
    struct ifreq ifr;

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        fd[i] = CreateTap(ifname);
        
        if ((raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                                "> creating socket");
                assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(raw, SIOCGIFINDEX, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> getting ifindex of the tap interface");
                assert(0);
        }

        struct sockaddr_ll sll;
        memset(&sll, 0, sizeof(struct sockaddr_ll));
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifr.ifr_ifindex;
        sll.sll_protocol = htons(ETH_P_ALL);
        if (bind(raw, (struct sockaddr *)&sll, sizeof(struct sockaddr_ll)) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> binding the socket to the tap interface");
                assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(raw, SIOCGIFFLAGS, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                                "> getting socket flags");
                assert(0);
        }

        ifr.ifr_flags |= IFF_UP;
        if (ioctl(raw, SIOCSIFFLAGS, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                                "> setting socket flags");
                assert(0);
        }
    }

}

void VnAddReq(int id, const char *name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, nil_uuid(),
                                              name, ipam, vn_ipam_data, id);
    usleep(1000);
}

void VnAddReq(int id, const char *name, int acl_id) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, 
                                              MakeUuid(acl_id),
                                              name, ipam, vn_ipam_data, id);
    usleep(1000);
}

void VnAddReq(int id, const char *name, int acl_id, const char *vrf_name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, 
                                              MakeUuid(acl_id), vrf_name, ipam,
                                              vn_ipam_data, id);
    usleep(1000);
}

void VnAddReq(int id, const char *name, const char *vrf_name) {
    std::vector<VnIpam> ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    Agent::GetInstance()->vn_table()->AddVn(MakeUuid(id), name, nil_uuid(), 
                                              vrf_name, ipam, vn_ipam_data, id);
    usleep(1000);
}

void VnDelReq(int id) {
    Agent::GetInstance()->vn_table()->DelVn(MakeUuid(id));
    usleep(1000);
}

void VrfAddReq(const char *name) {
    Agent::GetInstance()->vrf_table()->CreateVrfReq(name);
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
    VmData *data = new VmData(string(vm_name), sg_list);
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

    AclData *data = new AclData(*acl_spec);
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
        action.simple_action = TrafficAction::DROP;
    } else {
        action.simple_action = TrafficAction::PASS;
    }
    ae_spec->action_l.push_back(action);
    acl_spec->acl_entry_specs_.push_back(*ae_spec);
    delete ae_spec;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    AclKey *key = new AclKey(MakeUuid(id));
    AclData *data = new AclData(*acl_spec);

    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
    delete acl_spec;
    usleep(1000);
}

bool RouteFind(const string &vrf_name, const Ip4Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    Inet4UnicastRouteKey key(NULL, vrf_name, addr, plen);
    Inet4UnicastRouteEntry* route = 
        static_cast<Inet4UnicastRouteEntry *>
        (static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    return (route != NULL);
}

bool RouteFind(const string &vrf_name, const string &addr, int plen) {
    return RouteFind(vrf_name, Ip4Address::from_string(addr), plen);
}

bool L2RouteFind(const string &vrf_name, const struct ether_addr &mac) {
    VrfEntry *vrf = Agent::GetInstance()->
        vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return false;

    Layer2RouteKey key(Agent::GetInstance()->local_vm_peer(), vrf_name, mac);
    Layer2RouteEntry *route = 
        static_cast<Layer2RouteEntry *>
        (static_cast<Layer2AgentRouteTable *>(vrf->
             GetLayer2RouteTable())->FindActiveEntry(&key));
    return (route != NULL);
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

Layer2RouteEntry *L2RouteGet(const string &vrf_name, 
                             const struct ether_addr &mac) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Layer2RouteKey key(Agent::GetInstance()->local_vm_peer(), vrf_name, mac);
    Layer2RouteEntry *route = 
        static_cast<Layer2RouteEntry *>
        (static_cast<Layer2AgentRouteTable *>(vrf->
             GetLayer2RouteTable())->FindActiveEntry(&key));
    return route;
}

Inet4UnicastRouteEntry* RouteGet(const string &vrf_name, const Ip4Address &addr, int plen) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Inet4UnicastRouteKey key(NULL, vrf_name, addr, plen);
    Inet4UnicastRouteEntry* route = 
        static_cast<Inet4UnicastRouteEntry *>
        (static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    return route;
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
    InetInterfaceKey *intf_key = new InetInterfaceKey(ifname);
    return static_cast<NextHop *>
        (table->FindActiveEntry(new ReceiveNHKey(intf_key, policy)));
}

bool TunnelNHFind(const Ip4Address &server_ip) {
    return TunnelNHFind(server_ip, false, TunnelType::MPLS_GRE);
}

bool VlanNhFind(int id, uint16_t tag) {
    VlanNHKey key(MakeUuid(id), tag);
    NextHop *nh = static_cast<NextHop *>(Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key));
    return (nh != NULL);
}

bool Layer2TunnelRouteAdd(const Peer *peer, const string &vm_vrf, 
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, struct ether_addr &remote_vm_mac,
                          const Ip4Address &vm_addr, uint8_t plen) {
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, server_ip,
                              bmap, label, "", SecurityGroupList(),
                              PathPreference());
    Layer2AgentRouteTable::AddRemoteVmRouteReq(peer, vm_vrf, remote_vm_mac,
                                        vm_addr, plen, data);
    return true;
}

bool Layer2TunnelRouteAdd(const Peer *peer, const string &vm_vrf, 
                          TunnelType::TypeBmap bmap, const char *server_ip,
                          uint32_t label, struct ether_addr &remote_vm_mac,
                          const char *vm_addr, uint8_t plen) {
    boost::system::error_code ec;
    Layer2TunnelRouteAdd(peer, vm_vrf, bmap,
                        Ip4Address::from_string(server_ip, ec), label, remote_vm_mac,
                         Ip4Address::from_string(vm_addr, ec), plen);
}

bool EcmpTunnelRouteAdd(const Peer *peer, const string &vrf_name, const Ip4Address &vm_ip,
                       uint8_t plen, std::vector<ComponentNHData> &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const PathPreference &path_preference) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(vrf_name, vm_ip, plen,
                                        false));

    CompositeNH::CreateCompositeNH(vrf_name, vm_ip, false, comp_nh_list);
    nh_req.data.reset(new CompositeNHData(comp_nh_list,
                                          CompositeNHData::REPLACE));
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(peer, vm_ip, plen, vn_name, -1, false, vrf_name,
                                sg, path_preference, nh_req);
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vrf_name, vm_ip, plen, data);
}

bool Inet4TunnelRouteAdd(const Peer *peer, const string &vm_vrf, const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference) {
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, server_ip,
                              bmap, label, dest_vn_name, sg, path_preference);
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(peer, vm_vrf,
                                        vm_addr, plen, data);
    return true;
}

bool Inet4TunnelRouteAdd(const Peer *peer, const string &vm_vrf, char *vm_addr,
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
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(bgp_peer_,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(),
                              vm_vrf, Ip4Address::from_string(server, ec),
                              TunnelType::AllType(), label, "",
                              SecurityGroupList(), PathPreference());
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, vm_vrf,
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
    struct ether_addr mac = *ether_aton(mac_str);
    Interface *intf;
    PhysicalInterfaceKey key(ifname);
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    boost::system::error_code ec;
    Inet4UnicastAgentRouteTable::ArpRoute(DBRequest::DB_ENTRY_ADD_CHANGE,
                              Ip4Address::from_string(ip, ec), mac, 
                              Agent::GetInstance()->fabric_vrf_name(),
                              *intf, true, 32);

    return true;
}

bool DelArp(const string &ip, const char *mac_str, const string &ifname) {
    struct ether_addr mac = *ether_aton(mac_str);
    Interface *intf;
    PhysicalInterfaceKey key(ifname);
    intf = static_cast<Interface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    boost::system::error_code ec;
    Inet4UnicastAgentRouteTable::ArpRoute(DBRequest::DB_ENTRY_DELETE, 
                              Ip4Address::from_string(ip, ec),
                              mac, Agent::GetInstance()->fabric_vrf_name(), *intf, false, 32);
    return true;
}

void AddVm(const char *name, int id) {
    AddNode("virtual-machine", name, id);
}

void DelVm(const char *name) {
    DelNode("virtual-machine", name);
}

void AddVrf(const char *name, int id) {
    AddNode("routing-instance", name, id);
}

void DelVrf(const char *name) {
    DelNode("routing-instance", name);
}

void ModifyForwardingModeVn(const string &name, int id, const string &fw_mode) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <network-id>" << id << "</network-id>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>" << fw_mode << "</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;

    AddNode("virtual-network", name.c_str(), id, str.str().c_str());
}

void AddL2Vn(const char *name, int id) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <network-id>" << id << "</network-id>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>l2</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str());
}

void AddVn(const char *name, int id) {
    std::stringstream str;
    str << "<virtual-network-properties>" << endl;
    str << "    <network-id>" << id << "</network-id>" << endl;
    str << "    <vxlan-network-identifier>" << (id+100) << "</vxlan-network-identifier>" << endl;
    str << "    <forwarding-mode>l2_l3</forwarding-mode>" << endl;
    str << "</virtual-network-properties>" << endl;

    AddNode("virtual-network", name, id, str.str().c_str());
}

void DelVn(const char *name) {
    DelNode("virtual-network", name);
}

void AddPort(const char *name, int id, const char *attr) {
    if (attr)
        AddNode("virtual-machine-interface", name, id, attr);
    else
        AddNode("virtual-machine-interface", name, id);
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
              << "<prefix>\n" << rt->addr_.to_string() 
              << "/" << rt->plen_ << " \n"  << "</prefix>\n"
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

static string AddAclXmlString(const char *node_name, const char *name, int id,
                              const char *src_vn, const char *dest_vn,
                              const char *action, std::string vrf_assign) {
    char buff[10240];
    sprintf(buff, 
            "<?xml version=\"1.0\"?>\n"
            "<config>\n"
            "   <update>\n"
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
            "           </id-perms>\n"
            "           <access-control-list-entries>\n"
            "                <acl-rule>\n"
            "                    <match-condition>\n"
            "                        <protocol>\n"
            "                            any\n"
            "                        </protocol>\n"
            "                        <src-address>\n"
            "                            <virtual-network>\n"
            "                                %s\n"
            "                            </virtual-network>\n"
            "                        </src-address>\n"
            "                        <src-port>\n"
            "                            <start-port>\n"
            "                                10\n"
            "                            </start-port>\n"
            "                            <end-port>\n"
            "                                20\n"
            "                            </end-port>\n"
            "                        </src-port>\n"
            "                        <dst-address>\n"
            "                            <virtual-network>\n"
            "                                %s\n"
            "                            </virtual-network>\n"
            "                        </dst-address>\n"
            "                        <dst-port>\n"
            "                            <start-port>\n"
            "                                 10\n"
            "                            </start-port>\n"
            "                            <end-port>\n"
            "                                 20\n"
            "                            </end-port>\n"
            "                        </dst-port>\n"
            "                    </match-condition>\n"
            "                    <action-list>\n"
            "                        <simple-action>\n"
            "                            %s\n"
            "                        </simple-action>\n"
            "                        <assign-routing-instance>"
            "                            %s\n"
            "                        </assign-routing-instance>"
            "                    </action-list>\n"
            "                </acl-rule>\n"
            "           </access-control-list-entries>\n"
            "       </node>\n"
            "   </update>\n"
            "</config>\n", node_name, name, id, src_vn, dest_vn, action,
            vrf_assign.c_str());
    string s(buff);
    return s;
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
                                    src_vn, dest_vn, action, "");
    pugi::xml_document xdoc_;

    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->
        ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
}

void AddVrfAssignNetworkAcl(const char *name, int id, const char *src_vn,
                            const char *dest_vn, const char *action,
                            std::string vrf_name) {
    std::string s = AddAclXmlString("access-control-list", name, id,
                                    src_vn, dest_vn, action, vrf_name);
    pugi::xml_document xdoc_;

    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->
        ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
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

void AddFloatingIp(const char *name, int id, const char *addr) {
    char buff[128];

    sprintf(buff, "<floating-ip-address>%s</floating-ip-address>", addr);
    AddNode("floating-ip", name, id, buff);
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

void AddInstanceIp(const char *name, int id, const char *addr) {
    char buf[128];

    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>", addr);
    AddNode("instance-ip", name, id, buf);
}

void AddActiveActiveInstanceIp(const char *name, int id, const char *addr) {
    char buf[128];
    sprintf(buf, "<instance-ip-address>%s</instance-ip-address>"
                 "<instance-ip-mode>active-active</instance-ip-mode>", addr);
    AddNode("instance-ip", name, id, buf);
}

void DelInstanceIp(const char *name) {
    DelNode("instance-ip", name);
}

void AddVmPortVrf(const char *name, const string &ip, uint16_t tag) {
    char buff[256];
    int len = 0;

    len += sprintf(buff + len,   "<direction>both</direction>");
    len += sprintf(buff + len,   "<vlan-tag>%d</vlan-tag>", tag);
    len += sprintf(buff + len,   "<src-mac>02:00:00:00:00:02</src-mac>");
    len += sprintf(buff + len,   "<dst-mac>02:00:00:00:00:01</dst-mac>");
    len += sprintf(buff + len,   "<service-chain-address>%s</service-chain-address>", ip.c_str());
    AddLinkNode("virtual-machine-interface-routing-instance", name, buff);
}

void DelVmPortVrf(const char *name) {
    DelNode("virtual-machine-interface-routing-instance", name);
}

void AddIPAM(const char *name, IpamInfo *ipam, int ipam_size, const char *ipam_attr,
             const char *vdns_name, const std::vector<std::string> *vm_host_routes,
             const char *add_subnet_tags) {
    char buff[8192];
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
                  "virtual-network-network-ipam", node_name);
    AddLinkString(buff, len, "network-ipam", ipam_name,
                  "virtual-network-network-ipam", node_name);
    if (vdns_name) {
        AddLinkString(buff, len, "network-ipam", ipam_name,
                      "virtual-DNS", vdns_name);
    }
    AddXmlTail(buff, len);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
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
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
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

void DelEncapList() {
    DelNode("global-vrouter-config", "vrouter-config");
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
    VrfEntry *vrf;
    VrfKey vrf_key(input[id].vrf);
    vrf = static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->FindActiveEntry(&vrf_key));
    LOG(DEBUG, "Vrf id for " << input[id].vrf << " is " << vrf->vrf_id());

    FlowKey key;

    key.src_port = 0;
    key.dst_port = 0;
    key.nh = id;
    key.src.ipv4 = input[id].sip;
    key.dst.ipv4 = input[id].dip;
    key.protocol = IPPROTO_ICMP; 

    FlowEntry *fe = Agent::GetInstance()->pkt()->flow_table()->Find(key);
    if (fe == NULL) {
        LOG(DEBUG, "Flow not found");
        return false;
    }

    LOG(DEBUG, " bytes " << fe->stats().bytes << " pkts " << fe->stats().packets);
    if (fe->stats().bytes == bytes && fe->stats().packets == pkts) {
        return true;
    }

    return false;
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
        sprintf(instance_ip, "instance%d", input[i].vm_id);
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
            sprintf(vn_name, "vn%d", input[i].vn_id);
        if (vrf)
            strncpy(vrf_name, vrf, MAX_TESTNAME_LEN);
        else
            sprintf(vrf_name, "vrf%d", input[i].vn_id);
        sprintf(vm_name, "vm%d", input[i].vm_id);
        sprintf(instance_ip, "instance%d", input[i].vm_id);
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
                sprintf(vn_name, "vn%d", input[i].vn_id);
            if (vrf)
                sprintf(vrf_name, "%s", vrf);
            else
                sprintf(vrf_name, "vrf%d", input[i].vn_id);
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
        sprintf(instance_ip, "instance%d", input[i].vm_id);
        AddVn(vn_name, input[i].vn_id);
        AddVrf(vrf_name);
        AddVm(vm_name, input[i].vm_id);
        AddVmPortVrf(input[i].name, "", 0);

        //AddNode("virtual-machine-interface-routing-instance", input[i].name, 
        //        input[i].intf_id);
        IntfCfgAdd(input, i);
        AddPort(input[i].name, input[i].intf_id);
        AddActiveActiveInstanceIp(instance_ip, input[i].vm_id, input[i].addr);
        AddLink("virtual-network", vn_name, "routing-instance", vrf_name);
        AddLink("virtual-machine", vm_name, "virtual-machine-interface",
                input[i].name);
        AddLink("virtual-network", vn_name, "virtual-machine-interface",
                input[i].name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "routing-instance", vrf_name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "virtual-machine-interface", input[i].name);
        AddLink("virtual-machine-interface", input[i].name,
                "instance-ip", instance_ip);

        if (acl_id) {
            AddLink("virtual-network", vn_name, "access-control-list", acl_name);
        }
    }
}

void CreateVmportEnvInternal(struct PortInfo *input, int count, int acl_id, 
                     const char *vn, const char *vrf,
                     const char *vm_interface_attr,
                     bool l2_vn, bool with_ip, bool ecmp) {
    char vn_name[MAX_TESTNAME_LEN];
    char vm_name[MAX_TESTNAME_LEN];
    char vrf_name[MAX_TESTNAME_LEN];
    char acl_name[MAX_TESTNAME_LEN];
    char instance_ip[MAX_TESTNAME_LEN];

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
        sprintf(instance_ip, "instance%d", input[i].vm_id);
        if (!l2_vn) {
            AddVn(vn_name, input[i].vn_id);
            AddVrf(vrf_name);
        }
        AddVm(vm_name, input[i].vm_id);
        AddVmPortVrf(input[i].name, "", 0);

        //AddNode("virtual-machine-interface-routing-instance", input[i].name, 
        //        input[i].intf_id);
        IntfCfgAdd(input, i);
        AddPort(input[i].name, input[i].intf_id, vm_interface_attr);
        if (with_ip) {
            if (ecmp) {
                AddActiveActiveInstanceIp(instance_ip, input[i].vm_id,
                                          input[i].addr);
            } else {
                AddInstanceIp(instance_ip, input[i].vm_id, input[i].addr);
            }
        }
        if (!l2_vn) {
            AddLink("virtual-network", vn_name, "routing-instance", vrf_name);
        }
        AddLink("virtual-machine", vm_name, "virtual-machine-interface",
                input[i].name);
        AddLink("virtual-network", vn_name, "virtual-machine-interface",
                input[i].name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "routing-instance", vrf_name);
        AddLink("virtual-machine-interface-routing-instance", input[i].name,
                "virtual-machine-interface", input[i].name);
        if (with_ip) {
            AddLink("virtual-machine-interface", input[i].name,
                    "instance-ip", instance_ip);
        }

        if (acl_id) {
            AddLink("virtual-network", vn_name, "access-control-list", acl_name);
        }
    }
}

void CreateVmportEnvWithoutIp(struct PortInfo *input, int count, int acl_id, 
                              const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false, false,
                            false);
}

void CreateVmportEnv(struct PortInfo *input, int count, int acl_id, 
                     const char *vn, const char *vrf,
                     const char *vm_interface_attr) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf,
                            vm_interface_attr, false, true, false);
}

void CreateL2VmportEnv(struct PortInfo *input, int count, int acl_id, 
                     const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, true,
                            true, false);
}

void CreateVmportWithEcmp(struct PortInfo *input, int count, int acl_id,
                          const char *vn, const char *vrf) {
    CreateVmportEnvInternal(input, count, acl_id, vn, vrf, NULL, false,
                            true, true);
}

void FlushFlowTable() {
    Agent::GetInstance()->pkt()->flow_table()->DeleteAll();
    TestClient::WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
}

bool FlowDelete(const string &vrf_name, const char *sip, const char *dip,
                uint8_t proto, uint16_t sport, uint16_t dport, int nh_id) {

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    if (table->Find(key) == NULL) {
        return false;
    }

    table->Delete(key, true);
    return true;
}

bool FlowFail(int vrf_id, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, int nh_id) {
    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *fe = table->Find(key);
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

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *entry = table->Find(key);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow));
    if (!entry->is_flags_set(FlowEntry::NatFlow)) {
        return false;
    }

    EXPECT_STREQ(svn.c_str(), entry->data().source_vn.c_str());
    if (svn.compare(entry->data().source_vn) != 0) {
        return false;
    }

    EXPECT_STREQ(dvn.c_str(), entry->data().dest_vn.c_str());
    if (dvn.compare(entry->data().dest_vn) != 0) {
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
    key.src.ipv4 = ntohl(inet_addr(nat_dip));
    key.dst.ipv4 = ntohl(inet_addr(nat_sip));
    key.src_port = nat_dport;
    key.dst_port = nat_sport;
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

FlowEntry* FlowGet(int vrf_id, std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport, int nh_id) {
    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip.c_str()));
    key.dst.ipv4 = ntohl(inet_addr(dip.c_str()));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *entry = table->Find(key);
    return entry;
}

bool FlowGet(int vrf_id, const char *sip, const char *dip, uint8_t proto, 
             uint16_t sport, uint16_t dport, bool short_flow, int hash_id,
             int reverse_hash_id, int nh_id, int reverse_nh_id) {
    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *entry = table->Find(key);
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

        if (reverse_nh_id != rev->key().nh)
            ret = false;

        EXPECT_EQ(entry->key().protocol, rev->key().protocol);
        if (entry->key().protocol != rev->key().protocol)
            ret = false;

        EXPECT_EQ(entry->key().src.ipv4, rev->key().dst.ipv4);
        if (entry->key().src.ipv4 != rev->key().dst.ipv4)
            ret = false;

        EXPECT_EQ(entry->key().dst.ipv4, rev->key().src.ipv4);
        if (entry->key().dst.ipv4 != rev->key().src.ipv4)
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

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *entry = table->Find(key);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_STREQ(svn.c_str(), entry->data().source_vn.c_str());
    if (svn.compare(entry->data().source_vn) != 0) {
        return false;
    }

    EXPECT_STREQ(dvn.c_str(), entry->data().dest_vn.c_str());
    if (dvn.compare(entry->data().dest_vn) != 0) {
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
        key.src.ipv4 = ntohl(inet_addr(dip));
        key.dst.ipv4 = ntohl(inet_addr(sip));
        key.src_port = dport;
        key.dst_port = sport;
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

        EXPECT_EQ(entry->key().src.ipv4, rev->key().dst.ipv4);
        if (entry->key().src.ipv4 != rev->key().dst.ipv4)
            ret = false;

        EXPECT_EQ(entry->key().dst.ipv4, rev->key().src.ipv4);
        if (entry->key().dst.ipv4 != rev->key().src.ipv4)
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

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *entry = table->Find(key);
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
    EXPECT_EQ(flow_fwd, fwd);
    if (flow_fwd != fwd) {
        ret = false;
    }

    return ret;
}

bool FlowStatsMatch(const string &vrf_name, const char *sip,
                    const char *dip, uint8_t proto, uint16_t sport, 
                    uint16_t dport, uint64_t pkts, uint64_t bytes, int nh_id) {

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

    FlowEntry *fe = table->Find(key);
    EXPECT_TRUE(fe != NULL);
    if (fe == NULL) {
        return false;
    }
    LOG(DEBUG, " bytes " << fe->stats().bytes << " pkts " << fe->stats().packets);
    if (fe->stats().bytes == bytes && fe->stats().packets == pkts) {
        return true;
    }

    return false;
}

bool FindFlow(const string &vrf_name, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, bool nat,
              const string &nat_vrf_name, const char *nat_sip,
              const char *nat_dip, uint16_t nat_sport, uint16_t nat_dport,
              int fwd_nh_id, int rev_nh_id) {

    FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    EXPECT_TRUE(vrf != NULL);
    if (vrf == NULL)
        return false;

    FlowKey key;
    key.nh = fwd_nh_id;
    key.src.ipv4 = ntohl(inet_addr(sip));
    key.dst.ipv4 = ntohl(inet_addr(dip));
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;

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
    key.src.ipv4 = ntohl(inet_addr(nat_dip));
    key.dst.ipv4 = ntohl(inet_addr(nat_sip));
    key.src_port = nat_dport;
    key.dst_port = nat_sport;
    key.protocol = proto;

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
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_idx);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, false, 64);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
    delete pkt;
    return NULL;
}

PktGen *TxIpPacketUtil(int ifindex, const char *sip, const char *dip,
                       int proto, int hash_id) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1)
        pkt->AddIcmpHdr();

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
    delete pkt;
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
            vrf = nh1->GetVrf()->vrf_id();
        } else if (nh->GetType() == NextHop::VLAN) {
            const VlanNH *nh1 = static_cast<const VlanNH *>(nh);
            const VmInterface *intf =
                static_cast<const VmInterface *>(nh1->GetInterface());
            if (intf && intf->GetServiceVlanVrf(nh1->GetVlanTag())) {
                vrf = intf->GetServiceVlanVrf(nh1->GetVlanTag())->vrf_id();
            }
        } else {
            assert(0);
        }
    }
    return vrf;
}

PktGen *TxMplsPacketUtil(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int proto, int hash_idx) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_idx, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1)
        pkt->AddIcmpHdr();

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
    delete pkt;

    return NULL;
}

PktGen *TxMplsTcpPacketUtil(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int sport, int dport, int hash_idx) {
    PktGen *pkt = new PktGen();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_idx, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, false, 64);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
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

    Inet4UnicastRouteKey key(NULL, vrf_name, addr, plen);
    Inet4UnicastRouteEntry *route = 
        static_cast<Inet4UnicastRouteEntry *>
        (static_cast<Inet4UnicastAgentRouteTable *>(vrf->
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

    Inet4UnicastRouteKey key(NULL, vrf_name, addr, plen);
    Inet4UnicastRouteEntry* route = 
        static_cast<Inet4UnicastRouteEntry *>
        (static_cast<Inet4UnicastAgentRouteTable *>(vrf->
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
    Inet4UnicastRouteKey key(NULL, vrf_name, addr, plen);

    Inet4UnicastRouteEntry* route = 
        static_cast<Inet4UnicastRouteEntry *>
        (static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindActiveEntry(&key));
    if (route == NULL) {
        return 0;
    }

    return route->GetPathList().size();
}

bool FindMplsLabel(MplsLabel::Type type, uint32_t label) {
    MplsLabelKey key(type, label);
    MplsLabel *mpls = static_cast<MplsLabel *>(Agent::GetInstance()->mpls_table()->FindActiveEntry(&key));
    return (mpls != NULL);
}

MplsLabel* GetMplsLabel(MplsLabel::Type type, uint32_t label) {
    MplsLabelKey key(type, label);
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

BgpPeer *CreateBgpPeer(const Ip4Address &addr, std::string name) {
    XmppChannelMock *xmpp_channel = new XmppChannelMock();
    AgentXmppChannel *channel;
    Agent::GetInstance()->set_controller_ifmap_xmpp_server(addr.to_string(), 0);
    
    channel = new AgentXmppChannel(Agent::GetInstance(), xmpp_channel, 
                                   "XMPP Server", "", 0);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel, xmps::READY);
    client->WaitForIdle();
    return channel->bgp_peer_id();
}

void DeleteBgpPeer(Peer *peer) {
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);
    if (!bgp_peer)
        return;

    AgentXmppChannel *channel = bgp_peer->GetBgpXmppPeer();
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel, xmps::NOT_READY);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->
        Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    Agent::GetInstance()->controller()->Cleanup();
    client->WaitForIdle();
    delete channel;
}
