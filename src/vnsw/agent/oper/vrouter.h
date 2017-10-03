/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_h_
#define vnsw_agent_vrouter_h_

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;

struct VRouterSubnet {
    IpAddress ip_prefix;
    uint8_t plen;
    VRouterSubnet(const std::string& ip, uint8_t prefix_len);
    bool operator==(const VRouterSubnet& rhs) const;
};

// Handle VRouter configuration
class VRouter : public OperIFMapTable {
public:

    VRouter(Agent *agent);
    virtual ~VRouter();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    void FillSandeshInfo(VrouterInfoResp *resp);
    uint32_t SubnetCount() const { return subnets_.size(); }
    bool IsSubnetMember(const IpAddress &addr) const;
    void Shutdown();
private:
    void AddSubnetRoutes();
    void DeleteSubnetRoutes();
    void ClearSubnets();
    IFMapNode *FindTarget(IFMapNode *node, std::string node_type) const;

    std::string name_;
    std::vector<VRouterSubnet> subnets_;
    std::string display_name_;
    DISALLOW_COPY_AND_ASSIGN(VRouter);
};

#endif // vnsw_agent_vrouter_h_
