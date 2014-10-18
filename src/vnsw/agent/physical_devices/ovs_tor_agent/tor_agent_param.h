/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_PARAM_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_PARAM_H_

#include <init/agent_param.h>
#include <string>

// Class handling agent configuration parameters from config file and
// arguments
class TorAgentParam : public AgentParam  {
 public:
    enum TorType {
        INVALID,
        OVS
    };

    struct TorInfo {
        std::string type_;
        Ip4Address ip_;
        std::string id_;
        // Protocol to connect to ToR
        std::string protocol_;
        int port_;
    };

    explicit TorAgentParam(Agent *agent);
    virtual ~TorAgentParam();

    virtual int Validate();

    void AddOptions();
    std::string tor_id() const { return tor_info_.id_; }

 private:
    virtual void InitFromConfig();
    virtual void InitFromArguments();

    TorInfo tor_info_;
    uint16_t local_port_;
    DISALLOW_COPY_AND_ASSIGN(TorAgentParam);
};

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_PARAM_H_
