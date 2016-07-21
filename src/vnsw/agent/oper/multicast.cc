/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <oper/mpls.h>
#include <controller/controller_init.h>
#include <controller/controller_route_path.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

using namespace std;
using namespace boost::uuids;

#define INVALID_PEER_IDENTIFIER ControllerPeerPath::kInvalidPeerIdentifier

MulticastHandler *MulticastHandler::obj_;
SandeshTraceBufferPtr MulticastTraceBuf(SandeshTraceBufferCreate("Multicast",
                                                                     1000));

/*
 * Registeration for notification
 * VM - Looking for local VM added 
 * VN - Looking for subnet information from VN 
 * Enable trace print messages
 */
void MulticastHandler::Register() {
    vn_listener_id_ = agent_->vn_table()->Register(
        boost::bind(&MulticastHandler::ModifyVN, this, _1, _2));
    interface_listener_id_ = agent_->interface_table()->Register(
        boost::bind(&MulticastHandler::ModifyVmInterface, this, _1, _2));

    GetMulticastObjList().clear();
}

void MulticastHandler::Terminate() {
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
}

void MulticastHandler::AddL2BroadcastRoute(MulticastGroupObject *obj,
                                           const string &vrf_name,
                                           const string &vn_name,
                                           const Ip4Address &addr,
                                           uint32_t label,
                                           int vxlan_id,
                                           uint32_t ethernet_tag)
{
    boost::system::error_code ec;
    MCTRACE(Log, "add L2 bcast route ", vrf_name, addr.to_string(), 0);
    //Add Bridge FF:FF:FF:FF:FF:FF
    ComponentNHKeyList component_nh_key_list =
        GetInterfaceComponentNHKeyList(obj, InterfaceNHFlags::BRIDGE);
    if (component_nh_key_list.size() == 0)
        return;
    uint32_t route_tunnel_bmap = TunnelType::AllType();
    AgentRouteData *data =
        BridgeAgentRouteTable::BuildNonBgpPeerData(vrf_name,
                                                   vn_name,
                                                   label,
                                                   vxlan_id,
                                                   route_tunnel_bmap,
                                                   Composite::L2INTERFACE,
                                                   component_nh_key_list);
    BridgeAgentRouteTable::AddBridgeBroadcastRoute(agent_->local_vm_peer(),
                                                   vrf_name,
                                                   ethernet_tag,
                                                   data);
}

/*
 * Route address 255.255.255.255 deletion from last VM in VN del
 */
void MulticastHandler::DeleteBroadcast(const Peer *peer,
                                       const std::string &vrf_name,
                                       uint32_t ethernet_tag,
                                       COMPOSITETYPE type)
{
    boost::system::error_code ec;
    MCTRACE(Log, "delete bcast route ", vrf_name, "255.255.255.255", 0);
    BridgeAgentRouteTable::DeleteBroadcastReq(peer, vrf_name, ethernet_tag,
                                              type);
    ComponentNHKeyList component_nh_key_list; //dummy list
}

void MulticastHandler::HandleVxLanChange(const VnEntry *vn) {
    if (vn->IsDeleted() || !vn->GetVrf()) 
        return;

    MulticastGroupObject *obj =
        FindFloodGroupObject(vn->GetVrf()->GetName());
    if (!obj || obj->IsDeleted())
        return;

    uint32_t new_vxlan_id = vn->GetVxLanId();

    if (new_vxlan_id != obj->vxlan_id()) {
        boost::system::error_code ec;
        Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                       ec).to_v4();
        obj->set_vxlan_id(new_vxlan_id);
        //Rebake new vxlan id in mcast route
        AddL2BroadcastRoute(obj, vn->GetVrf()->GetName(), vn->GetName(),
                            broadcast, MplsTable::kInvalidLabel, new_vxlan_id, 0);
    }
}

void MulticastHandler::HandleVnParametersChange(DBTablePartBase *partition,
                                             DBEntryBase *e) {
    VnEntry *vn = static_cast<VnEntry *>(e);
    bool deleted = false;

    //Extract paramters from VN.
    const VrfEntry *vrf = vn->GetVrf();
    uint32_t vn_vxlan_id = vn->GetVxLanId();

    MulticastDBState *state = static_cast<MulticastDBState *>
        (vn->GetState(partition->parent(), vn_listener_id_));

    deleted = vn->IsDeleted() || !(vrf);
    //Extract old parameters from state
    uint32_t old_vxlan_id = state ? state->vxlan_id_ : 0;

    boost::system::error_code ec;
    Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                   ec).to_v4();
    //Add operation
    if (!deleted) {
        MulticastGroupObject *all_broadcast =
            FindFloodGroupObject(vn->GetVrf()->GetName());

        if (!state) {
            state = new MulticastDBState(vn->GetVrf()->GetName(),
                                         vn_vxlan_id);
            vn->SetState(partition->parent(),
                         vn_listener_id_,
                         state);
            //Also create multicast object
            if (all_broadcast == NULL) {
                all_broadcast = CreateMulticastGroupObject(state->vrf_name_, broadcast,
                                                           vn, state->vxlan_id_);
            }
            all_broadcast->set_vn(vn);
        } else {
            if (old_vxlan_id != vn_vxlan_id) {
                state->vxlan_id_ = vn_vxlan_id;
                if (all_broadcast) {
                    all_broadcast->set_vxlan_id(state->vxlan_id_);
                }
            }
        }
        ComponentNHKeyList component_nh_key_list;
        AgentRouteData *data =
            BridgeAgentRouteTable::BuildNonBgpPeerData(state->vrf_name_,
                                                       vn->GetName(),
                                                       0,
                                                       state->vxlan_id_,
                                                       TunnelType::VxlanType(),
                                                       Composite::L2COMP,
                                                       component_nh_key_list);
        BridgeAgentRouteTable::AddBridgeBroadcastRoute(agent_->local_peer(),
                                                       state->vrf_name_,
                                                       state->vxlan_id_,
                                                       data);
        Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                       ec).to_v4();
        AddL2BroadcastRoute(all_broadcast, state->vrf_name_, vn->GetName(),
                            broadcast, MplsTable::kInvalidLabel,
                            state->vxlan_id_, 0);
    }

    //Delete or withdraw old vxlan id
    if (deleted) {
        if (!state)
            return;

        MulticastGroupObject *all_broadcast =
            FindFloodGroupObject(state->vrf_name_);
        if (all_broadcast) {
            all_broadcast->reset_vn();
        }

        DeleteMulticastObject(state->vrf_name_, broadcast);
        BridgeAgentRouteTable::DeleteBroadcastReq(agent_->local_peer(),
                                                  state->vrf_name_,
                                                  old_vxlan_id,
                                                  Composite::L2COMP);

        vn->ClearState(partition->parent(), vn_listener_id_);
        delete state;
    }
}

/* Regsitered call for VN */
void MulticastHandler::ModifyVN(DBTablePartBase *partition, DBEntryBase *e)
{
    const VnEntry *vn = static_cast<const VnEntry *>(e);

    HandleIpam(vn);
    HandleVnParametersChange(partition, e);
}

bool MulticastGroupObject::CanBeDeleted() const {
    if ((local_olist_.size() == 0) && (vn_.get() == NULL))
        return true;
    return false;
}

MulticastGroupObject *MulticastHandler::CreateMulticastGroupObject
(const string &vrf_name, const Ip4Address &ip_addr, const VnEntry *vn,
 uint32_t vxlan_id) {
    MulticastGroupObject *obj =
        new MulticastGroupObject(vrf_name, ip_addr, vn->GetName());
    obj->set_vxlan_id(vxlan_id);
    AddToMulticastObjList(obj);
    return obj;
}

void MulticastHandler::HandleIpam(const VnEntry *vn) {
    const uuid &vn_uuid = vn->GetUuid();
    const std::vector<VnIpam> &ipam = vn->GetVnIpam();
    bool delete_ipam = false;
    std::map<uuid, string>::iterator it;
    string vrf_name;

    if (!(vn->layer3_forwarding()) || vn->IsDeleted() || (ipam.size() == 0) ||
        (vn->GetVrf() == NULL)) {
        delete_ipam = true;
    }

    it = vn_vrf_mapping_.find(vn_uuid);
    if (it != vn_vrf_mapping_.end()) {
        vrf_name = it->second;
        vrf_ipam_mapping_.erase(vrf_name);
        if (delete_ipam) {
            vn_vrf_mapping_.erase(vn_uuid);
            return;
        }
    } else {
        if (delete_ipam == false) {
            vrf_name = vn->GetVrf()->GetName();
            vn_vrf_mapping_.insert(std::pair<uuid, string>(vn_uuid, vrf_name));
        }
    }

    if (delete_ipam)
        return;

    vrf_ipam_mapping_.insert(std::pair<string, std::vector<VnIpam> >(vrf_name,
                                                                     ipam));
}

/* Registered call for VM */
void MulticastHandler::ModifyVmInterface(DBTablePartBase *partition, 
                                         DBEntryBase *e)
{
    const Interface *intf = static_cast<const Interface *>(e);
    const VmInterface *vm_itf;

    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    vm_itf = static_cast<const VmInterface *>(intf);
    if (vm_itf->device_type() == VmInterface::TOR) {
        //Ignore TOR VMI, they are not active VMI.
        return;
    }

    if (intf->IsDeleted() || ((vm_itf->l2_active() == false) &&
                              (vm_itf->ipv4_active() == false) &&
                              (vm_itf->ipv6_active() == false))) {
        DeleteVmInterface(intf);
        return;
    }
    assert(vm_itf->vn() != NULL);

    AddVmInterfaceInFloodGroup(vm_itf);
    return;
}

/*
 * Delete VM interface
 * Traverse the multicast obj list of which this VM is a member.
 * Delete the VM from them and check if local VM list size is zero.
 * If it is zero then delete the route and mpls.
 */
void MulticastHandler::DeleteVmInterface(const Interface *intf)
{
    const VmInterface *vm_itf = static_cast<const VmInterface *>(intf);
    std::list<MulticastGroupObject *> &obj_list = this->GetVmToMulticastObjMap(
                                                          vm_itf->GetUuid());
    for (std::list<MulticastGroupObject *>::iterator it = obj_list.begin(); 
         it != obj_list.end(); it++) {
        // When IPAM/VN goes off first than VM then it marks mc obj 
        // for deletion. Cleanup of related data structures like vm-mcobj
        // happens when VM goes off. So dont trigger any notify at same time.
        // However if all local VMs are gone then route will be deleted only
        // when VN/IPAM goes off. At that time notify xmpp to unsubscribe 
        //Deletelocalmember removes vm from mc obj 
        if (((*it)->DeleteLocalMember(vm_itf->GetUuid()) == true) && 
            ((*it)->IsDeleted() == false) &&
            ((*it)->GetLocalListSize() != 0)) {
            TriggerLocalRouteChange(*it, agent_->local_vm_peer());
            MCTRACE(Log, "modify route, vm is deleted ", (*it)->vrf_name(),
                    (*it)->GetGroupAddress().to_string(), 0);
        }

        if((*it)->GetLocalListSize() == 0) {
            MCTRACE(Info, "Del route for multicast address",
                    vm_itf->primary_ip_addr().to_string());
            //Time to delete route(for mcast address) and mpls
            DeleteBroadcast(agent_->local_vm_peer(),
                            (*it)->vrf_name_, 0, Composite::L2INTERFACE);
            /* delete mcast object */
            // TODO : delete only when all creators are gone
            DeleteMulticastObject((*it)->vrf_name_, (*it)->grp_address_);
        }
    }
    vm_to_mcobj_list_[vm_itf->GetUuid()].clear();
    DeleteVmToMulticastObjMap(vm_itf->GetUuid());
    MCTRACE(Info, "Del vm notify done ", vm_itf->primary_ip_addr().to_string());
}

//Delete multicast object for vrf/G
void MulticastHandler::DeleteMulticastObject(const std::string &vrf_name,
                                             const Ip4Address &grp_addr) {
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin(); 
        it != this->GetMulticastObjList().end(); it++) {

        if (((*it)->vrf_name() == vrf_name) &&
            ((*it)->GetGroupAddress() == grp_addr)) {
            if (!((*it)->CanBeDeleted()))
                return;
            MCTRACE(Log, "delete obj  vrf/grp/size ", vrf_name,
                    grp_addr.to_string(),
                    this->GetMulticastObjList().size());
            delete (*it);
            this->GetMulticastObjList().erase(it++);
            break;
        }
    }
}

MulticastGroupObject *MulticastHandler::FindFloodGroupObject(const std::string &vrf_name) {
    boost::system::error_code ec;
    Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                   ec).to_v4();
    return (FindGroupObject(vrf_name, broadcast));
}

//Helper to find object for VRF/G
MulticastGroupObject *MulticastHandler::FindGroupObject(const std::string &vrf_name, 
                                                        const Ip4Address &dip) {
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin(); 
        it != this->GetMulticastObjList().end(); it++) {
        if (((*it)->vrf_name() == vrf_name) &&
            ((*it)->GetGroupAddress() == dip)) {
            return (*it);
        }
    }
    MCTRACE(Log, "mcast obj size ", vrf_name, dip.to_string(),
            this->GetMulticastObjList().size());
    return NULL;
}

MulticastGroupObject *MulticastHandler::FindActiveGroupObject(
                                                    const std::string &vrf_name,
                                                    const Ip4Address &dip) {
    MulticastGroupObject *obj = FindGroupObject(vrf_name, dip);
    if ((obj == NULL) || obj->IsDeleted()) {
        MCTRACE(Log, "Multicast object deleted ", vrf_name, 
                dip.to_string(), 0);
        return NULL;
    }

    return obj;
}

void MulticastGroupObject::set_vn(const VnEntry *vn) {
    vn_.reset(vn);
}

void MulticastGroupObject::reset_vn() {
    vn_.reset();
}

ComponentNHKeyList
MulticastHandler::GetInterfaceComponentNHKeyList(MulticastGroupObject *obj,
                                                 uint8_t interface_flags) {
    ComponentNHKeyList component_nh_key_list;
    for (std::list<uuid>::const_iterator it = obj->GetLocalOlist().begin();
            it != obj->GetLocalOlist().end(); it++) {
        ComponentNHKeyPtr component_nh_key(new ComponentNHKey(0, (*it),
                                                        interface_flags,
                                                        MacAddress::ZeroMac()));
        component_nh_key_list.push_back(component_nh_key);
    }
    return component_nh_key_list;
}

void MulticastHandler::TriggerLocalRouteChange(MulticastGroupObject *obj,
                                          const Peer *peer) {
    DBRequest req;
    ComponentNHKeyList component_nh_key_list;

    component_nh_key_list =
        GetInterfaceComponentNHKeyList(obj, InterfaceNHFlags::BRIDGE);
    MCTRACE(Log, "enqueue route change with local peer",
            obj->vrf_name(),
            obj->GetGroupAddress().to_string(),
            component_nh_key_list.size());
    //Add Bridge FF:FF:FF:FF:FF:FF, local_vm_peer
    uint32_t route_tunnel_bmap = TunnelType::AllType();
    AgentRouteData *data =
        BridgeAgentRouteTable::BuildNonBgpPeerData(obj->vrf_name(),
                                                   obj->GetVnName(),
                                                   MplsTable::kInvalidLabel,
                                                   obj->vxlan_id(),
                                                   route_tunnel_bmap,
                                                   Composite::L2INTERFACE,
                                                   component_nh_key_list);
    BridgeAgentRouteTable::AddBridgeBroadcastRoute(peer,
                                                   obj->vrf_name(),
                                                   0,
                                                   data);
}

void MulticastHandler::TriggerRemoteRouteChange(MulticastGroupObject *obj,
                                                const Peer *peer,
                                                const string &vrf_name,
                                                const TunnelOlist &olist,
                                                uint64_t peer_identifier,
                                                bool delete_op,
                                                COMPOSITETYPE comp_type,
                                                uint32_t label,
                                                bool fabric,
                                                uint32_t ethernet_tag) {
    uint64_t obj_peer_identifier = obj ? obj->peer_identifier()
        : ControllerPeerPath::kInvalidPeerIdentifier;

    // Peer identifier cases
    // if its a delete operation -
    // 1) Internal delete (invalid peer identifier), dont update peer identifier
    // as it is a forced removal.
    // 2) Control node removing stales i.e. delete if local peer identifier is
    // less than global peer identifier.
    //
    // if its not a delete operation -
    // 1) Update only if local peer identifier is less than or equal to sent
    // global peer identifier.

    // if its internal delete then peer_identifier will be 0xFFFFFFFF;
    // if external delete(via control node) then its stale cleanup so delete
    // only when local peer identifier is less than global multicast sequence.
    if (delete_op) {
        if ((peer_identifier != ControllerPeerPath::kInvalidPeerIdentifier) &&
            (peer_identifier <= obj_peer_identifier))
            return;

        // After resetting tunnel and mpls label return if it was a delete call,
        // dont update peer_identifier. Let it get updated via update operation only
        MCTRACE(Log, "delete bcast path from remote peer", vrf_name,
                "255.255.255.255", 0);
        BridgeAgentRouteTable::DeleteBroadcastReq(peer, vrf_name,
                                                  ethernet_tag, comp_type);
        ComponentNHKeyList component_nh_key_list; //dummy list
        return;
    }

    // - Update operation with lower sequence number sent compared to
    // local identifier, ignore
    if ((peer_identifier < obj_peer_identifier) &&
        (comp_type != Composite::TOR)) {
        return;
    }

    // Ideally wrong update call
    if (peer_identifier == INVALID_PEER_IDENTIFIER) {
        MCTRACE(Log, "Invalid peer identifier sent for modification",
                obj->vrf_name(), "255.255.255.255", 0);
        return;
    }

    obj->set_peer_identifier(peer_identifier);
    ComponentNHKeyList component_nh_key_list;

    uint32_t route_tunnel_bmap = TunnelType::AllType();
    for (TunnelOlist::const_iterator it = olist.begin();
         it != olist.end(); it++) {
        TunnelNHKey *key =
            new TunnelNHKey(agent_->fabric_vrf_name(),
                            agent_->router_id(),
                            it->daddr_, false,
                            TunnelType::ComputeType(it->tunnel_bmap_));
        TunnelNHData *tnh_data = new TunnelNHData();
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(tnh_data);
        agent_->nexthop_table()->Enqueue(&req);

        MCTRACE(Log, "Enqueue add TOR TUNNEL ",
                agent_->fabric_vrf_name(),
                it->daddr_.to_string(), it->label_);

        ComponentNHKeyPtr component_key_ptr(new ComponentNHKey(it->label_,
                    agent_->fabric_vrf_name(),
                    agent_->router_id(), it->daddr_,
                    false, it->tunnel_bmap_));
        component_nh_key_list.push_back(component_key_ptr);
        route_tunnel_bmap = it->tunnel_bmap_;
    }

    MCTRACE(Log, "enqueue route change with remote peer",
            obj->vrf_name(),
            obj->GetGroupAddress().to_string(),
            component_nh_key_list.size());

    //Add Bridge FF:FF:FF:FF:FF:FF$
    if (comp_type == Composite::TOR)
        route_tunnel_bmap = TunnelType::VxlanType();
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    AgentRouteData *data = NULL;
    if (bgp_peer) {
        data = BridgeAgentRouteTable::BuildBgpPeerData(peer,
                                                       obj->vrf_name(),
                                                       obj->GetVnName(),
                                                       label,
                                                       obj->vxlan_id(),
                                                       ethernet_tag,
                                                       route_tunnel_bmap,
                                                       comp_type,
                                                       component_nh_key_list);
    } else {
        data = BridgeAgentRouteTable::BuildNonBgpPeerData(obj->vrf_name(),
                                                          obj->GetVnName(),
                                                          label,
                                                          obj->vxlan_id(),
                                                          route_tunnel_bmap,
                                                          comp_type,
                                                          component_nh_key_list);
    }
    BridgeAgentRouteTable::AddBridgeBroadcastRoute(peer,
                                                   obj->vrf_name(),
                                                   ethernet_tag,
                                                   data);
    MCTRACE(Log, "rebake subnet peer for subnet", vrf_name,
            "255.255.255.255", comp_type);
}

void MulticastHandler::AddVmInterfaceInFloodGroup(const VmInterface *vm_itf) {
    const string vrf_name = vm_itf->vrf()->GetName();
    const uuid intf_uuid = vm_itf->GetUuid();
    const VnEntry *vn = vm_itf->vn();
    MulticastGroupObject *all_broadcast = NULL;
    boost::system::error_code ec;
    Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                   ec).to_v4();
    bool add_route = false;
    std::string vn_name = vn->GetName();

    //TODO Push every thing via multi proto and remove multi proto check 
    //All broadcast addition 255.255.255.255
    all_broadcast = this->FindGroupObject(vrf_name, broadcast);
    if (all_broadcast == NULL) {
        all_broadcast = CreateMulticastGroupObject(vrf_name, broadcast, 
                                                   vn, vn->GetVxLanId());
        add_route = true;
    }

    //Modify Nexthops
    if (all_broadcast->AddLocalMember(intf_uuid) == true) {
        TriggerLocalRouteChange(all_broadcast, agent_->local_vm_peer());
        AddVmToMulticastObjMap(intf_uuid, all_broadcast);
    }
    //Modify routes
    if (add_route) {
        if (TunnelType::ComputeType(TunnelType::AllType()) ==
            TunnelType::VXLAN) {
            all_broadcast->set_vxlan_id(vn->GetVxLanId());
        } 
        TriggerLocalRouteChange(all_broadcast, agent_->local_vm_peer());
    }
}

/*
 * Static funtion to be called to handle XMPP info from ctrl node
 * Key is VRF/G/S
 * Info has label (for source to vrouter) and
 * OLIST of NH (server IP + label for that server)
 */
void MulticastHandler::ModifyFabricMembers(const Peer *peer,
                                           const std::string &vrf_name,
                                           const Ip4Address &grp,
                                           const Ip4Address &src,
                                           uint32_t label,
                                           const TunnelOlist &olist,
                                           uint64_t peer_identifier)
{
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name, grp);
    MCTRACE(Log, "XMPP call(edge replicate) multicast handler ", vrf_name,
            grp.to_string(), label);

    bool delete_op = false;

    //Invalid peer identifier signifies delete.
    //If add operation, obj should exist, else return.
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer,
                             vrf_name, olist, peer_identifier,
                             delete_op, Composite::FABRIC,
                             label, true, 0);
    MCTRACE(Log, "Add fabric grp label ", vrf_name, grp.to_string(), label);
}

/*
 * Request to populate evpn olist by list of TOR NH seen by control node
 * Currently this is done only for TOR/Gateway(outside contrail vrouter network)
 * Source label is ignored as it is used by non-vrouters.
 * Olist consists of TOR/Gateway endpoints with label advertised or use VXLAN.
 * Note: Non Vrouter can talk in VXLAN/MPLS. Encap received in XMPP will
 * convey the same.
 */
void MulticastHandler::ModifyEvpnMembers(const Peer *peer,
                                         const std::string &vrf_name,
                                         const TunnelOlist &olist,
                                         uint32_t ethernet_tag,
                                         uint64_t peer_identifier)
{
    boost::system::error_code ec;
    Ip4Address grp = Ip4Address::from_string("255.255.255.255", ec);
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name, grp);
    MCTRACE(Log, "XMPP call(EVPN) multicast handler ", vrf_name,
            grp.to_string(), 0);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer, vrf_name, olist,
                             peer_identifier, delete_op, Composite::EVPN,
                             MplsTable::kInvalidLabel, false, ethernet_tag);
    MCTRACE(Log, "Add EVPN TOR Olist ", vrf_name, grp.to_string(), 0);
}

void MulticastHandler::ModifyTorMembers(const Peer *peer,
                                        const std::string &vrf_name,
                                        const TunnelOlist &olist,
                                        uint32_t ethernet_tag,
                                        uint64_t peer_identifier)
{
    boost::system::error_code ec;

    Ip4Address grp = Ip4Address::from_string("255.255.255.255", ec);
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name, grp);
    MCTRACE(Log, "TOR multicast handler ", vrf_name, grp.to_string(), 0);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer, vrf_name, olist,
                             peer_identifier, delete_op, Composite::TOR,
                             MplsTable::kInvalidLabel, false, ethernet_tag);
    MCTRACE(Log, "Add external TOR Olist ", vrf_name, grp.to_string(), 0);
}

// Helper to delete fabric nh
// For internal delete it uses invalid identifier. 
// For delete via control node it uses the sequence sent.
void MulticastGroupObject::FlushAllPeerInfo(const Agent *agent,
                                            const Peer *peer,
                                            uint64_t peer_identifier) {
    if ((peer_identifier != peer_identifier_) ||
        (peer_identifier == INVALID_PEER_IDENTIFIER)) {
        agent->oper_db()->multicast()->DeleteBroadcast(peer, vrf_name_, 0,
                                                       Composite::FABRIC);
    }
    MCTRACE(Log, "Delete broadcast route", vrf_name_,
            grp_address_.to_string(), 0);
}

MulticastHandler::MulticastHandler(Agent *agent)
        : agent_(agent),
          vn_listener_id_(DBTable::kInvalidId),
          interface_listener_id_(DBTable::kInvalidId) { 
    obj_ = this; 
}

bool MulticastHandler::FlushPeerInfo(uint64_t peer_sequence) {
    for (std::set<MulticastGroupObject *>::iterator it = 
         GetMulticastObjList().begin(); it != GetMulticastObjList().end(); 
         it++) {
        //Delete all control node given paths
        (*it)->FlushAllPeerInfo(agent_, agent_->multicast_tree_builder_peer(),
                                peer_sequence);
    }
    return false;
}

/*
 * Shutdown for clearing all stuff related to multicast
 */
void MulticastHandler::Shutdown() {
    //Delete all route mpls and trigger cnh change
    for (std::set<MulticastGroupObject *>::iterator it =
         GetMulticastObjList().begin(); it != GetMulticastObjList().end();
         it++) {
        MulticastGroupObject *obj = (*it);
        BridgeAgentRouteTable *bridge_table =
            static_cast<BridgeAgentRouteTable *>
            (agent_->vrf_table()->GetBridgeRouteTable(obj->vrf_name()));
        AgentRoute *route =
            bridge_table->FindRoute(MacAddress::BroadcastMac());
        if (route == NULL)
            return;

        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            //Delete the tunnel OLIST
            (obj)->FlushAllPeerInfo(agent_,
                                    path->peer(),
                                    INVALID_PEER_IDENTIFIER);
        }
        //Delete the multicast object
        delete (obj);
    }
}

