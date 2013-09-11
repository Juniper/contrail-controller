/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>
#include <oper/inet4_mcroute.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

using namespace std;

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
        Inet4UcRouteTable::AddSubnetBroadcastRoute(Agent::GetInstance()->GetLocalVmPeer(), 
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
    Agent::GetInstance()->GetVnTable()->Register(boost::bind(&MulticastHandler::ModifyVNIpam,
                                              _1, _2));
    Agent::GetInstance()->GetInterfaceTable()->Register(boost::bind(&MulticastHandler::ModifyVmInterface,
                                                     _1, _2));

    MulticastHandler::GetInstance()->GetMulticastObjList().clear();
}

/*
 * Route address 255.255.255.255 addition from first VM in VN add
 */
void MulticastHandler::AddBroadcastRoute(const std::string &vrf_name,
                                         const Ip4Address &addr)
{
    boost::system::error_code ec;
    MCTRACE(Log, "add bcast route ", vrf_name, addr.to_string(), 0);
    Inet4McRouteTable::AddV4MulticastRoute(vrf_name,
                             IpAddress::from_string("0.0.0.0", ec).to_v4(),
                             addr);
}

/*
 * Route address 255.255.255.255 deletion from last VM in VN del
 */
void MulticastHandler::DeleteBroadcastRoute(const std::string &vrf_name,
                                            const Ip4Address &addr)
{
    boost::system::error_code ec;
    MCTRACE(Log, "delete bcast route ", vrf_name, addr.to_string(), 0);
    Inet4McRouteTable::DeleteV4MulticastRoute(vrf_name,
                             IpAddress::from_string("0.0.0.0", ec).to_v4(),
                             addr);
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
                             IpAddress::from_string("0.0.0.0", ec).to_v4(), false); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData();
    req.data.reset(cnh_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);

    MCTRACE(Log, "subnet route ", vrf_name, addr.to_string(), 0);
    Inet4UcRouteTable::AddSubnetBroadcastRoute(Agent::GetInstance()->GetLocalVmPeer(), 
                                 vrf_name,
                                 IpAddress::from_string("0.0.0.0", ec).to_v4(),
                                 addr, vn_name);

    MulticastGroupObject *subnet_broadcast = 
        this->FindGroupObject(vrf_name, addr);
    if (subnet_broadcast != NULL) {
        subnet_broadcast->Deleted(false);
        MCTRACE(Log, "mc obj rt added for subnet route ", 
                vrf_name, addr.to_string(), 0);
    }
}

/*
 * Delete the subnet route for which IPAM is gone.
 * Comp NH goes off as referring route goes off and refcount is zero
 */
void MulticastHandler::DeleteSubnetRoute(const std::string &vrf_name,
                                         const Ip4Address &addr)
{
    MCTRACE(Log, "delete subnet route ", vrf_name, addr.to_string(), 0);
    Inet4UcRouteTable::DeleteReq(Agent::GetInstance()->GetLocalVmPeer(), 
                                 vrf_name, addr, 32);
    MulticastGroupObject *subnet_broadcast = 
        this->FindGroupObject(vrf_name, addr);
    if (subnet_broadcast != NULL) {
        subnet_broadcast->Deleted(true);
        subnet_broadcast->SetSourceMPLSLabel(0);
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

        /*
        Inet4UcRouteTable::DeleteReq(Agent::GetInstance()->GetLocalVmPeer(), 
                                     GetAssociatedVrfForVn(vn->GetUuid()), 
                                     broadcast_addr, 32);
                                     */
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
    this->SetVrfNameForVn(vn->GetUuid(), vn->GetVrf()->GetName());
}

/*
 * VM dependant on VN will be moved to participating stage
 * This can happen when VM comes before VN is active with VRF and IPAM.
 */
void MulticastHandler::VisitUnresolvedVMList(const VnEntry *vn)
{
    const Interface *intf = NULL;
    const VmPortInterface *vm_itf = NULL;
    std::vector<VnIpam> &ipam = this->GetIpamMapList(vn->GetUuid());

    assert(ipam.size() != 0);

    //Now go thru unresolved vm list
    std::list<const VmPortInterface *> vmitf_list =
        this->GetUnresolvedSubnetVMList(vn->GetUuid());

    if (vmitf_list.size() == 0) {
        return;
    }
    for (std::list<const VmPortInterface *>::iterator it_itf = vmitf_list.begin(); 
         it_itf != vmitf_list.end();) {

        vm_itf = (*it_itf);
        intf = static_cast<const Interface *>(vm_itf);

        if ((vm_itf == NULL) || (intf->GetActiveState() != true)) {
            //Delete vm itf
            it_itf++;
            continue;
        }
        for (std::vector<VnIpam>::const_iterator it = ipam.begin(); 
            it != ipam.end(); it++) {
            if (IsSubnetMember(vm_itf->GetIpAddr(), (*it).ip_prefix, (*it).plen)) {
                this->AddVmInterface(vm_itf->GetVrf()->GetName(),
                      GetSubnetBroadcastAddress(vm_itf->GetIpAddr(),
                      (*it).plen), vm_itf->GetUuid(),
                      vn->GetName());
            }
        }
        it_itf++;
    }
}

/* Regsitered call for VN */
void MulticastHandler::ModifyVNIpam(DBTablePartBase *partition, DBEntryBase *e)
{
    const VnEntry *vn = static_cast<const VnEntry *>(e);
    const std::vector<VnIpam> &ipam = vn->GetVnIpam();

    MCTRACE(Info, "Modifyvnipam for ", vn->GetName());
    if ((vn->IsDeleted() == true) || (ipam.size() == 0) ||
        (vn->GetVrf() == NULL)) {
        MulticastHandler::GetInstance()->DeleteVnIPAM(vn);
        return;
    }

    //Now store the ipam for this vn and handle related ops
    MCTRACE(Info, "Modifyvnipam for handle ipam change ", vn->GetName());
    MulticastHandler::GetInstance()->HandleIPAMChange(vn, ipam);
}

/* Registered call for VM */
void MulticastHandler::ModifyVmInterface(DBTablePartBase *partition, 
                                         DBEntryBase *e)
{
    const Interface *intf = static_cast<const Interface *>(e);
    const VmPortInterface *vm_itf;

    if (intf->GetType() != Interface::VMPORT) {
        return;
    }

    if (intf->IsDeleted() || intf->GetActiveState() == false) {
        MulticastHandler::GetInstance()->DeleteVmInterface(intf);
        return;
    }

    vm_itf = static_cast<const VmPortInterface *>(intf);
    Ip4Address vm_itf_ip = vm_itf->GetIpAddr();
    assert(vm_itf->GetVnEntry() != NULL);

    //PIck up ipam from local ipam list
    std::vector<VnIpam> ipam = MulticastHandler::GetInstance()->
                       GetIpamMapList(vm_itf->GetVnEntry()->GetUuid());
    for(std::vector<VnIpam>::const_iterator it = ipam.begin(); 
        it != ipam.end(); it++) {
        if (IsSubnetMember(vm_itf_ip, (*it).ip_prefix, (*it).plen)) {
            MCTRACE(Log, "vm interface add being issued for ", 
                    vm_itf->GetVrf()->GetName(),
                    vm_itf->GetIpAddr().to_string(), 0);
            MulticastHandler::GetInstance()->AddVmInterface(
                              vm_itf->GetVrf()->GetName(),
                              GetSubnetBroadcastAddress(vm_itf->GetIpAddr(),
                              (*it).plen), vm_itf->GetUuid(),
                              vm_itf->GetVnEntry()->GetName());
            break;
        }
    }

    MCTRACE(Log, "vm ipam not found, add to unresolve ", 
            vm_itf->GetVrf()->GetName(),
            vm_itf->GetIpAddr().to_string(), 0);
    MulticastHandler::GetInstance()->AddToUnresolvedSubnetVMList(
                                           vm_itf->GetVnEntry()->GetUuid(), 
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
        Inet4McRouteTable::DeleteV4MulticastRoute(obj->GetVrfName(), 
                                                  obj->GetSourceAddress(), 
                                                  obj->GetGroupAddress());
    }
    /* delete the MPLS label route */
    obj->SetSourceMPLSLabel(0);
    MCTRACE(Log, "delete route mpls ", obj->GetVrfName(),
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
    const VmPortInterface *vm_itf = static_cast<const VmPortInterface *>(intf);
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
            MCTRACE(Log, "trigger cnh  ", (*it)->GetVrfName(),
                    (*it)->GetGroupAddress().to_string(),
                    (*it)->GetSourceMPLSLabel());
        }

        if((*it)->GetLocalListSize() == 0) {
            MCTRACE(Info, "Del vm notify ", vm_itf->GetIpAddr().to_string());
            //Empty tunnel list
            (*it)->FlushAllFabricOlist();
            //Update comp nh
            if ((*it)->IsDeleted() == false) {
                this->TriggerCompositeNHChange(*it);
                NotifyXMPPofRecipientChange((*it)->GetVrfName(),
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
    MCTRACE(Info, "Del vm notify done ", vm_itf->GetIpAddr().to_string());
}

//Delete multicast object for vrf/G
void MulticastHandler::DeleteMulticastObject(const std::string &vrf_name,
                                             const Ip4Address &grp_addr) {
    MCTRACE(Log, "delete obj  vrf/grp/size ", vrf_name, grp_addr.to_string(),
        this->GetMulticastObjList().size());
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin(); 
        it != this->GetMulticastObjList().end(); it++) {
        if (((*it)->GetVrfName() == vrf_name) &&
            ((*it)->GetGroupAddress() == grp_addr)) {
            delete (*it);
            this->GetMulticastObjList().erase(it++);
            break;
        }
    }
}

//Helper to find object for VRF/G
MulticastGroupObject *MulticastHandler::FindGroupObject(const std::string &vrf_name, 
                                                        const Ip4Address &dip) {
    for(std::set<MulticastGroupObject *>::iterator it =
        this->GetMulticastObjList().begin(); 
        it != this->GetMulticastObjList().end(); it++) {
        if (((*it)->GetVrfName() == vrf_name) &&
            ((*it)->GetGroupAddress() == dip)) {
            return (*it);
        }
    }
    MCTRACE(Log, "mcast obj size ", vrf_name, dip.to_string(),
            this->GetMulticastObjList().size());
    return NULL;
}

/*
 * Send the updated list for composite NH
 * Updated list is derived from local and tunnel olist
 */
void MulticastHandler::TriggerCompositeNHChange(MulticastGroupObject *obj)
{
    DBRequest req;
    NextHopKey *key; 
    std::vector<ComponentNHData> data;
    CompositeNHData *cnh_data;

    for (TunnelOlist::const_iterator it = obj->GetTunnelOlist().begin();
         it != obj->GetTunnelOlist().end(); it++) {
        ComponentNHData nh_data(it->label_, Agent::GetInstance()->GetDefaultVrf(),
                                Agent::GetInstance()->GetRouterId(), it->daddr_, false,
                                it->tunnel_bmap_);
        data.push_back(nh_data);
    }

    for (std::list<uuid>::const_iterator it = obj->GetLocalOlist().begin();
         it != obj->GetLocalOlist().end(); it++) {
        ComponentNHData nh_data(0, (*it), true);
        data.push_back(nh_data);
    }

    MCTRACE(Log, "enqueue comp ", obj->GetVrfName(),
            obj->GetGroupAddress().to_string(), data.size());
    key = new CompositeNHKey(obj->GetVrfName(), obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    cnh_data = new CompositeNHData(data, CompositeNHData::REPLACE);
    req.data.reset(cnh_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
}

/*
 * VM addition helper
 * Adds 255.255.255.255 route
 * Adds subnet broadcast if VN Ipam is present
 * If VN IPAM is not present then add to unresolved list
 * so that it can be added later
 */
void MulticastHandler::AddVmInterface(const std::string &vrf_name, 
                                      const Ip4Address &dip, 
                                      const uuid &intf_uuid, 
                                      const std::string &vn_name) {
    MulticastGroupObject *all_broadcast = NULL;
    MulticastGroupObject *subnet_broadcast = NULL;
    boost::system::error_code ec;
    Ip4Address broadcast =  IpAddress::from_string("255.255.255.255",
                                                   ec).to_v4();
    bool add_route = false;

    //All broadcast addition 255.255.255.255
    all_broadcast = this->FindGroupObject(vrf_name, broadcast);
    if (all_broadcast == NULL) {
        all_broadcast = new MulticastGroupObject(vrf_name, broadcast, vn_name);
        this->AddToMulticastObjList(all_broadcast); 
        add_route = true;
    }
    if (all_broadcast->AddLocalMember(intf_uuid) == true) {
        this->TriggerCompositeNHChange(all_broadcast);
        //Trigger route add 
        if (add_route) {
            this->AddBroadcastRoute(vrf_name, broadcast);
        }
        this->AddVmToMulticastObjMap(intf_uuid, all_broadcast);
    }

    //Subnet broadcast 
    add_route = false;
    subnet_broadcast = this->FindGroupObject(vrf_name, dip);
    if (subnet_broadcast == NULL) {
        subnet_broadcast = new MulticastGroupObject(vrf_name, dip, vn_name);
        this->AddToMulticastObjList(subnet_broadcast); 
    }
    if (subnet_broadcast->GetLocalListSize() == 0) {
        add_route = true;
    }
    if (subnet_broadcast->AddLocalMember(intf_uuid) == true) {
        this->TriggerCompositeNHChange(subnet_broadcast);
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
bool MulticastGroupObject::ModifyFabricMembers(const TunnelOlist &olist) 
{
    DBRequest req;
    NextHopKey *key; 
    TunnelNHData *tnh_data;

    GetTunnelOlist().clear();

    for (TunnelOlist::const_iterator it = olist.begin();
         it != olist.end(); it++) {
        AddMemberInTunnelOlist(it->label_, it->daddr_, it->tunnel_bmap_);

        key = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                              it->daddr_, false, 
                              TunnelType::ComputeType(it->tunnel_bmap_));
        tnh_data = new TunnelNHData();
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(tnh_data);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
        MCTRACE(Log, "Enqueue add TUNNEL ", Agent::GetInstance()->GetDefaultVrf(),
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
                                           const TunnelOlist &olist)
{
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindGroupObject(vrf_name, grp);
    MCTRACE(Log, "XMPP call multicast handler ", vrf_name, grp.to_string(), label);

    if ((obj == NULL) || obj->IsDeleted()) {
        MCTRACE(Log, "Multicast object deleted ", vrf_name, 
                grp.to_string(), label);
        return;
    }

    obj->SetSourceMPLSLabel(label);
    if (obj->ModifyFabricMembers(olist) == true) {
        MulticastHandler::GetInstance()->TriggerCompositeNHChange(obj);
    }

    MCTRACE(Log, "Add fabric grp label ", vrf_name, grp.to_string(), label);
}

//Helper to delete fabric nh
void MulticastGroupObject::FlushAllFabricOlist() {
    GetTunnelOlist().clear();
}

/*
 * XMPP peer has gone down, so flush off all the information
 * coming via control node i.e. MPLS label and tunnel info
 */
void MulticastHandler::HandlePeerDown() {
    for (std::set<MulticastGroupObject *>::iterator it =
         MulticastHandler::GetInstance()->GetMulticastObjList().begin(); 
         it != MulticastHandler::GetInstance()->GetMulticastObjList().end(); it++) {
        //Delete the label
        (*it)->SetSourceMPLSLabel(0);
        //Empty the tunnel OLIST
        (*it)->FlushAllFabricOlist();
        //Update comp NH
        MulticastHandler::GetInstance()->TriggerCompositeNHChange(*it);
    }
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

    //Add new label
    if ((label != 0) && (label != src_mpls_label_)) {
        MCTRACE(Log, "new src label ", vrf_name_, 
                grp_address_.to_string(), label);
        MplsLabel::CreateMcastLabelReq(GetVrfName(), GetGroupAddress(),
                                       GetSourceAddress(), label); 
    }
    //Delete old_label
    MplsLabel::DeleteMcastLabelReq(GetVrfName(), GetGroupAddress(),
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
        (*it)->FlushAllFabricOlist();
        //Update comp NH
        MulticastHandler::GetInstance()->TriggerCompositeNHChange(*it);
        //Delete the label and route
        MulticastHandler::GetInstance()->DeleteRouteandMPLS(*it);
        if (!IS_BCAST_MCAST((*it)->GetGroupAddress())) { 
            MulticastHandler::GetInstance()->DeleteSubnetRoute(
                                                   (*it)->GetVrfName(), 
                                                   (*it)->GetGroupAddress());
        }
        //Delete the multicast object
        delete (*it);
    }
    //Release the multicasthandler singleton object 
    delete (MulticastHandler::GetInstance()->obj_);
}
