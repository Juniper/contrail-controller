/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "http_parser/http_parser.h"

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

#include <oper/vn.h>
#include <oper/vrf.h>

#include "mac_learning/mac_learning_proto.h"

SandeshTraceBufferPtr
HealthCheckTraceBuf(SandeshTraceBufferCreate("HealthCheck", 5000));

const std::string HealthCheckInstanceTask::kHealthCheckCmd
("/usr/bin/contrail-vrouter-agent-health-check.py");

////////////////////////////////////////////////////////////////////////////////

HealthCheckInstanceBase::HealthCheckInstanceBase(HealthCheckService *service,
                                                 MetaDataIpAllocator *allocator,
                                                 VmInterface *intf,
                                                 bool ignore_status_event) :
    service_(NULL), intf_(intf),
    ip_(new MetaDataIp(allocator, intf, MetaDataIp::HEALTH_CHECK)),
    last_update_time_("-"), deleted_(false),
    ignore_status_event_(ignore_status_event) {
    // start with health check instance state as active, unless reported
    // down by the attached health check service, so that the existing
    // running traffic is not affected by attaching health check service
    active_ = true;
    ip_->set_active(true);
    if (!service->IsVnIpListHealthCheckService()) {
        intf->InsertHealthCheckInstance(this);
    }
    ResyncTarget(service);
}

HealthCheckInstanceBase::~HealthCheckInstanceBase() {
    VmInterface *intf = static_cast<VmInterface *>(intf_.get());
    if (!service_.get()->IsVnIpListHealthCheckService()) {
        intf->DeleteHealthCheckInstance(this);
    }
    ResyncTarget(service_.get());
}

void HealthCheckInstanceBase::EnqueueResync(const HealthCheckService *service,
                                            Interface *itf) const {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, itf->GetUuid(), ""));
    req.data.reset(new VmInterfaceHealthCheckData());
    service->table()->agent()->interface_table()->Enqueue(&req);
}

void HealthCheckInstanceBase::ResyncTarget(const HealthCheckService *service)
    const {
    EnqueueResync(service, intf_.get());
}

void HealthCheckInstanceBase::set_service(HealthCheckService *service) {
    // It is possible that the instance is queued for deletion at this point
    // since this can be called in the Agent::heathCheck Task, don't need to
    // set the service in this case because it is not needed.
    if (deleted_)
        return;
    if (service_ == service) {
        UpdateInstanceTask();
        return;
    }
    // The instance is not expected to be associated with  different instance
    // during its lifetime. Adding a check to make sure that service is null
    // in this case.
    assert(service_ == NULL);
    service_ = service;
    CreateInstanceTask();
}

std::string HealthCheckInstanceBase::to_string() {
    std::string str("Instance for service ");
    str += service_->name();
    str += " interface " + intf_->name();
    return str;
}

void HealthCheckInstanceBase::OnRead(const std::string &data) {
    HealthCheckInstanceEvent *event =
        new HealthCheckInstanceEvent(this, service_.get(),
                                     HealthCheckInstanceEvent::MESSAGE_READ,
                                     data);
    service_->table()->InstanceEventEnqueue(event);
}

void HealthCheckInstanceBase::OnExit(const boost::system::error_code &ec) {
    HealthCheckInstanceEvent *event =
        new HealthCheckInstanceEvent(this, service_.get(),
                                     HealthCheckInstanceEvent::TASK_EXIT, "");
    service_->table()->InstanceEventEnqueue(event);
}

void HealthCheckInstanceBase::SetService(HealthCheckService *service) {
    HealthCheckInstanceEvent *event =
        new HealthCheckInstanceEvent(this, service,
                                     HealthCheckInstanceEvent::SET_SERVICE, "");
    service->table()->InstanceEventEnqueue(event);
}

void HealthCheckInstanceBase::StopTask(HealthCheckService *service) {
    HealthCheckInstanceEvent *event =
        new HealthCheckInstanceEvent(this, service,
                                     HealthCheckInstanceEvent::STOP_TASK, "");
    service->table()->InstanceEventEnqueue(event);
}

IpAddress HealthCheckInstanceBase::update_source_ip() {
    if (source_ip_.is_unspecified()) {
        VmInterface *itf = static_cast<VmInterface *>(interface().get());
        if (itf) {
            source_ip_ = itf->GetGatewayIp(itf->primary_ip_addr());
        }
    }
    return source_ip_;
}

IpAddress HealthCheckInstanceBase::source_ip() const {
    if (source_ip_.is_unspecified() && ip_)
        return ip_->service_ip();
    return source_ip_;
}

IpAddress HealthCheckInstanceBase::destination_ip() const {
    if (destination_ip_.is_unspecified() && ip_)
        return ip_->destination_ip();
    return destination_ip_;
}

void HealthCheckInstanceBase::set_destination_ip(const IpAddress &ip) {
    ip_->set_destination_ip(ip);
    destination_ip_ = ip;
}

////////////////////////////////////////////////////////////////////////////////

HealthCheckInstanceTask::HealthCheckInstanceTask(HealthCheckService *service,
                                                 MetaDataIpAllocator *allocator,
                                                 VmInterface *intf,
                                                 bool ignore_status_event) :
    HealthCheckInstanceBase(service, allocator, intf, ignore_status_event),
    task_(NULL) {
}

HealthCheckInstanceTask::~HealthCheckInstanceTask() {
}

bool HealthCheckInstanceTask::CreateInstanceTask() {
    if (task_.get() != NULL) {
        return false;
    }

    assert(deleted_ == false);

    HEALTH_CHECK_TRACE(Trace, "Starting " + this->to_string());

    task_.reset(new HeathCheckProcessInstance("HealthCheckInstance", "", 0,
                                   service_->table()->agent()->event_manager()));
    if (task_.get() != NULL) {
        task_->set_pipe_stdout(true);
        task_->set_on_data_cb(
                boost::bind(&HealthCheckInstanceBase::OnRead, this, _2));
        task_->set_on_exit_cb(
                boost::bind(&HealthCheckInstanceBase::OnExit, this, _2));
        return RunInstanceTask();
    }

    return false;
}

bool HealthCheckInstanceTask::DestroyInstanceTask() {
    if (deleted_) {
        return true;
    }

    if (task_.get() == NULL) {
        return false;
    }

    HEALTH_CHECK_TRACE(Trace, "Deleting " + this->to_string());
    deleted_ = true;
    StopInstanceTask();
    return true;
}

bool HealthCheckInstanceTask::RunInstanceTask() {
    UpdateInstanceTaskCommand();
    return task_->Run();
}

bool HealthCheckInstanceTask::StopInstanceTask() {
    task_->Stop();
    return true;
}

void HealthCheckInstanceTask::UpdateInstanceTaskCommand() {
    if (service_->table()->agent()->test_mode()) {
        // in test mode, set task instance to run no-op shell
        task_->set_cmd("sleep 1");
        return;
    }

    std::stringstream cmd_str;
    cmd_str << kHealthCheckCmd << " -m " << service_->monitor_type();
    cmd_str << " -d " << ip_->GetLinkLocalIp().to_string();
    cmd_str << " -t " << service_->timeout() +
                         service_->timeout_usecs() / 1000000;
    cmd_str << " -r " << service_->max_retries();
    cmd_str << " -i " << service_->delay() +
                         service_->delay_usecs() / 1000000;

    if (service_->monitor_type().find("HTTP") != std::string::npos &&
        !service_->url_path().empty()) {
        // append non empty url string to script for HTTP
        cmd_str << " -u " << service_->url_path();
    }

    task_->set_cmd(cmd_str.str());
}

bool HealthCheckInstanceTask::IsRunning() const {
    return (task_.get() != NULL ? task_->is_running(): false);
}

////////////////////////////////////////////////////////////////////////////////

HealthCheckInstanceService::HealthCheckInstanceService(
    HealthCheckService *service, MetaDataIpAllocator *allocator,
    VmInterface *intf, VmInterface *other_intf, bool ignore_status_event,
    bool multi_hop) :
    HealthCheckInstanceBase(service, allocator, intf, ignore_status_event),
    other_intf_(other_intf), multi_hop_(multi_hop) {
    if (service->IsSegmentHealthCheckService() && other_intf) {
        other_intf->InsertHealthCheckInstance(this);
    }
}

HealthCheckInstanceService::~HealthCheckInstanceService() {
    if (service()->IsSegmentHealthCheckService() && other_intf_.get()) {
        VmInterface *vmi = static_cast<VmInterface *>(other_intf_.get());
        vmi->DeleteHealthCheckInstance(this);
        EnqueueResync(service(), other_intf_.get());
    }
}

bool HealthCheckInstanceService::CreateInstanceTask() {
    assert(deleted_ == false);
    HealthCheckService::HealthCheckType type = service_->health_check_type();
    HEALTH_CHECK_TRACE(Trace, "Starting " + this->to_string());
    assert(type == HealthCheckService::SEGMENT ||
           type == HealthCheckService::BFD);
    if (service_->table()->health_check_service_callback(type).empty() ||
        service_->table()->health_check_service_callback(type)
                           (HealthCheckTable::CREATE_SERVICE, this) == false) {
        HEALTH_CHECK_TRACE(Trace, "Failed to start  " + this->to_string());
        return false;
    }
    return true;
}

bool HealthCheckInstanceService::DestroyInstanceTask() {
    if (deleted_) {
        return true;
    }

    HealthCheckService::HealthCheckType type = service_->health_check_type();
    if (!service_->table()->health_check_service_callback(type).empty()) {
        HEALTH_CHECK_TRACE(Trace, "Deleting " + this->to_string());
        service_->table()->health_check_service_callback(type)
                           (HealthCheckTable::DELETE_SERVICE, this);
    }

    deleted_ = true;
    return false;
}

bool HealthCheckInstanceService::RunInstanceTask() {
    HealthCheckService::HealthCheckType type = service_->health_check_type();
    if (!service_->table()->health_check_service_callback(type).empty()) {
        HEALTH_CHECK_TRACE(Trace, "Run Instance " + this->to_string());
        assert(type == HealthCheckService::SEGMENT ||
               type == HealthCheckService::BFD);
        return service_->table()->health_check_service_callback(type)
                                  (HealthCheckTable::RUN_SERVICE, this);
    }
    HEALTH_CHECK_TRACE(Trace, "Run Instance failed " + this->to_string());
    return false;
}

bool HealthCheckInstanceService::StopInstanceTask() {
    HealthCheckService::HealthCheckType type = service_->health_check_type();
    if (!service_->table()->health_check_service_callback(type).empty()) {
        HEALTH_CHECK_TRACE(Trace, "Stop Instance " + this->to_string());
        return service_->table()->health_check_service_callback(type)
                                  (HealthCheckTable::STOP_SERVICE, this);
    }
    return false;
}

bool HealthCheckInstanceService::UpdateInstanceTask() {
    bool success = false;
    HealthCheckService::HealthCheckType type = service_->health_check_type();
    if (!service_->table()->health_check_service_callback(type).empty()) {
        HEALTH_CHECK_TRACE(Trace, "Updating " + this->to_string());
        assert(type == HealthCheckService::SEGMENT ||
               type == HealthCheckService::BFD);
        success = service_->table()->health_check_service_callback(type)
                                  (HealthCheckTable::UPDATE_SERVICE, this);
    }
    if (!success) {
        HEALTH_CHECK_TRACE(Trace, "Failed to Update " + this->to_string());
    }
    return success;
}

void HealthCheckInstanceService::ResyncTarget(const HealthCheckService
                                                 *service) const {
    HealthCheckInstanceBase::ResyncTarget(service);
    if (service->IsSegmentHealthCheckService() && other_intf_.get()) {
        EnqueueResync(service, other_intf_.get());
    }
}

HealthCheckMacIpInstanceService::HealthCheckMacIpInstanceService(
    HealthCheckService *service, MetaDataIpAllocator *allocator,
    VmInterface *intf, VmInterface *other_intf, bool ignore_status_event,
    bool multi_hop) :
    HealthCheckInstanceService(service, allocator, intf, other_intf,
                ignore_status_event, multi_hop) {
}

HealthCheckMacIpInstanceService::~HealthCheckMacIpInstanceService() {
}

void HealthCheckMacIpInstanceService::ResyncTarget(const HealthCheckService
                                                 *service) const {
    if (!active_) {
        IpAddress ip = destination_ip();
        MacAddress mac = destination_mac();
        service_->table()->agent()->mac_learning_proto()->
                GetMacIpLearningTable()->MacIpEntryUnreachable(
                        intf_->vrf()->vrf_id(), ip, mac);
    }
}

////////////////////////////////////////////////////////////////////////////////

HealthCheckInstanceEvent::HealthCheckInstanceEvent(
    HealthCheckInstanceBase *inst, HealthCheckService *service,
    EventType type, const std::string &message) :
    instance_(inst), service_(service), type_(type), message_(message) {
}

HealthCheckInstanceEvent::~HealthCheckInstanceEvent() {
}

////////////////////////////////////////////////////////////////////////////////

HealthCheckService::HealthCheckService(const HealthCheckTable *table,
                                       const boost::uuids::uuid &id) :
    AgentOperDBEntry(), table_(table), uuid_(id) {
    health_check_type_ = GetHealthCheckType();
}

HealthCheckService::~HealthCheckService() {
    // Call DeleteInstances() so that the instances can be gracefully removed
    // after all events queued are flushed.
    // TODO pdsouza: This code doesn't seem to be required since the instances
    // should already have been deleted via the same call on processing DB entry
    // delete on the Health Check Table ( i.e., the list should be empty so it
    // is benign.
    DeleteInstances();
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
    data.set_service_type(service_type_);
    data.set_monitor_type(monitor_type_);
    data.set_http_method(http_method_);
    data.set_url_path(url_path_);
    data.set_expected_codes(expected_codes_);
    data.set_delay(delay_);
    data.set_delay_usecs(delay_usecs_);
    data.set_timeout(timeout_);
    data.set_timeout_usecs(timeout_usecs_);
    data.set_max_retries(max_retries_);

    std::vector<HealthCheckInstanceSandeshData> inst_list;
    InstanceList::const_iterator it = intf_list_.begin();
    while (it != intf_list_.end()) {
        HealthCheckInstanceSandeshData inst_data;
        inst_data.set_vm_interface(UuidToString(it->first));
        inst_data.set_metadata_ip
            (it->second->ip()->GetLinkLocalIp().to_string());
        inst_data.set_service_ip(it->second->source_ip().to_string());
        inst_data.set_health_check_ip
            (it->second->destination_ip().to_string());
        inst_data.set_active(it->second->active());
        inst_data.set_running(it->second->IsRunning());
        inst_data.set_last_update_time(it->second->last_update_time());
        inst_list.push_back(inst_data);
        it++;
    }
    data.set_inst_list(inst_list);

    std::vector<HealthCheckSandeshData> &list =
        const_cast<std::vector<HealthCheckSandeshData>&>(resp->get_hc_list());
    list.push_back(data);
    return true;
}

void HealthCheckService::PostAdd() {
    UpdateInstanceServiceReference();
}

bool HealthCheckService::IsSegmentHealthCheckService() const {
    return (service_type_.find("segment") != std::string::npos);
}

bool HealthCheckService::IsInstanceTaskBased() const {
    return ((monitor_type_.find("BFD") == std::string::npos) &&
            !IsSegmentHealthCheckService());
}

bool HealthCheckService::IsVnIpListHealthCheckService() const {
    return (service_type_.find("vn-ip-list") != std::string::npos);
}

HealthCheckInstanceBase *
HealthCheckService::StartHealthCheckService(VmInterface *intrface,
                                            VmInterface *paired_vmi,
                                            const IpAddress &source_ip,
                                            const IpAddress &destination_ip,
                                            const MacAddress &destination_mac,
                                            bool ignore_status_event,
                                            bool multi_hop
                                            ) {
    HealthCheckInstanceBase *instance = NULL;
    if (IsInstanceTaskBased()) {
        instance = new HealthCheckInstanceTask(
                       this, table_->agent()->metadata_ip_allocator(),
                       intrface, ignore_status_event);
    } else if (IsVnIpListHealthCheckService()) {
        instance = new HealthCheckMacIpInstanceService(
                       this, table_->agent()->metadata_ip_allocator(),
                       intrface, paired_vmi, ignore_status_event, multi_hop);
        HealthCheckMacIpInstanceService *mac_ip_inst =
                       static_cast<HealthCheckMacIpInstanceService *>(instance);
        mac_ip_inst->set_destination_mac(destination_mac);
    } else {
        instance = new HealthCheckInstanceService(
                       this, table_->agent()->metadata_ip_allocator(),
                       intrface, paired_vmi, ignore_status_event, multi_hop);
    }

    instance->set_source_ip(source_ip);
    instance->set_destination_ip(destination_ip);
    return instance;
}

void
HealthCheckService::StopHealthCheckService(HealthCheckInstanceBase *instance) {
    if (!instance->DestroyInstanceTask()) {
        // Delete instance in Agent::HealthCheck task bacause there may be
        // events queued for this instance that need to be processed without
        //  crashing while trying to access the instance.
        // Note that db::DBTable Task and Agent::HealthCheck task are mutually
        // exclusive, so there are no concurrency issues to be addressed.
        instance->StopTask(instance->service());
    }
}

HealthCheckService::HealthCheckType
HealthCheckService::GetHealthCheckType() const {
    if (IsSegmentHealthCheckService())
        return HealthCheckService::SEGMENT;
    if (monitor_type_.find("BFD") != std::string::npos)
        return HealthCheckService::BFD;
    if (monitor_type_.find("HTTP") != std::string::npos)
        return HealthCheckService::HTTP;
    return HealthCheckService::PING;
}

bool HealthCheckService::Copy(HealthCheckTable *table,
                              const HealthCheckServiceData *data) {
    bool ret = false;
    bool dest_ip_changed = false;
    bool service_type_changed = false;
    bool monitor_type_changed = false;
    bool is_prev_hc_segment = IsSegmentHealthCheckService();

    HealthCheckService::HealthCheckType old_health_check_type =
                                            GetHealthCheckType();
    if (monitor_type_ != data->monitor_type_) {
        monitor_type_ = data->monitor_type_;
        monitor_type_changed = true;
        ret = true;
    }

    if (service_type_ != data->service_type_) {
        service_type_ = data->service_type_;
        service_type_changed = true;
        ret = true;
    }

    if (http_method_ != data->http_method_) {
        http_method_ = data->http_method_;
        ret = true;
    }

    if (ip_proto_ != data->ip_proto_) {
        ip_proto_ = data->ip_proto_;
        ret = true;
    }

    if (url_path_ != data->url_path_) {
        url_path_ = data->url_path_;
        ret = true;
    }

    if (url_port_ != data->url_port_) {
        url_port_ = data->url_port_;
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

    if (delay_usecs_ != data->delay_usecs_) {
        delay_usecs_ = data->delay_usecs_;
        ret = true;
    }

    if (timeout_ != data->timeout_) {
        timeout_ = data->timeout_;
        ret = true;
    }

    if (timeout_usecs_ != data->timeout_usecs_) {
        timeout_usecs_ = data->timeout_usecs_;
        ret = true;
    }

    if (max_retries_ != data->max_retries_) {
        max_retries_ = data->max_retries_;
        ret = true;
    }

    if (target_ip_list_ != data->new_target_ip_list_) {
        target_ip_list_ = data->new_target_ip_list_;
        ret = true;
    }

    if (is_hc_enable_all_ip_ != data->is_all_ip_) {
        is_hc_enable_all_ip_ = data->is_all_ip_;
        ret = true;
    }

    if (vn_uuid_list_ != data->vn_uuid_list_) {
        vn_uuid_list_ = data->vn_uuid_list_;
        ret = true;
    }

    if (dest_ip_ != data->dest_ip_) {
        dest_ip_ = data->dest_ip_;
        dest_ip_changed = true;
        ret = true;
    }

    if (ret) {
        /* If service-type of health-check changes from segment to non-segment
         * or vice-versa, remove all the health-check instance objects.
         * Addition of new health-check instances with updated config happens
         * later in this function */
        if ((service_type_changed &&
             is_prev_hc_segment != IsSegmentHealthCheckService()) ||
            (monitor_type_changed &&
             (GetHealthCheckType() == HealthCheckService::BFD ||
              old_health_check_type == HealthCheckService::BFD))) {
            DeleteInstances();
        } else {
            // stop previously allocated health check instances
            // to force them restart with updated values.
            InstanceList::iterator it = intf_list_.begin();
            while (it != intf_list_.end()) {
                it->second->StopInstanceTask();
                it++;
            }
        }
        // update type after deleting the previous instance
        health_check_type_ = GetHealthCheckType();
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
            StopHealthCheckService(it_prev->second);
            intf_list_.erase(it_prev);
            ret = true;
        } else {
            if ((it == intf_list_.end()) || ((*it_cfg) < it->first)) {
                VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, (*it_cfg), "");
                VmInterface *intf = static_cast<VmInterface *>
                    (table_->agent()->interface_table()->Find(&key, false));
                // interface might be unavailable if config is received
                // before nova message for interface creation, in such case
                // skip adding instancee for this interface
                // config dependency manager will then ensure re-notification
                // of dependent config Health-Check-Service in this case to
                // handle creation of interface later
                if (intf != NULL) {
                    IpAddress source_ip;
                    IpAddress destination_ip = dest_ip_;
                    VmInterface *paired_vmi = NULL;
                    if (IsSegmentHealthCheckService()) {
                        paired_vmi = intf->PortTuplePairedInterface();
                        if (paired_vmi == NULL) {
                            it_cfg++;
                            continue;
                        }
                        destination_ip = paired_vmi->GetServiceIp
                            (paired_vmi->primary_ip_addr());
                        if (destination_ip.is_unspecified()) {
                            it_cfg++;
                            continue;
                        }
                    }
                    if (health_check_type_ == HealthCheckService::BFD)
                        source_ip = intf->GetGatewayIp(intf->primary_ip_addr());
                    HealthCheckInstanceBase *inst =
                        // Note that a new instance is alway used when starting
                        // hence the same instance will not be re-used for a
                        // different service. From this we can be sure that once
                        // an instance is deleted it will not be re-used.
                        StartHealthCheckService(intf, paired_vmi, source_ip,
                                                destination_ip, MacAddress(),
                                                false, false);
                    intf_list_.insert(std::pair<boost::uuids::uuid,
                            HealthCheckInstanceBase *>(*(it_cfg), inst));
                    ret = true;
                }
            } else {
                if (dest_ip_changed || IsInstanceTaskBased()) {
                    // change in destination IP needs to be propagated
                    // explicitly to metadata-IP object
                    it->second->set_destination_ip(dest_ip_);
                }
                it++;
            }
            it_cfg++;
        }
    }

    return ret;
}

void HealthCheckService::UpdateInstanceServiceReference() {
    InstanceList::iterator it = intf_list_.begin();
    while (it != intf_list_.end()) {
        it->second->set_service(this);
        it++;
    }
}

void HealthCheckInstanceBase::EnqueueHealthCheckResync(
                                         const HealthCheckService *service,
                                         const VmInterface *itf) const {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new HealthCheckServiceKey(service->uuid(), AgentKey::RESYNC));
    req.data.reset(new HealthCheckResyncInterfaceData(NULL, NULL, itf));
    service->table()->agent()->health_check_table()->Enqueue(&req);
}

void HealthCheckService::ResyncHealthCheckInterface(
                                            const HealthCheckService *service,
                                            const VmInterface *intf) {
    InstanceList::iterator it;

    it = intf_list_.find(intf->vmi_cfg_uuid());
    if (it != intf_list_.end()) {
        HEALTH_CHECK_TRACE(Trace, "Enqueue instance " + intf->name());
        it->second->EnqueueHealthCheckResync(service, intf);
    } else {
        HEALTH_CHECK_TRACE(Trace, "Enqueue instance not found " + intf->name());
    }
}

void HealthCheckService::UpdateInterfaceInstanceServiceReference(
                                                const VmInterface *intf) {
    InstanceList::iterator it;

    it = intf_list_.find(intf->vmi_cfg_uuid());
    if (it != intf_list_.end()) {
        it->second->set_service(this);
    } else {
        HEALTH_CHECK_TRACE(Trace, "Service not found :" + intf->name());
    }
}

void HealthCheckService::DeleteInstances() {
    InstanceList::iterator it = intf_list_.begin();
    while (it != intf_list_.end()) {
        StopHealthCheckService(it->second);
        intf_list_.erase(it);
        it = intf_list_.begin();
    }
}

////////////////////////////////////////////////////////////////////////////////

HealthCheckTable::HealthCheckTable(Agent *agent, DB *db,
                                   const std::string &name) :
    AgentOperDBTable(db, name) {
    set_agent(agent);
    inst_event_queue_ = new WorkQueue<HealthCheckInstanceEvent *>(
            agent->task_scheduler()->GetTaskId(kTaskHealthCheck), 0,
            boost::bind(&HealthCheckTable::InstanceEventProcess, this, _1));
    inst_event_queue_->set_name("HealthCheck instance event queue");
}

HealthCheckTable::~HealthCheckTable() {
    inst_event_queue_->Shutdown();
    delete inst_event_queue_;
}

DBTableBase *HealthCheckTable::CreateTable(Agent *agent, DB *db,
                                           const std::string &name) {
    HealthCheckTable *health_check_table =
        new HealthCheckTable(agent, db, name);
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
    /*
     * Ideally db-infra should have removed the delete mark for the db-entry
     * when Add/Update happens for the db-entry. It has to be root-caused.
     * For now it is handled here and doing here wont give any side-effects.
     */
    bool ret = false;
    if (entry->IsDeleted()) {
        entry->ClearDelete();
        ret = true;
    }
    HealthCheckService *service = static_cast<HealthCheckService *>(entry);
    HealthCheckServiceData *data =
        dynamic_cast<HealthCheckServiceData *>(req->data.get());
    assert(data);
    ret |= service->Copy(this, data);
    service->UpdateInstanceServiceReference();
    return ret;
}

bool HealthCheckTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    HealthCheckResyncInterfaceData *resync_data =
          dynamic_cast<HealthCheckResyncInterfaceData *>(req->data.get());
    if (resync_data) {
        // resync triggered from Vmi for which there is no source ip specified
        // in the interface health check service instance.
        HEALTH_CHECK_TRACE(Trace, "Resync interface " + resync_data->intf_->name());
        HealthCheckService *service = static_cast<HealthCheckService *>(entry);
        service->UpdateInterfaceInstanceServiceReference(resync_data->intf_);
        return false;
    }

    return OperDBOnChange(entry, req);
}

bool HealthCheckTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    HealthCheckService *service = static_cast<HealthCheckService *>(entry);
    service->DeleteInstances();
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
    uint8_t ip_proto = 0;
    uint16_t url_port = 0;
    if (p.monitor_type.find("BFD") != std::string::npos) {
        boost::system::error_code ec;
        dest_ip = Ip4Address::from_string(p.url_path, ec);
        url_path = p.url_path;
        ip_proto = IPPROTO_UDP;
    } else if (p.monitor_type.find("HTTP") == std::string::npos) {
        boost::system::error_code ec;
        dest_ip = Ip4Address::from_string(p.url_path, ec);
        url_path = p.url_path;
        ip_proto = IPPROTO_ICMP;
    } else if (!p.url_path.empty()) {
        ip_proto = IPPROTO_TCP;
        // parse url if available
        struct http_parser_url urldata;
        int ret = http_parser_parse_url(p.url_path.c_str(), p.url_path.size(),
                                        false, &urldata);
        if (ret == 0) {
            if (urldata.field_set & (1 << UF_HOST)) {
                std::string dest_ip_str =
                    p.url_path.substr(urldata.field_data[UF_HOST].off,
                            urldata.field_data[UF_HOST].len);
                // Parse dest-ip from the url to translate to metadata IP
                dest_ip = Ip4Address::from_string(dest_ip_str, ec);
                // keep rest of the url string as is
                url_path = p.url_path.substr(urldata.field_data[UF_HOST].off +\
                        urldata.field_data[UF_HOST].len);
            }
            url_port = urldata.port;
            if ((urldata.field_set & (1 << UF_PORT)) == 0) {
                url_port = 80;
            }
        }
    }

    bool is_all_ip = false;
    if (p.target_ip_all) {
        is_all_ip = true;
    }
    std::set<IpAddress> ip_address_list;
    for (unsigned int i = 0; i < p.target_ip_list.ip_address.size(); ++i) {
        boost::system::error_code ec;
        IpAddress ip = Ip4Address::from_string(p.target_ip_list.ip_address[i], ec);
        if (ec.value() != 0) {
            ip = Ip6Address::from_string(p.target_ip_list.ip_address[i], ec);
        }
        if (ec.value() != 0) {
            continue;
        }

        ip_address_list.insert(ip);
    }

    HealthCheckServiceData *data =
        new HealthCheckServiceData(agent, dest_ip, node->name(),
                                   p.monitor_type, p.health_check_type,
                                   ip_proto, p.http_method,
                                   url_path, url_port, p.expected_codes,
                                   p.delay, p.delayUsecs, p.timeout,
                                   p.timeoutUsecs, p.max_retries,
                                   is_all_ip, ip_address_list, node);

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
        if (adj_node->table() == agent->cfg()->cfg_vn_table()) {
            boost::uuids::uuid vn_uuid;
            autogen::VirtualNetwork *vn =
             dynamic_cast<autogen::VirtualNetwork *>(adj_node->GetObject());
            assert(vn);
            const autogen::IdPermsType &id_perms = vn->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong,
                       id_perms.uuid.uuid_lslong, vn_uuid);

            data->vn_uuid_list_.insert(vn_uuid);
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

void
HealthCheckTable::InstanceEventEnqueue(HealthCheckInstanceEvent *event) const {
    inst_event_queue_->Enqueue(event);
}

bool HealthCheckTable::InstanceEventProcess(HealthCheckInstanceEvent *event) {
    HealthCheckInstanceBase *inst = event->instance_;
    switch (event->type_) {
    case HealthCheckInstanceEvent::MESSAGE_READ:
        {
            // We dont want to process status messages as the instance delete
            // has ben queued.
            if (inst->deleted_) {
                HEALTH_CHECK_TRACE(Trace,
                  "Read Event while deleted! " + inst->to_string());
                break;
            }
            if (inst->IsStatusEventIgnored())
                break;
            inst->last_update_time_ = UTCUsecToString(UTCTimestampUsec());
            std::string msg = event->message_;
            boost::algorithm::to_lower(msg);
            if (msg.find("success") != std::string::npos) {
                if (!inst->active_) {
                    inst->active_ = true;
                    inst->ResyncTarget(inst->service_.get());
                }
            }
            if (msg.find("failure") != std::string::npos) {
                if (inst->active_) {
                    inst->active_ = false;
                    inst->ResyncTarget(inst->service_.get());
                }
            }
            HEALTH_CHECK_TRACE(Trace, inst->to_string() +
                               " Received msg = " + event->message_);
        }
        break;

    case HealthCheckInstanceEvent::TASK_EXIT:
        if (inst->IsStatusEventIgnored())
            break;
        if (!inst->deleted_) {
            HEALTH_CHECK_TRACE(Trace, "Restarting " + inst->to_string());
            inst->RunInstanceTask();
        } else {
            HEALTH_CHECK_TRACE(Trace, "Stopped " + inst->to_string());
            delete inst;
        }
        break;

    case HealthCheckInstanceEvent::SET_SERVICE:
        inst->set_service(event->service_);
        break;

    case HealthCheckInstanceEvent::STOP_TASK:
        inst->DestroyInstanceTask();
        // Freeing instance is handled here and not in any other task context.
        // Unconditionally delete here since DestroyInstanceTask() may have
        // alrady been called in the db::DBTable task context before queueing
        // this event.
        delete inst;
        break;

    default:
        // unhandled event
        assert(0);
    }

    delete event;
    return true;
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

////////////////////////////////////////////////////////////////////////////////
