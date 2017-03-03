/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <bind/bind_resolver.h>
#include <vnc_cfg_types.h>
#include <oper_db.h>
#include <forwarding_class.h>
#include <global_qos_config.h>
#include <config_manager.h>

GlobalQosConfig::GlobalQosConfig(Agent *agent) : OperIFMapTable(agent) {
}

GlobalQosConfig::~GlobalQosConfig() {
}

void GlobalQosConfig::ResetDscp() {
    if (control_dscp_ != kInvalidDscp) {
        if (control_dscp_ != 0) {
            agent()->SetXmppDscp(0);
        }
        control_dscp_ = kInvalidDscp;
    }
    if (dns_dscp_ != kInvalidDscp) {
        if (dns_dscp_ != 0) {
            SetDnsDscp(0);
        }
        dns_dscp_ = kInvalidDscp;
    }
    if (analytics_dscp_ != kInvalidDscp) {
        if (analytics_dscp_ != 0) {
            Sandesh::SetDscpValue(0);
        }
        analytics_dscp_ = kInvalidDscp;
    }
}

void GlobalQosConfig::SetDnsDscp(uint8_t value) {
    BindResolver *resolver = BindResolver::Resolver();
    if (resolver) {
        resolver->SetDscpValue(value);
    }
}

void GlobalQosConfig::ConfigDelete(IFMapNode *node) {
    if (node->IsDeleted()) {
        ResetDscp();
    }
}

void GlobalQosConfig::ConfigAddChange(IFMapNode *node) {
    autogen::GlobalQosConfig *cfg =
        static_cast<autogen::GlobalQosConfig *>(node->GetObject());
    if (cfg &&
        cfg->IsPropertySet(autogen::GlobalQosConfig::CONTROL_TRAFFIC_DSCP)) {
        const autogen::ControlTrafficDscpType &dscp =
            cfg->control_traffic_dscp();
        if (control_dscp_ != dscp.control) {
            control_dscp_ = dscp.control;
            agent()->SetXmppDscp(control_dscp_);
        }
        if (dns_dscp_ != dscp.dns) {
            dns_dscp_ = dscp.dns;
            SetDnsDscp(dns_dscp_);
        }
        if (analytics_dscp_ != dscp.analytics) {
            analytics_dscp_ = dscp.analytics;
            Sandesh::SetDscpValue(analytics_dscp_);
        }
    } else {
        ResetDscp();
    }
}

void GlobalQosConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddGlobalQosConfigNode(node);
}
