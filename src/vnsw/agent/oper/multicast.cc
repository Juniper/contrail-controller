/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
#include <controller/controller_route_path.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

using namespace std;
#define INVALID_PEER_IDENTIFIER ControllerPeerPath::kInvalidPeerIdentifier

MulticastHandler *MulticastHandler::obj_;
SandeshTraceBufferPtr MulticastTraceBuf(SandeshTraceBufferCreate("Multicast",
                                                                     1000));
/*
 * Local funtion to derive subnet broadcast 
 */
static Ip4Address GetSubnetBroadcastAddress(const Ip4Address &ip_prefix, 
                                            uint32_t plen) {
    Ip4Address broadcast(ip_prefix.to_ulong() | 
                         ~(0xFFFFFFFF << (32 - plen)));
    return broadcast;
}

/*
 * Local function to find if its a subnet member
 */
static bool IsSubnetMember(const Ip4Address &ip, const Ip4Address &ip_prefix, 
                           uint32_t plen) {
    return ((ip_prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen))) == 
            (ip.to_ulong() | ~(0xFFFFFFFF << (32 - plen)))); 
}

/*
 * Helper to send route change for xmpp listener
 */
void NotifyXMPPofRecipientChange(const std::string &vrf_name, 
                                 const Ip4Address &dip, 
                                 const std::string &vn_name) {
    boost::system::error_code ec;
    if (!IS_BCAST_MCAST(dip)) { 
        MCTRACE(Log, "notify subnet route chg", vrf_name, dip.to_string(), 0);
        Inet4UnicastAgentRouteTable::AddSubnetBroadcastRoute(
                                     Agent::GetInstance()->local_vm_peer(), 
                                     vrf_name,
                                     IpAddress::from_string("0.0.0.0", ec).to_v4(),
                                     dip, vn_name);
    }
}

/*
 * Registeration for notification
 * VM - Looking for local VM added 
 * VN - Looking for subnet information from VN 
 * Enable trace print messages
 */
void MulticastHandler::Register() {
    vn_listener_id_ = agent_->vn_table()->Register(
        boost::bind(&MulticastHandler::ModifyVN, _1, _2));
    interface_listener_id_ = agent_->interface_table()->Register(
        boost::bind(&MulticastHandler::ModifyVmInterface, _1, _2));

    MulticastHandler::GetInstance()->GetMulticastObjList().clear();
}

void MulticastHandler::Terminate() {
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
}

/*
 * Route address 255.255.255.255 addition from first VM in VN add
 */
void MulticastHandler::AddBroadcastRoute(const string &vrf_name,
                                         const string &vn_name,
                                         const Ip4Address &addr)
{
    boost::system::error_code ec;
    MCTRACE(Log, "add IP V4 bcast route ", vrf_name, addr.to_string(), 0);
    Inet4MulticastAgentRouteTable::AddMulticastRoute(vrf_name, vn_name,
                       IpAddress::from_string("0.0.0.0", ec).to_v4(), addr);
}

void MulticastHandler::AddL2BroadcastRoute(const string &vrf_name,
                                           const string &vn_name,
                                           const Ip4Address &addr,
                                           int vxlan_id)
{
    boost::system::error_code ec;
    MCTRACE(Log, "add L2 bcast route ", vrf_name, addr.to_string(), 0);
    //Add Layer2 FF:FF:FF:FF:FF:FF
    Layer2AgentRouteTable::AddLayer2BroadcastRoute(vrf_name, vn_name, addr,
                  IpAddress::from_string("0.0.0.0", ec).to_v4(), vxlan_id); 
}

/*
 * Route address 255.255.255.255 deletion from last VM in VN del
 */
void MulticastHandler::DeleteBroadcastRoute(const std::string &vrf_name,
                                            const Ip4Address &addr)
{
    boost::system::error_code ec;
    MCTRACE(Log, "delete bcast route ", vrf_name, addr.to_string(), 0);
    Inet4MulticastAgentRouteTable::DeleteMulticastRoute(vrf_name,
                       IpAddress::from_string("0.0.0.0", ec).to_v4(),
                       addr);
    Layer2AgentRouteTable::DeleteBroadcastReq(vrf_name);
}

/*
 * Called from IPAM addition in VN.
 * Creates empty comp NH and add the route pointing to it.
 * Later VM additions result in modification of comp NH
 */
void MulticastHandler::AddSubnetRoute(const std::string &vrf_name,
                                      const Ip4Address &addr, 
                                      const std::string &vn_name)
{
    DBRequest req;
    NextHopKey *key;
    boost::system::error_code ec;
    CompositeNHData *cnh_data;

    MCTRACE(Log, "subnet comp NH creation ", vrf_name, addr.to_string(), 0);
    key = new CompositeNHKey(vrf_name, addr,
                             IpAddress::from_string("0.0.0.0", ec).to_v4(),  
                             false, Composite::L3COMP); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);

    MulticastGroupObject *subnet_broadcast = 
        this->FindGroupObject(vrf_name, addr);
    if (subnet_broadcast != NULL) {
        subnet_broadcast->Deleted(false);
        MCTRACE(Log, "mc obj rt added for subnet route ", 
                vrf_name, addr.to_string(), 0);
    }

    MCTRACE(Log, "subnet route ", vrf_name, addr.to_string(), 0);
    Inet4UnicastAgentRouteTable::AddSubnetBroadcastRoute(
                                 Agent::GetInstance()->local_vm_peer(), 
                                 vrf_name,
                                 IpAddress::from_string("0.0.0.0", ec).to_v4(),
                                 addr, vn_name);

}

/*
 * Delete the subnet route for which IPAM is gone.
 * Comp NH goes off as referring route goes off and refcount is zero
 */
void MulticastHandler::DeleteSubnetRoute(const std::string &vrf_name,
                                         const Ip4Address &addr)
{
    MCTRACE(Log, "delete subnet route ", vrf_name, addr.to_string(), 0);
    Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->local_vm_peer(), 
                                           vrf_name, addr, 32, NULL);
    MulticastGroupObject *subnet_broadcast = 
        this->FindGroupObject(vrf_name, addr);
    if (subnet_broadcast != NULL) {
        subnet_broadcast->Deleted(true);
        subnet_broadcast->FlushAllPeerInfo(INVALID_PEER_IDENTIFIER);
        MCTRACE(Log, "rt gone mc obj marked for deletion for subnet route ", 
                vrf_name, addr.to_string(), 0);
    }
}

/*
 * Delete corresponding subnet route using IPAM of VN and delete ipam/vn
 * Also release vm if any from unresolved vm list dependant on this VN
 */
void MulticastHandler::DeleteVnIPAM(const VnEntry *vn) 
{
    std::map<uuid, std::vector<VnIpam> > &vn_ipam_map = 
        this->GetIpamMap();
    std::vector<VnIpam> ipam_list = vn_ipam_map[vn->GetUuid()];
    Ip4Address broadcast_addr;

    MCTRACE(Info, "delete vnipam for ", vn->GetName());
    if (ipam_list.size() == 0) {
        MCTRACE(Info, "delete vnipam found no ipam for ", vn->GetName());
        return;
    }

    boost::system::error_code ec;
    std::vector<VnIpam>::iterator it = ipam_list.begin();    
    while (it != ipam_list.end()) {    
        broadcast_addr = (*it).GetBroadcastAddress();
        //Delete route
        MCTRACE(Log, "vn delete, remove subnet route ", 
                GetAssociatedVrfForVn(vn->GetUuid()), 
                broadcast_addr.to_string(), 0);
        DeleteSubnetRoute(GetAssociatedVrfForVn(vn->GetUuid()),
                          broadcast_addr);

        //Erase will give the next object 
        it = ipam_list.erase(it);
    }

    vn_ipam_map.erase(vn->GetUuid());
    this->RemoveVrfVnAssociation(vn->GetUuid());
}

/* 
 * IPAM change handling
 * Delete subnet route of non existing IPAM
 * Add subnet route for new IPAM
 * No changes for retained IPAM
 * As this function is used for creation/modification also visit
 * unresolved VM list
 */
void MulticastHandler::HandleIPAMChange(const VnEntry *vn, 
                                        const std::vector<VnIpam> &ipam)
{
    bool evaluate_vmlist = false;
    //std::map<uuid, std::vector<VnIpam> > &vn_ipam_map = 
    //    this->GetIpamMap();
    std::vector<VnIpam> &old_ipam = this->GetIpamMapList(vn->GetUuid());
    //vm list is evaluated when vn is late or has been deleted and
    //added again. In both the cases local ipam goes is flushed and that
    //means vm need to be revisited. In rest of the cases where ipam is 
    //populated VM already had been seen.
    if (old_ipam.size() == 0) {
        evaluate_vmlist = true;
    }

    //TODO Optimize by not keeping vnipam but subnet bcast address
    std::sort(old_ipam.begin(), old_ipam.end());
    std::vector<VnIpam>::iterator old_it = old_ipam.begin();
    std::vector<VnIpam>::const_iterator new_it = ipam.begin();

    //Diff and delete or add
    while ((old_it != old_ipam.end()) && (new_it != ipam.end())) {
        if (*old_it < *new_it) {
            DeleteSubnetRoute(vn->GetVrf()->GetName(), 
                              (*old_it).GetBroadcastAddress());
            old_it = old_ipam.erase(old_it);
        } else if (*new_it < *old_it) {
            AddSubnetRoute(vn->GetVrf()->GetName(), 
                           (*new_it).GetBroadcastAddress(),
                           vn->GetName());
            old_it = old_ipam.insert(old_it, (*new_it));
        } else {
            old_it++;
            new_it++;
        }
    }

    //Residual element handling
    while (old_it != old_ipam.end()) {
        DeleteSubnetRoute(vn->GetVrf()->GetName(), 
                          (*old_it).GetBroadcastAddress());
        old_it = old_ipam.erase(old_it);
    }
    for (; new_it != ipam.end(); new_it++) {
        AddSubnetRoute(vn->GetVrf()->GetName(), 
                       (*new_it).GetBroadcastAddress(),
                       vn->GetName());
        old_ipam.push_back(*new_it);
    }

    //Push back the common elements and new elements
    assert(old_ipam.size() == ipam.size());

    //Visit any unresolved vm list
    if (evaluate_vmlist == true) {
        this->VisitUnresolvedVMList(vn);
    }
    //Store the vrf name for this VN
    this->set_vrf_nameForVn(vn->GetUuid(), vn->GetVrf()->GetName());
}

/*
 * VM dependant on VN will be moved to participating stage
 * This can happen when VM comes before VN is active with VRF and IPAM.
 */
void MulticastHandler::VisitUnresolvedVMList(const VnEntry *vn)
{
    const Interface *intf = NULL;
    const VmInterface *vm_itf = NULL;
    std::vector<VnIpam> &ipam = this->GetIpamMapList(vn->GetUuid());

    assert(ipam.size() != 0);

    //Now go thru unresolved vm list
    std::list<const VmInterface *> vmitf_list =
        this->GetUnresolvedSubnetVMList(vn->GetUuid());

    if (vmitf_list.size() == 0) {
        return;
    }
    for (std::list<const VmInterface *>::iterator it_itf = vmitf_list.begin(); 
         it_itf != vmitf_list.end();) {

        vm_itf = (*it_itf);
        intf = static_cast<const Interface *>(vm_itf);

        if ((vm_itf == NULL) || ((intf->ipv4_active() != true) &&
                                 (intf->l2_active() != true))) {
            //Delete vm itf
            it_itf++;
            continue;
        }
        this->AddVmInterfaceInFloodGroup(vm_itf->vrf()->GetName(),
                    vm_itf->GetUuid(), vn);
        if (vn->Ipv4Forwarding()) {
            for (std::vector<VnIpam>::const_iterator it = ipam.begin(); 
                 it != ipam.end(); it++) {
                if (IsSubnetMember(vm_itf->ip_addr(), (*it).ip_prefix, 
                                   (*it).plen)) {
                    this->AddVmInterfaceInSubnet(vm_itf->vrf()->GetName(),
                                  GetSubnetBroadcastAddress(vm_itf->ip_addr(),
                                  (*it).plen), vm_itf->GetUuid(), vn);
                }
            }
        }
        it_itf++;
    }
}

void MulticastHandler::HandleVxLanChange(const VnEntry *vn) {
    if (vn->IsDeleted() || !vn->GetVrf()) 
        return;

    MulticastGroupObject *obj =
        FindFloodGroupObject(vn->GetVrf()->GetName());
    if (!obj || obj->IsDeleted())
        return;

    int new_vxlan_id = 0;
    int vn_vxlan_id = vn->GetVxLanId();

    if (TunnelType::ComputeType(TunnelType::AllType()) ==
        TunnelType::VXLAN) {
        if (vn_vxlan_id != 0) {
            new_vxlan_id = vn_vxlan_id;
        }
    }

    if (new_vxlan_id != obj->vxlan_id()) {
        boost::system::error_code ec;
        Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                       ec).to_v4();
        obj->set_vxlan_id(new_vxlan_id);
        AddL2BroadcastRoute(vn->GetVrf()->GetName(), vn->GetName(), 
                            broadcast, new_vxlan_id);
    }
}

void MulticastHandler::HandleFamilyConfig(const VnEntry *vn) 
{
    bool new_layer2_forwarding = vn->layer2_forwarding();
    bool new_ipv4_forwarding = vn->Ipv4Forwarding();

    if (!vn->GetVrf())
        return;

    std::string vrf_name = vn->GetVrf()->GetName();
    for (std::set<MulticastGroupObject *>::iterator it =
         MulticastHandler::GetInstance()->GetMulticastObjList().begin(); 
         it != MulticastHandler::GetInstance()->GetMulticastObjList().end(); it++) {
        if (vrf_name != (*it)->vrf_name())
            continue;

        if (!(new_layer2_forwarding) && (*it)->layer2_forwarding()) {
            (*it)->SetLayer2Forwarding(new_layer2_forwarding);
            if (IS_BCAST_MCAST((*it)->GetGroupAddress())) { 
                Layer2AgentRouteTable::DeleteBroadcastReq((*it)->vrf_name());
            } 
        }
        if (!(new_ipv4_forwarding) && (*it)->Ipv4Forwarding()) {
            (*it)->SetIpv4Forwarding(new_ipv4_forwarding);
            if (IS_BCAST_MCAST((*it)->GetGroupAddress())) { 
                Inet4MulticastAgentRouteTable::DeleteMulticastRoute(
                                           (*it)->vrf_name(), 
                                           (*it)->GetSourceAddress(), 
                                           (*it)->GetGroupAddress());
            } else {
                DeleteSubnetRoute((*it)->vrf_name(), (*it)->GetGroupAddress());
            }
        }
        if ((*it)->IsMultiProtoSupported() && 
            ((*it)->GetSourceMPLSLabel() != 0)) { 
            this->AddChangeMultiProtocolCompositeNH((*it));
        }
    }
}

/* Regsitered call for VN */
void MulticastHandler::ModifyVN(DBTablePartBase *partition, DBEntryBase *e)
{
    const VnEntry *vn = static_cast<const VnEntry *>(e);
    const std::vector<VnIpam> &ipam = vn->GetVnIpam();
    bool ret = false;

    MCTRACE(Info, "Modifyvn for ", vn->GetName());
    if ((vn->IsDeleted() == true) || (ipam.size() == 0) ||
        (vn->GetVrf() == NULL)) { 
        MulticastHandler::GetInstance()->DeleteVnIPAM(vn);
        ret = true;
    }

    MulticastHandler::GetInstance()->HandleFamilyConfig(vn);
    MulticastHandler::GetInstance()->HandleVxLanChange(vn);

    if (ret)
        return;

    //Now store the ipam for this vn and handle related ops
    MCTRACE(Info, "Modifyvn for handle ipam change ", vn->GetName());
    if (vn->Ipv4Forwarding()) {
        MulticastHandler::GetInstance()->HandleIPAMChange(vn, ipam);
    }
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

    if (intf->IsDeleted() || ((intf->ipv4_active() == false) &&
                             (intf->l2_active() == false))) {
        MulticastHandler::GetInstance()->DeleteVmInterface(intf);
        return;
    }

    vm_itf = static_cast<const VmInterface *>(intf);
    Ip4Address vm_itf_ip = vm_itf->ip_addr();
    assert(vm_itf->vn() != NULL);

    MulticastHandler::GetInstance()->AddVmInterfaceInFloodGroup(
                                     vm_itf->vrf()->GetName(),
                                     vm_itf->GetUuid(), 
                                     vm_itf->vn());
    if (vm_itf->vn()->Ipv4Forwarding()) {
        //PIck up ipam from local ipam list
        std::vector<VnIpam> ipam = MulticastHandler::GetInstance()->
            GetIpamMapList(vm_itf->vn()->GetUuid());
        for(std::vector<VnIpam>::const_iterator it = ipam.begin(); 
            it != ipam.end(); it++) {
            if (IsSubnetMember(vm_itf_ip, (*it).ip_prefix, (*it).plen)) {
                MCTRACE(Log, "vm interface add being issued for ", 
                        vm_itf->vrf()->GetName(),
                        vm_itf->ip_addr().to_string(), 0);
                MulticastHandler::GetInstance()->AddVmInterfaceInSubnet(
                                  vm_itf->vrf()->GetName(),
                                  GetSubnetBroadcastAddress(vm_itf->ip_addr(),
                                  (*it).plen), vm_itf->GetUuid(),
                                  vm_itf->vn());
                break;
            }
        }
    }
    MCTRACE(Log, "vm ipam not found, add to unresolve ", 
            vm_itf->vrf()->GetName(),
            vm_itf->ip_addr().to_string(), 0);
    MulticastHandler::GetInstance()->AddToUnresolvedSubnetVMList(
                                           vm_itf->vn()->GetUuid(), 
                                           vm_itf);
    return;
}

/*
 * Delete the old mpls label and add the new mpls label if it is non zero
 * Delete route if group is non subnet broadcast, subnet bcast is deleted 
 * via IPAM going off
 */
void MulticastHandler::DeleteRouteandMPLS(MulticastGroupObject *obj)
{
    //delete mcast routes, subnet bcast gets deleted via vn delete
    if (IS_BCAST_MCAST(obj->GetGroupAddress())) { 
        Inet4MulticastAgentRouteTable::DeleteMulticastRoute(obj->vrf_name(), 
                                                  obj->GetSourceAddress(), 
                                                  obj->GetGroupAddress());
        Layer2AgentRouteTable::DeleteBroadcastReq(obj->vrf_name());
    }
    /* delete the MPLS label route */
    obj->FlushAllPeerInfo(INVALID_PEER_IDENTIFIER);
    MCTRACE(Log, "delete route mpls ", obj->vrf_name(),
            obj->GetGroupAddress().to_string(),
            obj->GetSourceMPLSLabel());
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
            this->TriggerCompositeNHChange(*it);
            MCTRACE(Log, "trigger cnh  ", (*it)->vrf_name(),
                    (*it)->GetGroupAddress().to_string(),
                    (*it)->GetSourceMPLSLabel());
        }

        if((*it)->GetLocalListSize() == 0) {
            MCTRACE(Info, "Del vm notify ", vm_itf->ip_addr().to_string());
            //Empty tunnel list
            (*it)->FlushAllPeerInfo(INVALID_PEER_IDENTIFIER);
            //Update comp nh
            if ((*it)->IsDeleted() == false) {
                this->TriggerCompositeNHChange(*it);
                NotifyXMPPofRecipientChange((*it)->vrf_name(),
                                            (*it)->GetGroupAddress(),
                                            (*it)->GetVnName());
            }
            //Time to delete route(for mcast address) and mpls
            this->DeleteRouteandMPLS(*it);
            /* delete mcast object */
            this->DeleteMulticastObject((*it)->vrf_name_, (*it)->grp_address_);
        } 
    }
    //Remove all mc obj references from reverse map of vm to mc obj and the
    //delete the uuid of vm
    this->vm_to_mcobj_list_[vm_itf->GetUuid()].clear();
    this->DeleteVmToMulticastObjMap(vm_itf->GetUuid());
    this->DeleteVMFromUnResolvedList(vm_itf);
    MCTRACE(Info, "Del vm notify done ", vm_itf->ip_addr().to_string());
}

//Delete multicast object for vrf/G
void MulticastHandler::DeleteMulticastObject(const std::string &vrf_name,
                                             const Ip4Address &grp_addr) {
    MCTRACE(Log, "delete obj  vrf/grp/size ", vrf_name, grp_addr.to_string(),
        this->GetMulticastObjList().size());
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin(); 
        it != this->GetMulticastObjList().end(); it++) {
        if (((*it)->vrf_name() == vrf_name) &&
            ((*it)->GetGroupAddress() == grp_addr)) {
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

void MulticastHandler::AddChangeMultiProtocolCompositeNH(
                                     MulticastGroupObject *obj)
{
    DBRequest req;
    NextHopKey *key; 
    std::vector<ComponentNHData> data;
    CompositeNHData *cnh_data;

    if (obj->Ipv4Forwarding()) {
        ComponentNHData l3_data(obj->vrf_name(), obj->GetGroupAddress(),
                                obj->GetSourceAddress(), false,
                                Composite::L3COMP);
        data.push_back(l3_data);
    }
    if (obj->layer2_forwarding()) {
        ComponentNHData l2_data(obj->vrf_name(), obj->GetGroupAddress(),
                                obj->GetSourceAddress(), false,
                                Composite::L2COMP);
        data.push_back(l2_data);
    }

    MCTRACE(Log, "enqueue multiproto comp ", obj->vrf_name(),
            obj->GetGroupAddress().to_string(), data.size());
    key = new CompositeNHKey(obj->vrf_name(), obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false,
                             Composite::MULTIPROTO); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(data, CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

void MulticastHandler::AddChangeFabricCompositeNH(MulticastGroupObject *obj)
{
    DBRequest req;
    NextHopKey *key; 
    std::vector<ComponentNHData> data;
    CompositeNHData *cnh_data;

    for (TunnelOlist::const_iterator it = obj->GetTunnelOlist().begin();
         it != obj->GetTunnelOlist().end(); it++) {
        ComponentNHData nh_data(it->label_, Agent::GetInstance()->fabric_vrf_name(),
                                Agent::GetInstance()->router_id(), it->daddr_, false,
                                it->tunnel_bmap_);
        data.push_back(nh_data);
    }

    MCTRACE(Log, "enqueue fabric comp ", obj->vrf_name(),
            obj->GetGroupAddress().to_string(), data.size());
    key = new CompositeNHKey(obj->vrf_name(), obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false,
                             Composite::FABRIC); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(data, CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

void MulticastHandler::TriggerL2CompositeNHChange(MulticastGroupObject *obj)
{
    DBRequest req;
    NextHopKey *key; 
    std::vector<ComponentNHData> data;
    CompositeNHData *cnh_data;

    //Add fabric Comp NH
    AddChangeFabricCompositeNH(obj);

    ComponentNHData fabric_nh_data(obj->vrf_name(), 
                                   obj->GetGroupAddress(),
                                   obj->GetSourceAddress(), false,
                                   Composite::FABRIC);

    data.push_back(fabric_nh_data);
    for (std::list<uuid>::const_iterator it = obj->GetLocalOlist().begin();
         it != obj->GetLocalOlist().end(); it++) {
        ComponentNHData nh_data(0, (*it), InterfaceNHFlags::LAYER2);
        data.push_back(nh_data);
    }
    MCTRACE(Log, "enqueue l2 comp ", obj->vrf_name(),
            obj->GetGroupAddress().to_string(), data.size());
    key = new CompositeNHKey(obj->vrf_name(), obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false,
                             Composite::L2COMP); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(data, CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

void MulticastHandler::TriggerCompositeNHChange(MulticastGroupObject *obj)
{
    if (obj->layer2_forwarding()) {
        this->TriggerL2CompositeNHChange(obj);
    }
    if (obj->Ipv4Forwarding()) {
        this->TriggerL3CompositeNHChange(obj);
    }
}
/*
 * Send the updated list for composite NH
 * Updated list is derived from local and tunnel olist
 */
void MulticastHandler::TriggerL3CompositeNHChange(MulticastGroupObject *obj)
{
    DBRequest req;
    NextHopKey *key; 
    std::vector<ComponentNHData> data;
    CompositeNHData *cnh_data;

    //Add fabric Comp NH
    AddChangeFabricCompositeNH(obj);

    ComponentNHData fabric_nh_data(obj->vrf_name(), obj->GetGroupAddress(),
                                   obj->GetSourceAddress(), false,
                                   Composite::FABRIC);

    data.push_back(fabric_nh_data);
    for (std::list<uuid>::const_iterator it = obj->GetLocalOlist().begin();
         it != obj->GetLocalOlist().end(); it++) {
        ComponentNHData nh_data(0, (*it), InterfaceNHFlags::MULTICAST);
        data.push_back(nh_data);
    }

    MCTRACE(Log, "enqueue l3 comp ", obj->vrf_name(),
            obj->GetGroupAddress().to_string(), data.size());
    key = new CompositeNHKey(obj->vrf_name(), obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false,
                             Composite::L3COMP); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(data, CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

void MulticastHandler::AddVmInterfaceInFloodGroup(const std::string &vrf_name, 
                                                  const uuid &intf_uuid, 
                                                  const VnEntry *vn) {
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
        all_broadcast = new MulticastGroupObject(vrf_name, broadcast, 
                                                 vn_name, true);
        this->AddToMulticastObjList(all_broadcast); 
        add_route = true;
    }
    //Modify Nexthops
    if (all_broadcast->AddLocalMember(intf_uuid) == true) {
        if (vn->Ipv4Forwarding()) {
            this->TriggerL3CompositeNHChange(all_broadcast);
        }
        if (vn->layer2_forwarding()) {
            this->TriggerL2CompositeNHChange(all_broadcast);
        }
        //Add l2/l3 comp nh in multi proto in case one of them is enabled later
        if (!add_route && (all_broadcast->GetSourceMPLSLabel() != 0) &&
            all_broadcast->IsMultiProtoSupported()) {
            this->AddChangeMultiProtocolCompositeNH(all_broadcast);
        }
        this->AddVmToMulticastObjMap(intf_uuid, all_broadcast);
    }
    //Modify routes
    if ((add_route || (all_broadcast->Ipv4Forwarding() != 
                       vn->Ipv4Forwarding())) && vn->Ipv4Forwarding()) {
        all_broadcast->SetIpv4Forwarding(vn->Ipv4Forwarding());
        this->TriggerL3CompositeNHChange(all_broadcast);
        this->AddBroadcastRoute(vrf_name, vn_name, broadcast); 
    }
    if ((add_route || (all_broadcast->layer2_forwarding() != 
                       vn->layer2_forwarding())) && vn->layer2_forwarding()) {
        if (TunnelType::ComputeType(TunnelType::AllType()) ==
            TunnelType::VXLAN) {
            all_broadcast->set_vxlan_id(vn->GetVxLanId());
        } 
        all_broadcast->SetLayer2Forwarding(vn->layer2_forwarding());
        this->TriggerL2CompositeNHChange(all_broadcast);
        this->AddL2BroadcastRoute(vrf_name, vn_name, broadcast,
                                  all_broadcast->vxlan_id()); 
    }
}


/*
 * VM addition helper
 * Adds subnet broadcast if VN Ipam is present
 */
void MulticastHandler::AddVmInterfaceInSubnet(const std::string &vrf_name, 
                                              const Ip4Address &dip, 
                                              const uuid &intf_uuid, 
                                              const VnEntry *vn) {
    MulticastGroupObject *subnet_broadcast = NULL;
    boost::system::error_code ec;
    bool add_route = false;
    const string vn_name = vn->GetName();

    //Subnet broadcast 
    subnet_broadcast = this->FindGroupObject(vrf_name, dip);
    if (subnet_broadcast == NULL) {
        subnet_broadcast = new MulticastGroupObject(vrf_name, dip, 
                                                    vn_name, false);
        this->AddToMulticastObjList(subnet_broadcast); 
    }
    if (subnet_broadcast->GetLocalListSize() == 0) {
        add_route = true;
    }
    if ((subnet_broadcast->Ipv4Forwarding() != 
                       vn->Ipv4Forwarding()) && vn->Ipv4Forwarding()) {
        this->TriggerL3CompositeNHChange(subnet_broadcast);
        NotifyXMPPofRecipientChange(vrf_name, dip, vn_name);
    }
    subnet_broadcast->SetLayer2Forwarding(false);
    subnet_broadcast->SetIpv4Forwarding(true);

    if (subnet_broadcast->AddLocalMember(intf_uuid) == true) {
        this->TriggerL3CompositeNHChange(subnet_broadcast);
        this->AddVmToMulticastObjMap(intf_uuid, subnet_broadcast);
        //Dummy notification to let xmpp know that cnh is populated
        if (add_route) {
            NotifyXMPPofRecipientChange(vrf_name, dip, vn_name);
        }
    }
}

/*
 * Release all info coming via ctrl node for this multicast object
 */
bool MulticastGroupObject::ModifyFabricMembers(const TunnelOlist &olist, 
                                               uint64_t peer_identifier,
                                               bool delete_op, uint32_t label) 
{
    DBRequest req;
    NextHopKey *key; 
    TunnelNHData *tnh_data;

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
    if (delete_op && peer_identifier <= peer_identifier_) {
        return true;
    }

    // - Update operation with lower sequence number sent compared to 
    // local identifier, ignore
    if (!delete_op && peer_identifier < peer_identifier_) {
        return true;
    }

    tunnel_olist_.clear();
    SetSourceMPLSLabel(label);

    // After resetting tunnel and mpls label return if it was a delete call,
    // dont update peer_identifier. Let it get updated via update operation only 
    if (delete_op) {
        return true;
    }

    // Ideally wrong update call
    if (peer_identifier == INVALID_PEER_IDENTIFIER) {
        MCTRACE(Log, "Invalid peer identifier sent for modification", 
                vrf_name_, grp_address_.to_string(), label);
        return false;
    }

    peer_identifier_ = peer_identifier;

    for (TunnelOlist::const_iterator it = olist.begin();
         it != olist.end(); it++) {
        AddMemberInTunnelOlist(it->label_, it->daddr_, it->tunnel_bmap_);

        key = new TunnelNHKey(Agent::GetInstance()->fabric_vrf_name(), 
                              Agent::GetInstance()->router_id(),
                              it->daddr_, false, 
                              TunnelType::ComputeType(it->tunnel_bmap_));
        tnh_data = new TunnelNHData();
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(tnh_data);
        Agent::GetInstance()->nexthop_table()->Enqueue(&req);
        MCTRACE(Log, "Enqueue add TUNNEL ", Agent::GetInstance()->fabric_vrf_name(),
                it->daddr_.to_string(), it->label_);
    }
    return true;
}

/*
 * Static funtion to be called to handle XMPP info from ctrl node
 * Key is VRF/G/S
 * Info has label (for source to vrouter) and
 * OLIST of NH (server IP + label for that server)
 */
void MulticastHandler::ModifyFabricMembers(const std::string &vrf_name, 
                                           const Ip4Address &grp, 
                                           const Ip4Address &src, 
                                           uint32_t label, 
                                           const TunnelOlist &olist,
                                           uint64_t peer_identifier)
{
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindGroupObject(vrf_name, grp);
    MCTRACE(Log, "XMPP call multicast handler ", vrf_name, grp.to_string(), label);

    if ((obj == NULL) || obj->IsDeleted()) {
        MCTRACE(Log, "Multicast object deleted ", vrf_name, 
                grp.to_string(), label);
        return;
    }

    if (obj->ModifyFabricMembers(olist, peer_identifier, false, label) == true) {
        MulticastHandler::GetInstance()->TriggerCompositeNHChange(obj);
    }

    MCTRACE(Log, "Add fabric grp label ", vrf_name, grp.to_string(), label);
}

// Helper to delete fabric nh
// For internal delete it uses invalid identifier. 
// For delete via control node it uses the sequence sent.
void MulticastGroupObject::FlushAllPeerInfo(uint64_t peer_identifier) {
    TunnelOlist olist;
    ModifyFabricMembers(olist, peer_identifier, true, 0);
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
        //Empty the tunnel OLIST
        (*it)->FlushAllPeerInfo(peer_sequence);
        //Update comp NH
        //Ignore modification of comp NH if route is not present i.e. multicast
        //object is marked for deletion.
        if ((*it)->IsDeleted() == false) {
            MulticastHandler::GetInstance()->TriggerCompositeNHChange(*it);
        }
    }
    return false;
}

/*
 * handle mpls label changes/deletion/addition of new label
 */
void MulticastGroupObject::SetSourceMPLSLabel(uint32_t label) { 
    MCTRACE(Log, "current src label ", vrf_name_, 
            grp_address_.to_string(), src_mpls_label_);
    if (label == src_mpls_label_) {
        return;
    }

    COMPOSITETYPE comp_type = Composite::L3COMP;
    //Add new label
    if ((label != 0) && (label != src_mpls_label_)) {
        MCTRACE(Log, "new src label ", vrf_name_, 
                grp_address_.to_string(), label);
        if (IsMultiProtoSupported()) {
            //EVPN may be off and put on later so keep 
            //l2 comp prepared under multi proto.
            //In case evpn is disabled then this is equivalent of drop NH
            MulticastHandler::GetInstance()->
                TriggerCompositeNHChange(this);
            MulticastHandler::GetInstance()->
                AddChangeMultiProtocolCompositeNH(this);
            comp_type = Composite::MULTIPROTO;
        }
        MplsLabel::CreateMcastLabelReq(vrf_name(), GetGroupAddress(),
                                       GetSourceAddress(), label, comp_type); 
    }
    //Delete old_label
    MplsLabel::DeleteMcastLabelReq(vrf_name(), GetGroupAddress(),
                                   GetSourceAddress(), src_mpls_label_);
    src_mpls_label_ = label; 
}

/*
 * Shutdown for clearing all stuff related to multicast
 */
void MulticastHandler::Shutdown() {
    //Delete all route mpls and trigger cnh change
    for (std::set<MulticastGroupObject *>::iterator it =
         MulticastHandler::GetInstance()->GetMulticastObjList().begin(); 
         it != MulticastHandler::GetInstance()->GetMulticastObjList().end(); it++) {
        //Empty the tunnel OLIST
        (*it)->FlushAllPeerInfo(INVALID_PEER_IDENTIFIER);
        //Update comp NH
        MulticastHandler::GetInstance()->TriggerCompositeNHChange(*it);
        //Delete the label and route
        MulticastHandler::GetInstance()->DeleteRouteandMPLS(*it);
        if (!IS_BCAST_MCAST((*it)->GetGroupAddress())) { 
            MulticastHandler::GetInstance()->DeleteSubnetRoute(
                                                   (*it)->vrf_name(), 
                                                   (*it)->GetGroupAddress());
        }
        //Delete the multicast object
        delete (*it);
    }
}
