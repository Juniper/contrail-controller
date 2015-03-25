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

DBTable *AgentVnSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vn_table());
}

void AgentVnSandesh::Alloc() {
    resp_ = new VnListResp();
}

void AgentVnSandesh::SetSandeshPageReq(Sandesh *sresp,
                                       const SandeshPageReq *req) {
    VnListResp *resp = static_cast<VnListResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentSgSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->sg_table());
}

void AgentSgSandesh::Alloc() {
    resp_ = new SgListResp();
}

void AgentSgSandesh::SetSandeshPageReq(Sandesh *sresp,
                                       const SandeshPageReq *req) {
    SgListResp *resp = static_cast<SgListResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentVmSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vm_table());
}

void AgentVmSandesh::Alloc() {
    resp_ = new VmListResp();
}

void AgentVmSandesh::SetSandeshPageReq(Sandesh *sresp,
                                       const SandeshPageReq *req) {
    VmListResp *resp = static_cast<VmListResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentIntfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->interface_table());
}

void AgentIntfSandesh::Alloc() {
    resp_ = new ItfResp();
}

void AgentIntfSandesh::SetSandeshPageReq(Sandesh *sresp,
                                         const SandeshPageReq *req) {
    ItfResp *resp = static_cast<ItfResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentNhSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->nexthop_table());
}

void AgentNhSandesh::Alloc() {
    resp_ = new NhListResp();
}

void AgentNhSandesh::SetSandeshPageReq(Sandesh *sresp,
                                       const SandeshPageReq *req) {
    NhListResp *resp = static_cast<NhListResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentMplsSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->mpls_table());
}

void AgentMplsSandesh::Alloc() {
    resp_ = new MplsResp();
}

void AgentMplsSandesh::SetSandeshPageReq(Sandesh *sresp,
                                         const SandeshPageReq *req) {
    MplsResp *resp = static_cast<MplsResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentVrfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_table());
}

void AgentVrfSandesh::Alloc() {
    resp_ = new VrfListResp();
}

void AgentVrfSandesh::SetSandeshPageReq(Sandesh *sresp,
                                       const SandeshPageReq *req) {
    VrfListResp *resp = static_cast<VrfListResp *>(sresp);
    resp->set_req(*req);
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

void AgentInet4UcRtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                              const SandeshPageReq *req) {
    Inet4UcRouteResp *resp = static_cast<Inet4UcRouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentInet6UcRtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                              const SandeshPageReq *req) {
    Inet6UcRouteResp *resp = static_cast<Inet6UcRouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentInet4McRtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                              const SandeshPageReq *req) {
    Inet4McRouteResp *resp = static_cast<Inet4McRouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentEvpnRtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                           const SandeshPageReq *req) {
    EvpnRouteResp *resp = static_cast<EvpnRouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentLayer2RtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                             const SandeshPageReq *req) {
    Layer2RouteResp *resp = static_cast<Layer2RouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentBridgeRtSandesh::SetSandeshPageReq(Sandesh *sresp,
                                             const SandeshPageReq *req) {
    BridgeRouteResp *resp = static_cast<BridgeRouteResp *>(sresp);
    resp->set_req(*req);
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

void AgentAclSandesh::SetSandeshPageReq(Sandesh *sresp,
                                        const SandeshPageReq *req) {
    AclResp *resp = static_cast<AclResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentMirrorSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->mirror_table());
}

void AgentMirrorSandesh::Alloc(){
    resp_ = new MirrorEntryResp();
}

void AgentMirrorSandesh::SetSandeshPageReq(Sandesh *sresp,
                                           const SandeshPageReq *req) {
    MirrorEntryResp *resp = static_cast<MirrorEntryResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentVrfAssignSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_assign_table());
}

void AgentVrfAssignSandesh::Alloc(){
    resp_ = new VrfAssignResp();
}

void AgentVrfAssignSandesh::SetSandeshPageReq(Sandesh *sresp,
                                              const SandeshPageReq *req) {
    VrfAssignResp *resp = static_cast<VrfAssignResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentVxLanSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vxlan_table());
}

void AgentVxLanSandesh::Alloc() {
    resp_ = new VxLanResp();
}

void AgentVxLanSandesh::SetSandeshPageReq(Sandesh *sresp,
                                          const SandeshPageReq *req) {
    VxLanResp *resp = static_cast<VxLanResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentServiceInstanceSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->service_instance_table());
}

void AgentServiceInstanceSandesh::Alloc() {
    resp_ = new ServiceInstanceResp();
}

void AgentServiceInstanceSandesh::SetSandeshPageReq(Sandesh *sresp,
                                                    const SandeshPageReq *req) {
    ServiceInstanceResp *resp = static_cast<ServiceInstanceResp *>(sresp);
    resp->set_req(*req);
}

DBTable *AgentLoadBalancerSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->db()->
                        FindTable("db.loadbalancer-pool.0"));
}

void AgentLoadBalancerSandesh::Alloc() {
    resp_ = new LoadBalancerResp();
}

void AgentLoadBalancerSandesh::SetSandeshPageReq(Sandesh *sresp,
                                                 const SandeshPageReq *req) {
    LoadBalancerResp *resp = static_cast<LoadBalancerResp *>(sresp);
    resp->set_req(*req);
}

/////////////////////////////////////////////////////////////////////////////
// Routines to manage arguments
/////////////////////////////////////////////////////////////////////////////
bool AgentSandeshArguments::Add(const std::string &key, const std::string &val){
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

bool AgentSandeshArguments::Get(const std::string &key, std::string *val) {
    ArgumentMap::iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        *val = "INVALID";
        return false;
    }
    *val = it->second;
    return true;
}

bool AgentSandeshArguments::Get(const std::string &key, int *val) {
    ArgumentMap::iterator it = arguments_.find(key);
    if (it == arguments_.end()) {
        *val = -1;
        return false;
    }
    *val = strtoul(it->second.c_str(), NULL, 0);
    return true;
}

int AgentSandeshArguments::Encode(std::string *str) {
    *str = "";
    ArgumentMap::iterator it = arguments_.begin();
    while (it != arguments_.end()) {
        *str += it->first + '=' + it->second;
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
        if (Split((*it), '=', args) == 2) {
            Add(args[0], args[1]);
        }
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////
// AgentSandesh Utilities
///////////////////////////////////////////////////////////////////////////
static int ComputeCount(int first, int last) {
    if (last <= first)
        return AgentSandesh::kEntriesPerPage;

    return last - first;
}

static int ComputeFirst(int first, int len) {
    if (first < 0 || first > len)
        return 0;

    return first;
}

static int ComputeLast(int first, int last, int len) {
    if (last < 0)
        return len;

    if (last < first)
        return first + AgentSandesh::kEntriesPerPage;

    return last;
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

static void EncodeOne(string *s, DBTable *table, int begin, int end) {
    AgentSandeshArguments args;
    args.Add("table", table->name());
    args.Add("begin", begin);
    args.Add("end", end);
    args.Encode(s);
}

static void MakeSandeshPageReq(SandeshPageReq *req, DBTable *table, int first,
                               int last, int end, int count) {
    int len = table->Size();
    std::stringstream entries_ss;
    entries_ss << first << "-" << end << "/" << len;
    req->set_entries(entries_ss.str());

    if (last < len) {
        string s;
        EncodeOne(&s, table, last, (last+count));
        req->set_next_page(s);
    }

    if (first > 0) {
        first = first - count;
        if (first < 0)
            first = 0;
        last = first + count;
        string s;
        EncodeOne(&s, table, first, last);
        req->set_prev_page(s);
    }

    string s;
    EncodeOne(&s, table, 0, count);
    req->set_first_page(s);

    EncodeOne(&s, table, -1, -1);
    req->set_all(s);
}

void SandeshDBEntryIndex::HandleRequest() const {
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
        AgentSandeshPtr sandesh(agent_table->GetAgentSandesh(req.context_));
        if (sandesh) {
            sandesh->DoSandesh(first, last);
        } else {
            SandeshError(table, "Pagination not supported", req.context_);
        }
        return true;
    }

    AgentRouteTable *route_table = dynamic_cast<AgentRouteTable *>(table);
    if (route_table) {
        AgentSandeshPtr sandesh(route_table->GetAgentSandesh(req.context_));
        if (sandesh) {
            sandesh->DoSandesh(first, last);
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
void AgentSandesh::DoSandesh(int first, int last) {
    if (first == -1 && last == -1) {
        DoSandesh();
        return;
    }

    DBTable *table = AgentGetTable();
    DBTablePartition *part = static_cast<DBTablePartition *>
        (table->GetTablePartition(0));

    if (table == NULL || part == NULL) {
        SandeshError(table, "Invalid DBTable name", context_);
        return;
    }

    int len = (int)table->Size();
    int count = ComputeCount(first, last);
    first = ComputeFirst(first, len);
    last = ComputeLast(first, last, len);

    SetResp();
    DBEntry *entry = static_cast<DBEntry *>(part->GetFirst());
    int i = 0;
    while (entry && i < last) {
        if (i >= first)
            UpdateResp(entry);
        entry = static_cast<DBEntry *>(part->GetNext(entry));
        i++;
    }

    SandeshPageReq req;
    MakeSandeshPageReq(&req, table, first, last, i, count);
    SetSandeshPageReq(resp_, &req);

    resp_->set_context(resp_->context());
    resp_->Response();
}

void AgentSandesh::DoSandesh() {
    DBTable *table;

    table = AgentGetTable();
    if (table == NULL) {
        SandeshError(table, "Invalid DBTable name", context_);
        return;
    }

    SetResp();
    DBTableWalker *walker = table->database()->GetWalker();
    walkid_ = walker->WalkTable(table, NULL,
        boost::bind(&AgentSandesh::EntrySandesh, this, _2),
        boost::bind(&AgentSandesh::SandeshDone, this, AgentSandeshPtr(this)));
}

void AgentSandesh::SetResp() {
    Alloc();
    resp_->set_context(context_);
}

bool AgentSandesh::UpdateResp(DBEntryBase *entry) {
    AgentDBEntry *ent = static_cast<AgentDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

bool AgentSandesh::EntrySandesh(DBEntryBase *entry) {
    if (!UpdateResp(entry)) {
        return true;
    }

    count_++;
    if (!(count_ % entries_per_sandesh)) {
        // send partial sandesh
        resp_->set_context(resp_->context());
        resp_->set_more(true);

        DBTable *table = AgentGetTable();
        int len = table->Size();
        SandeshPageReq req;
        MakeSandeshPageReq(&req, table, 0, len, len, len);
        SetSandeshPageReq(resp_, &req);

        resp_->Response();
        SetResp();
    }

    return true;
}

void AgentSandesh::SandeshDone(AgentSandeshPtr ptr) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    DBTable *table = AgentGetTable();
    int len = table->Size();
    SandeshPageReq req;
    MakeSandeshPageReq(&req, table, 0, len, len, len);
    SetSandeshPageReq(resp_, &req);
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
