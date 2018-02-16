/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_PROJECT_CONFIG_H
#define __AGENT_OPER_PROJECT_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
namespace autogen {
    class ProjectConfig;
}

class ProjectConfig : public OperIFMapTable {
public:
    typedef boost::function<void()> Callback;
    typedef std::vector<Callback> CallbackList;
    ProjectConfig(Agent *agent);
    virtual ~ProjectConfig();

    void Reset();
    void Register(ProjectConfig::Callback cb);
    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);

    bool vxlan_routing() const {
        return vxlan_routing_;
    }

private:
    void Notify();

    bool vxlan_routing_;
    CallbackList callbacks_;
    DISALLOW_COPY_AND_ASSIGN(ProjectConfig);
};
#endif
