/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/instance_task.h>
#include <oper/crypt_tunnel.h>
#include <oper/agent_sandesh.h>
#include <oper/config_manager.h>
#include <oper/nexthop.h>

using namespace autogen;
using namespace std;

SandeshTraceBufferPtr
    CryptTunnelTraceBuf(SandeshTraceBufferCreate("CryptTunnel", 5000));

const std::string CryptTunnelTask::kCryptTunnelCmd
("/usr/bin/contrail_crypt_tunnel_client.py");


CryptTunnelTable *CryptTunnelTable::crypt_tunnel_table_;

bool CryptTunnelEntry::IsLess(const DBEntry &rhs) const {
    const CryptTunnelEntry &a = static_cast<const CryptTunnelEntry &>(rhs);
    return (remote_ip_ < a.remote_ip_);
}

string CryptTunnelEntry::ToString() const {
    return remote_ip_.to_string();
}

DBEntryBase::KeyPtr CryptTunnelEntry::GetDBRequestKey() const {
    CryptTunnelKey *key = new CryptTunnelKey(remote_ip_);
    return DBEntryBase::KeyPtr(key);
}

void CryptTunnelEntry::SetKey(const DBRequestKey *key) {
    const CryptTunnelKey *k = static_cast<const CryptTunnelKey *>(key);
    remote_ip_ = k->remote_ip_;
}

void CryptTunnelEntry::PostAdd() {
    UpdateTunnelReference();
    ResyncNH();
}
void CryptTunnelEntry::ResyncNH() {
    Agent *agent = static_cast<CryptTunnelTable *>(get_table())->agent();
    typedef std::list<TunnelType::Type> TunnelTypeList;
    TunnelTypeList type_list;
    type_list.push_back(TunnelType::MPLS_GRE);
    type_list.push_back(TunnelType::MPLS_UDP);
    type_list.push_back(TunnelType::VXLAN);
    for (TunnelTypeList::const_iterator it = type_list.begin();
         it != type_list.end(); it++) {
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        TunnelNHKey *tnh_key =
                new TunnelNHKey(agent->fabric_vrf_name(), agent->router_id(),
                                remote_ip_.to_v4(), false, *it);
        tnh_key->sub_op_ = AgentKey::RESYNC;
        nh_req.key.reset(tnh_key);
        agent->nexthop_table()->Process(nh_req);
    }
}

void CryptTunnelTable::Process(DBRequest &req) {
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    Input(tpart, NULL, &req);
}

void CryptTunnelTable::CryptAvailability(const std::string &remote_ip,
                                         bool &crypt_traffic,
                                         bool &crypt_path_available) {
    crypt_traffic = false;
    crypt_path_available = false;
    CryptTunnelEntry *entry = Find(remote_ip);
    if (entry) {
        crypt_traffic = entry->GetVRToVRCrypt();
        crypt_path_available = entry->GetTunnelAvailable();
    }
}

bool CryptTunnelTable::IsCryptPathAvailable(const std::string &remote_ip) {
    bool ret = false;
    CryptTunnelEntry *entry = Find(remote_ip);
    if (entry && entry->GetTunnelAvailable())
        ret = true;
    return ret;
}

bool CryptTunnelTable::IsCryptTraffic(const std::string &remote_ip) {
    bool ret = false;
    CryptTunnelEntry *entry = Find(remote_ip);
    if (entry && entry->GetVRToVRCrypt())
        ret = true;
    return ret;
}

CryptTunnelEntry *CryptTunnelTable::Find(const std::string &remote_ip) {
    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(remote_ip, ec);
    if (ec) {
        return NULL;
    }
    CryptTunnelKey key(ip);
    return static_cast<CryptTunnelEntry *>(FindActiveEntry(&key));
}

void CryptTunnelTable::Delete(const std::string &remote_ip) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(remote_ip, ec);
    if (ec) {
        return;
    }
    req.key.reset(new CryptTunnelKey(ip));
    req.data.reset(NULL);
    Process(req);
    return;
}

void CryptTunnelTable::Create(const std::string &remote_ip, bool vr_to_vr_crypt) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(remote_ip, ec);
    if (ec) {
        return;
    }
    req.key.reset(new CryptTunnelKey(ip));
    req.data.reset(new CryptTunnelConfigData(vr_to_vr_crypt));
    Process(req);
}

DBTableBase *CryptTunnelTable::CreateTable(Agent *agent, DB *db, const std::string &name) {
    CryptTunnelTable *crypt_tunnel_table = new CryptTunnelTable(agent, db, name);
    (static_cast<DBTable *>(crypt_tunnel_table))->Init();
    crypt_tunnel_table_ = crypt_tunnel_table;
    return crypt_tunnel_table;
};

std::auto_ptr<DBEntry> CryptTunnelTable::AllocEntry(const DBRequestKey *k) const {
    const CryptTunnelKey *key = static_cast<const CryptTunnelKey *>(k);
    CryptTunnelEntry *e = new CryptTunnelEntry(key->remote_ip_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(e));
}

DBEntry *CryptTunnelTable::Add(const DBRequest *req) {
    const CryptTunnelKey *key = static_cast<const CryptTunnelKey *>(req->key.get());
    CryptTunnelEntry *crypt_tunnel_entry = new CryptTunnelEntry(key->remote_ip_);
    ChangeHandler(crypt_tunnel_entry, req);
    crypt_tunnel_entry->SendObjectLog(GetOperDBTraceBuf(), AgentLogEvent::ADD);
    return crypt_tunnel_entry;
}

bool CryptTunnelTable::ChangeHandler(CryptTunnelEntry *entry, const DBRequest *req) {
    bool ret = false;
    CryptTunnelConfigData *cdata = dynamic_cast<CryptTunnelConfigData *>(req->data.get());
    if (cdata && cdata->vr_to_vr_crypt_ != entry->vr_to_vr_crypt_) {
        entry->vr_to_vr_crypt_ = cdata->vr_to_vr_crypt_;
        ret = true;
    }
    CryptTunnelAvailableData *tdata = dynamic_cast<CryptTunnelAvailableData *>(req->data.get());
    if (tdata && tdata->tunnel_available_ != entry->tunnel_available_) {
        entry->tunnel_available_ = tdata->tunnel_available_;
        ret = true;
    }
    if (!entry->tunnel_task_) {
        entry->StartCryptTunnel();
        ret = true;
    }
    boost::system::error_code ec;
    IpAddress source_ip = IpAddress::from_string(agent()->router_id().to_string(), ec);
    entry->source_ip_ = source_ip;
    return ret;
}

bool CryptTunnelTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret;
    CryptTunnelEntry *crypt_tunnel_entry = static_cast<CryptTunnelEntry *>(entry);
    ret = ChangeHandler(crypt_tunnel_entry, req);
    if (ret)
        crypt_tunnel_entry->ResyncNH();
    crypt_tunnel_entry->UpdateTunnelReference();
    crypt_tunnel_entry->SendObjectLog(GetOperDBTraceBuf(), AgentLogEvent::CHANGE);
    return ret;
}

bool CryptTunnelTable::Resync(DBEntry *entry, const DBRequest *req) {
    bool ret;
    CryptTunnelEntry *crypt_tunnel_entry = static_cast<CryptTunnelEntry *>(entry);
    ret = ChangeHandler(crypt_tunnel_entry, req);
    if (ret)
        crypt_tunnel_entry->ResyncNH();
    crypt_tunnel_entry->SendObjectLog(GetOperDBTraceBuf(), AgentLogEvent::RESYNC);
    return ret;
}

bool CryptTunnelTable::Delete(DBEntry *entry, const DBRequest *req) {
    CryptTunnelEntry *crypt_tunnel_entry = static_cast<CryptTunnelEntry *>(entry);
    crypt_tunnel_entry->StopCryptTunnel();
    crypt_tunnel_entry->SendObjectLog(GetOperDBTraceBuf(), AgentLogEvent::DEL);
    return true;
}

CryptTunnelTable::CryptTunnelTable(Agent *agent, DB *db, const std::string &name) :
        AgentDBTable(db, name), vr_to_vr_crypt_(false), crypt_interface_(NULL),
    tunnel_event_queue_(agent->task_scheduler()->GetTaskId(kTaskCryptTunnel), 0,
            boost::bind(&CryptTunnelTable::TunnelEventProcess, this, _1)) {
    tunnel_event_queue_.set_name("CryptTunnel event queue");
    set_agent(agent);
}

CryptTunnelTable::~CryptTunnelTable() {
    tunnel_event_queue_.Shutdown();
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
bool CryptTunnelEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    CryptTunnelResp *resp = static_cast<CryptTunnelResp *>(sresp);
    if (name.empty() ||
        name == remote_ip_.to_string()) {
        CryptTunnelSandeshData data;
        data.set_source(std::string());
        data.set_remote(remote_ip_.to_string());
        data.set_available(tunnel_available_);
        data.set_crypt(vr_to_vr_crypt_);
        std::vector<CryptTunnelSandeshData> &list =
           const_cast<std::vector<CryptTunnelSandeshData>&>(resp->get_crypt_tunnel_list());
        list.push_back(data);
        return true;
    }
    return false;
}

void CryptTunnelEntry::SendObjectLog(SandeshTraceBufferPtr buf,
                                     AgentLogEvent::type event) const {
    CryptTunnelObjectLogInfo info;
    string str;
    switch(event) {
        case AgentLogEvent::ADD:
            str.assign("Addition");
            break;
        case AgentLogEvent::DEL:
            str.assign("Deletion");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification");
            break;
        case AgentLogEvent::RESYNC:
            str.assign("Resync");
            break;
        default:
            str.assign("");
            break;
    }
    info.set_event(str);
    info.set_remote(remote_ip_.to_string());
    info.set_source(std::string());
    info.set_available(tunnel_available_);
    info.set_crypt(vr_to_vr_crypt_);
    CRYPT_TUNNEL_OBJECT_LOG_LOG("CryptTunnel", SandeshLevel::SYS_INFO, info);
    CRYPT_TUNNEL_TRACE_TRACE(buf, info);
}

void CryptTunnelReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentCryptTunnelSandesh(context(), get_remote()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr CryptTunnelTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                                  const std::string &context) {
    return AgentSandeshPtr(new AgentCryptTunnelSandesh(context,
                                              args->GetString("remote")));
}



CryptTunnelEvent::CryptTunnelEvent(CryptTunnelTaskBase *task,
                                   CryptTunnelEntry *entry, EventType type,
                                   const std::string &message) :
        tunnel_task_(task), entry_(entry), type_(type), message_(message) {
}

CryptTunnelEvent::~CryptTunnelEvent() {
}


/////////////////////////////////////////////////////////////////////////////
// Instance base class methods
/////////////////////////////////////////////////////////////////////////////
CryptTunnelTaskBase::CryptTunnelTaskBase(CryptTunnelEntry *entry) :
    entry_(NULL), active_(false), last_update_time_("-"), deleted_(false) {
}

CryptTunnelTaskBase::~CryptTunnelTaskBase() {
    entry_ = NULL;
}

void CryptTunnelTaskBase::UpdateTunnel(const CryptTunnelEntry *entry, bool available) const {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    CryptTunnelKey *key = new CryptTunnelKey(*entry->GetRemoteIp());
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    CryptTunnelTable *ctable = static_cast<CryptTunnelTable *>(entry->get_table());
    req.data.reset(new CryptTunnelAvailableData(available));
    ctable->Process(req);
}

void CryptTunnelTaskBase::set_tunnel_entry(CryptTunnelEntry *entry) {
    if (entry_ == entry) {
        UpdateTunnelTask();
        return;
    }
    entry_ = entry;
    CreateTunnelTask();
}

std::string CryptTunnelTaskBase::to_string() {
    std::string str("Instance for crypt tunnel ");
    str += entry_->ToString();
    return str;
}

void CryptTunnelTaskBase::OnRead(const std::string &data) {
    CryptTunnelEvent *event =
            new CryptTunnelEvent(this, entry_.get(),
                             CryptTunnelEvent::MESSAGE_READ,
                             data);
    static_cast<CryptTunnelTable *>(entry_->get_table())->TunnelEventEnqueue(event);
}

void CryptTunnelTaskBase::OnExit(const boost::system::error_code &ec) {
    CryptTunnelEvent *event =
            new CryptTunnelEvent(this, entry_.get(),
                                 CryptTunnelEvent::TASK_EXIT, "");
    static_cast<CryptTunnelTable *>(entry_->get_table())->TunnelEventEnqueue(event);
}

void CryptTunnelTaskBase::SetTunnelEntry(CryptTunnelEntry *entry) {
    CryptTunnelEvent *event =
            new CryptTunnelEvent(this, entry,
                                 CryptTunnelEvent::SET_TUNNEL_ENTRY, "");
    static_cast<CryptTunnelTable *>(entry_->get_table())->TunnelEventEnqueue(event);
}

void CryptTunnelTaskBase::StopTask(CryptTunnelEntry *entry) {
    CryptTunnelEvent *event =
            new CryptTunnelEvent(this, entry,
                                 CryptTunnelEvent::STOP_TASK, "");
    static_cast<CryptTunnelTable *>(entry_->get_table())->TunnelEventEnqueue(event);
}


////////////////////////////////////////////////////////////////////////////////

CryptTunnelTask::CryptTunnelTask(CryptTunnelEntry *entry) :
    CryptTunnelTaskBase(entry),
    task_(NULL) {
}

CryptTunnelTask::~CryptTunnelTask() {
}

bool CryptTunnelTask::CreateTunnelTask() {
    if (!deleted_ && task_.get() != NULL) {
        return false;
    }

    deleted_ = false;

    CRYPT_TUNNEL_TASK_TRACE(Trace, "Starting " + this->to_string());

    Agent *agent = static_cast<CryptTunnelTable *>(entry_->get_table())->agent();
    task_.reset(new CryptTunnelProcessTunnel("CryptTunnel", "", 0,
                                             agent->event_manager()));
    if (task_.get() != NULL) {
        task_->set_pipe_stdout(true);
        task_->set_on_data_cb(
                boost::bind(&CryptTunnelTaskBase::OnRead, this, _2));
        task_->set_on_exit_cb(
                boost::bind(&CryptTunnelTaskBase::OnExit, this, _2));
        return RunTunnelTask(CryptTunnelTaskBase::CREATE_TUNNEL);
    }

    return false;
}

bool CryptTunnelTask::DestroyTunnelTask() {
    if (deleted_) {
        return true;
    }
    if (task_.get() == NULL) {
        return false;
    }

    CRYPT_TUNNEL_TASK_TRACE(Trace, "Deleting " + this->to_string());
    deleted_ = true;
    active_ = false;
    //StopTunnelTask();
    RunTunnelTask(CryptTunnelTaskBase::DELETE_TUNNEL);
    return true;
}

bool CryptTunnelTask::RunTunnelTask(CommandType cmd_type) {
    UpdateTunnelTaskCommand(cmd_type);
    return task_->Run();
}

bool CryptTunnelTask::StopTunnelTask() {
    task_->Stop();
    return true;
}

void CryptTunnelTask::UpdateTunnelTaskCommand(CommandType cmd_type) {
    Agent *agent = static_cast<CryptTunnelTable *>(entry_->get_table())->agent();
    if (agent->test_mode()) {
        // in test mode, set task instance to run no-op shell
        task_->set_cmd("echo success");
        return;
    }
    std::stringstream cmd_str;
    cmd_str << kCryptTunnelCmd;
    switch (cmd_type) {
    case CryptTunnelTaskBase::CREATE_TUNNEL:
        {
            cmd_str << " --oper create ";
        }
        break;
    case CryptTunnelTaskBase::UPDATE_TUNNEL:
        {
            cmd_str << " --oper update ";
        }
        break;
    case CryptTunnelTaskBase::MONITOR_TUNNEL:
        {
            cmd_str << " --oper status ";
        }
        break;
    case CryptTunnelTaskBase::DELETE_TUNNEL:
        {
            cmd_str << " --oper delete ";
        }
        break;
    default:
        // not supported
        assert(0);
    }
    cmd_str << " --source_ip " << entry_->GetSourceIp()->to_string();
    cmd_str << " --remote_ip " << entry_->GetRemoteIp()->to_string();
    task_->set_cmd(cmd_str.str());
}

bool CryptTunnelTask::IsRunning() const {
    return (task_.get() != NULL ? task_->is_running(): false);
}

////////////////////////////////////////////////////////////////////////////////

void CryptTunnelEntry::UpdateTunnelReference() {
    if (tunnel_task_)
        tunnel_task_->set_tunnel_entry(this);
}

CryptTunnelTaskBase *CryptTunnelEntry::StartCryptTunnel() {
    if (!tunnel_task_) {
        tunnel_task_ = new CryptTunnelTask(this);
    }
    return tunnel_task_;
}

void CryptTunnelEntry::StopCryptTunnel() {
    if (!tunnel_task_->DestroyTunnelTask()) {
        delete tunnel_task_;
        tunnel_task_ = NULL;
    }
    tunnel_available_ = false;
    vr_to_vr_crypt_ = false;
    ResyncNH();
}

void
CryptTunnelTable::TunnelEventEnqueue(CryptTunnelEvent *event) {
    tunnel_event_queue_.Enqueue(event);
}

bool CryptTunnelTable::TunnelEventProcess(CryptTunnelEvent *event) {
    CryptTunnelTaskBase *tunnel_task = event->tunnel_task_;
    switch (event->type_) {
    case CryptTunnelEvent::MESSAGE_READ:
        {
            if (tunnel_task->deleted_)
                break;
            tunnel_task->last_update_time_ = UTCUsecToString(UTCTimestampUsec());
            std::string msg = event->message_;
            boost::algorithm::to_lower(msg);
            if (msg.find("success") != std::string::npos) {
                if (!tunnel_task->active_) {
                    tunnel_task->active_ = true;
                    tunnel_task->UpdateTunnel(tunnel_task->entry_.get(), true);
                }
            }
            if (msg.find("failure") != std::string::npos) {
                if (tunnel_task->active_) {
                    tunnel_task->active_ = false;
                    tunnel_task->UpdateTunnel(tunnel_task->entry_.get(), false);
                }
            }
            //CRYPT_TUNNEL_TASK_TRACE(Trace, tunnel_task->to_string() +
            //                   " Received msg = " + event->message_);
        }
        break;

    case CryptTunnelEvent::TASK_EXIT:
        if (tunnel_task->deleted_) {
            CRYPT_TUNNEL_TASK_TRACE(Trace, "Exit Deleted " + tunnel_task->to_string());
            delete tunnel_task;
        } else {
            tunnel_task->RunTunnelTask(CryptTunnelTaskBase::MONITOR_TUNNEL);
        }
        break;

    case CryptTunnelEvent::SET_TUNNEL_ENTRY:
        tunnel_task->set_tunnel_entry(event->entry_);
        break;

    case CryptTunnelEvent::STOP_TASK:
        if (!tunnel_task->DestroyTunnelTask()) {
            CRYPT_TUNNEL_TASK_TRACE(Trace, "Stopped " + tunnel_task->to_string());
            delete tunnel_task;
        }
        break;

    default:
        // unhandled event
        assert(0);
    }

    delete event;
    return true;
}
