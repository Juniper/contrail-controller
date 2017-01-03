/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ifmap/ifmap_node.h>
#include <vnc_cfg_types.h>
#include <oper/operdb_init.h>
#include <oper/vrouter.h>
#include <oper/config_manager.h>

VRouter::VRouter(Agent *agent) : OperIFMapTable(agent) {
}

VRouter::~VRouter() {
}

void VRouter::ConfigDelete(IFMapNode *node) {
    return;
}

void VRouter::ConfigAddChange(IFMapNode *node) {
    return;
}

void VRouter::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddVirtualRouterNode(node);
    return;
}
