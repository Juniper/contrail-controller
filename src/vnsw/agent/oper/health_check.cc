/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/string/case_conv.hpp>

#include <cmn/agent_cmn.h>

#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <init/agent_param.h>
#include <cfg/cfg_init.h>

#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/config_manager.h>
#include <oper/agent_sandesh.h>
#include <oper/instance_task.h>
#include <oper/interface_common.h>
#include <oper/metadata_ip.h>
#include <oper/health_check.h>

SandeshTraceBufferPtr
HealthCheckTraceBuf(SandeshTraceBufferCreate("HealthCheck", 5000));

const std::string HealthCheckInstance::kHealthCheckCmd
("/usr/share/contrail-utils/contrail-vrouter-agent-health-check.py");

HealthCheckService::HealthCheckService(const HealthCheckTable *table,
                                       const boost::uuids::uuid &id) :
    AgentOperDBEntry(), table_(table), uuid_(id) {
}

HealthCheckService::~HealthCheckService() {
    InstanceList::iterator it = intf_list_.begin();
    while (it != intf_list_.end()) {
        delete it->second;
        intf_list_.erase(it);
        it = intf_list_.begin();
    }
}

HealthCheckInstance::HealthCheckInstance(HealthCheckService *service,
                                         MetaDataIpAllocator *allocator,
                                         VmInterface *intf) :
    service_(service), intf_(intf), ip_(new MetaDataIp(allocator, intf)),
    task_(NULL), last_update_time_("-") {
    active_ = false;
    ip_->set_active(true);
    intf->InsertHealthCheckInstance(this);
    ResyncInterface();
}

HealthCheckInstance::~HealthCheckInstance() {
    VmInterface *intf = static_cast<VmInterface *>(intf_.get());
    intf->DeleteHealthCheckInstance(this);
    ResyncInterface();
    DestroyInstanceTask();
}

void HealthCheckInstance::ResyncInterface() {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, intf_->GetUuid(), ""));
    req.data.reset(new VmInterfaceHealthCheckData());
    service_->table_->agent()->interface_table()->Enqueue(&req);
}

bool HealthCheckInstance::CreateInstanceTask() {
    if (task_ != NULL) {
        return false;
    }

    HEALTH_CHECK_TRACE(Trace, "Starting " + this->to_string());
    std::stringstream cmd_str;
    cmd_str << kHealthCheckCmd << " -m " << service_->monitor_type_;
    cmd_str << " -d " << ip_->GetLinkLocalIp().to_string();
    cmd_str << " -t " << service_->timeout_;
    cmd_str << " -r " << service_->max_retries_;
    cmd_str << " -i " << service_->delay_;

    if (service_->monitor_type_.find("HTTP") != std::string::npos &&
        !service_->url_path_.empty()) {
        // append non empty url string to script for HTTP
        cmd_str << " -u " << service_->url_path_;
    }

    task_ = new HeathCheckProcessInstance("HealthCheckInstance", cmd_str.str(),
                                 0, service_->table_->agent()->event_manager());
    if (task_ != NULL) {
        task_->set_pipe_stdout(true);
        task_->set_on_data_cb(
                boost::bind(&HealthCheckInstance::OnRead, this, _1, _2));
        task_->set_on_exit_cb(
                boost::bind(&HealthCheckInstance::OnExit, this, _1, _2));
        return task_->Run();
    }

    return false;
}

void HealthCheckInstance::DestroyInstanceTask() {
    if (task_ == NULL) {
        return;
    }

    HeathCheckProcessInstance *task = task_;
    task_ = NULL;
    task->Stop();
    delete task;
}

std::string HealthCheckInstance::to_string() {
    std::string str("Instance for service ");
    str += service_->name_;
    str += " interface " + intf_->name();
    return str;
}

void HealthCheckInstance::OnRead(InstanceTask *task, const std::string data) {
    last_update_time_ = UTCUsecToString(UTCTimestampUsec());
    std::string msg = data;
    boost::algorithm::to_lower(msg);
    if (msg.find("success") != std::string::npos) {
        if (!active_) {
            active_ = true;
            ResyncInterface();
        }
    }
    if (msg.find("failure") != std::string::npos) {
        if (active_) {
            active_ = false;
            ResyncInterface();
        }
    }
    HEALTH_CHECK_TRACE(Trace, this->to_string() + " Received msg = " + data);
}

void HealthCheckInstance::OnExit(InstanceTask *task,
                                 const boost::system::error_code &ec) {
    if (task_ != NULL) {
        HEALTH_CHECK_TRACE(Trace, "Restarting " + this->to_string());
        task_->Run();
    } else {
        HEALTH_CHECK_TRACE(Trace, "Stopped " + this->to_string());
    }
}

bool HealthCheckService::IsLess(const DBEntry &rhs) const {
    const HealthCheckService &a =
        static_cast<const HealthCheckService &>(rhs);
    return (uuid_ < a.uuid_);
}

std::string HealthCheckService::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr HealthCheckService::GetDBRequestKey() const {
    HealthCheckServiceKey *key = new HealthCheckServiceKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void HealthCheckService::SetKey(const DBRequestKey *key) {
    const HealthCheckServiceKey *k =
        static_cast<const HealthCheckServiceKey *>(key);
    uuid_ = k->uuid_;
}

bool HealthCheckService::DBEntrySandesh(Sandesh *sresp,
                                        std::string &name) const {
    HealthCheckSandeshResp *resp = static_cast<HealthCheckSandeshResp *>(sresp);

    HealthCheckSandeshData data;
    data.set_uuid(UuidToString(uuid()));
    data.set_name(name_);
    data.set_monitor_type(monitor_type_);
    data.set_http_method(http_method_);
    data.set_url_path(url_path_);
    data.set_expected_codes(expected_codes_);
    data.set_delay(delay_);
    data.set_timeout(timeout_);
    data.set_max_retries(max_retries_);

    std::vector<HealthCheckInstanceSandeshData> inst_list;
    InstanceList::const_iterator it = intf_list_.begin();
    while (it != intf_list_.end()) {
        HealthCheckInstanceSandeshData inst_data;
        inst_data.set_vm_interface(UuidToString(it->first));
        inst_data.set_metadata_ip
            (it->second->ip_->GetLinkLocalIp().to_string());
        inst_data.set_service_ip(it->second->ip_->service_ip().to_string());
        inst_data.set_health_check_ip
            (it->second->ip_->destination_ip().to_string());
        inst_data.set_active(it->second->active_);
        inst_data.set_running(it->second->task_ != NULL ?
                              it->second->task_->is_running(): false);
        inst_data.set_last_update_time(it->second->last_update_time_);
        inst_list.push_back(inst_data);
        it++;
    }
    data.set_inst_list(inst_list);

    std::vector<HealthCheckSandeshData> &list =
        const_cast<std::vector<HealthCheckSandeshData>&>(resp->get_hc_list());
    list.push_back(data);
    return true;
}

bool HealthCheckService::Copy(HealthCheckTable *table,
                              const HealthCheckServiceData *data) {
    bool ret = false;

    if (monitor_type_ != data->monitor_type_) {
        monitor_type_ = data->monitor_type_;
        ret = true;
    }

    if (http_method_ != data->http_method_) {
        http_method_ = data->http_method_;
        ret = true;
    }

    if (url_path_ != data->url_path_) {
        url_path_ = data->url_path_;
        ret = true;
    }

    if (expected_codes_ != data->expected_codes_) {
        expected_codes_ = data->expected_codes_;
        ret = true;
    }

    if (delay_ != data->delay_) {
        delay_ = data->delay_;
        ret = true;
    }

    if (timeout_ != data->timeout_) {
        timeout_ = data->timeout_;
        ret = true;
    }

    if (max_retries_ != data->max_retries_) {
        max_retries_ = data->max_retries_;
        ret = true;
    }

    if (dest_ip_ != data->dest_ip_) {
        dest_ip_ = data->dest_ip_;
        ret = true;
    }

    if (ret) {
        // stop previously allocated health check instances
        // to force them restart with updated values.
        InstanceList::iterator it = intf_list_.begin();
        while (it != intf_list_.end()) {
            it->second->task_->Stop();
            it++;
        }
    }

    if (name_ != data->name_) {
        name_ = data->name_;
        ret = true;
    }

    std::set<boost::uuids::uuid>::iterator it_cfg =
        data->intf_uuid_list_.begin();
    InstanceList::iterator it = intf_list_.begin();
    while (it_cfg != data->intf_uuid_list_.end() ||
           it != intf_list_.end()) {
        if (it_cfg == data->intf_uuid_list_.end() ||
            ((it != intf_list_.end()) && ((*it_cfg) > it->first))) {
            InstanceList::iterator it_prev = it;
            it++;
            delete it_prev->second;
            intf_list_.erase(it_prev);
            ret = true;
        } else {
            if ((it == intf_list_.end()) || ((*it_cfg) < it->first)) {
                VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, (*it_cfg), "");
                VmInterface *intf = static_cast<VmInterface *>
                    (table_->agent()->interface_table()->Find(&key, false));
                // interface should be available as config manager assures
                // the order of creation of objects
                assert(intf != NULL);
                HealthCheckInstance *inst = new HealthCheckInstance
                    (this, table_->agent()->metadata_ip_allocator(), intf);
                intf_list_.insert(std::pair<boost::uuids::uuid,
                        HealthCheckInstance *>(*(it_cfg), inst));
                inst->ip_->set_destination_ip(dest_ip_);
                inst->CreateInstanceTask();
                ret = true;
            } else {
                it++;
            }
            it_cfg++;
        }
    }

    return ret;
}

HealthCheckTable::HealthCheckTable(DB *db, const std::string &name) :
    AgentOperDBTable(db, name) {
}

HealthCheckTable::~HealthCheckTable() {
}

DBTableBase *HealthCheckTable::CreateTable(DB *db, const std::string &name) {
    HealthCheckTable *health_check_table = new HealthCheckTable(db, name);
    (static_cast<DBTable *>(health_check_table))->Init();
    return health_check_table;
};

std::auto_ptr<DBEntry>
HealthCheckTable::AllocEntry(const DBRequestKey *k) const {
    const HealthCheckServiceKey *key =
        static_cast<const HealthCheckServiceKey *>(k);
    HealthCheckService *service = new HealthCheckService(this, key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(service));
}

DBEntry *HealthCheckTable::OperDBAdd(const DBRequest *req) {
    HealthCheckServiceKey *key =
        static_cast<HealthCheckServiceKey *>(req->key.get());
    HealthCheckServiceData *data =
        static_cast<HealthCheckServiceData *>(req->data.get());
    HealthCheckService *service = new HealthCheckService(this, key->uuid_);
    service->Copy(this, data);
    return service;
}

bool HealthCheckTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    HealthCheckService *service = static_cast<HealthCheckService *>(entry);
    HealthCheckServiceData *data =
        dynamic_cast<HealthCheckServiceData *>(req->data.get());
    assert(data);
    bool ret = service->Copy(this, data);
    return ret;
}

bool HealthCheckTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool HealthCheckTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    return true;
}


static HealthCheckServiceKey *BuildKey(const boost::uuids::uuid &u) {
    return new HealthCheckServiceKey(u);
}

static HealthCheckServiceData *BuildData(Agent *agent, IFMapNode *node,
                                         const autogen::ServiceHealthCheck *s) {
    boost::system::error_code ec;
    const autogen::ServiceHealthCheckType &p = s->properties();
    Ip4Address dest_ip;
    std::string url_path;
    if (p.monitor_type.find("HTTP") == std::string::npos) {
        boost::system::error_code ec;
        dest_ip = Ip4Address::from_string(p.url_path, ec);
        url_path = p.url_path;
    } else {
        std::string url = p.url_path;
        boost::algorithm::to_lower(url);
        std::size_t found = url.find("http://");
        assert(found == 0);
        std::string url_path_substr = p.url_path.substr(7);
        found = url_path_substr.find("/");
        assert(found != 0);
        std::string dest_ip_str = p.url_path.substr(7, found);
        url_path = p.url_path.substr(7 + found + 1);
        boost::system::error_code ec;
        dest_ip = Ip4Address::from_string(dest_ip_str, ec);
    }
    HealthCheckServiceData *data =
        new HealthCheckServiceData(agent, dest_ip, node->name(),
                                   p.monitor_type, p.http_method, url_path,
                                   p.expected_codes, p.delay, p.timeout,
                                   p.max_retries, node);

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent->cfg()->cfg_vm_interface_table()) {
            boost::uuids::uuid intf_uuid;
            autogen::VirtualMachineInterface *intf =
             dynamic_cast<autogen::VirtualMachineInterface *>(adj_node->GetObject());
            assert(intf);
            const autogen::IdPermsType &id_perms = intf->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong,
                       id_perms.uuid.uuid_lslong, intf_uuid);

            data->intf_uuid_list_.insert(intf_uuid);
        }
    }
    return data;
}

bool HealthCheckTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {
    autogen::ServiceHealthCheck *service =
        static_cast<autogen::ServiceHealthCheck *>(node->GetObject());
    assert(service);

    assert(!u.is_nil());

    req.key.reset(BuildKey(u));
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddHealthCheckServiceNode(node);
    return false;
}

bool HealthCheckTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                     const boost::uuids::uuid &u) {
    autogen::ServiceHealthCheck *service =
        static_cast <autogen::ServiceHealthCheck *>(node->GetObject());
    assert(service);

    req.key.reset(BuildKey(u));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, service));
    Enqueue(&req);

    return false;
}

bool HealthCheckTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::ServiceHealthCheck *service =
        static_cast<autogen::ServiceHealthCheck *>(node->GetObject());
    autogen::IdPermsType id_perms = service->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}


HealthCheckService *HealthCheckTable::Find(const boost::uuids::uuid &u) {
    HealthCheckServiceKey key(u);
    return static_cast<HealthCheckService *>(FindActiveEntry(&key));
}

AgentSandeshPtr
HealthCheckTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                  const std::string &context) {
    return AgentSandeshPtr(new AgentHealthCheckSandesh(context,
                                                       args->GetString("uuid")));
}

void HealthCheckSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentHealthCheckSandesh(context(), get_uuid()));
    sand->DoSandesh(sand);
}

