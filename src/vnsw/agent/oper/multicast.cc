/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>

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
#include <oper/bridge_domain.h>
#include <oper/inet4_multicast_route.h>
#include <oper/physical_device.h>
#include <oper/agent_route_walker.h>
#include <oper/tsn_elector.h>
#include <xmpp/xmpp_channel.h>
#include <controller/controller_init.h>
#include <controller/controller_peer.h>
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
const Ip4Address MulticastHandler::kBroadcast = Ip4Address(0xFFFFFFFF);
/*
 * Registeration for notification
 * VM - Looking for local VM added
 * VN - Looking for subnet information from VN
 * Enable trace print messages
 */
void MulticastHandler::Register() {
    vn_listener_id_ = agent_->vn_table()->Register(
        boost::bind(&MulticastHandler::ModifyVN, this, _1, _2));
    vrf_listener_id_ = agent_->vrf_table()->Register(
        boost::bind(&MulticastHandler::ModifyVRF, this, _1, _2));
    interface_listener_id_ = agent_->interface_table()->Register(
        boost::bind(&MulticastHandler::ModifyVmInterface, this, _1, _2));
    bridge_domain_id_ = agent_->bridge_domain_table()->Register(
            boost::bind(&MulticastHandler::AddBridgeDomain, this, _1, _2));
    physical_device_listener_id_ = DBTable::kInvalidId;
    if (agent_->tsn_no_forwarding_enabled()) {
        physical_device_listener_id_ = agent_->physical_device_table()->
            Register(boost::bind(&MulticastHandler::NotifyPhysicalDevice,
                                 this, _1, _2));
        agent_->oper_db()->agent_route_walk_manager()->
            RegisterWalker(static_cast<AgentRouteWalker *>(te_walker_.get()));
    }

    GetMulticastObjList().clear();
}

void MulticastHandler::Terminate() {
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->bridge_domain_table()->Unregister(bridge_domain_id_);
    if (physical_device_listener_id_ != DBTable::kInvalidId) {
        agent_->physical_device_table()->
            Unregister(physical_device_listener_id_);
    }
    if (te_walker_.get()) {
        agent_->oper_db()->agent_route_walk_manager()->
            ReleaseWalker(te_walker_.get());
        te_walker_.reset();
    }
}

void MulticastHandler::AddL2BroadcastRoute(MulticastGroupObject *obj,
                                           const string &vrf_name,
                                           const string &vn_name,
                                           const Ip4Address &addr,
                                           uint32_t label,
                                           int vxlan_id,
                                           uint32_t ethernet_tag)
{
    if (obj->pbb_vrf() && obj->dependent_mg() == NULL) {
        return;
    }

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
                                                   component_nh_key_list,
                                                   obj->pbb_vrf(),
                                                   obj->learning_enabled());
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
            vn->SetState(partition->parent(), vn_listener_id_, state);
            //Also create multicast object
            if (all_broadcast == NULL) {
                all_broadcast = CreateMulticastGroupObject(state->vrf_name_,
                                        vn->GetName(), Ip4Address(), broadcast,
                                        state->vxlan_id_);
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
                                                       component_nh_key_list,
                                                       all_broadcast->pbb_vrf(),
                                                       all_broadcast->
                                                       learning_enabled());
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

        DeleteMulticastObject(state->vrf_name_, Ip4Address(), broadcast);
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

void MulticastHandler::McastTableNotify(DBTablePartBase *partition,
                                    DBEntryBase *e) {

    AgentRoute *route = static_cast<AgentRoute *>(e);
    if (route->GetTableType() != Agent::INET4_MULTICAST) {
        return;
    }
    Inet4MulticastRouteEntry *mc_entry =
                        static_cast<Inet4MulticastRouteEntry*>(route);
    VrfEntry *vrf = mc_entry->vrf();
    Ip4Address src = mc_entry->src_ip_addr();
    Ip4Address grp = mc_entry->dest_ip_addr();

    MulticastGroupObject *sg_object = NULL;
    sg_object = FindGroupObject(vrf->GetName(), src, grp);
    if (!sg_object) {
        return;
    }

    bool add = false;
    const AgentPath *path = NULL;
    AgentXmppChannel *channel = NULL;
    for (uint32_t i = 0; i < MAX_XMPP_SERVERS; i++) {
        channel = agent_->controller_xmpp_channel(i);
        if (channel && channel->bgp_peer_id() != NULL) {
            path = route->FindPath(channel->bgp_peer_id());
            if (path) {
                add = true;
                break;
            }
        }
    }

    if (sg_object->mvpn_registered() == add) {
        return;
    }

    if (sg_object->GetLocalListSize() == 0) {
        return;
    }

    MulticastGroupObject *target_object = NULL;
    target_object = FindGroupObject(agent_->fabric_policy_vrf_name(),
                                    sg_object->GetSourceAddress(),
                                    sg_object->GetGroupAddress());
    if (!target_object) {
        return;
    }

    std::map<uuid, MacAddress>::const_iterator it;
    for (it = sg_object->GetLocalList().begin();
                    it != sg_object->GetLocalList().end(); it++) {
        if (add) {
            if (target_object->AddLocalMember(it->first, it->second) == true) {
                AddVmToMulticastObjMap(it->first, target_object);
            }
        } else {
            if (target_object->DeleteLocalMember(it->first) == true) {
                DeleteVmToMulticastObjMap(it->first, target_object);
            }
        }
    }

    if (target_object->GetLocalListSize() == 0) {
        Composite::Type comp_type = Composite::L3INTERFACE;
        DeleteMulticastRoute(agent_->local_vm_peer(), target_object->vrf_name(),
                                    target_object->GetSourceAddress(),
                                    target_object->GetGroupAddress(),
                                    0, comp_type);
        DeleteMulticastObject(target_object->vrf_name(),
                                    target_object->GetSourceAddress(),
                                    target_object->GetGroupAddress());
    } else {
        TriggerLocalRouteChange(target_object, agent_->local_vm_peer());
    }

    sg_object->set_mvpn_registered(add);

    return;
}

void MulticastHandler::ModifyVRF(DBTablePartBase *partition, DBEntryBase *e) {

    if (!agent_->params()->mvpn_ipv4_enable()) {
        return;
    }

    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    MulticastVrfDBState *state = static_cast<MulticastVrfDBState *>
        (vrf->GetState(partition->parent(), vrf_listener_id_));
    if (!vrf) {
        return;
    }

    //Add operation
    if (!vrf->IsDeleted()) {
        if (!state) {
            state = new MulticastVrfDBState();
            AgentRouteTable *table = static_cast<AgentRouteTable *>
                            (vrf->GetRouteTable(Agent::INET4_MULTICAST));
            state->id_ = table->Register(
                            boost::bind(&MulticastHandler::McastTableNotify,
                            this, _1, _2));
            vrf->SetState(partition->parent(), vrf_listener_id_, state);
        }
        state->vrf_name_ = vrf->GetName();
    } else {
        if (!state) {
            return;
        }
        AgentRouteTable *table = static_cast<AgentRouteTable *>
                            (vrf->GetRouteTable(Agent::INET4_MULTICAST));
        table->Unregister(state->id_);
        vrf->ClearState(partition->parent(), vrf_listener_id_);
        delete state;
    }
}

MulticastDBState*
MulticastHandler::CreateBridgeDomainMG(DBTablePartBase *partition,
                                            BridgeDomainEntry *bd) {
    MulticastDBState *state = new MulticastDBState(bd->vrf()->GetName(),
                                                   bd->isid());

    MulticastGroupObject *obj = FindFloodGroupObject(bd->vrf()->GetName());
    if (obj == NULL) {
        obj = CreateMulticastGroupObject(state->vrf_name_, bd->vn()->GetName(),
                                        Ip4Address(), kBroadcast,
                                        state->vxlan_id_);
        //Add a mutlicast route point to empty composite list
        AddL2BroadcastRoute(obj, state->vrf_name_,
                            bd->vn()->GetName(), kBroadcast,
                            MplsTable::kInvalidLabel, 0,
                            state->vxlan_id_);
    }

    obj->set_bridge_domain(bd);
    obj->set_vxlan_id(bd->isid());
    bd->SetState(partition->parent(), bridge_domain_id_, state);
    return state;
}

void MulticastHandler::AddBridgeDomain(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    BridgeDomainEntry *bd = static_cast<BridgeDomainEntry *>(e);

    MulticastDBState *state = static_cast<MulticastDBState *>(
        bd->GetState(partition->parent(), bridge_domain_id_));

    if (e->IsDeleted() || bd->vrf() == NULL || bd->vn() == NULL) {
        if (state) {
            MulticastGroupObject *obj =
                FindFloodGroupObject(state->vrf_name_);
            assert(obj);
            obj->reset_bridge_domain();
            bd->ClearState(partition->parent(), bridge_domain_id_);
            DeleteBroadcast(agent_->local_vm_peer(),
                            state->vrf_name_, 0, Composite::L2INTERFACE);
            DeleteMulticastObject(state->vrf_name_, Ip4Address(), kBroadcast);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state = CreateBridgeDomainMG(partition, bd);
    }

    MulticastGroupObject *obj = FindFloodGroupObject(bd->vrf()->GetName());
    if (obj == NULL) {
        obj = CreateMulticastGroupObject(state->vrf_name_, bd->vn()->GetName(),
                                        Ip4Address(), kBroadcast,
                                        state->vxlan_id_);
    }

    if (state->vxlan_id_ != bd->isid()) {
        DeleteEvpnPath(obj);
        state->vxlan_id_ = bd->isid();
        obj->set_vxlan_id(state->vxlan_id_);
        Resync(obj);
    }

    if (state->learning_enabled_ != bd->learning_enabled()) {
        state->learning_enabled_ = bd->learning_enabled();
        ChangeLearningMode(obj, state->learning_enabled_);
    }

    if (state->pbb_etree_enabled_ != bd->pbb_etree_enabled()) {
        state->pbb_etree_enabled_ = bd->pbb_etree_enabled();
        ChangePbbEtreeMode(obj, state->pbb_etree_enabled_);
    }

    if (state->layer2_control_word_ != bd->layer2_control_word()) {
        state->layer2_control_word_ = bd->layer2_control_word();
        Resync(obj);
    }
}

void MulticastHandler::ChangeLearningMode(MulticastGroupObject *obj,
                                          bool learning_enabled) {
    if (obj->learning_enabled_ == learning_enabled) {
        return;
    }

    obj->set_learning_enabled(learning_enabled);
    Resync(obj);
}

void MulticastHandler::ChangePbbEtreeMode(MulticastGroupObject *obj,
                                          bool pbb_etree_enabled) {
    if (obj->pbb_etree_enabled_ == pbb_etree_enabled) {
        return;
    }

    obj->set_pbb_etree_enabled(pbb_etree_enabled);
    Resync(obj);
}

bool MulticastGroupObject::CanBeDeleted() const {
    if ((local_olist_.size() == 0) && (vn_.get() == NULL) &&
         bridge_domain_.get() == NULL)
        return true;
    return false;
}

MulticastGroupObject*
MulticastGroupObject::GetDependentMG(uint32_t isid) {
    for (MGList::iterator it = mg_list_.begin();
        it != mg_list_.end(); it++) {
        if (it->vxlan_id_ == isid) {
            return it.operator->();
        }
    }
    return NULL;
}

MulticastGroupObject*
MulticastHandler::CreateMulticastGroupObject(const string &vrf_name,
                                             const string &vn_name,
                                             const Ip4Address &src_addr,
                                             const Ip4Address &grp_addr,
                                             uint32_t vxlan_id) {
    MulticastGroupObject *obj =
        new MulticastGroupObject(vrf_name, vn_name, grp_addr, src_addr);
    AddToMulticastObjList(obj);
    obj->set_vxlan_id(vxlan_id);

    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    if (obj->GetGroupAddress() == bcast_addr) {
        UpdateReference(obj);
    }
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

void MulticastHandler::NotifyPhysicalDevice(DBTablePartBase *partition,
                                            DBEntryBase *e)
{
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(e);
    IpAddress dev_ip = dev->ip();
    ManagedPhysicalDevicesList::iterator pd_it =
        std::find(physical_devices_.begin(), physical_devices_.end(),
                  dev_ip.to_string());
    if (dev->IsDeleted()) {
        if (pd_it == physical_devices_.end())
            return;
        physical_devices_.erase(pd_it);
    } else {
        if (pd_it != physical_devices_.end())
            return;
        physical_devices_.push_back(dev_ip.to_string());
    }
    std::sort(physical_devices_.begin(), physical_devices_.end());
    //Start walk to update evpn nh
    if (te_walker_.get()) {
        te_walker_.get()->StartVrfWalk();
    }
}

bool MulticastHandler::FilterVmi(const VmInterface *vm_itf) {
    if (vm_itf->device_type() == VmInterface::TOR) {
        //Ignore TOR VMI, they are not active VMI.
        return true;
    }

    if (vm_itf->vmi_type() == VmInterface::VHOST) {
        return true;
    }

    if (vm_itf->device_type() == VmInterface::VMI_ON_LR) {
        return true;
    }

    if (vm_itf->device_type() == VmInterface::VM_SRIOV) {
        return true;
    }

    return false;
}

/* Registered call for VM */
void MulticastHandler::ModifyVmInterface(DBTablePartBase *partition,
                                         DBEntryBase *e)
{
    Interface *intf = static_cast<Interface *>(e);
    VmInterface *vm_itf;

    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    vm_itf = static_cast<VmInterface *>(intf);
    if (FilterVmi(vm_itf)) {
        return;
    }

    MulticastIntfDBState *state = static_cast<MulticastIntfDBState *>(
        vm_itf->GetState(partition->parent(), interface_listener_id_));

    if (intf->IsDeleted() || ((vm_itf->l2_active() == false) &&
                              (vm_itf->ipv4_active() == false) &&
                              (vm_itf->ipv6_active() == false))) {
        if (state) {
            DeleteVmInterface(vm_itf, state);
            if (intf->IsDeleted()) {
                vm_itf->ClearState(partition->parent(), interface_listener_id_);
                delete state;
            }
        }
        return;
    }
    assert(vm_itf->vn() != NULL);

    if (state == NULL) {
        state = new MulticastIntfDBState();
        vm_itf->SetState(partition->parent(), interface_listener_id_, state);
    }

    //Build all the VRF group interface belong to
    AddVmInterfaceInFloodGroup(vm_itf, state);
    return;
}

void MulticastHandler::AddVmInterfaceInFloodGroup(const VmInterface *vm_intf,
                                                  MulticastIntfDBState *state) {
    std::set<std::string> new_vrf_list;
    new_vrf_list.insert(vm_intf->vrf()->GetName());

    //Build all PBB VRF
    VmInterface::BridgeDomainEntrySet::const_iterator it =
        vm_intf->bridge_domain_list().list_.begin();
    for (;it != vm_intf->bridge_domain_list().list_.end(); it++) {
        if (it->bridge_domain_.get() && it->bridge_domain_->vrf()) {
            new_vrf_list.insert(it->bridge_domain_->vrf()->GetName());
        }
    }

    //Delete interface multicast group, if bridge domain is deleted
    for (std::set<std::string>::const_iterator it = new_vrf_list.begin();
         it != new_vrf_list.end(); it++) {
        AddVmInterfaceInFloodGroup(vm_intf, *it);
        state->vrf_list_.erase(*it);
    }

    for (std::set<std::string>::const_iterator it = state->vrf_list_.begin();
         it != state->vrf_list_.end(); it++) {
        DeleteVmInterface(vm_intf, *it);
    }

    state->vrf_list_ = new_vrf_list;
}

void MulticastHandler::DeleteVmInterface(const VmInterface *intf,
                                         MulticastIntfDBState *state) {
    for (std::set<std::string>::const_iterator it = state->vrf_list_.begin();
         it != state->vrf_list_.end(); it++) {
        DeleteVmInterface(intf, *it);
    }
}

/*
 * Delete VM interface
 * Traverse the multicast obj list of which this VM is a member.
 * Delete the VM from them and check if local VM list size is zero.
 * If it is zero then delete the route and mpls.
 */
void MulticastHandler::DeleteVmInterface(const VmInterface *intf,
                                         const std::string &vrf_name) {

    const VmInterface *vm_itf = static_cast<const VmInterface *>(intf);
    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    std::set<MulticastGroupObject *> &obj_list = GetVmToMulticastObjMap(
                                                          vm_itf->GetUuid());
    for (std::set<MulticastGroupObject *>::iterator it = obj_list.begin();
         it != obj_list.end(); it++) {

        if (((*it)->vrf_name() != vrf_name) ||
            ((*it)->GetGroupAddress() != bcast_addr)) {
            continue;
        }
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
            MCTRACE(LogSG, "modify route, vm is deleted ", (*it)->vrf_name(),
                    (*it)->GetSourceAddress().to_string(),
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
            DeleteMulticastObject((*it)->vrf_name_, Ip4Address(), bcast_addr);
        }
        DeleteVmToMulticastObjMap(vm_itf->GetUuid(), *it);
        break;
    }
    MCTRACE(Info, "Del vm notify done ", vm_itf->primary_ip_addr().to_string());
}

//Delete multicast object for vrf/S/G
void MulticastHandler::DeleteMulticastObject(const std::string &vrf_name,
                                             const Ip4Address &src_addr,
                                             const Ip4Address &grp_addr) {

    for (std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin();
        it != this->GetMulticastObjList().end(); it++) {

        if (((*it)->vrf_name() == vrf_name) &&
            ((*it)->GetGroupAddress() == grp_addr) &&
            ((*it)->GetSourceAddress() == src_addr)) {

            if (!((*it)->CanBeDeleted())) {
                return;
            }

            if ((*it)->dependent_mg() != NULL) {
                MulticastGroupObject *dp_obj = (*it)->dependent_mg();
                (*it)->set_dependent_mg(NULL);
                ModifyFabricMembers(dp_obj->peer(), dp_obj->vrf_name(),
                                    dp_obj->GetGroupAddress(),
                                    dp_obj->GetSourceAddress(),
                                    dp_obj->fabric_label(),
                                    dp_obj->fabric_olist(),
                                    dp_obj->peer_identifier());
            }

            MCTRACE(LogSG, "delete obj  vrf/source/grp/size ", vrf_name,
                    src_addr.to_string(),
                    grp_addr.to_string(),
                    this->GetMulticastObjList().size());
            delete (*it);
            this->GetMulticastObjList().erase(it);
            break;
        }
    }
}

MulticastGroupObject *MulticastHandler::FindFloodGroupObject(const std::string &vrf_name) {
    boost::system::error_code ec;
    Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                   ec).to_v4();
    return (FindGroupObject(vrf_name, Ip4Address(), broadcast));
}

//Helper to find object for VRF/S/G
MulticastGroupObject *MulticastHandler::FindGroupObject(
                                    const std::string &vrf_name,
                                    const Ip4Address &sip,
                                    const Ip4Address &dip) {
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin();
        it != this->GetMulticastObjList().end(); it++) {
        if (((*it)->vrf_name() == vrf_name) &&
            ((*it)->GetGroupAddress() == dip) &&
            ((*it)->GetSourceAddress() == sip)) {
            return (*it);
        }
    }
    MCTRACE(LogSG, "mcast obj size ", vrf_name, sip.to_string(),
            dip.to_string(), this->GetMulticastObjList().size());
    return NULL;
}

MulticastGroupObject *MulticastHandler::FindActiveGroupObject(
                                                    const std::string &vrf_name,
                                                    const Ip4Address &sip,
                                                    const Ip4Address &dip) {
    MulticastGroupObject *obj = FindGroupObject(vrf_name, sip, dip);
    if ((obj == NULL) || obj->IsDeleted()) {
        MCTRACE(LogSG, "Multicast object deleted ", vrf_name,
                sip.to_string(), dip.to_string(), 0);
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
MulticastGroupObject::GetInterfaceComponentNHKeyList(uint8_t interface_flags) {

    ComponentNHKeyList component_nh_key_list;
    for (std::map<uuid, MacAddress>::iterator it = local_olist_.begin();
            it != local_olist_.end(); it++) {
        ComponentNHKeyPtr component_nh_key(new ComponentNHKey(0, it->first,
                                                        interface_flags,
                                                        it->second));
        component_nh_key_list.push_back(component_nh_key);
    }
    return component_nh_key_list;
}

ComponentNHKeyList
MulticastHandler::GetInterfaceComponentNHKeyList(MulticastGroupObject *obj,
                                                 uint8_t interface_flags) {

    ComponentNHKeyList component_nh_key_list =
                        obj->GetInterfaceComponentNHKeyList(interface_flags);
    return component_nh_key_list;
}

// For MVPN: <S,G> entry is added to the multicast table only.
// For EVPN-multicast: <*,G> entry is added to the multicast table.
//      Currently support is for multicast source outside of the contrail
//      Also, the vrouter does lookup not using the <S,G> of the multicast
//      data packet, but using the multicast MAC DA.
//      With this as design choice, route entry corresponding to the
//      multicast MAC dervied from the G of <*,G> is added as entry to the
//      bridge table.
void MulticastHandler::TriggerLocalRouteChange(MulticastGroupObject *obj,
                                          const Peer *peer) {
    DBRequest req;
    ComponentNHKeyList component_nh_key_list;

    if (obj->pbb_vrf() && obj->dependent_mg() == NULL) {
        return;
    }

    uint8_t intf_flags = InterfaceNHFlags::BRIDGE;
    Composite::Type comp_type = Composite::L2INTERFACE;
    uint32_t route_tunnel_bmap = TunnelType::AllType();
    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    if (obj->GetGroupAddress() != bcast_addr) {
        intf_flags = InterfaceNHFlags::INET4 | InterfaceNHFlags::MULTICAST;
        comp_type = Composite::L3INTERFACE;
        route_tunnel_bmap = agent_->params()->mvpn_ipv4_enable() ?
                            TunnelType::AllType() : TunnelType::VxlanType();
    }
    component_nh_key_list =
        GetInterfaceComponentNHKeyList(obj, intf_flags);

    MCTRACE(LogSG, "enqueue route change with local peer",
            obj->vrf_name(),
            obj->GetSourceAddress().to_string(),
            obj->GetGroupAddress().to_string(),
            component_nh_key_list.size());

    AgentRouteData *data =
        BridgeAgentRouteTable::BuildNonBgpPeerData(obj->vrf_name(),
                                    obj->GetVnName(), MplsTable::kInvalidLabel,
                                    obj->vxlan_id(), route_tunnel_bmap,
                                    comp_type, component_nh_key_list,
                                    obj->pbb_vrf(),
                                    obj->learning_enabled());

    if (obj->GetGroupAddress() != bcast_addr) {
        Inet4MulticastAgentRouteTable::AddMulticastRoute(peer,
                                    obj->vrf_name(), obj->GetSourceAddress(),
                                    obj->GetGroupAddress(), 0,
                                    data);

        if (!agent_->params()->mvpn_ipv4_enable()) {
            intf_flags = InterfaceNHFlags::BRIDGE;
            comp_type = Composite::L2INTERFACE;
            component_nh_key_list =
                GetInterfaceComponentNHKeyList(obj, intf_flags);

            MacAddress mac;
            GetMulticastMacFromIp(obj->GetGroupAddress(), mac);

            AgentRouteData *bridge_data =
                BridgeAgentRouteTable::BuildNonBgpPeerData(obj->vrf_name(),
                                    obj->GetVnName(), MplsTable::kInvalidLabel,
                                    obj->vxlan_id(), route_tunnel_bmap,
                                    comp_type,component_nh_key_list,
                                    obj->pbb_vrf(),
                                    obj->learning_enabled());
            BridgeAgentRouteTable::AddBridgeRoute(peer, obj->vrf_name(), mac,
                                    0, bridge_data);
        }
    } else {
        //Add Bridge FF:FF:FF:FF:FF:FF, local_vm_peer
        BridgeAgentRouteTable::AddBridgeBroadcastRoute(peer,
                                                   obj->vrf_name(),
                                                   0,
                                                   data);
    }
}

void MulticastHandler::AddMulticastRoute(MulticastGroupObject *obj,
                                    const Peer *peer,
                                    uint32_t ethernet_tag,
                                    AgentRouteData *data,
                                    AgentRouteData *bridge_data) {

    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();

    if (obj->GetGroupAddress() != bcast_addr) {
        Inet4MulticastAgentRouteTable::AddMulticastRoute(peer,
                                    obj->vrf_name(), obj->GetSourceAddress(),
                                    obj->GetGroupAddress(), ethernet_tag, data);

        if (!agent_->params()->mvpn_ipv4_enable()) {
            MacAddress mac;
            GetMulticastMacFromIp(obj->GetGroupAddress(), mac);

            BridgeAgentRouteTable::AddBridgeRoute(peer, obj->vrf_name(), mac,
                                    ethernet_tag, bridge_data);
        }
    } else {
        BridgeAgentRouteTable::AddBridgeBroadcastRoute(peer,
                                    obj->vrf_name(), ethernet_tag, data);
    }
}

void MulticastHandler::DeleteMulticastRoute(const Peer *peer,
                                    const string &vrf_name,
                                    const Ip4Address &src,
                                    const Ip4Address &grp,
                                    uint32_t ethernet_tag,
                                    COMPOSITETYPE comp_type) {

    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();

    if (grp != bcast_addr) {
        Inet4MulticastAgentRouteTable::DeleteMulticastRoute(peer, vrf_name,
                                    src, grp, ethernet_tag, comp_type);

        if (!agent_->params()->mvpn_ipv4_enable()) {
            MacAddress mac;
            GetMulticastMacFromIp(grp, mac);

            COMPOSITETYPE l2_comp_type;
            if (comp_type == Composite::L3FABRIC) {
                l2_comp_type = Composite::FABRIC;
            } else {
                l2_comp_type = Composite::L2INTERFACE;
            }
            BridgeAgentRouteTable::DeleteBridgeRoute(peer, vrf_name, mac,
                                    ethernet_tag, l2_comp_type);
        }
    } else {
        BridgeAgentRouteTable::DeleteBroadcastReq(peer, vrf_name, ethernet_tag,
                                    comp_type);
    }
}

void MulticastHandler::TriggerRemoteRouteChange(MulticastGroupObject *obj,
                                                const Peer *peer,
                                                const string &vrf_name,
                                                const Ip4Address &src,
                                                const Ip4Address &grp,
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
        MCTRACE(LogSG, "delete bcast path from remote peer", vrf_name,
                src.to_string(), grp.to_string(), 0);
        DeleteMulticastRoute(peer, vrf_name, src, grp, ethernet_tag, comp_type);
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
        MCTRACE(LogSG, "Invalid peer identifier sent for modification",
                vrf_name, src.to_string(), grp.to_string(), 0);
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

    MCTRACE(LogSG, "enqueue route change with remote peer",
            obj->vrf_name(),
            obj->GetSourceAddress().to_string(),
            obj->GetGroupAddress().to_string(),
            component_nh_key_list.size());

    //Delete fabric path, so that only PBB route points
    //to MPLS route
    if (comp_type == Composite::FABRIC &&
        ((obj->mg_list_.empty() == false || obj->pbb_etree_enabled() == true))) {
        DeleteMulticastRoute(peer, vrf_name, src, grp, ethernet_tag, comp_type);
        return;
    }

    //Add Bridge FF:FF:FF:FF:FF:FF for L2 Multicast
    if (comp_type == Composite::TOR)
        route_tunnel_bmap = TunnelType::VxlanType();
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    AgentRouteData *data = NULL;
    AgentRouteData *bridge_data = NULL;
    if (bgp_peer) {
        data = BridgeAgentRouteTable::BuildBgpPeerData(peer,
                                                       obj->vrf_name(),
                                                       obj->GetVnName(),
                                                       label,
                                                       obj->vxlan_id(),
                                                       ethernet_tag,
                                                       route_tunnel_bmap,
                                                       comp_type,
                                                       component_nh_key_list,
                                                       obj->pbb_vrf(), false);
    } else {
        data = BridgeAgentRouteTable::BuildNonBgpPeerData(obj->vrf_name(),
                                                          obj->GetVnName(),
                                                          label,
                                                          obj->vxlan_id(),
                                                          route_tunnel_bmap,
                                                          comp_type,
                                                          component_nh_key_list,
                                                          obj->pbb_vrf(), false);
    }

    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    if ((grp != bcast_addr) && !agent_->params()->mvpn_ipv4_enable()) {
        if (bgp_peer) {
            bridge_data = BridgeAgentRouteTable::BuildBgpPeerData(peer,
                                    obj->vrf_name(), obj->GetVnName(), label,
                                    obj->vxlan_id(), ethernet_tag,
                                    route_tunnel_bmap, Composite::FABRIC,
                                    component_nh_key_list,
                                    obj->pbb_vrf(), false);
        } else {
            bridge_data = BridgeAgentRouteTable::BuildNonBgpPeerData(
                                    obj->vrf_name(), obj->GetVnName(), label,
                                    obj->vxlan_id(), route_tunnel_bmap,
                                    Composite::FABRIC, component_nh_key_list,
                                    obj->pbb_vrf(), false);
        }
    }

    AddMulticastRoute(obj, peer, ethernet_tag, data, bridge_data);
    MCTRACE(LogSG, "rebake subnet peer for subnet", vrf_name,
            obj->GetSourceAddress().to_string(),
            obj->GetGroupAddress().to_string(), comp_type);
}

void MulticastHandler::AddVmInterfaceInFloodGroup(const VmInterface *vm_itf,
                                                  const std::string &vrf_name) {
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
    all_broadcast = this->FindGroupObject(vrf_name, Ip4Address(), broadcast);
    if (all_broadcast == NULL) {
        all_broadcast = CreateMulticastGroupObject(vrf_name, vn->GetName(),
                                        Ip4Address(), broadcast,
                                        vn->GetVxLanId());
        add_route = true;
    }

    //Modify Nexthops
    if (all_broadcast->AddLocalMember(intf_uuid, vm_itf->vm_mac()) == true) {
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
    boost::system::error_code ec;
    MulticastGroupObject *obj = NULL;
    obj = FindActiveGroupObject(vrf_name, src, grp);
    MCTRACE(LogSG, "XMPP call(edge replicate) multicast handler ", vrf_name,
            src.to_string(), grp.to_string(), label);

    bool delete_op = false;

    //Invalid peer identifier signifies delete.
    //If add operation, obj should exist, else return.
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    if (peer == NULL) {
        return;
    }

    if (obj) {
        if (delete_op) {
            obj->set_peer(NULL);
            TunnelOlist empty_list;
            obj->set_fabric_olist(empty_list);
            obj->set_fabric_label(label);
        } else {
            obj->set_peer(peer);
            obj->set_fabric_olist(olist);
            obj->set_fabric_label(label);
        }
    }

    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    Composite::Type comp_type = Composite::FABRIC;
    if (grp != bcast_addr) {
        comp_type = Composite::L3FABRIC;
    }

    TriggerRemoteRouteChange(obj, peer, vrf_name, src, grp, olist,
                             peer_identifier,
                             delete_op, comp_type,
                             label, true, 0);

    if (obj == NULL) {
        return;
    }

    for (MulticastGroupObject::MGList::iterator iter = obj->mg_list_begin();
         iter != obj->mg_list_end(); iter++) {
        MulticastGroupObject *mg =
            static_cast<MulticastGroupObject *>(iter.operator->());
        TriggerRemoteRouteChange(mg, peer, iter->vrf_name(), src, grp,
                                 olist, peer_identifier,
                                 delete_op, comp_type,
                                 label, true, 0);
    }

    MCTRACE(LogSG, "Add fabric grp label ", vrf_name, src.to_string(),
                                grp.to_string(), label);
}

void MulticastHandler::ModifyEvpnMembers(const Peer *peer,
                                         const std::string &vrf_name,
                                         const Ip4Address &grp,
                                         const Ip4Address &src,
                                         const TunnelOlist &olist,
                                         uint32_t ethernet_tag,
                                         uint64_t peer_identifier) {

    boost::system::error_code ec;
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name, src, grp);

    MCTRACE(LogSG, "XMPP call(EVPN) multicast handler ", vrf_name, src.to_string(),
                                    grp.to_string(), 0);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer, vrf_name, src, grp, olist,
                                    peer_identifier, delete_op,
                                    Composite::EVPN, MplsTable::kInvalidLabel,
                                    false, ethernet_tag);

    MCTRACE(LogSG, "Add EVPN TOR Olist ", vrf_name, src.to_string(),
                                    grp.to_string(), 0);

    return;
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
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name,
                                    Ip4Address(), grp);
    std::string derived_vrf_name = vrf_name;

    if (ethernet_tag && obj) {
        MulticastGroupObject *dependent_mg =
            obj->GetDependentMG(ethernet_tag);
        if (dependent_mg) {
            obj = dependent_mg;
            derived_vrf_name = obj->vrf_name();
        }
    }

    MCTRACE(Log, "XMPP call(EVPN) multicast handler ", derived_vrf_name,
            grp.to_string(), 0);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer, derived_vrf_name, Ip4Address(), grp,
                             olist, peer_identifier, delete_op, Composite::EVPN,
                             MplsTable::kInvalidLabel, false, ethernet_tag);
    MCTRACE(Log, "Add EVPN TOR Olist ", derived_vrf_name, grp.to_string(), 0);
}

void MulticastHandler::ModifyTorMembers(const Peer *peer,
                                        const std::string &vrf_name,
                                        const TunnelOlist &olist,
                                        uint32_t ethernet_tag,
                                        uint64_t peer_identifier)
{
    boost::system::error_code ec;

    Ip4Address grp = Ip4Address::from_string("255.255.255.255", ec);
    MulticastGroupObject *obj = FindActiveGroupObject(vrf_name, Ip4Address(),
                                    grp);
    MCTRACE(Log, "TOR multicast handler ", vrf_name, grp.to_string(), 0);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } else if (obj == NULL) {
        return;
    }

    TriggerRemoteRouteChange(obj, peer, vrf_name, Ip4Address(), grp, olist,
                             peer_identifier, delete_op, Composite::TOR,
                             MplsTable::kInvalidLabel, false, ethernet_tag);
    MCTRACE(Log, "Add external TOR Olist ", vrf_name, grp.to_string(), 0);
}

void MulticastHandler::ModifyMvpnVrfRegistration(const Peer *peer,
                             const std::string &vrf_name,
                             const Ip4Address &grp,
                             const Ip4Address &src,
                             uint64_t peer_identifier) {

    MulticastGroupObject *obj = NULL;
    obj = FindGroupObject(vrf_name, src, grp);

    bool delete_op = false;
    if (peer_identifier == ControllerPeerPath::kInvalidPeerIdentifier) {
        delete_op = true;
    } if (!obj) {
        return;
    }

    TunnelOlist olist;
    TriggerRemoteRouteChange(obj, peer, vrf_name, src, grp, olist,
                             peer_identifier, delete_op, Composite::L3FABRIC, 0,
                             true, 0);
    return;
}

// Helper to delete fabric nh
// For internal delete it uses invalid identifier.
// For delete via control node it uses the sequence sent.
void MulticastGroupObject::FlushAllPeerInfo(const Agent *agent,
                                            const Peer *peer,
                                            uint64_t peer_identifier) {
    if ((peer_identifier != peer_identifier_) ||
        (peer_identifier == INVALID_PEER_IDENTIFIER)) {
        boost::system::error_code ec;
        Ip4Address bcast_addr = IpAddress::from_string("255.255.255.255",
                                                       ec).to_v4();
        if (GetGroupAddress() != bcast_addr) {
            agent->oper_db()->multicast()->DeleteMulticastRoute(peer,
                                                       vrf_name_, src_address_,
                                                       grp_address_, 0,
                                                       Composite::L3FABRIC);
        } else {
            agent->oper_db()->multicast()->DeleteBroadcast(peer, vrf_name_, 0,
                                                       Composite::FABRIC);
            MCTRACE(Log, "Delete broadcast route", vrf_name_,
                grp_address_.to_string(), 0);
        }
    }
}

MulticastHandler::MulticastHandler(Agent *agent) :
    agent_(agent),
    vn_listener_id_(DBTable::kInvalidId),
    interface_listener_id_(DBTable::kInvalidId),
    physical_device_listener_id_(DBTable::kInvalidId),
    physical_devices_() {
    if (agent_->tsn_no_forwarding_enabled()) {
        te_walker_.reset(new MulticastTEWalker("MulticastTorElectorWalker", agent));
    }
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

    boost::system::error_code ec;
    Ip4Address bcast_addr = IpAddress::from_string("255.255.255.255",
                                                       ec).to_v4();
    //Delete all route mpls and trigger cnh change
    for (std::set<MulticastGroupObject *>::iterator it =
         GetMulticastObjList().begin(); it != GetMulticastObjList().end();
         it++) {
        MulticastGroupObject *obj = (*it);

        AgentRoute *route = NULL;
        if (obj->GetGroupAddress() != bcast_addr) {
            Inet4MulticastAgentRouteTable *mtable =
                dynamic_cast<Inet4MulticastAgentRouteTable *>
                (agent_->vrf_table()->GetInet4MulticastRouteTable(obj->vrf_name()));
            if (mtable) {
                route = mtable->FindRoute(obj->GetGroupAddress(),
                                obj->GetSourceAddress());
            }
        } else {
            BridgeAgentRouteTable *bridge_table =
                static_cast<BridgeAgentRouteTable *>
                (agent_->vrf_table()->GetBridgeRouteTable(obj->vrf_name()));
            if (bridge_table) {
                route = bridge_table->FindRoute(MacAddress::BroadcastMac());
            }
        }

        if (route == NULL) {
            delete (obj);
            this->GetMulticastObjList().erase(obj);
            continue;
        }

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
        this->GetMulticastObjList().erase(obj);
    }
}

void MulticastHandler::ResyncDependentVrf(MulticastGroupObject *obj) {
    std::set<MulticastGroupObject *>::iterator it =
        multicast_obj_list_.begin();
    for (; it != multicast_obj_list_.end(); it++) {
        MulticastGroupObject *mg =
            static_cast<MulticastGroupObject *>(*it);
        if (mg->pbb_vrf() && mg->pbb_vrf_name() == obj->vrf_name()) {
            //Since reference is getting added on
            //addition, any eventual fabric notification
            //will result in ISID VRF also getting updated
            mg->set_dependent_mg(obj);
            TriggerLocalRouteChange(mg, agent_->local_vm_peer());
        }
    }
}

void MulticastHandler::UpdateReference(MulticastGroupObject *obj) {
    VrfKey key(obj->vrf_name());
    VrfEntry *vrf =
        static_cast<VrfEntry *>(agent_->vrf_table()->FindActiveEntry(&key));
    if (vrf && vrf->IsPbbVrf()) {
        //ISID VRF are dependent on BMAC VRF to build egde replication
        //tree, take a reference on BMAC VRF so that every time
        //BMAC VRF fabric list changes, ISID VRF can also be updated
        obj->set_pbb_vrf(true);
        obj->set_pbb_vrf_name(vrf->bmac_vrf_name());

        //Set dependent VRF
        MulticastGroupObject *dependent_mg =
            FindFloodGroupObject(vrf->bmac_vrf_name());
        obj->set_dependent_mg(dependent_mg);
        Resync(obj);
    } else {
        //If this a BMAC VRF, there may be ISID VRF dependent
        //on this new BMAC VRF
        //Evaulate them
        ResyncDependentVrf(obj);
    }
}

void MulticastHandler::Resync(MulticastGroupObject *obj) {
    MulticastGroupObject *dependent_mg = obj->dependent_mg();
    if (dependent_mg && dependent_mg->peer()) {
        ModifyFabricMembers(dependent_mg->peer(),
                            dependent_mg->vrf_name(),
                            dependent_mg->GetGroupAddress(),
                            dependent_mg->GetSourceAddress(),
                            dependent_mg->fabric_label(),
                            dependent_mg->fabric_olist(),
                            dependent_mg->peer_identifier());
    }
    TriggerLocalRouteChange(obj, agent_->local_vm_peer());
}

void MulticastHandler::DeleteEvpnPath(MulticastGroupObject *obj) {
    VrfKey key(obj->vrf_name());
    VrfEntry *vrf =
        static_cast <VrfEntry *>(agent_->vrf_table()->FindActiveEntry(&key));
    if (vrf == NULL) {
        return;
    }

    BridgeAgentRouteTable *br_table =
        static_cast<BridgeAgentRouteTable *>(vrf->GetBridgeRouteTable());
    BridgeRouteEntry *bridge_route =
        br_table->FindRoute(MacAddress::BroadcastMac());
    if (bridge_route == NULL){
        return;
    }

    for(Route::PathList::iterator it = bridge_route->GetPathList().begin();
        it != bridge_route->GetPathList().end();it++) {
        AgentPath *path =
            static_cast<AgentPath *>(it.operator->());
        const Peer *peer = path->peer();
        if (peer && peer->GetType() == Peer::BGP_PEER) {
            DeleteBroadcast(peer, obj->vrf_name(), obj->vxlan_id(),
                            Composite::EVPN);
        }
    }
}

void MulticastHandler::AddLocalPeerRoute(MulticastGroupObject *sg_object) {

    ComponentNHKeyList component_nh_key_list;

    uint32_t route_tunnel_bmap;
    route_tunnel_bmap = agent_->params()->mvpn_ipv4_enable() ?
                            TunnelType::AllType() : TunnelType::VxlanType();

    AgentRouteData *data =
        BridgeAgentRouteTable::BuildNonBgpPeerData(sg_object->vrf_name(),
                                    sg_object->GetVnName(), 0,
                                    sg_object->vxlan_id(),
                                    route_tunnel_bmap, Composite::L3COMP,
                                    component_nh_key_list, sg_object->pbb_vrf(),
                                    sg_object->learning_enabled());
    Inet4MulticastAgentRouteTable::AddMulticastRoute(agent_->local_peer(),
                                    sg_object->vrf_name(),
                                    sg_object->GetSourceAddress(),
                                    sg_object->GetGroupAddress(),
                                    sg_object->vxlan_id(), data);

    if (!agent_->params()->mvpn_ipv4_enable()) {
        MacAddress mac;
        GetMulticastMacFromIp(sg_object->GetGroupAddress(), mac);

        AgentRouteData *bridge_data =
            BridgeAgentRouteTable::BuildNonBgpPeerData(sg_object->vrf_name(),
                                    sg_object->GetVnName(), 0,
                                    sg_object->vxlan_id(),
                                    route_tunnel_bmap, Composite::L2COMP,
                                    component_nh_key_list, sg_object->pbb_vrf(),
                                    sg_object->learning_enabled());
        BridgeAgentRouteTable::AddBridgeRoute(agent_->local_peer(),
                                    sg_object->vrf_name(), mac,
                                    sg_object->vxlan_id(), bridge_data);
    }

    return;
}

void MulticastHandler::DeleteLocalPeerRoute(MulticastGroupObject *sg_object) {

    Inet4MulticastAgentRouteTable::DeleteMulticastRoute(agent_->local_peer(),
                                    sg_object->vrf_name(),
                                    sg_object->GetSourceAddress(),
                                    sg_object->GetGroupAddress(),
                                    sg_object->vxlan_id(), Composite::L3COMP);

    MacAddress mac;
    GetMulticastMacFromIp(sg_object->GetGroupAddress(), mac);

    BridgeAgentRouteTable::DeleteBridgeRoute(agent_->local_peer(),
                                    sg_object->vrf_name(), mac,
                                    sg_object->vxlan_id(), Composite::L2COMP);
}

// Create MulticastGroupObject on learning new <S,G>
//
// Note :
//   For EVPN:  Routes are added to both Inet Multicast table
//              and Bridge table in the native VRF only.
//   For MVPN:  Routes are added to Inet Multicast table only
//              but in both the native VRF and ip-fabric VRF.
//   However, API CreateMulticastVrfSourceGroup is used only for
//   EVPN for now.
//
void MulticastHandler::CreateMulticastVrfSourceGroup(
                                    const std::string &vrf_name,
                                    const std::string &vn_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(vrf_name);
    if (!vrf || vrf->IsDeleted()) return;

    MCTRACE(LogSG, "Add SG ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    bool created = false;
    MulticastGroupObject *sg_object = NULL;
    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);
    if (sg_object == NULL) {
        sg_object = CreateMulticastGroupObject(vrf_name, vn_name,
                                    src_addr, grp_addr, vrf->vxlan_id());
        AddLocalPeerRoute(sg_object);
        created = true;
    }

    if (created) {
        TriggerLocalRouteChange(sg_object, agent_->local_vm_peer());
    }

    MCTRACE(LogSG, "Add SG done ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    return;
}

// Delete VM-Interface from a MulticastGroupObject
// Delete of VMI will trigger route change for the <S,G>
// Last VMI to go will also trigger cleaning up of route
// and also the VRF,<S,G> data structure.
// API used in case of EVPN
void MulticastHandler::HandleRouteChangeAndMulticastObject(
                                MulticastGroupObject *sg_object,
                                boost::uuids::uuid vm_itf_uuid) {

    if (!sg_object) {
        return;
    }

    Composite::Type comp_type = Composite::L3INTERFACE;

    if ((sg_object->DeleteLocalMember(vm_itf_uuid) == true) &&
        (sg_object->IsDeleted() == false) &&
        (sg_object->GetLocalListSize() != 0)) {

        TriggerLocalRouteChange(sg_object, agent_->local_vm_peer());
        MCTRACE(LogSG, "modify route, vm is deleted for <S,G> ",
                            sg_object->vrf_name(),
                            sg_object->GetSourceAddress().to_string(),
                            sg_object->GetGroupAddress().to_string(), 0);
    }

    if (sg_object->GetLocalListSize() == 0) {
        if (sg_object->vrf_name() != agent_->fabric_policy_vrf_name()) {
            MulticastGroupObject *mvpn_sg_object = NULL;
            mvpn_sg_object = FindGroupObject(
                                    agent_->fabric_policy_vrf_name(),
                                    sg_object->GetSourceAddress(),
                                    sg_object->GetGroupAddress());
            if (mvpn_sg_object)
                mvpn_sg_object->decr_vn_count();
        }
    }

    if ((sg_object->GetLocalListSize() == 0) &&
        (sg_object->vn_count() == 0)) {

        //Time to delete route(for mcast address) and mpls
        DeleteMulticastRoute(agent_->local_vm_peer(),
                            sg_object->vrf_name(),
                            sg_object->GetSourceAddress(),
                            sg_object->GetGroupAddress(), 0, comp_type);
        DeleteLocalPeerRoute(sg_object);
        DeleteMulticastObject(sg_object->vrf_name(),
                            sg_object->GetSourceAddress(),
                            sg_object->GetGroupAddress());
    }

    return;
}

// Delete all VM-Interfaces for a particular <S,G> for a particular VRF
// API used in case of EVPN
void MulticastHandler::DeleteMulticastVrfSourceGroup(
                                    const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    MulticastGroupObject *sg_object = NULL;

    MCTRACE(LogSG, "Delete SG ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);
    if (!sg_object) {
        return;
    }

    std::map<uuid, MacAddress>::const_iterator it;
    it = sg_object->GetLocalList().begin();
    while (it != sg_object->GetLocalList().end()) {

        boost::uuids::uuid vm_itf_uuid = it->first;
        HandleRouteChangeAndMulticastObject(sg_object, vm_itf_uuid);

        DeleteVmToMulticastObjMap(vm_itf_uuid, sg_object);

        MCTRACE(LogSG, "VMI delete notify done for <S,G> ", vrf_name,
                                src_addr.to_string(), grp_addr.to_string(), 0);

        sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);
        if (sg_object) {
            it = sg_object->GetLocalList().begin();
        } else  {
            break;
        }
    }

    MCTRACE(LogSG, "Delete SG done ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    return;
}

// Add VM-Interface for a particular <S,G> for a particular VRF
// API used in case of MVPN and also EVPN
bool MulticastHandler::AddVmInterfaceToVrfSourceGroup(
                                    const std::string &vrf_name,
                                    const std::string &vn_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(vrf_name);
    if (!vrf || vrf->IsDeleted()) return false;

    MCTRACE(LogSG, "VMI add notify ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    const uuid intf_uuid = vm_itf->GetUuid();
    MulticastGroupObject *sg_object = NULL;

    bool created = false;

    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);
    if (sg_object == NULL) {
        sg_object = CreateMulticastGroupObject(vrf_name, vn_name, src_addr,
                                    grp_addr, vrf->vxlan_id());
        AddLocalPeerRoute(sg_object);
        created = true;
    }

    //Modify Nexthops
    if (sg_object->AddLocalMember(intf_uuid, vm_itf->vm_mac()) == true) {
        TriggerLocalRouteChange(sg_object, agent_->local_vm_peer());
        AddVmToMulticastObjMap(intf_uuid, sg_object);
    } else if (created) {
        //Modify routes
        TriggerLocalRouteChange(sg_object, agent_->local_vm_peer());
    }

    MCTRACE(LogSG, "VMI add notify done ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    return created;
}

// Add VM-Interface for a particular <S,G>
// API used in case of MVPN
void MulticastHandler::AddVmInterfaceToSourceGroup(
                                    const std::string &mc_vrf_name,
                                    const std::string &vn_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(mc_vrf_name);
    if (!vrf || vrf->IsDeleted()) return;

    MCTRACE(LogSG, "VMI add notify ", mc_vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    bool created = false;
    created = AddVmInterfaceToVrfSourceGroup(vm_itf->vrf()->GetName(), vn_name,
                                    vm_itf, src_addr, grp_addr);

    MulticastGroupObject *mvpn_sg_object = NULL;

    MulticastGroupObject *sg_object = NULL;
    sg_object = FindGroupObject(vm_itf->vrf()->GetName(), src_addr, grp_addr);
    if (sg_object) {
        if (sg_object->mvpn_registered()) {
            AddVmInterfaceToVrfSourceGroup(mc_vrf_name,
                                    vn_name, vm_itf, src_addr, grp_addr);
            mvpn_sg_object = FindGroupObject(mc_vrf_name, src_addr, grp_addr);
        } else {
            mvpn_sg_object = FindGroupObject(mc_vrf_name, src_addr, grp_addr);
            if (mvpn_sg_object == NULL) {
                mvpn_sg_object = CreateMulticastGroupObject(mc_vrf_name,
                                    vn_name, src_addr, grp_addr, vrf->vxlan_id());
                AddLocalPeerRoute(sg_object);
                TriggerLocalRouteChange(mvpn_sg_object, agent_->local_vm_peer());
            }
        }
    }
    if (created) {
        mvpn_sg_object->incr_vn_count();
    }

    MCTRACE(LogSG, "VMI add notify done ", mc_vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    return;
}

// Delete VM-Interface from a particular <S,G> for the specified VRF.
// API used in case of MVPN and also EVPN
void MulticastHandler::DeleteVmInterfaceFromVrfSourceGroup(
                                    const std::string &vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    MulticastGroupObject *sg_object = NULL;

    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);
    if (!sg_object) {
        return;
    }

    MCTRACE(LogSG, "VMI delete notify ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    boost::uuids::uuid vm_itf_uuid = vm_itf->GetUuid();
    HandleRouteChangeAndMulticastObject(sg_object, vm_itf_uuid);

    DeleteVmToMulticastObjMap(vm_itf->GetUuid(), sg_object);

    MCTRACE(LogSG, "VMI delete notify done ", vrf_name, src_addr.to_string(),
                            grp_addr.to_string(), 0);

    return;
}

// Delete VM-Interface from a particular <S,G>
// API used in case of MVPN
void MulticastHandler::DeleteVmInterfaceFromSourceGroup(
                                    const std::string &mc_vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    DeleteVmInterfaceFromVrfSourceGroup(vm_itf->vrf()->GetName(),
                                    vm_itf, src_addr, grp_addr);
    DeleteVmInterfaceFromVrfSourceGroup(mc_vrf_name, vm_itf, src_addr,
                                    grp_addr);
}

// Delete VM-Interface from all the learnt <S,G>s belonging
// to a particular group for the specified VRF.
// API used in case of MVPN and also EVPN
// grp_addr, when default, indicate all <S,G>s except broadcast.
void MulticastHandler::DeleteVmInterfaceFromVrfSourceGroup(
                                    const std::string &vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &grp_addr) {

    MulticastGroupObjectList objList;

    if (!FindVmToMulticastObjMap(vm_itf->GetUuid(), objList)) {
        return;
    }

    MCTRACE(LogSG, "VMI delete notify ", vrf_name, Ip4Address().to_string(),
                            grp_addr.to_string(), 0);

    std::set<MulticastGroupObject *> sg_to_delete;
    boost::system::error_code ec;
    Ip4Address bcast_addr =
                    IpAddress::from_string("255.255.255.255", ec).to_v4();
    for(std::set<MulticastGroupObject *>::iterator sg_it = objList.begin();
        sg_it != objList.end(); sg_it++) {

        if ((*sg_it)->vrf_name() != vrf_name) {

            continue;
        }

        if ((grp_addr.is_unspecified() &&
            (*sg_it)->GetGroupAddress() == bcast_addr)) {

            continue;
        }

        if ((!grp_addr.is_unspecified() &&
            (*sg_it)->GetGroupAddress() != grp_addr)) {

            continue;
        }

        boost::uuids::uuid vm_itf_uuid = vm_itf->GetUuid();
        HandleRouteChangeAndMulticastObject((*sg_it), vm_itf_uuid);

        sg_to_delete.insert(*sg_it);
    }

    for(std::set<MulticastGroupObject *>::iterator sg_it = sg_to_delete.begin();
        sg_it != sg_to_delete.end(); sg_it++) {
        DeleteVmToMulticastObjMap(vm_itf->GetUuid(), *sg_it);
    }

    MCTRACE(LogSG, "VMI delete notify done ", vrf_name,
                            Ip4Address().to_string(),
                            grp_addr.to_string(), 0);

    return;
}

// Delete VM-Interface from all the learnt <S,G>s belonging
// to a particular group
// API used in case of MVPN
void MulticastHandler::DeleteVmInterfaceFromSourceGroup(
                                    const std::string &mc_vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &grp_addr) {

    DeleteVmInterfaceFromVrfSourceGroup(vm_itf->vrf()->GetName(), vm_itf,
                                    grp_addr);
    DeleteVmInterfaceFromVrfSourceGroup(mc_vrf_name, vm_itf, grp_addr);
}

// Delete VM-Interface from all the learnt <S,G>s
// API used in case of MVPN
void MulticastHandler::DeleteVmInterfaceFromSourceGroup(
                                    const std::string &mc_vrf_name,
                                    const std::string &vm_vrf_name,
                                    const VmInterface *vm_itf) {

    DeleteVmInterfaceFromVrfSourceGroup(vm_vrf_name, vm_itf);
    DeleteVmInterfaceFromVrfSourceGroup(mc_vrf_name, vm_itf);
}

// Flags set to for use in SMET routes. Flags sent in XMPP message
// to the control BGP
void MulticastHandler::SetEvpnMulticastSGFlags(
                                    const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr,
                                    uint32_t flags) {

    MulticastGroupObject *sg_object = NULL;
    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);

    if (sg_object) sg_object->set_evpn_igmp_flags(flags);

    return;
}

// Flags Get API for use of flags in SMET routes. Flags sent in XMPP
// message to the control BGP
uint32_t MulticastHandler::GetEvpnMulticastSGFlags(
                                    const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr) {

    MulticastGroupObject *sg_object = NULL;
    sg_object = FindGroupObject(vrf_name, src_addr, grp_addr);

    return (sg_object ? sg_object->evpn_igmp_flags() : 0);
}

MulticastTEWalker::MulticastTEWalker(const std::string &name, Agent *agent) :
    AgentRouteWalker(name, agent) {
}

MulticastTEWalker::~MulticastTEWalker() {
}

bool MulticastTEWalker::RouteWalkNotify(DBTablePartBase *partition,
                                        DBEntryBase *e) {
    Agent *agent = (static_cast<AgentRouteTable *>
                    (partition->parent()))->agent();
    BridgeRouteEntry *bridge_route = static_cast<BridgeRouteEntry *>(e);
    bool notify = false;
    for(Route::PathList::iterator it = bridge_route->GetPathList().begin();
        it != bridge_route->GetPathList().end();it++) {
        MulticastRoutePath *path = dynamic_cast<MulticastRoutePath *>
            (it.operator->());
        if (!path)
            continue;

        const Peer *peer = path->peer();
        if (!peer)
            continue;

        if (peer->GetType() != Peer::BGP_PEER)
            continue;

        const CompositeNH *cnh = dynamic_cast<const CompositeNH *>
            (path->nexthop());
        if (!cnh)
            continue;

        if (cnh->composite_nh_type() != Composite::EVPN)
            continue;
        NextHop *nh = path->UpdateNH(agent,
                                     static_cast<CompositeNH *>(path->original_nh().get()),
                                     agent->oper_db()->tsn_elector());
        if (path->ChangeNH(agent, nh) && bridge_route->ReComputePathAdd(path))
            notify = true;
    }
    if (notify)
        partition->Notify(bridge_route);
    return true;
}
