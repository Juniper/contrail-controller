/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_h_
#define vnsw_agent_vrouter_h_

#include <cmn/agent_cmn.h>

class IFMapNode;

// Handle VRouter configuration
class VRouter {
public:

    VRouter(OperDB *oper);
    ~VRouter();
    void VRouterConfig(DBTablePartBase *partition, DBEntryBase *dbe);
private:
    OperDB *oper_;
    DBTableBase::ListenerId vrouter_listener_id_;
};

#endif // vnsw_agent_vrouter_h_
