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
    VRouterSubnet() : ip_prefix(), plen(0) {}
    VRouterSubnet(const std::string& ip, uint8_t prefix_len);
    bool operator==(const VRouterSubnet& rhs) const;
    bool IsLess(const VRouterSubnet *rhs) const;
    bool operator() (const VRouterSubnet &lhs, const VRouterSubnet &rhs) const;
};

// Handle VRouter configuration
class VRouter : public OperIFMapTable {
public:
    typedef std::set<VRouterSubnet, VRouterSubnet> VRouterSubnetSet;
    VRouter(Agent *agent);
    virtual ~VRouter();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    void FillSandeshInfo(VrouterInfoResp *resp);
    uint32_t SubnetCount() const { return subnet_list_.size(); }
    bool IsSubnetMember(const IpAddress &addr) const;
    void Shutdown();
    void Insert(const VRouterSubnet *rhs);
    void Update(const VRouterSubnet *lhs, const VRouterSubnet *rhs) {}
    void Remove(VRouterSubnetSet::iterator &it);
private:
    void DeleteSubnetRoutes();
    void ClearSubnets();
    void DeleteRoute(const VRouterSubnet &subnet);
    void AddRoute(const VRouterSubnet &subnet);
    IFMapNode *FindTarget(IFMapNode *node, std::string node_type) const;

    std::string name_;
    VRouterSubnetSet subnet_list_;
    std::string display_name_;
    DISALLOW_COPY_AND_ASSIGN(VRouter);
};

#endif // vnsw_agent_vrouter_h_
