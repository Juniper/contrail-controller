/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <project_config.h>
#include <config_manager.h>

ProjectConfig::ProjectConfig(Agent *agent) : OperIFMapTable(agent),
    callbacks_() {
    Reset();
}

ProjectConfig::~ProjectConfig() {
}

void ProjectConfig::Register(ProjectConfig::Callback cb)
{
    callbacks_.push_back(cb);
}

void ProjectConfig::Notify() {
    for (ProjectConfig::CallbackList::iterator it =
         callbacks_.begin(); it != callbacks_.end(); it++) {
        (*it)();
    }
}

void ProjectConfig::Reset() {
    vxlan_routing_ = false;
}

void ProjectConfig::ConfigAddChange(IFMapNode *node) {
    autogen::Project *cfg =
        dynamic_cast<autogen::Project *>(node->GetObject());
    bool changed = false;
    bool new_vxlan_routing = true;

    if (!cfg || !(cfg->IsPropertySet(autogen::Project::VXLAN_ROUTING))) {
        new_vxlan_routing = false;
        Reset();
    } else {
        new_vxlan_routing = cfg->vxlan_routing();
    }

    if (vxlan_routing_ != new_vxlan_routing) {
        vxlan_routing_ = new_vxlan_routing;
        changed = true;
    }

    if (changed) {
        //TODO though config and oper are excluded to each other,
        //this should be sent in oper context.
        Notify();
    }
}

void ProjectConfig::ConfigDelete(IFMapNode *node) {
    if (node->IsDeleted()) {
        Reset();
    }
}

void ProjectConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddProjectNode(node);
}
