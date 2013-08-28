/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_cfg_filter_h
#define vnsw_cfg_filter_h

class CfgFilter {
public:
    CfgFilter() { };
    virtual ~CfgFilter() { };

    static void Init();
    static void Shutdown();
private:
    bool CheckProperty(DBTable *table, IFMapNode *node, DBRequest *req,
                       int property_id);
    static CfgFilter *singleton_;
    DISALLOW_COPY_AND_ASSIGN(CfgFilter);
};

#endif // vnsw_cfg_filter_h
