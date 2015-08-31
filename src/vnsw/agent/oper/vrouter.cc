/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ifmap/ifmap_node.h>
#include <vnc_cfg_types.h>
#include <oper/operdb_init.h>
#include <oper/vrouter.h>

const uint32_t VRouter::kDefaultFlowExportRate = 1000;

VRouter::VRouter(OperDB *oper)
    : oper_(oper), flow_export_rate_(kDefaultFlowExportRate) {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper->agent()->db(),
        "virtual-router");
    assert(cfg_db);

    vrouter_listener_id_ = cfg_db->Register(boost::bind(
        &VRouter::VRouterConfig, this, _1, _2));
}

VRouter::~VRouter() {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper_->agent()->db(),
        "virtual-router");
    if (cfg_db)
        cfg_db->Unregister(vrouter_listener_id_);
}

void VRouter::VRouterConfig(DBTablePartBase *partition, DBEntryBase *dbe) {
    IFMapNode *node = static_cast <IFMapNode *> (dbe);
    if (node->IsDeleted() == false) {
        autogen::VirtualRouter *cfg = 
            static_cast<autogen::VirtualRouter *>(node->GetObject());
        if (cfg->IsPropertySet(autogen::VirtualRouter::FLOW_EXPORT_RATE)) {
            flow_export_rate_ = cfg->flow_export_rate();
        } else {
            flow_export_rate_ = kDefaultFlowExportRate;
        }
    } else {
        flow_export_rate_ = kDefaultFlowExportRate;
    }
}
