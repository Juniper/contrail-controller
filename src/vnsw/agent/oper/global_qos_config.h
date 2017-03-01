/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_QOS_CONFIG_H
#define __AGENT_OPER_GLOBAL_QOS_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
class GlobalQosConfig : public OperIFMapTable {
public:
    static const uint8_t kInvalidDscp = 0xFF;
    GlobalQosConfig(Agent *agent);
    virtual ~GlobalQosConfig();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    uint8_t control_dscp() const { return control_dscp_; }
    uint8_t dns_dscp() const { return dns_dscp_; }
    uint8_t analytics_dscp() const { return analytics_dscp_; }
private:
    void ResetDscp();
    void SetDnsDscp(uint8_t value);
    uint8_t control_dscp_;
    uint8_t dns_dscp_;
    uint8_t analytics_dscp_;

    DISALLOW_COPY_AND_ASSIGN(GlobalQosConfig);
};
#endif
