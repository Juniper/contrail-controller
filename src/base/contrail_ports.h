/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef CONTRAIL_PORTS_H
#define CONTRAIL_PORTS_H

class ContrailPorts {
public:
    static const short ControlXmpp = 5269;
    static const short DiscoveryServerPort = 5998;
    static const short RedisQueryPort = 6380;
    static const short RedisUvePort = 6381;
    static const short RedisWebuiPort = 6383;
    static const short AnalyticsRedisSentinelPort = 26379;
    static const short WebConsole = 8080;
    static const short OpServer = 8081;
    static const short ApiServer = 8082;
    static const short HttpPortControl = 8083;
    static const short HttpPortApiServer = 8084;
    static const short HttpPortAgent = 8085;
    static const short CollectorPort = 8086;
    static const short HttpPortSchemaTransformer = 8087;
    static const short HttpPortSvcMonitor = 8088;
    static const short HttpPortCollector = 8089;
    static const short HttpPortOpserver = 8090;
    static const short HttpPortQueryEngine = 8091;
    static const short HttpPortDns = 8092;
    static const short DnsXmpp = 8093;
    static const short DnsRndc = 8094;
    static const short ApiServerOpen = 8095;
    static const short AnalyzerUdpPort = 8099;

    // following ports are reserved for supervisord usage
    static const short supervisord_analytics = 9002;
    static const short supervisord_control = 9003;
    static const short supervisord_config = 9004;
    static const short supervisord_vrouter = 9005;
    static const short supervisord_dnsd = 9006;
    static const short supervisord_contrail_database = 9007;
    static const short supervisord_webui = 9008;
private:
    ContrailPorts() {}
};

#endif
