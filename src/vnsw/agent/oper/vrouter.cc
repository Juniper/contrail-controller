#include <oper/vrf.h>
/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ifmap/ifmap_node.h>
#include <vnc_cfg_types.h>
#include <net/address_util.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/route_leak.h>
#include <oper/vrouter.h>
#include <oper/vrf.h>
#include <oper/config_manager.h>

VRouterSubnet::VRouterSubnet(const std::string& ip, uint8_t prefix_len) {
    boost::system::error_code ec;
    ip_prefix = IpAddress::from_string(ip, ec);
    plen = prefix_len;
}

bool VRouterSubnet::operator==(const VRouterSubnet& rhs) const {
    if (plen != rhs.plen) {
        return false;
    }
    if (ip_prefix != rhs.ip_prefix) {
        return false;
    }
    return true;
}

VRouter::VRouter(Agent *agent) : OperIFMapTable(agent) {
}

VRouter::~VRouter() {
}

void VRouter::ConfigDelete(IFMapNode *node) {
    return;
}

IFMapNode *VRouter::FindTarget(IFMapNode *node, std::string node_type) const {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        if (adj_node->table()->Typename() == node_type)
            return adj_node;
    }
    return NULL;
}

void VRouter::ClearSubnets() {
    if (SubnetCount() == 0) {
        return;
    }
    DeleteSubnetRoutes();
    subnets_.clear();
    agent()->oper_db()->route_leak_manager()->ReEvaluateRouteExports();
}

void VRouter::ConfigAddChange(IFMapNode *node) {
    autogen::VirtualRouter *cfg = static_cast<autogen::VirtualRouter *>
        (node->GetObject());
    std::vector<VRouterSubnet> new_subnet_list;
    if (node->IsDeleted() == false) {
        name_ = node->name();
        display_name_ = cfg->display_name();
        IFMapNode *vr_ipam_link = agent()->config_manager()->
            FindAdjacentIFMapNode(node, "virtual-router-network-ipam");
        /* If the link is deleted, clear the subnets configured earlier */
        if (!vr_ipam_link) {
            ClearSubnets();
            return;
        }
        autogen::VirtualRouterNetworkIpam *vr_ipam =
            static_cast<autogen::VirtualRouterNetworkIpam *>
            (vr_ipam_link->GetObject());
        const autogen::VirtualRouterNetworkIpamType &data = vr_ipam->data();
        std::vector<autogen::SubnetType>::const_iterator it =
            data.subnet.begin();
        while (it != data.subnet.end()) {
            new_subnet_list.push_back
                (VRouterSubnet(it->ip_prefix, it->ip_prefix_len));
            ++it;
        }

        if (new_subnet_list != subnets_) {
            DeleteSubnetRoutes();
            subnets_ = new_subnet_list;
            AddSubnetRoutes();
            agent()->oper_db()->route_leak_manager()->ReEvaluateRouteExports();
        }
    } else {
        ClearSubnets();
    }
    return;
}

void VRouter::AddSubnetRoutes() {
    VrfEntry *vrf = agent()->fabric_vrf();
    for (unsigned int i = 0; i < subnets_.size(); ++i) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->AddVrouterSubnetRoute
            (subnets_[i].ip_prefix, subnets_[i].plen);
    }
}

void VRouter::DeleteSubnetRoutes() {
    VrfEntry *vrf = agent()->fabric_vrf();
    for (unsigned int i = 0; i < subnets_.size(); ++i) {
        if (subnets_[i].ip_prefix.is_v4()) {
            static_cast<InetUnicastAgentRouteTable *>(vrf->
                GetInet4UnicastRouteTable())->DeleteReq
                (agent()->fabric_rt_export_peer(), vrf->GetName(),
                 subnets_[i].ip_prefix, subnets_[i].plen, NULL);
        } else if (subnets_[i].ip_prefix.is_v6()) {
            static_cast<InetUnicastAgentRouteTable *>(vrf->
                GetInet6UnicastRouteTable())->DeleteReq
                (agent()->fabric_rt_export_peer(), vrf->GetName(),
                 subnets_[i].ip_prefix, subnets_[i].plen, NULL);
        }
    }
}

void VRouter::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddVirtualRouterNode(node);
    return;
}

bool VRouter::IsSubnetMember(const IpAddress &addr) const {
    bool v4 = false;
    if (addr.is_v4()) {
        v4 = true;
    }
    for (unsigned int i = 0; i < subnets_.size(); ++i) {
        if (v4 && subnets_[i].ip_prefix.is_v4()) {
            if (IsIp4SubnetMember(addr.to_v4(), subnets_[i].ip_prefix.to_v4(),
                                  subnets_[i].plen)) {
                return true;
            }
        } else if (!v4 && subnets_[i].ip_prefix.is_v6()) {
            if (IsIp6SubnetMember(addr.to_v6(), subnets_[i].ip_prefix.to_v6(),
                                  subnets_[i].plen)) {
                return true;
            }
        }
    }
    return false;
}

void VRouter::Shutdown() {
    ClearSubnets();
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void VRouter::FillSandeshInfo(VrouterInfoResp *resp) {
    resp->set_name(name_);
    resp->set_display_name(display_name_);
    vector<VnIpamData> list;
    for (unsigned int i = 0; i < subnets_.size(); ++i) {
        VnIpamData item;
        item.set_ip_prefix(subnets_[i].ip_prefix.to_string());
        item.set_prefix_len(subnets_[i].plen);
        list.push_back(item);
    }
    resp->set_subnet_list(list);
}

void VrouterInfoReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    VRouter *vr = agent->oper_db()->vrouter();
    VrouterInfoResp *resp = new VrouterInfoResp();
    resp->set_context(context());
    if (!vr) {
        resp->set_more(false);
        resp->Response();
        return;
    }
    vr->FillSandeshInfo(resp);
    resp->set_more(false);
    resp->Response();
    return;

}
