/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_cfg_filter_h
#define vnsw_cfg_filter_h

class AgentConfig;
class DBTable;
class IFMapNode;
struct DBRequest;

class CfgFilter {
public:
    CfgFilter(AgentConfig *cfg);
    virtual ~CfgFilter();

    void Init();
    void Shutdown();
private:
    AgentConfig *agent_cfg_;
    bool CheckVmInterfaceProperty(DBTable *table,
                                  const IFMapIdentifier *req_id,
                                  DBRequest *req);
    bool CheckIdPermsProperty(DBTable *table,
                              const IFMapIdentifier *req_id,
                              DBRequest *req,
                              int property_id);
    bool CheckProperty(DBTable *table, IFMapNode *node, DBRequest *req);
    int GetIdPermsPropertyId(DBTable *table) const;
    DISALLOW_COPY_AND_ASSIGN(CfgFilter);
};

#endif // vnsw_cfg_filter_h
