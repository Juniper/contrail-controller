/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/health_check.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/service_instance.h>
#include <filter/acl.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/agent_sandesh.h>
#include <oper/vrf_assign.h>
#include <oper/forwarding_class.h>
#include <oper/qos_config.h>
#include <oper/qos_queue.h>
#include <oper/bridge_domain.h>
#include <filter/acl.h>

/////////////////////////////////////////////////////////////////////////////
// Utility routines
/////////////////////////////////////////////////////////////////////////////
static bool MatchSubString(const string &str, const string &sub_str) {
    if (sub_str.empty())
        return true;

    return (str.find(sub_str) != string::npos);
}

AgentVnSandesh::AgentVnSandesh(const std::string &context,
                                   const std::string &name,
                                   const std::string &u,
                                   const std::string &vxlan_id,
                                   const std::string &ipam_name) :
        AgentSandesh(context, ""), name_(name), uuid_str_(u),
        vxlan_id_(vxlan_id), ipam_name_(ipam_name) {

    boost::system::error_code ec;
    uuid_ = StringToUuid(u);
}

static bool MatchUuid(const string &uuid_str, const boost::uuids::uuid &u,
                      const boost::uuids::uuid val) {
    if (uuid_str.empty())
        return true;
    return u == val;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines per DBTable
/////////////////////////////////////////////////////////////////////////////
DBTable *AgentVnSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vn_table());
}

void AgentVnSandesh::Alloc() {
    resp_ = new VnListResp();
}

bool AgentVnSandesh::Filter(const DBEntryBase *entry) {
    const VnEntry *vn = dynamic_cast<const VnEntry *>(entry);
    assert(vn);

    if (MatchSubString(vn->GetName(), name_) == false)
        return false;

    if (MatchUuid(uuid_str_ , uuid_,  vn->GetUuid()) == false)
        return false;

    if (vxlan_id_.empty() == false) {
        if (((vn->GetVxLanId()) == boost::lexical_cast<int>(vxlan_id_)) == false) {
           return false;
       }
    }

    const std::vector<VnIpam> VnIpams = vn->GetVnIpam();
    std::vector<VnIpam>::const_iterator pos;
    bool ipam_flag = true;
    for(pos=VnIpams.begin();pos < VnIpams.end();pos++) {
            if ((MatchSubString(pos->ipam_name , ipam_name_) == true)) {
                ipam_flag = false;
            }
    }
    if (ipam_flag == true) {
       return false;
    }

    return true;
}

bool AgentVnSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("name", name_);
    args->Add("uuid", uuid_str_);
    args->Add("vxlan_id", vxlan_id_);
    args->Add("ipam_name", ipam_name_);

    return true;
}

DBTable *AgentSgSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->sg_table());
}

void AgentSgSandesh::Alloc() {
    resp_ = new SgListResp();
}

DBTable *AgentVmSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vm_table());
}

void AgentVmSandesh::Alloc() {
    resp_ = new VmListResp();
}

AgentIntfSandesh::AgentIntfSandesh(const std::string &context,
                                   const std::string &type,
                                   const std::string &name,
                                   const std::string &u,
                                   const std::string &vn,
                                   const std::string &mac,
                                   const std::string &v4,
                                   const std::string &v6,
                                   const std::string &parent,
                                   const std::string &ip_active,
                                   const std::string &ip6_active,
                                   const std::string &l2_active) :
        AgentSandesh(context, ""), type_(type), name_(name), uuid_str_(u),
        vn_(vn), mac_str_(mac), v4_str_(v4), v6_str_(v6),
        parent_uuid_str_(parent), ip_active_str_(ip_active),
        ip6_active_str_(ip6_active), l2_active_str_(l2_active) {

    boost::system::error_code ec;
    uuid_ = StringToUuid(u);
    v4_ = Ip4Address::from_string(v4, ec);
    v6_ = Ip6Address::from_string(v6, ec);
    parent_uuid_ = StringToUuid(parent);
}

DBTable *AgentIntfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->interface_table());
}

void AgentIntfSandesh::Alloc() {
    resp_ = new ItfResp();
}

bool AgentIntfSandesh::Filter(const DBEntryBase *entry) {
    const Interface *intf = dynamic_cast<const Interface *>(entry);
    assert(intf);

    if (MatchSubString(intf->name(), name_) == false)
        return false;

    if (type_.empty() == false) {
        if (type_ == "physical" &&
            (intf->type() != Interface::PHYSICAL &&
             intf->type() != Interface::REMOTE_PHYSICAL))
            return false;
        if (type_ == "logical" && intf->type() != Interface::LOGICAL)
            return false;
        if (type_ == "vmi" && intf->type() != Interface::VM_INTERFACE)
            return false;
        if (type_ == "inet" && intf->type() != Interface::INET)
            return false;
        if (type_ == "pkt" && intf->type() != Interface::PACKET)
            return false;
    }

    if (MatchUuid(uuid_str_, uuid_, intf->GetUuid()) == false)
        return false;

    // vn_, mac_str_, v4_str_ or v6_str_ set means get VM-Interfaces only
    if (vn_.empty() == false || mac_str_.empty() == false ||
        v4_str_.empty() == false || v6_str_.empty() == false) {
        if (intf->type() != Interface::VM_INTERFACE)
            return false;
    }

    if (ip_active_str_.empty() == false) {
        if (ip_active_str_ == "no" || ip_active_str_ == "inactive") {
            if (intf->ipv4_active())
                return false;
        }
    }

    if (ip6_active_str_.empty() == false) {
        if (ip6_active_str_ == "no" || ip6_active_str_ == "inactive") {
            if (intf->ipv6_active())
                return false;
        }
    }

    if (l2_active_str_.empty() == false) {
        if (l2_active_str_ == "no" || l2_active_str_ == "inactive") {
            if (intf->l2_active())
                return false;
        }
    }

    const LogicalInterface *li = dynamic_cast<const LogicalInterface *>(entry);
    if (li) {
        if (parent_uuid_str_.empty() == false && parent_uuid_.is_nil() == false
            && li->physical_interface()) {
            if (li->physical_interface()->GetUuid() != parent_uuid_)
                return false;
        }

        return true;
    }

    const VmInterface *vmi = dynamic_cast<const VmInterface *>(entry);
    if (vmi == NULL)
        return true;

    if (vn_.empty() == false && vmi->vn()) {
        if (MatchSubString(vmi->vn()->GetName(), vn_) == false)
            return false;
    }

    if (MatchSubString(vmi->vm_mac().ToString(), mac_str_) == false)
        return false;

    if (v4_str_.empty() == false) {
        if (v4_ != vmi->primary_ip_addr()) {
            return false;
        }
    }

    if (v6_str_.empty() == false) {
        if (v6_ != vmi->primary_ip6_addr()) {
            return false;
        }
    }

    return true;
}

bool AgentIntfSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("type", type_);
    args->Add("name", name_);
    args->Add("uuid", uuid_str_);
    args->Add("vn", vn_);
    args->Add("mac", mac_str_);
    args->Add("ipv4", v4_str_);
    args->Add("ipv6", v6_str_);
    args->Add("parent", parent_uuid_str_);
    args->Add("ip-active", ip_active_str_);
    args->Add("ip6-active", ip6_active_str_);
    args->Add("l2-active", l2_active_str_);
    return true;
}

DBTable *AgentNhSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->nexthop_table());
}

void AgentNhSandesh::Alloc() {
    resp_ = new NhListResp();
}

AgentNhSandesh::AgentNhSandesh(const std::string &context,
                               const std::string &type,
                               const std::string &nh_index,
                               const std::string &policy_enabled) :
        AgentSandesh(context, ""), type_(type), nh_index_(nh_index),
                                   policy_enabled_(policy_enabled) {

}

bool AgentNhSandesh::Filter(const DBEntryBase *entry) {
    const NextHop *nh = dynamic_cast<const NextHop *>(entry);
    assert(nh);

    if (type_.empty() == false) {
        NextHop::Type nh_type = nh->GetType();
        if (type_ == "invalid" &&
             (nh_type != NextHop::INVALID))
           return false;
        if (type_ == "discard" &&
             (nh_type != NextHop::DISCARD))
           return false;
        if (type_ == "l2-receive" &&
             (nh_type != NextHop::L2_RECEIVE))
           return false;
        if (type_ == "receive" &&
             (nh_type != NextHop::RECEIVE))
           return false;
        if (type_ == "resolve" &&
             (nh_type != NextHop::RESOLVE))
           return false;
        if (type_ == "arp" &&
             (nh_type != NextHop::ARP))
           return false;
        if (type_ == "vrf" &&
             (nh_type != NextHop::VRF))
           return false;
        if (type_ == "interface" &&
             (nh_type != NextHop::INTERFACE))
           return false;
        if (type_ == "tunnel" &&
             (nh_type != NextHop::TUNNEL))
           return false;
        if (type_ == "mirror" &&
             (nh_type != NextHop::MIRROR))
           return false;
        if (type_ == "composite" &&
             (nh_type != NextHop::COMPOSITE))
           return false;
        if (type_ == "vlan" &&
             (nh_type != NextHop::VLAN))
           return false;

    }

    if (policy_enabled_.empty() == false) {
        bool policy_flag = true;
        if (MatchSubString("enabled", policy_enabled_) == false) {
            policy_flag = false;
        }
        if (policy_flag != nh->PolicyEnabled()) {
           return false;
        }
    }
    if (nh_index_.empty() == false) {
        if (((nh->id()) == boost::lexical_cast<uint32_t>(nh_index_)) == false)
           return false;
    }
    return true;

    }

bool AgentNhSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("type", type_);
    args->Add("nh_index", nh_index_);
    args->Add("policy_enabled", policy_enabled_);
    return true;
}

DBTable *AgentMplsSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->mpls_table());
}

void AgentMplsSandesh::Alloc() {
    resp_ = new MplsResp();
}

AgentMplsSandesh::AgentMplsSandesh(const std::string &context,
                               const std::string &type,
                               const std::string &label) :
        AgentSandesh(context, ""), type_(type), label_(label) {

}

bool AgentMplsSandesh::Filter(const DBEntryBase *entry) {
    const MplsLabel *mplsl = dynamic_cast<const MplsLabel *>(entry);
    assert(mplsl);

    if (label_.empty() == false) {
        if (((mplsl->label()) == boost::lexical_cast<uint32_t>(label_)) == false)
           return false;
        NextHop::Type nh_type = mplsl->nexthop()->GetType();
        if (type_ == "invalid" &&
             (nh_type != NextHop::INVALID))
           return false;
        if (type_ == "interface" &&
             (nh_type != NextHop::INTERFACE))
           return false;
        if (type_ == "vlan" &&
             (nh_type != NextHop::VLAN))
           return false;
        if (type_ == "vrf" &&
             (nh_type != NextHop::VRF))
           return false;
        if (type_ == "composite" &&
             (nh_type != NextHop::COMPOSITE))
           return false;
    }
    return true;
}

bool AgentMplsSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("type", type_);
    args->Add("label", label_);
    return true;
}

DBTable *AgentVrfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_table());
}

void AgentVrfSandesh::Alloc() {
    resp_ = new VrfListResp();
}

DBTable *AgentInet4UcRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet4UnicastRouteTable());
}

void AgentInet4UcRtSandesh::Alloc() {
    resp_ = new Inet4UcRouteResp();
}

bool AgentInet4UcRtSandesh::UpdateResp(DBEntryBase *entry) {
    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(entry);
    if (dump_table_) {
        return rt->DBEntrySandesh(resp_, stale_);
    }
    return rt->DBEntrySandesh(resp_, addr_, plen_, stale_);
}

DBTable *AgentInet6UcRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet6UnicastRouteTable());
}

void AgentInet6UcRtSandesh::Alloc() {
    resp_ = new Inet6UcRouteResp();
}

bool AgentInet6UcRtSandesh::UpdateResp(DBEntryBase *entry) {
    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(entry);
    if (dump_table_) {
        return rt->DBEntrySandesh(resp_, stale_);
    }
    return rt->DBEntrySandesh(resp_, addr_, plen_, stale_);
}

DBTable *AgentInet4McRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet4MulticastRouteTable());
}

void AgentInet4McRtSandesh::Alloc() {
    resp_ = new Inet4McRouteResp();
}

bool AgentInet4McRtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentEvpnRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetEvpnRouteTable());
}

void AgentEvpnRtSandesh::Alloc() {
    resp_ = new EvpnRouteResp();
}

bool AgentEvpnRtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentLayer2RtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetBridgeRouteTable());
}

void AgentLayer2RtSandesh::Alloc() {
    resp_ = new Layer2RouteResp();
}

bool AgentLayer2RtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentBridgeRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetBridgeRouteTable());
}

void AgentBridgeRtSandesh::Alloc() {
    resp_ = new BridgeRouteResp();
}

bool AgentBridgeRtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentAclSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->acl_table());
}

void AgentAclSandesh::Alloc() {
    resp_ = new AclResp();
}

bool AgentAclSandesh::UpdateResp(DBEntryBase *entry) {
    AclDBEntry *ent = static_cast<AclDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

DBTable *AgentMirrorSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->mirror_table());
}

void AgentMirrorSandesh::Alloc(){
    resp_ = new MirrorEntryResp();
}

AgentMirrorSandesh::AgentMirrorSandesh(const std::string &context,
                               const std::string &analyzer_name) :
        AgentSandesh(context, ""), analyzer_name_(analyzer_name) {

}

bool AgentMirrorSandesh::Filter(const DBEntryBase *entry) {
    const MirrorEntry *mrentry = dynamic_cast<const MirrorEntry *>(entry);
    assert(mrentry);

    if (MatchSubString(mrentry->GetAnalyzerName(), analyzer_name_) == false) {
        return false;
    }

    return true;
}

bool AgentMirrorSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("analyzer_name", analyzer_name_);

    return true;
}

DBTable *AgentVrfAssignSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_assign_table());
}

void AgentVrfAssignSandesh::Alloc(){
    resp_ = new VrfAssignResp();
}

DBTable *AgentVxLanSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vxlan_table());
}

void AgentVxLanSandesh::Alloc() {
    resp_ = new VxLanResp();
}

AgentVxLanSandesh::AgentVxLanSandesh(const std::string &context,
                             const std::string &vxlan_id):
       AgentSandesh(context, ""), vxlan_id_(vxlan_id) {

}

bool AgentVxLanSandesh::Filter(const DBEntryBase *entry) {
    const VxLanId *identry = dynamic_cast<const VxLanId *>(entry);
    assert(identry);

    if (vxlan_id_.empty() == false) {
        if (((identry->vxlan_id()) == boost::lexical_cast<uint32_t>(vxlan_id_)) == false) {
           return false;
       }
    }

    return true;
}

bool AgentVxLanSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("vxlan_id", vxlan_id_);

    return true;
}

DBTable *AgentServiceInstanceSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->service_instance_table());
}

void AgentServiceInstanceSandesh::Alloc() {
    resp_ = new ServiceInstanceResp();
}

/////////////////////////////////////////////////////////////////////////////
// Routines to manage arguments
/////////////////////////////////////////////////////////////////////////////
bool AgentSandeshArguments::Add(const std::string &key, const std::string &val){
    if (val.empty())
        return true;
    ArgumentMap::iterator it = arguments_.find(key);
    if (it != arguments_.end()) {
        it->second = val;
        return false;
    }

    arguments_.insert(make_pair(key, val));
    return true;
}

bool AgentSandeshArguments::Add(const std::string &key, int val) {
    stringstream ss;
    ss << val;
    ArgumentMap::iterator it = arguments_.find(key);
    if (it != arguments_.end()) {
        it->second = ss.str();
        return false;
    }

    arguments_.insert(make_pair(key, ss.str()));
    return true;
}

bool AgentSandeshArguments::Del(const std::string &key) {
    ArgumentMap::iterator it = arguments_.find(key);
    if (it != arguments_.end()) {
        arguments_.erase(it);
        return true;
    }

    return false;
}

bool AgentSandeshArguments::Get(const std::string &key, std::string *val) const{
    ArgumentMap::const_iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        *val = "INVALID";
        return false;
    }
    *val = it->second;
    return true;
}

string AgentSandeshArguments::GetString(const std::string &key) const {
    ArgumentMap::const_iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        return "";
    }
    return it->second;
}

bool AgentSandeshArguments::Get(const std::string &key, int *val) const {
    ArgumentMap::const_iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        *val = -1;
        return false;
    }
    *val = strtoul(it->second.c_str(), NULL, 0);
    return true;
}

int AgentSandeshArguments::GetInt(const std::string &key) const {
    ArgumentMap::const_iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        return -1;
    }
    return (strtoul(it->second.c_str(), NULL, 0));
}

int AgentSandeshArguments::Encode(std::string *str) {
    ArgumentMap::iterator it = arguments_.begin();
    while (it != arguments_.end()) {
        *str += it->first + ':' + it->second;
        it++;
        if (it != arguments_.end())
            *str += ",";
    }
    return arguments_.size();
}

static int Split(const string &s, char delim, vector<string> &tokens) {
    stringstream ss(s);
    string item;
    int count = 0;
    while(getline(ss, item, delim)) {
        tokens.push_back(item);
        count++;
    }

    return count;
}

int AgentSandeshArguments::Decode(const std::string &str) {
    vector<string> token_list;
    int count = Split(str, ',', token_list);

    for (vector<string>::iterator it = token_list.begin();
         it != token_list.end(); ++it) {
        vector<string> args;
        if (Split((*it), ':', args) < 2) {
            return 0;
        }
        string val = (*it).substr(args[0].length() + 1);
        Add(args[0], val);
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////
// AgentSandesh Utilities
///////////////////////////////////////////////////////////////////////////
static int ComputeFirst(int first, int len) {
    if (first < 0 || first > len)
        return 0;

    return first;
}

static int ComputeLast(int first, int last, int len) {
    if (last < 0)
        return -1;

    if (last >= len)
        return first + AgentSandesh::kEntriesPerPage - 1;

    if (last < first)
        return first + AgentSandesh::kEntriesPerPage - 1;

    return last;
}

static int ComputePageSize(int first, int last) {
    if (first < 0 || last < 0)
        return AgentSandesh::kEntriesPerPage;

    if (last <= first)
        return AgentSandesh::kEntriesPerPage;
    return (last - first + 1);
}

void SandeshError(DBTable *table, const std::string &msg,
                  const std::string &context) {
    ErrorResp *resp = new ErrorResp();

    if (table) {
        stringstream s;
        s << table->name() << ":" << msg;
        resp->set_resp(s.str());
    } else {
        resp->set_resp(msg);
    }
    resp->set_context(context);
    resp->Response();
    return;
}

static void EncodeOne(string *s, DBTable *table, int begin, int end,
                      AgentSandeshArguments *filter) {
    *s = "";
    AgentSandeshArguments args;
    args.Add("table", table->name());
    args.Add("begin", begin);
    args.Add("end", end);
    args.Encode(s);

    if (filter) {
        *s += ",";
        filter->Del("table");
        filter->Del("begin");
        filter->Del("end");
        filter->Encode(s);
    }
}

void PageReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    agent->oper_db()->agent_sandesh_manager()->AddPageRequest(key, context());
    return;
}

///////////////////////////////////////////////////////////////////////////
// AgentSandeshManager routines
///////////////////////////////////////////////////////////////////////////
AgentSandeshManager::AgentSandeshManager(Agent *agent) :
    agent_(agent),
    page_request_queue_(TaskScheduler::GetInstance()->GetTaskId(AGENT_SANDESH_TASKNAME),
                        0, boost::bind(&AgentSandeshManager::Run, this, _1)) {
    page_request_queue_.set_name("Introspect page request");
}

AgentSandeshManager::~AgentSandeshManager() {
}

void AgentSandeshManager::Init() {
}

void AgentSandeshManager::Shutdown() {
    page_request_queue_.Shutdown();
}

void AgentSandeshManager::AddPageRequest(const string &key,
                                         const string &context) {
    page_request_queue_.Enqueue(PageRequest(key, context));
}

bool AgentSandeshManager::Run(PageRequest req) {
    AgentSandeshArguments args;
    args.Decode(req.key_);

    string table_name;
    int first = 0;
    int last = 0;
    if (args.Get("table", &table_name) == false ||
        args.Get("begin", &first) == false ||
        args.Get("end", &last) == false) {
        SandeshError(NULL, "Invalid request", req.context_);
        return true;
    }

    DBTable *table = static_cast<DBTable *>
        (Agent::GetInstance()->db()->FindTable(table_name));
    if (table == NULL) {
        SandeshError(NULL, "Invalid DBTable", req.context_);
        return true;
    }

    AgentDBTable *agent_table = dynamic_cast<AgentDBTable *>(table);
    if (agent_table) {
        AgentSandeshPtr sandesh = agent_table->GetAgentSandesh(&args,
                                                            req.context_);
        if (sandesh) {
            sandesh->DoSandesh(sandesh, first, last);
        } else {
            SandeshError(table, "Pagination not supported", req.context_);
        }
        return true;
    }

    AgentRouteTable *route_table = dynamic_cast<AgentRouteTable *>(table);
    if (route_table) {
        AgentSandeshPtr sandesh = route_table->GetAgentSandesh(&args,
                                                             req.context_);
        if (sandesh) {
            sandesh->DoSandesh(sandesh, first, last);
        } else {
            SandeshError(table, "Pagination not supported", req.context_);
        }
        return true;
    }

    SandeshError(table, "Pagination not supported", req.context_);
    return true;
}

///////////////////////////////////////////////////////////////////////////
// AgentSandesh routines
///////////////////////////////////////////////////////////////////////////
void AgentSandesh::MakeSandeshPageReq(PageReqData *req, DBTable *table,
                                      int first, int count, int match_count,
                                      int table_size, int page_size) {
    AgentSandeshArguments filter;

    FilterToArgs(&filter);

    // Set table-size
    int len = table->Size();
    req->set_table_size(len);

    // Set entries
    int last = first + count - 1;
    std::stringstream entries_ss;
    if (match_count >= 0) {
        if (count == 0) {
            entries_ss << " 0 / " << match_count;
        } else {
            entries_ss << first << "-" << last << "/" << match_count;
        }
    } else {
        entries_ss << first << "-" << last;
    }
    req->set_entries(entries_ss.str());

    // Next-Page link
    int next_page_first = last + 1;
    int next_page_last = next_page_first + page_size - 1;
    if (match_count >= 0 && next_page_last > match_count) {
        next_page_last = match_count - 1;
    }

    if (next_page_last >= table_size) {
        next_page_last = table_size - 1;
    }

    if ((match_count >= 0 && next_page_first < match_count) ||
        (match_count < 0 && next_page_first < table_size)) {
        string s;
        EncodeOne(&s, table, next_page_first, next_page_last, &filter);
        req->set_next_page(s);
    }

    // Prev-Page link
    if (first > 0) {
        int prev_page_first;
        if (page_size <  AgentSandesh::kEntriesPerPage) {
            prev_page_first = first - AgentSandesh::kEntriesPerPage;
        } else {
            prev_page_first = first - page_size;
        }

        if (prev_page_first < 0)
            prev_page_first = 0;

        int prev_page_last;

        if (page_size <  AgentSandesh::kEntriesPerPage) {
            prev_page_last = prev_page_first + AgentSandesh::kEntriesPerPage;
        } else {
            prev_page_last = prev_page_first + page_size;
        }

        if (prev_page_last >= first)
            prev_page_last = first - 1;
        string s;
        EncodeOne(&s, table, prev_page_first, prev_page_last, &filter);
        req->set_prev_page(s);
    }

    // First-Page link
    string s;
    if ((len - AgentSandesh::kEntriesPerPage) >= 0) {
        EncodeOne(&s, table, 0, (AgentSandesh::kEntriesPerPage - 1), &filter);
    } else {
        EncodeOne(&s, table, 0, (page_size - 1), &filter);
    }

    req->set_first_page(s);

    // All-Page link
    s = "";
    EncodeOne(&s, table, -1, -1, &filter);
    req->set_all(s);
}

void AgentSandesh::DoSandeshInternal(AgentSandeshPtr sandesh, int first,
                                     int last) {
    DBTable *table = AgentGetTable();
    DBTablePartition *part = static_cast<DBTablePartition *>
        (table->GetTablePartition(0));

    if (table == NULL || part == NULL) {
        SandeshError(table, "Invalid DBTable name", context_);
        return;
    }

    int len = (int)table->Size();
    int page_size = ComputePageSize(first, last);
    first = ComputeFirst(first, len);
    last = ComputeLast(first, last, len);

    SetResp();
    DBTableWalker *walker = table->database()->GetWalker();
    walkid_ = walker->WalkTable(table, NULL,
        boost::bind(&AgentSandesh::EntrySandesh, this, _2, first, last),
        boost::bind(&AgentSandesh::SandeshDone, this, sandesh, first,
                    page_size));
}

void AgentSandesh::DoSandesh(AgentSandeshPtr sandesh, int first, int last) {
    sandesh->DoSandeshInternal(sandesh, first, last);
}

void AgentSandesh::DoSandesh(AgentSandeshPtr sandesh) {
    DoSandesh(sandesh, 0, (kEntriesPerPage - 1));
}

void AgentSandesh::SetResp() {
    Alloc();
    resp_->set_context(context_);
}

bool AgentSandesh::UpdateResp(DBEntryBase *entry) {
    AgentDBEntry *ent = static_cast<AgentDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

bool AgentSandesh::EntrySandesh(DBEntryBase *entry, int first, int last) {
    if (Filter(entry) == false)
        return true;

    if (total_entries_ >= first && ((last < 0) || (total_entries_ <= last))) {
        if (!UpdateResp(entry)) {
            return true;
        }
        count_++;

        if ((count_ % entries_per_sandesh) == 0) {
            // send partial sandesh
            resp_->set_more(true);
            resp_->Response();
            SetResp();
        }
    }
    total_entries_++;

    return true;
}

void AgentSandesh::SandeshDone(AgentSandeshPtr ptr, int first, int page_size) {
    walkid_ = DBTableWalker::kInvalidWalkerId;

    resp_->set_more(true);
    resp_->Response();

    Pagination *page = new Pagination();
    resp_ = page;
    resp_->set_context(context_);

    DBTable *table = AgentGetTable();
    PageReqData req;
    MakeSandeshPageReq(&req, table, first, count_, total_entries_,
                       table->Size(), page_size);
    page->set_req(req);
    resp_->Response();
}

void AgentInitStateReq::HandleRequest() const {
    AgentInitState *resp = new AgentInitState();
    resp->set_context(context());
    Agent *agent = Agent::GetInstance();
    if (agent->init_done()) {
        resp->set_state("InitDone");
    } else {
        resp->set_state("InProgress");
    }
    resp->Response();
}

void VrouterObjectLimitsReq::HandleRequest() const {
   VrouterObjectLimitsResp *resp = new VrouterObjectLimitsResp();
   resp->set_context(context());

   Agent *agent = Agent::GetInstance();

   VrouterObjectLimits vr_limits = agent->GetVrouterObjectLimits();
   resp->set_vrouter_object_limit(vr_limits);
   resp->Response();
}

AgentHealthCheckSandesh::AgentHealthCheckSandesh(const std::string &context,
                                                 const std::string &u) :
        AgentSandesh(context, ""), uuid_str_(u) {
    boost::system::error_code ec;
    uuid_ = StringToUuid(u);
}

DBTable *AgentHealthCheckSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->health_check_table());
}

void AgentHealthCheckSandesh::Alloc() {
    resp_ = new HealthCheckSandeshResp();
}

bool AgentHealthCheckSandesh::Filter(const DBEntryBase *entry) {
    const HealthCheckService *service =
        dynamic_cast<const HealthCheckService *>(entry);
    assert(service);

    if (MatchUuid(uuid_str_ , uuid_,  service->uuid()) == false)
        return false;

    return true;
}

bool AgentHealthCheckSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("uuid", uuid_str_);
    return true;
}

AgentQosConfigSandesh::AgentQosConfigSandesh(const std::string &context,
                                             const std::string &u,
                                             const std::string &name,
                                             const std::string &id) :
    AgentSandesh(context, ""), uuid_(u), name_(name), id_(id) {
}

DBTable *AgentQosConfigSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->qos_config_table());
}

void AgentQosConfigSandesh::Alloc() {
    resp_ = new AgentQosConfigSandeshResp();
}

bool AgentQosConfigSandesh::Filter(const DBEntryBase *entry) {
    const AgentQosConfig *qos =
        dynamic_cast<const AgentQosConfig *>(entry);
    assert(qos);

    if (uuid_.empty() && name_.empty() && id_.empty()) {
        return true;
    }

    if (id_.empty() == false) {
        uint32_t id;
        stringToInteger(id_, id);
        if (qos->id() != id) {
            return false;
        }
    }

    if (name_.empty() == false &&
        qos->name() != name_) {
        return false;
    }

    if (uuid_.empty() == false && qos->uuid() != StringToUuid(uuid_)) {
        return false;
    }

    return true;
}

bool AgentQosConfigSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("uuid", uuid_);
    args->Add("id", id_);
    args->Add("name", name_);
    return true;
}

ForwardingClassSandesh::ForwardingClassSandesh(const std::string &context,
                                               const std::string &u,
                                               const std::string &name,
                                               const std::string &id) :
    AgentSandesh(context, ""), uuid_(u), name_(name), id_(id) {
}

DBTable *ForwardingClassSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->forwarding_class_table());
}

void ForwardingClassSandesh::Alloc() {
    resp_ = new ForwardingClassSandeshResp();
}

bool ForwardingClassSandesh::Filter(const DBEntryBase *entry) {
    const ForwardingClass *fc =
        dynamic_cast<const ForwardingClass *>(entry);
    assert(fc);

    if (uuid_.empty() && name_.empty() && id_.empty()) {
        return true;
    }

    if (id_.empty() == false) {
        uint32_t id;
        stringToInteger(id_, id);
        if (fc->id() != id) {
            return false;
        }
    }

    if (name_.empty() == false &&
        fc->name() != name_) {
        return false;
    }

    if (uuid_.empty() == false && fc->uuid() != StringToUuid(uuid_)) {
        return false;
    }

    return true;
}

bool ForwardingClassSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("uuid", uuid_);
    args->Add("id", id_);
    args->Add("name", name_);
    return true;
}

QosQueueSandesh::QosQueueSandesh(const std::string &context,
                                 const std::string &u,
                                 const std::string &name,
                                 const std::string &id) :
    AgentSandesh(context, ""), uuid_(u), name_(name), id_(id) {
}

DBTable *QosQueueSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->qos_queue_table());
}

void QosQueueSandesh::Alloc() {
    resp_ = new QosQueueSandeshResp();
}

bool QosQueueSandesh::Filter(const DBEntryBase *entry) {
    const QosQueue *qos_queue = dynamic_cast<const QosQueue *>(entry);
    assert(qos_queue);

    if (uuid_.empty() && name_.empty() && id_.empty()) {
        return true;
    }

    if (id_.empty() == false) {
        uint32_t id;
        stringToInteger(id_, id);
        if (qos_queue->id() != id) {
            return false;
        }
    }

    if (name_.empty() == false &&
        qos_queue->name() != name_) {
        return false;
    }

    if (uuid_.empty() == false && qos_queue->uuid() != StringToUuid(uuid_)) {
        return false;
    }

    return true;
}

bool QosQueueSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("uuid", uuid_);
    args->Add("name", name_);
    args->Add("id", id_);
    return true;
}

BridgeDomainSandesh::BridgeDomainSandesh(const std::string &context,
                                         const std::string &u,
                                         const std::string &name) :
    AgentSandesh(context, ""), uuid_str_(u), name_(name) {
    boost::system::error_code ec;
    uuid_ = StringToUuid(u);
}

DBTable *BridgeDomainSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->bridge_domain_table());
}

void BridgeDomainSandesh::Alloc() {
    resp_ = new BridgeDomainSandeshResp();
}

bool BridgeDomainSandesh::Filter(const DBEntryBase *entry) {
    const BridgeDomainEntry *bd =
        dynamic_cast<const BridgeDomainEntry *>(entry);
    assert(bd);

    if (MatchUuid(uuid_str_ , uuid_,  bd->uuid()) == false)
        return false;

    if (name_.empty() == false &&
        bd->name() != name_) {
        return false;
    }

    return true;
}

bool BridgeDomainSandesh::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("uuid", uuid_str_);
    args->Add("name", name_);
    return true;
}
