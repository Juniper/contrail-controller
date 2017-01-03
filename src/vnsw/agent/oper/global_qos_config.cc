/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <forwarding_class.h>
#include <global_qos_config.h>
#include <config_manager.h>

GlobalQosConfig::GlobalQosConfig(Agent *agent) : OperIFMapTable(agent) {
}

GlobalQosConfig::~GlobalQosConfig() {
}

void GlobalQosConfig::ConfigDelete(IFMapNode *node) {
}

void GlobalQosConfig::ConfigAddChange(IFMapNode *node) {
}

void GlobalQosConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddGlobalQosConfigNode(node);
}
