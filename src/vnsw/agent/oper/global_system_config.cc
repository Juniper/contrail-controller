/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <bind/bind_resolver.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <forwarding_class.h>
#include <global_system_config.h>
#include <config_manager.h>

GlobalSystemConfig::GlobalSystemConfig(Agent *agent) : OperIFMapTable(agent) {
}

GlobalSystemConfig::~GlobalSystemConfig() {
}

void GlobalSystemConfig::ConfigDelete(IFMapNode *node) {
    if (node->IsDeleted()) {
        bgpaas_parameters_.port_start = 0;
        bgpaas_parameters_.port_end = 0;
    }
}

void GlobalSystemConfig::ConfigAddChange(IFMapNode *node) {
    autogen::GlobalSystemConfig *cfg =
        static_cast<autogen::GlobalSystemConfig *>(node->GetObject());
    if (cfg ) {
        bgpaas_parameters_.port_start = cfg->bgpaas_parameters().port_start;
        bgpaas_parameters_.port_end = cfg->bgpaas_parameters().port_end;
    } else {
        bgpaas_parameters_.port_start = 0;
        bgpaas_parameters_.port_end = 0;
    }
}

void GlobalSystemConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddGlobalSystemConfigNode(node);
}
