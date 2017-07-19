/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H
#define __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
struct BGPaaServiceParameters {
typedef std::pair<uint16_t, uint16_t> BGPaaServicePortRangePair;
    int port_start;
    int port_end;
};
class GlobalSystemConfig : public OperIFMapTable {
public:
    GlobalSystemConfig(Agent *agent);
    virtual ~GlobalSystemConfig();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    BGPaaServiceParameters::BGPaaServicePortRangePair bgpaas_port_range() const {
         return std::make_pair(bgpaas_parameters_.port_start,
                               bgpaas_parameters_.port_end);
    }
private:
    BGPaaServiceParameters bgpaas_parameters_;
    DISALLOW_COPY_AND_ASSIGN(GlobalSystemConfig);
};
#endif
