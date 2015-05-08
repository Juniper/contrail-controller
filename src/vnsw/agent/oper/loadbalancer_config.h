/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include <string>
#include <ostream>
#include <boost/uuid/uuid.hpp>
#include "base/util.h"

class LoadbalancerProperties;
class Agent;

class LoadbalancerConfig {
public:
    LoadbalancerConfig(Agent *);

    void GenerateConfig(const std::string &filename,
                        const boost::uuids::uuid &pool_id,
                        const LoadbalancerProperties &props) const;

private:
    void GenerateVip(std::ostream *out,
                     const LoadbalancerProperties &props) const;
    void GeneratePool(std::ostream *out,
                      const boost::uuids::uuid &pool_id,
                      const LoadbalancerProperties &props) const;
    void GenerateMembers(std::ostream *out,
                         const LoadbalancerProperties &props) const;
    void GenerateHealthMonitors(std::ostream *out,
                                const LoadbalancerProperties &props) const;

    Agent *agent_;

    DISALLOW_COPY_AND_ASSIGN(LoadbalancerConfig);
};
