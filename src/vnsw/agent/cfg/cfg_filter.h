/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_cfg_filter_h
#define vnsw_cfg_filter_h

class AgentConfig;

class CfgFilter {
public:
    CfgFilter(AgentConfig *cfg) : agent_cfg_(cfg) { };
    virtual ~CfgFilter() { };

    void Init();
    void Shutdown();
private:
    AgentConfig *agent_cfg_;
    bool CheckProperty(DBTable *table, IFMapNode *node, DBRequest *req,
                       int property_id);
    DISALLOW_COPY_AND_ASSIGN(CfgFilter);
};

#endif // vnsw_cfg_filter_h
