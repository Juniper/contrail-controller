/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef CONTRAIL_PORTS_H
#define CONTRAIL_PORTS_H

#include <stdint.h>

class ContrailPorts {
public:
    static const uint16_t HttpPortConfigNodemgr = 8100;
    static const uint16_t HttpPortControlNodemgr = 8101;
    static const uint16_t HttpPortVrouterNodemgr = 8102;
    static const uint16_t HttpPortDatabaseNodemgr = 8103;
    static const uint16_t HttpPortAnalyticsNodemgr = 8104;
    static const uint16_t ControlBgp = 179;
    static const uint16_t ControlXmpp = 5269;
    static const uint16_t DiscoveryServerPort = 5998;
    static const uint16_t RedisQueryPort = 6380;
    static const uint16_t RedisUvePort = 6381;
    static const uint16_t RedisWebuiPort = 6383;
    static const uint16_t RedisQueryEnginePort = 6379;
    static const uint16_t AnalyticsRedisSentinelPort = 26379;
    static const uint16_t WebConsole = 8080;
    static const uint16_t OpServer = 8081;
    static const uint16_t ApiServer = 8082;
    static const uint16_t HttpPortControl = 8083;
    static const uint16_t HttpPortApiServer = 8084;
    static const uint16_t HttpPortAgent = 8085;
    static const uint16_t CollectorPort = 8086;
    static const uint16_t HttpPortSchemaTransformer = 8087;
    static const uint16_t HttpPortSvcMonitor = 8088;
    static const uint16_t HttpPortCollector = 8089;
    static const uint16_t HttpPortOpserver = 8090;
    static const uint16_t HttpPortQueryEngine = 8091;
    static const uint16_t HttpPortDns = 8092;
    static const uint16_t DnsXmpp = 8093;
    static const uint16_t DnsRndc = 8094;
    static const uint16_t ApiServerOpen = 8095;
    static const uint16_t AnalyzerUdpPort = 8099;

    // following uint16_t are reserved for supervisord usage
    static const uint16_t supervisord_analytics = 9002;
    static const uint16_t supervisord_control = 9003;
    static const uint16_t supervisord_config = 9004;
    static const uint16_t supervisord_vrouter = 9005;
    static const uint16_t supervisord_dnsd = 9006;
    static const uint16_t supervisord_contrail_database = 9007;
    static const uint16_t supervisord_webui = 9008;

    static const uint16_t NovaVifVrouterAgentPort = 9090;
private:
    ContrailPorts() {}
};

#endif
