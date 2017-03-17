/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_param_test_hpp
#define vnsw_agent_param_test_hpp

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>

// Class handling agent configuration parameters from config file and 
// arguments
class AgentParamTest {
public:
    AgentParamTest(AgentParam *ap);
    virtual ~AgentParamTest();
    Ip4Address StrToIp(const char *ip);
    void set_collector_server_list(const char*);
    void set_controller_server_list(const char*);
    void set_dns_server_list(const char*);
private:
    AgentParam *params_;
};

#endif // vnsw_agent_param_test_hpp
