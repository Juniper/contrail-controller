/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_param_test.h"

AgentParamTest::AgentParamTest(AgentParam *ap) {
    params_ = ap;
}

AgentParamTest::~AgentParamTest() {
}

void AgentParamTest::set_controller_server_list(const char *ip) {
    std::string element(ip);
    if (element.length()) {
        params_->controller_server_list_.push_back(element);
    } else {
        params_->controller_server_list_.clear();
    }
}

void AgentParamTest::set_dns_server_list(const char *ip) {
    std::string element(ip);
    if (element.length()) {
        params_->dns_server_list_.push_back(element);
    } else {
        params_->dns_server_list_.clear();
    }
}

void AgentParamTest::set_collector_server_list(const char *ip) {
    std::string element(ip);
    if (element.length()) {
        params_->collector_server_list_.push_back(element);
    } else {
        params_->collector_server_list_.clear();
    }
}
