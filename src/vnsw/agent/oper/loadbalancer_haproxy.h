/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include <string>
#include <ostream>
#include <boost/uuid/uuid.hpp>
#include "base/util.h"

#define LB_HAPROXY_SSL_PORT 443

class LoadbalancerProperties;
class Agent;

class LoadbalancerHaproxy {
public:
    LoadbalancerHaproxy(Agent *);

    void GenerateConfig(const std::string &filename,
                        const boost::uuids::uuid &pool_id,
                        const LoadbalancerProperties &props) const;

private:
    void GenerateGlobal(std::ostream *out,
                        const LoadbalancerProperties &props) const;
    void GenerateDefaults(std::ostream *out,
                          const LoadbalancerProperties &props) const;
    void GenerateListen(std::ostream *out,
                          const LoadbalancerProperties &props) const;
    void GenerateFrontend(std::ostream *out,
                          const boost::uuids::uuid &pool_id,
                          const LoadbalancerProperties &props) const;
    void GenerateBackend(std::ostream *out,
                         const boost::uuids::uuid &pool_id,
                         const LoadbalancerProperties &props) const;

    const std::string &ProtocolMap(const std::string &proto) const;
    const std::string &BalanceMap(const std::string &balance) const;

    std::map<std::string, std::string> protocol_map_;
    std::map<std::string, std::string> balance_map_;
    std::string protocol_default_;
    std::string balance_default_;
    Agent *agent_;

    DISALLOW_COPY_AND_ASSIGN(LoadbalancerHaproxy);
};
