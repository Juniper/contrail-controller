/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <forwarding_class.h>
#include <global_qos_config.h>

GlobalQosConfig::GlobalQosConfig(OperDB *oper) :
    oper_db_(oper) {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper_db_->agent()->db(),
                                                "global-qos-config");
    global_qos_config_listener_id_ =
        cfg_db->Register(boost::bind(&GlobalQosConfig::ConfigHandler, this, _1, _2));
};

GlobalQosConfig::~GlobalQosConfig() {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper_db_->agent()->db(),
                                                "global-qos-config");
    cfg_db->Unregister(global_qos_config_listener_id_);
}

void GlobalQosConfig::ConfigHandler(DBTablePartBase *partition,
                                    DBEntryBase *dbe) {
    IFMapNode *node = static_cast <IFMapNode *> (dbe);

    if (node->IsDeleted() == true) {
        return;
    }
}
