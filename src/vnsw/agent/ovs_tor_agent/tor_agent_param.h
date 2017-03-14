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
        TorInfo() : keepalive_interval_(-1), ha_stale_route_interval_(-1) {}
        std::string type_;
        Ip4Address ip_;
        Ip4Address tsn_ip_;
        std::string id_;
        // Protocol to connect to ToR
        std::string protocol_;
        // SSL certificates required for SSL protocol
        std::string ssl_cert_;
        std::string ssl_privkey_;
        std::string ssl_cacert_;
        int port_;
        // keepalive interval in milli seconds, -1 for unconfigured
        // 0 for no keep alive
        int keepalive_interval_;
        // interval in milliseconds to keep unicast local routes as HA
        // stale routes on connection close, -1 for unconfigured
        int ha_stale_route_interval_;
    };

    explicit TorAgentParam();
    virtual ~TorAgentParam();

    virtual int Validate();

    void AddOptions();
    std::string tor_id() const { return tor_info_.id_; }
    std::string tor_protocol() const { return tor_info_.protocol_; }
    Ip4Address tor_ip() const { return tor_info_.ip_; }
    Ip4Address tsn_ip() const { return tor_info_.tsn_ip_; }
    int tor_port() const { return tor_info_.port_; }
    std::string ssl_cert() const { return tor_info_.ssl_cert_; }
    std::string ssl_privkey() const { return tor_info_.ssl_privkey_; }
    std::string ssl_cacert() const { return tor_info_.ssl_cacert_; }
    int keepalive_interval() const { return tor_info_.keepalive_interval_; }
    int ha_stale_route_interval() const {
        return tor_info_.ha_stale_route_interval_;
    }

 private:
    virtual void ProcessArguments();
    virtual void ParseTorArguments();

    TorInfo tor_info_;
    uint16_t local_port_;
    DISALLOW_COPY_AND_ASSIGN(TorAgentParam);
};

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_PARAM_H_
