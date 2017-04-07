/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <list>
#include <map>

#include <boost/function.hpp>

#include <base/logging.h>

#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <ifmap/ifmap_agent_table.h>
#include <vnc_cfg_types.h>

#include <base/parse_object.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_filter.h>

using namespace std;
using namespace autogen;

CfgFilter::CfgFilter(AgentConfig *cfg) : agent_cfg_(cfg) {
}

CfgFilter::~CfgFilter() {
}

bool CfgFilter::CheckIdPermsProperty(DBTable *table,
                                     const IFMapIdentifier *req_id,
                                     DBRequest *req,
                                     int property_id) {
    if (property_id < 0) {
        return true;
    }

    if (req_id->IsPropertySet(property_id)) {
        return true;
    }

    // When ID_PERMS is not present, ignore the request
    IFMapAgentTable::RequestKey *key =
        static_cast<IFMapAgentTable::RequestKey *>(req->key.get());
    LOG(ERROR, "ID-PERM not set for object <" << key->id_name << "> Table <" <<
        table->name() << ">. Ignoring it");
    return false;
}

int CfgFilter::GetIdPermsPropertyId(DBTable *table) const {
    if (table == agent_cfg_->cfg_vm_table())
        return VirtualMachine::ID_PERMS;
    if (table == agent_cfg_->cfg_vn_table())
        return VirtualNetwork::ID_PERMS;
    if (table == agent_cfg_->cfg_vm_interface_table())
        return VirtualMachineInterface::ID_PERMS;
    if (table == agent_cfg_->cfg_acl_table())
        return AccessControlList::ID_PERMS;
    if (table == agent_cfg_->cfg_service_instance_table())
        return ServiceInstance::ID_PERMS;
    if (table == agent_cfg_->cfg_security_group_table())
        return SecurityGroup::ID_PERMS;
    if (table == agent_cfg_->cfg_logical_port_table())
        return LogicalInterface::ID_PERMS;
    if (table == agent_cfg_->cfg_physical_device_table())
        return PhysicalRouter::ID_PERMS;
    if (table == agent_cfg_->cfg_health_check_table())
        return ServiceHealthCheck::ID_PERMS;
    if (table == agent_cfg_->cfg_qos_table())
        return autogen::QosConfig::ID_PERMS;
    if (table == agent_cfg_->cfg_qos_queue_table())
       return autogen::QosQueue::ID_PERMS;
    if (table == agent_cfg_->cfg_forwarding_class_table())
        return autogen::ForwardingClass::ID_PERMS;
    if (table == agent_cfg_->cfg_bridge_domain_table())
        return autogen::BridgeDomain::ID_PERMS;
    return -1;
}

bool CfgFilter::CheckProperty(DBTable *table, IFMapNode *node, DBRequest *req) {
    if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        return true;
    }

    if (req->oper == DBRequest::DB_ENTRY_NOTIFY) {
        return true;
    }

    assert(req->oper == DBRequest::DB_ENTRY_ADD_CHANGE);

    IFMapAgentTable::IFMapAgentData *data =
        static_cast<IFMapAgentTable::IFMapAgentData *>(req->data.get());
    IFMapObject *req_obj = static_cast<IFMapObject *>(data->content.get());
    const IFMapIdentifier *req_id = static_cast<const IFMapIdentifier *>(req_obj);

    if (CheckIdPermsProperty(table, req_id, req,
                             GetIdPermsPropertyId(table)) == false)
        return false;

    //Table specific property checks
    if ((table == agent_cfg_->cfg_vm_interface_table()) &&
        (CheckVmInterfaceProperty(table, req_id, req) == false)) {
        return false;
    }

    return true;
}

bool CfgFilter::CheckVmInterfaceProperty(DBTable *table,
                                         const IFMapIdentifier *req_id,
                                         DBRequest *req) {
    if (req_id->IsPropertySet(VirtualMachineInterface::MAC_ADDRESSES) ==
        false) {
        return true;
    }

    const VirtualMachineInterface *vmi =
        dynamic_cast<const VirtualMachineInterface *>(req_id);
    if ((vmi->mac_addresses().at(0) == MacAddress::ZeroMac().ToString()) ||
        (vmi->mac_addresses().size() == 0)) {
        return false;
    }

    return true;
}

void CfgFilter::Init() {
    agent_cfg_->cfg_vm_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_vn_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_vm_interface_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_acl_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_service_instance_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_security_group_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_logical_port_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_physical_device_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_health_check_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_qos_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_forwarding_class_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_qos_queue_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));

    agent_cfg_->cfg_bridge_domain_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3));
}

void CfgFilter::Shutdown() {
    agent_cfg_->cfg_vm_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_vn_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_vm_interface_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_acl_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_service_instance_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_security_group_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_logical_port_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_physical_device_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_health_check_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_qos_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_forwarding_class_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_qos_queue_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_bridge_domain_table()->RegisterPreFilter(NULL);
}
