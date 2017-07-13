/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H
#define __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
class GlobalSystemConfig {
public:
    GlobalSystemConfig(OperDB *oper_db);
    ~GlobalSystemConfig();

    void GlobalSystemConfigHandler(DBTablePartBase *partition,
                                   DBEntryBase *dbe);
    OperDB* oper_db() const {
        return oper_db_;
    }
    const std::vector<uint16_t> &bgpaas_port_range() const {
         return bgpaas_port_range_;
    }
private:
    DBTableBase::ListenerId global_system_config_listener_id_;
    OperDB *oper_db_;
    std::vector<uint16_t>  bgpaas_port_range_;
};
#endif
