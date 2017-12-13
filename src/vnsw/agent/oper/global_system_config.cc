/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <global_system_config.h>
#include "oper/bgp_as_service.h"

GlobalSystemConfig::GlobalSystemConfig(OperDB *oper) :
    oper_db_(oper) {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper_db_->agent()->db(),
                                                "global-system-config");
    global_system_config_listener_id_ =
        cfg_db->Register(boost::bind(&GlobalSystemConfig::GlobalSystemConfigHandler, this, _1, _2));
};

GlobalSystemConfig::~GlobalSystemConfig() {
    DBTableBase *cfg_db = IFMapTable::FindTable(oper_db_->agent()->db(),
                                                "global-system-config");
    cfg_db->Unregister(global_system_config_listener_id_);
}

void GlobalSystemConfig::GlobalSystemConfigHandler(DBTablePartBase *partition,
                                                   DBEntryBase *dbe) {
    IFMapNode *node = static_cast <IFMapNode *> (dbe);

    if (node->IsDeleted() == false) {
        autogen::GlobalSystemConfig *cfg =
                static_cast<autogen::GlobalSystemConfig *>(node->GetObject());
        if (cfg) {
            //Populate BGP-aas params
            if ((bgpaas_parameters_.port_start != cfg->bgpaas_parameters().port_start) ||
                    (bgpaas_parameters_.port_end != cfg->bgpaas_parameters().port_end)) {
                bgpaas_parameters_.port_start = cfg->bgpaas_parameters().port_start;
                bgpaas_parameters_.port_end = cfg->bgpaas_parameters().port_end;
                // update BgpaaS session info
                oper_db()->bgp_as_a_service()->UpdateBgpAsAServiceSessionInfo();
            }
        } else {
            bgpaas_parameters_.port_start = 0;
            bgpaas_parameters_.port_end = 0;
        }
    } else {
        bgpaas_parameters_.port_start = 0;
        bgpaas_parameters_.port_end = 0;
    }
}
