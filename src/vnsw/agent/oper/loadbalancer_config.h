/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include <string>
#include <ostream>
#include <boost/uuid/uuid.hpp>
#include "base/util.h"

class LBPoolInfo;
class Agent;
class Loadbalancer;

class LoadbalancerConfig {
public:
    LoadbalancerConfig(Agent *);

    void GenerateConfig(const std::string &filename,
                        const boost::uuids::uuid &pool_id,
                        const LBPoolInfo &props) const;
    void GenerateV2Config(const std::string &filename, Loadbalancer *lb) const;

private:
    void GenerateVip(std::ostream *out,
                     const LBPoolInfo &props) const;
    void GeneratePool(std::ostream *out,
                      const boost::uuids::uuid &pool_id,
                      const LBPoolInfo &props, const std::string &indent) const;
    void GenerateMembers(std::ostream *out, const LBPoolInfo &props,
                         const std::string &indent) const;
    void GenerateHealthMonitors(std::ostream *out, const LBPoolInfo &props,
                                const std::string &indent) const;
    void GenerateCustomAttributes(std::ostream *out, const LBPoolInfo &props,
                                  const std::string &indent) const;
    void GenerateLoadbalancer(std::ostream *out, Loadbalancer *lb) const;
    void GenerateListeners(std::ostream *out, Loadbalancer *lb) const;
    void GeneratePools(std::ostream *out, Loadbalancer *lb) const;

    Agent *agent_;

    DISALLOW_COPY_AND_ASSIGN(LoadbalancerConfig);
};
