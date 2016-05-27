/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_QOS_CONFIG_H
#define __AGENT_OPER_GLOBAL_QOS_CONFIG_H

#include <cmn/agent_cmn.h>

class GlobalQosConfig {
public:
    GlobalQosConfig(OperDB *oper_db);
    ~GlobalQosConfig();
    void ConfigHandler(DBTablePartBase *partition,
                       DBEntryBase *dbe);
    OperDB* oper_db() const {
        return oper_db_;
    }
private:
    DBTableBase::ListenerId global_qos_config_listener_id_;
    OperDB *oper_db_;
};
#endif
