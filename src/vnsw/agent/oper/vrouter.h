/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_h_
#define vnsw_agent_vrouter_h_

#include <cmn/agent_cmn.h>

class IFMapNode;
class AgentUtXmlFlowThreshold;

// Handle VRouter configuration
class VRouter {
public:
    static const uint32_t kDefaultFlowExportRate;

    VRouter(OperDB *oper);
    ~VRouter();
    uint32_t flow_export_rate() const { return flow_export_rate_; }
    void VRouterConfig(DBTablePartBase *partition, DBEntryBase *dbe);
    friend class AgentUtXmlFlowThreshold;
private:
    OperDB *oper_;
    DBTableBase::ListenerId vrouter_listener_id_;
    uint32_t flow_export_rate_;
};

#endif // vnsw_agent_vrouter_h_
