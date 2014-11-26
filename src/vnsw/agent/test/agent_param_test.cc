/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <agent_param_test.h>

AgentParamTest::AgentParamTest(Agent *agent) : AgentParam(agent) {
}

AgentParamTest::~AgentParamTest() {
}

Ip4Address AgentParamTest::StrToIp(const char *ip) {
    boost::system::error_code ec;
    Ip4Address addr = Ip4Address::from_string(ip, ec);
    if (ec.value() == 0) {
        return addr;
    } else {
        return Ip4Address(0);
    }
}

void AgentParamTest::set_xmpp_server_1(const char *ip) {
    xmpp_server_1_ = StrToIp(ip);
}

void AgentParamTest::set_xmpp_server_2(const char *ip) {
    xmpp_server_2_ = StrToIp(ip);
}

void AgentParamTest::set_dns_server_1(const char *ip) {
    dns_server_1_ = StrToIp(ip);
}

void AgentParamTest::set_dns_server_2(const char *ip) {
    dns_server_2_ = StrToIp(ip);
}

void AgentParamTest::set_collector_server_list(const char *ip) {
    std::string element(ip);
    if (element.length()) {
        collector_server_list_.push_back(element);
    } else {
        collector_server_list_.clear();
    }
}

void AgentParamTest::set_discovery_server(const char *ip) {
    dss_server_ = StrToIp(ip);
}
