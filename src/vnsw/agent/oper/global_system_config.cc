/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <init/agent_param.h>
#include <bind/bind_resolver.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <forwarding_class.h>
#include <global_system_config.h>
#include <config_manager.h>
#include "oper/bgp_as_service.h"

void BGPaaServiceParameters::Reset() {
    port_start = 0;
    port_end = 0;
}

void GracefulRestartParameters::Reset() {
    enable_ = true;
    end_of_rib_time_ = 0;
    xmpp_helper_enable_ = true;
    config_seen_ = false;
}

void GracefulRestartParameters::Update(autogen::GlobalSystemConfig *cfg) {
    bool changed = false;

    config_seen_ = true;
    if (enable_ != cfg->graceful_restart_parameters().enable) {
        enable_ = cfg->graceful_restart_parameters().enable;
        changed = true;
    }

    if (xmpp_helper_enable_ !=
        cfg->graceful_restart_parameters().xmpp_helper_enable) {
        xmpp_helper_enable_ =
            cfg->graceful_restart_parameters().xmpp_helper_enable;
        changed = true;
    }

    if (end_of_rib_time_ !=
        (uint32_t)cfg->graceful_restart_parameters().end_of_rib_timeout) {
        end_of_rib_time_ =
            (uint32_t)cfg->graceful_restart_parameters().end_of_rib_timeout;
        changed = true;
    }

    if (changed) {
        Notify();
    }
}

void GracefulRestartParameters::Notify() {
    for (GracefulRestartParameters::CallbackList::iterator it =
         callbacks_.begin(); it != callbacks_.end(); it++) {
        (*it)();
    }
}

void GracefulRestartParameters::Register(GracefulRestartParameters::Callback cb)
{
    callbacks_.push_back(cb);
}

GlobalSystemConfig::GlobalSystemConfig(Agent *agent) : OperIFMapTable(agent) {
    Reset();
}

GlobalSystemConfig::~GlobalSystemConfig() {
}

void GlobalSystemConfig::ConfigDelete(IFMapNode *node) {
    if (node->IsDeleted()) {
        Reset();
    }
}

void GlobalSystemConfig::Reset() {
    bgpaas_parameters_.Reset();
    gres_parameters_.Reset();
}

void GlobalSystemConfig::ConfigAddChange(IFMapNode *node) {
    autogen::GlobalSystemConfig *cfg =
        dynamic_cast<autogen::GlobalSystemConfig *>(node->GetObject());

    if (!cfg) {
        Reset();
        return;
    }

    //Populate BGP-aas params
    if ((bgpaas_parameters_.port_start != cfg->bgpaas_parameters().port_start) ||
            (bgpaas_parameters_.port_end != cfg->bgpaas_parameters().port_end)) {
        bgpaas_parameters_.port_start = cfg->bgpaas_parameters().port_start;
        bgpaas_parameters_.port_end = cfg->bgpaas_parameters().port_end;
        // update BgpaaS session info
        agent()->oper_db()->bgp_as_a_service()->UpdateBgpAsAServiceSessionInfo();
    }

    //Populate gres params
    gres_parameters_.Update(cfg);
}

void GlobalSystemConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddGlobalSystemConfigNode(node);
}

GracefulRestartParameters &GlobalSystemConfig::gres_parameters() {
    return gres_parameters_;
}
