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
class GlobalSystemConfig {
public:
    GlobalSystemConfig(OperDB *oper_db);
    ~GlobalSystemConfig();

    void GlobalSystemConfigHandler(DBTablePartBase *partition,
                                   DBEntryBase *dbe);
    OperDB* oper_db() const {
        return oper_db_;
    }
    BGPaaServiceParameters::BGPaaServicePortRangePair bgpaas_port_range() const {
         return std::make_pair(bgpaas_parameters_.port_start,
                               bgpaas_parameters_.port_end);
    }
private:
    DBTableBase::ListenerId global_system_config_listener_id_;
    OperDB *oper_db_;
    BGPaaServiceParameters bgpaas_parameters_;
};
#endif
