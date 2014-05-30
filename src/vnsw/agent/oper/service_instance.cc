/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "service_instance.h"
#include "ifmap/ifmap_node.h"
#include "schema/vnc_cfg_types.h"
#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <cmn/agent_param.h>

using boost::uuids::uuid;

class ServiceInstanceData : public AgentData {
  public:
    ServiceInstanceData(const autogen::ServiceInstanceType &data,
                        uuid instance_id, int service_type, int virtualization_type,
                        uuid vmi_inside, uuid vmi_outside) :
        data_(data), instance_id_(instance_id), service_type_(service_type),
        virtualization_type_(virtualization_type),
        vmi_inside_(vmi_inside), vmi_outside_(vmi_outside) {
    }

    static int StrServiceTypeToInt(std::string type);
    static int StrVirtualizationTypeToInt(std::string type);

    uuid GetInstanceId() const {return instance_id_;};
    int GetServiceType() const {return service_type_;};
    int GetVirtualizationType() const {return virtualization_type_;};
    uuid GetUuidInside() const {return vmi_inside_;};
    uuid GetUuidOutside() const {return vmi_outside_;};

  private:
    typedef std::map<std::string, int> StrTypeToIntMap;
    typedef std::pair<std::string, int> StrTypeToIntPair;
    static StrTypeToIntMap service_type_map_;
    static StrTypeToIntMap virtualization_type_map_;

    static StrTypeToIntMap InitServiceTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("analyzer", ServiceInstance::Analyzer));
        types.insert(StrTypeToIntPair("firewall", ServiceInstance::Firewall));
        types.insert(StrTypeToIntPair("source-nat", ServiceInstance::SourceNAT));

        return types;
    };

    static StrTypeToIntMap InitVirtualizationTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("virtual-machine", ServiceInstance::VirtualMachine));
        types.insert(StrTypeToIntPair("network-namespace", ServiceInstance::NetworkNamespace));

        return types;
    };

    autogen::ServiceInstanceType data_;
    uuid instance_id_;
    int service_type_;
    int virtualization_type_;
    uuid vmi_inside_;
    uuid vmi_outside_;
};

static uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static IFMapNode *LookupIFMapNode(Agent *agent, IFMapNode *node,
                                  IFMapAgentTable *table) {
    IFMapAgentTable *t = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(t->GetGraph());
         iter != node->end(t->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->cfg_listener()->SkipNode(adj_node, table)) {
            continue;
        }
        return adj_node;
    }
    return NULL;
}

/*
 * ServiceInstance class
 */
ServiceInstance::ServiceInstance(uuid si_uuid)
        : uuid_(si_uuid), service_type_(0), virtualization_type_(0) {
}

ServiceInstance::ServiceInstance(uuid si_uuid, uuid instance_id,
                                 int service_type, int virtualization_type,
                                 uuid vmi_inside, uuid vmi_outside)
        : uuid_(si_uuid), instance_id_(instance_id),
          service_type_(service_type), virtualization_type_(virtualization_type),
          vmi_inside_(vmi_inside), vmi_outside_(vmi_outside) {
}

bool ServiceInstance::IsLess(const DBEntry &rhs) const {
    const ServiceInstance &si = static_cast<const ServiceInstance &>(rhs);
    return uuid_ < si.uuid_;
}

std::string ServiceInstance::ToString() const {
    std::stringstream uuid_str;
    uuid_str << uuid_;
    return uuid_str.str();
}

void ServiceInstance::SetKey(const DBRequestKey *key) {
    const ServiceInstanceKey *si_key =
            static_cast<const ServiceInstanceKey *>(key);
    uuid_ = si_key->si_uuid();
}

DBEntryBase::KeyPtr ServiceInstance::GetDBRequestKey() const {
    ServiceInstanceKey *key = new ServiceInstanceKey(uuid_);
    return KeyPtr(key);
}

bool ServiceInstance::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    return false;
}

void ServiceInstance::StartNetworkNamespace(bool restart) {
    std::stringstream cmd_str;

    Agent *agent = Agent::GetInstance();

    std::string cmd = agent->params()->si_network_namespace();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }

    cmd_str << cmd;
    if (restart) {
        cmd_str << " restart ";
    }
    else {
        cmd_str << " start ";
    }
    cmd_str << " --service_instance_id " << UuidToString(uuid_);
    cmd_str << " --instance_id " << UuidToString(instance_id_);
    cmd_str << " --vmi_inside " << UuidToString(vmi_inside_);
    cmd_str << " --vmi_outside " << UuidToString(vmi_outside_);

    system(cmd_str.str().c_str());
}

void ServiceInstance::StopNetworkNamespace() {
    std::stringstream cmd_str;

    Agent *agent = Agent::GetInstance();

    std::string cmd = agent->params()->si_network_namespace();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }

    cmd_str << cmd;
    cmd_str << " stop ";
    cmd_str << " --service_instance_id " << UuidToString(uuid_);

    system(cmd_str.str().c_str());
}

/*
 * ServiceInstanceTable class
 */
ServiceInstanceTable::ServiceInstanceTable(DB *db, const std::string &name)
        : AgentDBTable(db, name) {
}

std::auto_ptr<DBEntry> ServiceInstanceTable::AllocEntry(
    const DBRequestKey *k) const {
    const ServiceInstanceKey *key = static_cast<const ServiceInstanceKey *>(k);
    std::auto_ptr<DBEntry> entry(new ServiceInstance(key->si_uuid()));
    entry->SetKey(key);
    return entry;
}

DBEntry *ServiceInstanceTable::Add(const DBRequest *req) {
    ServiceInstanceKey *key = static_cast<ServiceInstanceKey *>(req->key.get());
    ServiceInstanceData *data = static_cast<ServiceInstanceData *>(req->data.get());
    int virtualization_type = data->GetVirtualizationType();
    ServiceInstance *si = new ServiceInstance(key->si_uuid(),
                                              data->GetInstanceId(),
                                              data->GetServiceType(),
                                              virtualization_type,
                                              data->GetUuidInside(),
                                              data->GetUuidOutside());

    if (virtualization_type == ServiceInstance::NetworkNamespace) {
        si->StartNetworkNamespace(false);
    }

    /*
     * TODO(safchain)
     */
    //sg->SendObjectLog(AgentLogEvent::ADD);
    return si;
}

bool ServiceInstanceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);

    ServiceInstance *si = static_cast<ServiceInstance *>(entry);
    if (ret) {
        si->StartNetworkNamespace(true);
    }

    /*
     * TODO(safchain)
     */
    //
    //si->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool ServiceInstanceTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    ServiceInstance *si = static_cast<ServiceInstance *>(entry);
    ServiceInstanceData *data = static_cast<ServiceInstanceData *>(req->data.get());

    if (si->GetServiceType() != data->GetServiceType()) {
        si->SetServiceType(data->GetServiceType());
        ret = true;
    }
    if (si->GetVirtualizationType() != data->GetVirtualizationType()) {
        si->SetVirtualizationType(data->GetVirtualizationType());
        ret = true;
    }
    if (si->GetUuidOutside() != data->GetUuidOutside()) {
        si->SetUuidOutside(data->GetUuidOutside());
        ret = true;
    }
    if (si->GetUuidInside() != data->GetUuidInside()) {
        si->SetUuidInside(data->GetUuidInside());
        ret = true;
    }

    return ret;
}

void ServiceInstanceTable::Delete(DBEntry *entry, const DBRequest *req) {
    ServiceInstance *si = static_cast<ServiceInstance *>(entry);
    si->StopNetworkNamespace();

    //si->SendObjectLog(AgentLogEvent::DELETE);
}

void ServiceInstanceTable::GetVnsUuid(Agent *agent, IFMapNode *vm_node, std::string left, std::string right,
                                      uuid &left_uuid, uuid &right_uuid) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(vm_node->table());
    for (DBGraphVertex::adjacency_iterator vm_iter =
         vm_node->begin(table->GetGraph());
         vm_iter != vm_node->end(table->GetGraph()); ++vm_iter) {

        IFMapNode *vmi_node = static_cast<IFMapNode *>(vm_iter.operator->());
        if (agent->cfg_listener()->SkipNode(vmi_node, agent->cfg()->cfg_vm_interface_table())) {
            continue;
        }

        IFMapAgentTable *t = static_cast<IFMapAgentTable *>(vmi_node->table());
        for (DBGraphVertex::adjacency_iterator vmi_iter =
             vmi_node->begin(t->GetGraph());
             vmi_iter != vmi_node->end(t->GetGraph()); ++vmi_iter) {

            IFMapNode *vn_node = static_cast<IFMapNode *>(vmi_iter.operator->());
            if (agent->cfg_listener()->SkipNode(vn_node, agent->cfg()->cfg_vn_table())) {
                continue;
            }

            autogen::VirtualNetwork *vn =
                static_cast<autogen::VirtualNetwork *>(vn_node->GetObject());
            autogen::IdPermsType id = vn->id_perms();

            if (left.compare(vn_node->name()) == 0) {
                left_uuid = IdPermsGetUuid(id);
            } else if (right.compare(vn_node->name()) == 0) {
                right_uuid = IdPermsGetUuid(id);
            }
        }
    }
}

bool ServiceInstanceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::ServiceInstance *svc_instance =
            static_cast<autogen::ServiceInstance *>(node->GetObject());
    autogen::IdPermsType id = svc_instance->id_perms();
    req.key.reset(new ServiceInstanceKey(IdPermsGetUuid(id)));
    if (!node->IsDeleted()) {
        Agent *agent = Agent::GetInstance();

        IFMapNode *vm_node = LookupIFMapNode(agent, node,
                                             agent->cfg()->cfg_vm_table());
        if (vm_node == NULL) {
            return false;
        }
        autogen::VirtualMachine *vm =
                static_cast<autogen::VirtualMachine *>(vm_node->GetObject());
        uuid instance_id = IdPermsGetUuid(vm->id_perms());

        autogen::ServiceInstanceType si_properties = svc_instance->properties();
        std::string left_vn = si_properties.left_virtual_network;
        std::string right_vn = si_properties.right_virtual_network;

        uuid left_uuid;
        uuid right_uuid;
        GetVnsUuid(agent, vm_node, left_vn, right_vn, left_uuid, right_uuid);

        IFMapNode *st_node = LookupIFMapNode(agent, node,
                                             agent->cfg()->cfg_service_template_table());
        if (st_node == NULL) {
            return false;
        }

        autogen::ServiceTemplate *svc_template =
                static_cast<autogen::ServiceTemplate *>(st_node->GetObject());
        autogen::ServiceTemplateType svc_template_props = svc_template->properties();
        
        int service_type = ServiceInstanceData::StrServiceTypeToInt(svc_template_props.service_type);

        /* 
         * TODO(safchain) waiting for the edouard's patch merge
         */
        /*int virtualization_type = ServiceInstanceData::StrServiceTypeToInt(
            svc_template_props.service_virtualization_type);*/
        int virtualization_type = ServiceInstance::NetworkNamespace;

        ServiceInstanceData *data = new ServiceInstanceData(si_properties, instance_id,
                                                            service_type, virtualization_type,
                                                            left_uuid, right_uuid);
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.data.reset(data);
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    }
    return true;
}

DBTableBase *ServiceInstanceTable::CreateTable(
    DB *db, const std::string &name) {
    ServiceInstanceTable *table = new ServiceInstanceTable(db, name);
    table->Init();
    return table;
}

/*
 * ServiceInstanceData class
 */
ServiceInstanceData::StrTypeToIntMap
ServiceInstanceData::service_type_map_ = InitServiceTypeMap();
ServiceInstanceData::StrTypeToIntMap
ServiceInstanceData::virtualization_type_map_ = InitVirtualizationTypeMap();

int ServiceInstanceData::StrServiceTypeToInt(std::string type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.find(type);
    if (it != service_type_map_.end()) {
        return it->second;
    }
    return ServiceInstance::Other;
}

int ServiceInstanceData::StrVirtualizationTypeToInt(std::string type) {
    StrTypeToIntMap::const_iterator it = virtualization_type_map_.find(type);
    if (it != virtualization_type_map_.end()) {
        return it->second;
    }
    return ServiceInstance::VirtualMachine;
}
