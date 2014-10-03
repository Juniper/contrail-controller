/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BASE_CONTRAIL_PORTS_H_
#define BASE_CONTRAIL_PORTS_H_

#include <stdint.h>
#include <sandesh/common/vns_constants.h>

class ContrailPorts {
public:
    // HTTP (Introspect) ports
    static const uint16_t HttpPortConfigNodemgr();
    static const uint16_t HttpPortControlNodemgr();
    static const uint16_t HttpPortVRouterNodemgr();
    static const uint16_t HttpPortDatabaseNodemgr();
    static const uint16_t HttpPortAnalyticsNodemgr();

    static const uint16_t HttpPortControl();
    static const uint16_t HttpPortApiServer();
    static const uint16_t HttpPortAgent();
    static const uint16_t HttpPortSchemaTransformer();
    static const uint16_t HttpPortSvcMonitor();
    static const uint16_t HttpPortCollector();
    static const uint16_t HttpPortOpserver();
    static const uint16_t HttpPortQueryEngine();
    static const uint16_t HttpPortDns();

    // Supervisor control ports
    static const uint16_t AnalyticsSupervisor();
    static const uint16_t ControlSupervisor();
    static const uint16_t ConfigSupervisor();
    static const uint16_t VRouterSupervisor();
    static const uint16_t DatabaseSupervisor();
    static const uint16_t WebuiSupervisor();

    // Daemon ports
    static const uint16_t DnsServerPort();
    static const uint16_t ControlBgp();
    static const uint16_t ControlXmpp();
    static const uint16_t DiscoveryServerPort();
    static const uint16_t RedisQueryPort();
    static const uint16_t RedisUvePort();
    static const uint16_t RedisWebuiPort();
    static const uint16_t WebConsole();
    static const uint16_t OpServer();
    static const uint16_t ApiServer();
    static const uint16_t CollectorPort();
    static const uint16_t CollectorProtobufPort();
    static const uint16_t DnsXmpp();
    static const uint16_t DnsRndc();
    static const uint16_t ApiServerOpen();
    static const uint16_t AnalyzerUdpPort();
    static const uint16_t NovaVifVrouterAgentPort();

private:
    ContrailPorts() {}

    static vnsConstants& VnsPorts();
};

#endif  // BASE_CONTRAIL_PORTS_H_
