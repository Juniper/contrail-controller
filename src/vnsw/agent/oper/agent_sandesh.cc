/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
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

#include <filter/acl.h>

/////////////////////////////////////////////////////////////////////////////
// Utility routines
/////////////////////////////////////////////////////////////////////////////
static bool MatchSubString(const string &str, const string &sub_str) {
    if (sub_str.empty())
        return true;

    return (str.find(sub_str) != string::npos);
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

    if (MatchSubString(vmi->vm_mac(), mac_str_) == false)
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

DBTable *AgentMplsSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->mpls_table());
}

void AgentMplsSandesh::Alloc() {
    resp_ = new MplsResp();
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

DBTable *AgentServiceInstanceSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->service_instance_table());
}

void AgentServiceInstanceSandesh::Alloc() {
    resp_ = new ServiceInstanceResp();
}


DBTable *AgentLoadBalancerSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->db()->
                        FindTable("db.loadbalancer-pool.0"));
}

void AgentLoadBalancerSandesh::Alloc() {
    resp_ = new LoadBalancerResp();
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
    page_request_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
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
        int prev_page_first = first - page_size - 1;
        if (prev_page_first < 0)
            prev_page_first = 0;

        int prev_page_last = prev_page_first + page_size;
        if (prev_page_last >= first)
            prev_page_last = first - 1;
        string s;
        EncodeOne(&s, table, prev_page_first, prev_page_last, &filter);
        req->set_prev_page(s);
    }

    // First-Page link
    string s;
    EncodeOne(&s, table, 0, (page_size - 1), &filter);
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
   VrouterObjectLimits vr_limits;
   vr_limits.set_max_labels(agent->vrouter_max_labels());
   vr_limits.set_max_nexthops(agent->vrouter_max_nexthops());
   vr_limits.set_max_interfaces(agent->vrouter_max_interfaces());
   vr_limits.set_max_vrfs(agent->vrouter_max_vrfs());
   vr_limits.set_max_mirror_entries(agent->vrouter_max_mirror_entries());
   vr_limits.set_vrouter_max_bridge_entries(agent->vrouter_max_bridge_entries());
   vr_limits.set_vrouter_max_oflow_bridge_entries(agent->
           vrouter_max_oflow_bridge_entries());
   vr_limits.set_vrouter_build_info(agent->vrouter_build_info());
   resp->set_vrouter_object_limit(vr_limits);
   resp->Response();
}
