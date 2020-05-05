/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <init/agent_param.h>
#include <bind/bind_resolver.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <controller/controller_init.h>
#include <forwarding_class.h>
#include <global_system_config.h>
#include <config_manager.h>
#include <cfg/cfg_init.h>
#include "oper/bgp_as_service.h"

void BGPaaServiceParameters::Reset() {
    port_start = 0;
    port_end = 0;
}

void GracefulRestartParameters::Reset() {
    enable_ = true;
    end_of_rib_time_ = 0;
    long_lived_restart_time_ = 0;
    xmpp_helper_enable_ = true;
    config_seen_ = false;
}

void FastConvergenceParameters::Reset() {
    enable = false;
    xmpp_hold_time = 90;
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

    if (long_lived_restart_time_ !=
        (uint32_t)cfg->graceful_restart_parameters().long_lived_restart_time) {
        long_lived_restart_time_ =
            (uint32_t)cfg->graceful_restart_parameters().long_lived_restart_time;
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
    fc_params_.Reset();
    cfg_igmp_enable_ = false;
}

void GlobalSystemConfig::ConfigAddChange(IFMapNode *node) {
    autogen::GlobalSystemConfig *cfg =
        dynamic_cast<autogen::GlobalSystemConfig *>(node->GetObject());

    if (!cfg) {
        Reset();
        return;
    }

    //Populate BGP-aas params
    if ((bgpaas_parameters_.port_start !=
                    cfg->bgpaas_parameters().port_start) ||
        (bgpaas_parameters_.port_end !=
                    cfg->bgpaas_parameters().port_end)) {
        bgpaas_parameters_.port_start = cfg->bgpaas_parameters().port_start;
        bgpaas_parameters_.port_end = cfg->bgpaas_parameters().port_end;
        // update BgpaaS session info
        agent()->oper_db()->bgp_as_a_service()->
                                UpdateBgpAsAServiceSessionInfo();
    }

    //Populate fast convergence params
    if ((fc_params_.enable !=
                    cfg->fast_convergence_parameters().enable) ||
        (fc_params_.xmpp_hold_time !=
                    cfg->fast_convergence_parameters().xmpp_hold_time)) {
        fc_params_.enable = cfg->fast_convergence_parameters().enable;
        fc_params_.xmpp_hold_time =
                    cfg->fast_convergence_parameters().xmpp_hold_time;
        // update hold time in existing xmpp sessions
        agent()->controller()->XmppServerUpdate(fc_params_.xmpp_hold_time);
    }

    if (cfg_igmp_enable_ != cfg->igmp_enable()) {

        cfg_igmp_enable_ = cfg->igmp_enable();

        agent()->cfg()->cfg_vm_interface_table()->NotifyAllEntries();
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

bool GlobalSystemConfig::cfg_igmp_enable() const {
    return cfg_igmp_enable_;
}

void GlobalSystemConfig::FillSandeshInfo(GlobalSystemConfigResp *resp)
{
  BGPaaSSandeshData bgp_params;
  bgp_params.set_port_start(bgpaas_parameters_.port_start);
  bgp_params.set_end_port(bgpaas_parameters_.port_end);
  GracefulRestartSandeshData graceful_restart;
  graceful_restart.set_enable(gres_parameters_.enable());
  graceful_restart.set_end_of_rib_time(gres_parameters_.end_of_rib_time());
  graceful_restart.set_xmpp_helper_enable(gres_parameters_.xmpp_helper_enable());
  graceful_restart.set_config_seen(gres_parameters_.config_seen());
  IgmpSandeshData igmp_params;
  igmp_params.set_cfg_igmp_enable(cfg_igmp_enable());
  resp->set_bgp_parameters(bgp_params);
  resp->set_llgr_parameters(graceful_restart);
  resp->set_igmp_parameters(igmp_params);
}

void GlobalSystemConfigReq::HandleRequest() const {
    GlobalSystemConfigResp *resp = new GlobalSystemConfigResp();
    Agent *agent = Agent::GetInstance();
    GlobalSystemConfig  *global_sys_config = agent->oper_db()->global_system_config();
    if (!global_sys_config) {
        resp->set_more(false);
        resp->Response();
        return;
    }
    resp->set_context(context());
    global_sys_config->FillSandeshInfo(resp);
    resp->set_more(false);
    resp->Response();
    return;
}
