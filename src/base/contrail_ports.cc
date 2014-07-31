/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/contrail_ports.h"

vnsConstants& ContrailPorts::VnsPorts() {
    static vnsConstants vnsPort;
    return vnsPort;
}

// HTTP (Introspect) ports
const uint16_t ContrailPorts::HttpPortConfigNodemgr() {
    return VnsPorts().HttpPortConfigNodemgr;
}

const uint16_t ContrailPorts::HttpPortControlNodemgr() {
    return VnsPorts().HttpPortControlNodemgr;
}

const uint16_t ContrailPorts::HttpPortVRouterNodemgr() {
    return VnsPorts().HttpPortVRouterNodemgr;
}

const uint16_t ContrailPorts::HttpPortDatabaseNodemgr() {
    return VnsPorts().HttpPortDatabaseNodemgr;
}

const uint16_t ContrailPorts::HttpPortAnalyticsNodemgr() {
    return VnsPorts().HttpPortAnalyticsNodemgr;
}

const uint16_t ContrailPorts::HttpPortControl() {
    return VnsPorts().HttpPortControl;
}

const uint16_t ContrailPorts::HttpPortApiServer() {
    return VnsPorts().HttpPortApiServer;
}

const uint16_t ContrailPorts::HttpPortAgent() {
    return VnsPorts().HttpPortAgent;
}

const uint16_t ContrailPorts::HttpPortSchemaTransformer() {
    return VnsPorts().HttpPortSchemaTransformer;
}

const uint16_t ContrailPorts::HttpPortSvcMonitor() {
    return VnsPorts().HttpPortSvcMonitor;
}

const uint16_t ContrailPorts::HttpPortCollector() {
    return VnsPorts().HttpPortCollector;
}

const uint16_t ContrailPorts::HttpPortOpserver() {
    return VnsPorts().HttpPortOpserver;
}

const uint16_t ContrailPorts::HttpPortQueryEngine() {
    return VnsPorts().HttpPortQueryEngine;
}

const uint16_t ContrailPorts::HttpPortDns() {
    return VnsPorts().HttpPortDns;
}

// Supervisor control ports
const uint16_t ContrailPorts::AnalyticsSupervisor() {
    return VnsPorts().AnalyticsSupervisorPort;
}

const uint16_t ContrailPorts::ControlSupervisor() {
    return VnsPorts().ControlSupervisorPort;
}

const uint16_t ContrailPorts::ConfigSupervisor() {
    return VnsPorts().ConfigSupervisorPort;
}

const uint16_t ContrailPorts::VRouterSupervisor() {
    return VnsPorts().VRouterSupervisorPort;
}

const uint16_t ContrailPorts::DatabaseSupervisor() {
    return VnsPorts().DatabaseSupervisorPort;
}

const uint16_t ContrailPorts::WebuiSupervisor() {
    return VnsPorts().WebuiSupervisorPort;
}

// Daemon ports
const uint16_t ContrailPorts::DnsServerPort() {
    return VnsPorts().DnsServerPort;
}

const uint16_t ContrailPorts::ControlBgp() {
    return VnsPorts().ControlBgpPort;
}

const uint16_t ContrailPorts::ControlXmpp() {
    return VnsPorts().ControlXmppPort;
}

const uint16_t ContrailPorts::DiscoveryServerPort() {
    return VnsPorts().DiscoveryServerPort;
}

const uint16_t ContrailPorts::RedisQueryPort() {
    return VnsPorts().RedisQueryPort;
}

const uint16_t ContrailPorts::RedisUvePort() {
    return VnsPorts().RedisUvePort;
}

const uint16_t ContrailPorts::RedisWebuiPort() {
    return VnsPorts().RedisWebuiPort;
}

const uint16_t ContrailPorts::WebConsole() {
    return VnsPorts().WebConsolePort;
}

const uint16_t ContrailPorts::OpServer() {
    return VnsPorts().OpServerPort;
}

const uint16_t ContrailPorts::ApiServer() {
    return VnsPorts().ApiServerPort;
}

const uint16_t ContrailPorts::CollectorPort() {
    return VnsPorts().CollectorPort;
}

const uint16_t ContrailPorts::CollectorProtobufPort() {
    return VnsPorts().CollectorProtobufPort;
}

const uint16_t ContrailPorts::DnsXmpp() {
    return VnsPorts().DnsXmppPort;
}

const uint16_t ContrailPorts::DnsRndc() {
    return VnsPorts().DnsRndcPort;
}

const uint16_t ContrailPorts::ApiServerOpen() {
    return VnsPorts().ApiServerOpenPort;
}

const uint16_t ContrailPorts::AnalyzerUdpPort() {
    return VnsPorts().AnalyzerUdpPort;
}

const uint16_t ContrailPorts::NovaVifVrouterAgentPort() {
    return VnsPorts().NovaVifVrouterAgentPort;
}
