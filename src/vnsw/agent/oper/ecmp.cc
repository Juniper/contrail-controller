/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <base/address_util.h>
#include <base/task_annotations.h>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/ecmp.h>
#include <oper/ecmp_load_balance.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

EcmpData::EcmpData(Agent *agent,
                   const string &vrf_name,
                   const string &route_str,
                   AgentPath *path,
                   bool del) :
    path_(path), ecmp_path_(NULL), delete_(del),
    alloc_label_(true), label_(MplsTable::kInvalidLabel),
    vrf_name_(vrf_name), route_str_(route_str),
    vn_list_(path->dest_vn_list()),
    sg_list_(path->sg_list()),
    tag_list_(path->tag_list()),
    community_list_(path->communities()),
    path_preference_(path->path_preference()),
    tunnel_bmap_(path->tunnel_bmap()),
    ecmp_load_balance_(path->ecmp_load_balance()),
    nh_req_(), agent_(agent) {
}

bool EcmpData::Update(AgentRoute *rt) {
    if (path_->peer() == NULL) {
        return false;
    }

    if (path_->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
        return LocalVmPortPeerEcmp(rt);
    }

    if (path_->peer()->GetType() == Peer::BGP_PEER) {
        alloc_label_ = false;
        return BgpPeerEcmp();
    }

    return false;
}

bool EcmpData::UpdateWithParams(const SecurityGroupList &sg_list,
                                const TagList &tag_list,
                                const CommunityList &community_list,
                                const PathPreference &path_preference,
                                const TunnelType::TypeBmap bmap,
                                const EcmpLoadBalance &ecmp_load_balance,
                                const VnListType &vn_list,
                                DBRequest &nh_req) {
    sg_list_ = sg_list;
    vn_list_ = vn_list;
    tag_list_ = tag_list;
    community_list_ = community_list;
    path_preference_ = path_preference;
    tunnel_bmap_ = bmap;
    ecmp_load_balance_ = ecmp_load_balance;
    nh_req_.Swap(&nh_req);

    return Update(NULL);
}

bool EcmpData::LocalVmPortPeerEcmp(AgentRoute *rt) {
    ecmp_path_ = rt->FindPath(agent_->ecmp_peer());
    if (delete_) {
        return EcmpDeletePath(rt);
    } else {
        if (path_->path_preference().is_ecmp() == false) {
            return false;
        }
        return EcmpAddPath(rt);
    }
}

bool EcmpData::BgpPeerEcmp() {
    ecmp_path_ = path_;
    //Bgp peer update for ecmp should always accompany nh_req.
    //Any other request like sync is to be ignored.
    NextHopKey *key = static_cast<NextHopKey *>(nh_req_.key.get());
    if (!key)
        return false;
    return ModifyEcmpPath();
}

// Handle add/update of a path in route.
// If there are more than one path of type LOCAL_VM_PORT_PEER, creates/updates
// Composite-NH for them
bool EcmpData::EcmpAddPath(AgentRoute *rt) {
    if (path_->tunnel_bmap() & TunnelType::NativeType()) {
        path_->set_tunnel_bmap(TunnelType::MplsType() |
                               TunnelType::NativeType());
    } else {
        path_->set_tunnel_bmap(TunnelType::MplsType());
    }

    // Count number of paths from LOCAL_VM_PORT_PEER already present
    const AgentPath *vm_port_path = NULL;
    int count = 0;
    for(Route::PathList::const_iterator it = rt->GetPathList().begin();
        it != rt->GetPathList().end(); it++) {
        const AgentPath *it_path =
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() == agent_->ecmp_peer())
            assert(ecmp_path_ == it_path);

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER &&
            it_path->path_preference().is_ecmp() == true) {
            count++;
            if (it_path != path_)
                vm_port_path = it_path;
        }
    }

    if (count == 0) {
        return false;
    }

    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count > 2) {
        assert(ecmp_path_ != NULL);
    }

    if (count == 1) {
        assert(ecmp_path_ == NULL);
        return false;
    }

    bool ret = false;
    if (count == 2 && ecmp_path_ == NULL) {
        // This is second path being added, make ECMP
        AllocateEcmpPath(rt, vm_port_path);
        ret = true;
    } else if (count > 2) {
        // ECMP already present, add/update Component-NH for the path
        AppendEcmpPath(rt, path_);
        ret = true;
    } else if (ecmp_path_) {
        bool updated = UpdateComponentNH(rt, path_);
        //No update happened for component NH, so verify if params are to be
        //synced. If update of component NH is done, then params would also have
        //been updated, so no need to do it again.
        if (!updated) {
            updated = SyncParams();
        }
        if (updated) {
            ret = true;
        }
    }

    return ret;
}

// Function to create a ECMP path from path and path2
// Creates Composite-NH with 2 Component-NH (one for each of path and path2)
// Creates a new MPLS Label for the ECMP path
void EcmpData::AllocateEcmpPath(AgentRoute *rt, const AgentPath *path2) {
    // Allocate and insert a path
    ecmp_path_ = new AgentPath(agent_->ecmp_peer(), rt);
    rt->InsertPath(ecmp_path_);

    const NextHop* path1_nh = path_->ComputeNextHop(agent_);
    bool composite_nh_policy = path1_nh->NexthopToInterfacePolicy();

    // Create Component NH to be added to ECMP path
    DBEntryBase::KeyPtr key1 = path1_nh->GetDBRequestKey();
    NextHopKey *nh_key1 = static_cast<NextHopKey *>(key1.release());
    std::auto_ptr<const NextHopKey> nh_akey1(nh_key1);
    nh_key1->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data1(new ComponentNHKey(path_->label(),
                                                            nh_akey1));

    const NextHop* path2_nh = path2->ComputeNextHop(agent_);
    if (!composite_nh_policy) {
        composite_nh_policy = path2_nh->NexthopToInterfacePolicy();
    }
    DBEntryBase::KeyPtr key2 = path2_nh->GetDBRequestKey();
    NextHopKey *nh_key2 = static_cast<NextHopKey *>(key2.release());
    std::auto_ptr<const NextHopKey> nh_akey2(nh_key2);
    nh_key2->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data2(new ComponentNHKey(path2->label(),
                                                            nh_akey2));

    ComponentNHKeyList component_nh_list;
    component_nh_list.push_back(component_nh_data2);
    component_nh_list.push_back(component_nh_data1);

    // Directly call AddChangePath to update NH in the ECMP path
    // It will also create CompositeNH if necessary
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy, component_nh_list,
                                        vrf_name_));
    nh_req.data.reset(new CompositeNHData());
    nh_req_.Swap(&nh_req);

    label_ = MplsTable::kInvalidLabel;
    ModifyEcmpPath();

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, ecmp_path_);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path Add", rt->ToString(), vrf_name_,
                    GETPEERNAME(agent_->ecmp_peer()));
}

void EcmpData::AppendEcmpPath(AgentRoute *rt, AgentPath *path) {
    assert(ecmp_path_);
    const NextHop* path_nh = path->ComputeNextHop(agent_);
    DBEntryBase::KeyPtr key = path_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path_->ComputeNextHop(agent_));
    bool composite_nh_policy = false;
    component_nh_key_list = comp_nh->AddComponentNHKey(comp_nh_key_ptr,
                                                       composite_nh_policy);
    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy,
                                        component_nh_key_list,
                                        vrf_name_));
    nh_req.data.reset(new CompositeNHData());
    nh_req_.Swap(&nh_req);

    label_ = ecmp_path_->label();
    ModifyEcmpPath();

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path change", rt->ToString(), vrf_name_,
                    GETPEERNAME(agent_->ecmp_peer()));
}

// Handle deletion of a path in route. If the path being deleted is part of
// ECMP, then deletes the Component-NH for the path.
// Delete ECMP path if there is single Component-NH in Composite-NH
bool EcmpData::EcmpDeletePath(AgentRoute *rt) {
    if (path_->peer() == NULL) {
        return false;
    }

    if (path_->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
        return false;
    }

    // Composite-NH is made from LOCAL_VM_PORT_PEER, count number of paths
    // with LOCAL_VM_PORT_PEER
    int count = 0;
    for(Route::PathList::const_iterator it = rt->GetPathList().begin();
        it != rt->GetPathList().end(); it++) {
        const AgentPath *it_path =
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER &&
            it_path->path_preference().is_ecmp() == true &&
            it_path != path_)
            count++;
    }

    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count >= 1) {
        if (ecmp_path_ == NULL) {
            return false;
        }
    }

    if (count == 1 && ecmp_path_) {
        // There is single path of type LOCAL_VM_PORT_PEER. Delete the ECMP path
        rt->RemovePath(ecmp_path_);
        //Enqueue MPLS label delete request
        agent_->mpls_table()->FreeLabel(ecmp_path_->label());
        delete ecmp_path_;
    } else if (count > 1) {
        // Remove Component-NH for the path being deleted
        DeleteComponentNH(rt, path_);
    }

    return true;
}

bool EcmpData::UpdateNh() {
    NextHop *nh = NULL;
    bool ret = false;

    agent_->nexthop_table()->Process(nh_req_);
    NextHopKey *key = static_cast<NextHopKey *>(nh_req_.key.get());
    // Create MPLS label and point it to Composite NH
    if (alloc_label_) {
        label_ = agent_->mpls_table()->CreateRouteLabel(label_, key, vrf_name_,
                                                        route_str_);
    }
    nh = static_cast<NextHop *>(agent_->nexthop_table()->
                                FindActiveEntry(nh_req_.key.get()));
    if (nh == NULL) {
        VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(vrf_name_);
        if (vrf->IsDeleted())
            return ret;
        assert(0);
    }

    MplsLabel *mpls = agent_->mpls_table()->
             FindMplsLabel(label_);
    if (mpls && (ecmp_path_->local_ecmp_mpls_label() == NULL)) {
        ecmp_path_->set_local_ecmp_mpls_label(mpls);
    }
    if (ecmp_path_->ChangeNH(agent_, nh) == true)
        ret = true;

    return ret;
}

bool EcmpData::SyncParams() {
    bool ret = false;

    ecmp_path_->set_tunnel_bmap(tunnel_bmap_);
    TunnelType::Type new_tunnel_type =
        TunnelType::ComputeType(tunnel_bmap_);
    if (ecmp_path_->tunnel_type() != new_tunnel_type) {
        ecmp_path_->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    if (ecmp_path_->dest_vn_list() != vn_list_) {
        ecmp_path_->set_dest_vn_list(vn_list_);
        ret = true;
    }

    if (ecmp_path_->sg_list() != sg_list_) {
        ecmp_path_->set_sg_list(sg_list_);
        ret = true;
    }

    if (ecmp_path_->tag_list() != tag_list_) {
        ecmp_path_->set_tag_list(tag_list_);
        ret = true;
    }

    if (ecmp_path_->communities() != community_list_) {
        ecmp_path_->set_communities(community_list_);
        ret = true;
    }

    if (path_preference_ != ecmp_path_->path_preference()) {
        ecmp_path_->set_path_preference(path_preference_);
        ret = true;
    }

    if (ecmp_path_->ecmp_load_balance() != ecmp_load_balance_) {
        ecmp_path_->set_ecmp_load_balance(ecmp_load_balance_);
        ret = true;
    }

    return ret;
}

bool EcmpData::ModifyEcmpPath() {
    bool ret = false;

    if (UpdateNh()) {
        ret = true;
    }

    if (ecmp_path_->label() != label_) {
        ecmp_path_->set_label(label_);
        ret = true;
    }

    if (SyncParams()) {
        ret = true;
    }

    ecmp_path_->set_unresolved(false);
    ret = true;

    return ret;
}

/* When label of VMI changes and if that VMI (ie VMI's InterfaceNH) is part of
 * ECMP, then update the CompositeNH for ECMP route to point to right label for
 * that VMI. Label of VMI can change when policy-status of VMI changes */
bool EcmpData::UpdateComponentNH(AgentRoute *rt, AgentPath *path) {
    if (!ecmp_path_) {
        return false;
    }
    //Build ComponentNHKey for new path
    const NextHop* path_nh = path->ComputeNextHop(agent_);
    DBEntryBase::KeyPtr key = path_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    nh_key->SetPolicy(false);

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path_->ComputeNextHop(agent_));
    bool composite_nh_policy = false;
    bool updated = comp_nh->UpdateComponentNHKey(path->label(), nh_key,
                                                 component_nh_key_list,
                                                 composite_nh_policy);

    if (!updated) {
        return false;
    }
    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy,
                                        component_nh_key_list,
                                        vrf_name_));
    nh_req.data.reset(new CompositeNHData());
    nh_req_.Swap(&nh_req);
    label_ = ecmp_path_->label();
    ModifyEcmpPath();

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path Update", rt->ToString(), vrf_name_,
                    GETPEERNAME(agent_->ecmp_peer()));
    return true;
}

void EcmpData::DeleteComponentNH(AgentRoute *rt, AgentPath *path) {
    assert(ecmp_path_);
    DBEntryBase::KeyPtr key = path->ComputeNextHop(agent_)->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    bool comp_nh_policy = false;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path_->ComputeNextHop(agent_));
    component_nh_key_list = comp_nh->DeleteComponentNHKey(comp_nh_key_ptr,
                                                          comp_nh_policy);

    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        comp_nh_policy, component_nh_key_list,
                                        vrf_name_));
    nh_req.data.reset(new CompositeNHData());
    nh_req_.Swap(&nh_req);
    label_ = ecmp_path_->label();
    UpdateNh();

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path change", rt->ToString(), vrf_name_,
                    GETPEERNAME(agent_->ecmp_peer()));
}

const NextHop* EcmpData::GetLocalNextHop(const AgentRoute *rt) {
    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *> (rt->get_table()))->agent();

    if (rt->FindPath(agent->ecmp_peer())) {
        return rt->FindPath(agent->ecmp_peer())->ComputeNextHop(agent);
    }

    //If a route is leaked, and it points to local composite nexthop
    //then choose that
    if (rt->GetActivePath()->local_ecmp_mpls_label()) {
        return rt->GetActivePath()->local_ecmp_mpls_label()->nexthop();
    }

    //Choose the first local vm peer path
    for (Route::PathList::const_iterator it = rt->GetPathList().begin();
            it != rt->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            if (path->peer() &&
                path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
                return path->ComputeNextHop(agent);
            }
        }
    }

    const NextHop *nh = rt->GetActiveNextHop();
    if (nh->GetType() == NextHop::COMPOSITE) {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //Get the local composite NH
        return comp_nh->GetLocalNextHop();
    }
    return NULL;
}
