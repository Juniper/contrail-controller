/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_h_
#define vnsw_agent_vrouter_h_

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;

// Handle VRouter configuration
class VRouter : public OperIFMapTable {
public:

    VRouter(Agent *agent);
    virtual ~VRouter();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
private:
    DISALLOW_COPY_AND_ASSIGN(VRouter);
};

#endif // vnsw_agent_vrouter_h_
