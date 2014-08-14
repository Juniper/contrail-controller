/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/common/vns_constants.h"
#include "base/contrail_ports.h"

// HTTP (Introspect) ports
const uint16_t ContrailPorts::HttpPortConfigNodemgr() {
    return g_vns_constants.HttpPortConfigNodemgr;
}

const uint16_t ContrailPorts::HttpPortControlNodemgr() {
    return g_vns_constants.HttpPortControlNodemgr;
}

const uint16_t ContrailPorts::HttpPortVRouterNodemgr() {
    return g_vns_constants.HttpPortVRouterNodemgr;
}

const uint16_t ContrailPorts::HttpPortDatabaseNodemgr() {
    return g_vns_constants.HttpPortDatabaseNodemgr;
}

const uint16_t ContrailPorts::HttpPortAnalyticsNodemgr() {
    return g_vns_constants.HttpPortAnalyticsNodemgr;
}

const uint16_t ContrailPorts::HttpPortControl() {
    return g_vns_constants.HttpPortControl;
}

const uint16_t ContrailPorts::HttpPortApiServer() {
    return g_vns_constants.HttpPortApiServer;
}

const uint16_t ContrailPorts::HttpPortAgent() {
    return g_vns_constants.HttpPortAgent;
}

const uint16_t ContrailPorts::HttpPortSchemaTransformer() {
    return g_vns_constants.HttpPortSchemaTransformer;
}

const uint16_t ContrailPorts::HttpPortSvcMonitor() {
    return g_vns_constants.HttpPortSvcMonitor;
}

const uint16_t ContrailPorts::HttpPortCollector() {
    return g_vns_constants.HttpPortCollector;
}

const uint16_t ContrailPorts::HttpPortOpserver() {
    return g_vns_constants.HttpPortOpserver;
}

const uint16_t ContrailPorts::HttpPortQueryEngine() {
    return g_vns_constants.HttpPortQueryEngine;
}

const uint16_t ContrailPorts::HttpPortDns() {
    return g_vns_constants.HttpPortDns;
}

// Supervisor control ports
const uint16_t ContrailPorts::AnalyticsSupervisor() {
    return g_vns_constants.AnalyticsSupervisorPort;
}

const uint16_t ContrailPorts::ControlSupervisor() {
    return g_vns_constants.ControlSupervisorPort;
}

const uint16_t ContrailPorts::ConfigSupervisor() {
    return g_vns_constants.ConfigSupervisorPort;
}

const uint16_t ContrailPorts::VRouterSupervisor() {
    return g_vns_constants.VRouterSupervisorPort;
}

const uint16_t ContrailPorts::DatabaseSupervisor() {
    return g_vns_constants.DatabaseSupervisorPort;
}

const uint16_t ContrailPorts::WebuiSupervisor() {
    return g_vns_constants.WebuiSupervisorPort;
}

// Daemon ports
const uint16_t ContrailPorts::DnsServerPort() {
    return g_vns_constants.DnsServerPort;
}

const uint16_t ContrailPorts::ControlBgp() {
    return g_vns_constants.ControlBgpPort;
}

const uint16_t ContrailPorts::ControlXmpp() {
    return g_vns_constants.ControlXmppPort;
}

const uint16_t ContrailPorts::DiscoveryServerPort() {
    return g_vns_constants.DiscoveryServerPort;
}

const uint16_t ContrailPorts::RedisQueryPort() {
    return g_vns_constants.RedisQueryPort;
}

const uint16_t ContrailPorts::RedisUvePort() {
    return g_vns_constants.RedisUvePort;
}

const uint16_t ContrailPorts::RedisWebuiPort() {
    return g_vns_constants.RedisWebuiPort;
}

const uint16_t ContrailPorts::WebConsole() {
    return g_vns_constants.WebConsolePort;
}

const uint16_t ContrailPorts::OpServer() {
    return g_vns_constants.OpServerPort;
}

const uint16_t ContrailPorts::ApiServer() {
    return g_vns_constants.ApiServerPort;
}

const uint16_t ContrailPorts::CollectorPort() {
    return g_vns_constants.CollectorPort;
}

const uint16_t ContrailPorts::DnsXmpp() {
    return g_vns_constants.DnsXmppPort;
}

const uint16_t ContrailPorts::DnsRndc() {
    return g_vns_constants.DnsRndcPort;
}

const uint16_t ContrailPorts::ApiServerOpen() {
    return g_vns_constants.ApiServerOpenPort;
}

const uint16_t ContrailPorts::AnalyzerUdpPort() {
    return g_vns_constants.AnalyzerUdpPort;
}

const uint16_t ContrailPorts::NovaVifVrouterAgentPort() {
    return g_vns_constants.NovaVifVrouterAgentPort;
}
