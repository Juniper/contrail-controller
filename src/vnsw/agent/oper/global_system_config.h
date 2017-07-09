/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H
#define __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
class GlobalSystemConfig : public OperIFMapTable {
public:
    GlobalSystemConfig(Agent *agent);
    virtual ~GlobalSystemConfig();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    const std::vector<uint16_t> &bgpaas_port_range() const {
         return bgpaas_port_range_;
    }
private:
    std::vector<uint16_t>  bgpaas_port_range_;

    DISALLOW_COPY_AND_ASSIGN(GlobalSystemConfig);
};
#endif
