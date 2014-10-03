/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <list>
#include <map>

#include <boost/uuid/string_generator.hpp>
#include <boost/function.hpp>

#include <base/logging.h>

#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <ifmap/ifmap_agent_table.h>
#include <vnc_cfg_types.h>

#include <base/parse_object.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>

#include <cfg/cfg_listener.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_filter.h>

using namespace std;
using namespace autogen;

CfgFilter::CfgFilter(AgentConfig *cfg) : agent_cfg_(cfg) {
}

CfgFilter::~CfgFilter() {
}

bool CfgFilter::CheckProperty(DBTable *table, IFMapNode *node, DBRequest *req,
                              int property_id) {
    if (property_id < 0) {
        return true;
    }

    if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        return true;
    }

    assert(req->oper == DBRequest::DB_ENTRY_ADD_CHANGE);

    IFMapAgentTable::IFMapAgentData *data = 
        static_cast<IFMapAgentTable::IFMapAgentData *>(req->data.get());
    IFMapObject *req_obj = static_cast<IFMapObject *>(data->content.get());
    const IFMapIdentifier *req_id = static_cast<const IFMapIdentifier *>(req_obj);

    if (req_id->IsPropertySet(property_id)) {
        return true;
    } 

    IFMapAgentTable::RequestKey *key =
        static_cast<IFMapAgentTable::RequestKey *>(req->key.get());
    LOG(ERROR, "ID-PERM not set for object <" << key->id_name << "> Table <" <<
        table->name() << ">. Converting to DELETE");

    // Convert operation to DELETE if ID_PERMS is not present
    req->oper = DBRequest::DB_ENTRY_DELETE;
    return true;
}

void CfgFilter::Init() {
    agent_cfg_->cfg_vm_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3, 
                     VirtualMachine::ID_PERMS));

    agent_cfg_->cfg_vn_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3, 
                     VirtualNetwork::ID_PERMS));

    agent_cfg_->cfg_vm_interface_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3, 
                     VirtualMachineInterface::ID_PERMS));

    agent_cfg_->cfg_acl_table()->RegisterPreFilter
        (boost::bind(&CfgFilter::CheckProperty, this, _1, _2, _3, 
                     AccessControlList::ID_PERMS));
}

void CfgFilter::Shutdown() {
    agent_cfg_->cfg_vm_table()->RegisterPreFilter(NULL);
    agent_cfg_->cfg_vn_table()->RegisterPreFilter(NULL);
}
