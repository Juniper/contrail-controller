/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef multicast_agent_oper_hpp
#define multicast_agent_oper_hpp

#include <netinet/in.h>
#include <net/ethernet.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/agent_route_walker.h>

extern SandeshTraceBufferPtr MulticastTraceBuf;

#define MCTRACE(obj, ...)                                                        \
do {                                                                             \
    Multicast##obj::TraceMsg(MulticastTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false)

#define IS_BCAST_MCAST(grp)    ((grp.to_ulong() == 0xFFFFFFFF) || \
                               ((grp.to_ulong() & 0xF0000000) == 0xE0000000))
class BgpPeer;

struct OlistTunnelEntry {
    OlistTunnelEntry(const boost::uuids::uuid &device_uuid,
                     uint32_t label,
                     const Ip4Address &addr,
                     TunnelType::TypeBmap bmap) :
        device_uuid_(device_uuid),
        label_(label),
        daddr_(addr),
        tunnel_bmap_(bmap) { }
    virtual ~OlistTunnelEntry() { }

    boost::uuids::uuid device_uuid_;
    uint32_t label_;
    Ip4Address daddr_;
    TunnelType::TypeBmap tunnel_bmap_;
};

struct MulticastDBState : DBState {
    MulticastDBState(const std::string &vrf_name, const uint32_t vxlan_id) :
        vrf_name_(vrf_name), vxlan_id_(vxlan_id), learning_enabled_(false),
        pbb_etree_enabled_(false) { }

    std::string vrf_name_;
    uint32_t vxlan_id_;
    bool learning_enabled_;
    bool pbb_etree_enabled_;
    bool layer2_control_word_;
};

struct MulticastVrfDBState : DBState {
    MulticastVrfDBState() : vrf_name_() { }

    std::string vrf_name_;
    DBTableBase::ListenerId id_;
};

struct MulticastIntfDBState : DBState {
    MulticastIntfDBState() {}
    std::set<std::string> vrf_list_;
};

typedef std::vector<OlistTunnelEntry> TunnelOlist;

class MulticastGroupObject {
public:
    typedef DependencyList<MulticastGroupObject, MulticastGroupObject> MGList;
    MulticastGroupObject(const std::string &vrf_name,
                         const Ip4Address &grp_addr,
                         const std::string &vn_name) :
        vrf_name_(vrf_name), grp_address_(grp_addr), vn_name_(vn_name),
        vxlan_id_(0), peer_identifier_(0), deleted_(false), vn_(NULL),
        dependent_mg_(this, NULL) , pbb_vrf_(false), pbb_vrf_name_(""),
        peer_(NULL), fabric_label_(0), learning_enabled_(false),
        pbb_etree_enabled_(false), bridge_domain_(NULL),
        mvpn_registered_(false), vn_count_(0), evpn_igmp_flags_(0) {
        boost::system::error_code ec;
        src_address_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
        local_olist_.clear();
    };
    MulticastGroupObject(const std::string &vrf_name,
                         const std::string &vn_name,
                         const Ip4Address &grp_addr,
                         const Ip4Address &src_addr) :
        vrf_name_(vrf_name), grp_address_(grp_addr), vn_name_(vn_name),
        src_address_(src_addr), vxlan_id_(0), peer_identifier_(0),
        deleted_(false), vn_(NULL), dependent_mg_(this, NULL),
        pbb_vrf_(false), pbb_vrf_name_(""),
        peer_(NULL), fabric_label_(0), learning_enabled_(false),
        pbb_etree_enabled_(false), bridge_domain_(NULL),
        mvpn_registered_(false), vn_count_(0), evpn_igmp_flags_(0) {
        local_olist_.clear();
    };
    virtual ~MulticastGroupObject() { };

    bool CanBeDeleted() const;

    //Add local member is local VM in server.
    bool AddLocalMember(const boost::uuids::uuid &intf_uuid,
                                    const MacAddress &mac) {
        local_olist_[intf_uuid] = mac;
        return true;
    };

    //Delete local member from VM list in server
    bool DeleteLocalMember(const boost::uuids::uuid &intf_uuid) {
        if (local_olist_.find(intf_uuid) == local_olist_.end()) {
            return false;
        }
        local_olist_.erase(intf_uuid);
        return true;
    };

    // Get list of local VMs.
    const std::map<boost::uuids::uuid, MacAddress> &GetLocalList() {
        return local_olist_;
    }

    uint32_t GetLocalListSize() { return local_olist_.size(); };
    void ClearLocalListSize() { local_olist_.clear(); };

    //Labels for server + server list + ingress source label
    void FlushAllPeerInfo(const Agent *agent,
                          const Peer *peer,
                          uint64_t peer_identifier);

    //Gets
    const std::string &vrf_name() const { return vrf_name_; };
    const Ip4Address &GetGroupAddress() { return grp_address_; };
    const Ip4Address &GetSourceAddress() { return src_address_; };
    ComponentNHKeyList GetInterfaceComponentNHKeyList(uint8_t interface_flags);
    const std::string &GetVnName() { return vn_name_; };
    bool IsDeleted() { return deleted_; };
    void Deleted(bool val) { deleted_ = val; };
    bool CanUnsubscribe() const {return (deleted_);}
    void set_vxlan_id(uint32_t vxlan_id) {vxlan_id_ = vxlan_id;}
    uint32_t vxlan_id() const {return vxlan_id_;}
    void set_peer_identifier(uint64_t peer_id) {peer_identifier_ = peer_id;}
    uint64_t peer_identifier() {return peer_identifier_;}
    void set_vn(const VnEntry *vn);
    void reset_vn();
    void set_bridge_domain(const BridgeDomainEntry *bd) {
        bridge_domain_.reset(bd);
    }

    void reset_bridge_domain() {
        bridge_domain_.reset(NULL);
    }

    const VnEntry *vn() const {return vn_.get();}

    void set_pbb_vrf(bool is_pbb_vrf) {
        pbb_vrf_ = is_pbb_vrf;
    }
    bool pbb_vrf() const {
        return pbb_vrf_;
    }

    MGList::iterator mg_list_begin() {
        return mg_list_.begin();
    }

    MGList::iterator mg_list_end() {
        return mg_list_.end();
    }

    void set_dependent_mg(MulticastGroupObject *obj) {
        dependent_mg_.reset(obj);
    }

    MulticastGroupObject* dependent_mg() {
        return dependent_mg_.get();
    }

    void set_fabric_olist(const TunnelOlist &olist) {
        fabric_olist_ = olist;
    }

    const TunnelOlist& fabric_olist() const {
        return fabric_olist_;
    }

    void set_pbb_vrf_name(std::string name) {
        pbb_vrf_name_ = name;
    }

    const std::string& pbb_vrf_name() {
        return pbb_vrf_name_;
    }

    const Peer *peer() {
        return peer_;
    }

    void set_peer(const Peer *peer) {
        peer_ = peer;
    }

    uint32_t fabric_label() const {
        return fabric_label_;
    }

    void set_fabric_label(uint32_t label) {
        fabric_label_ = label;
    }

    bool learning_enabled() const {
        return learning_enabled_;
    }

    void set_learning_enabled(bool learning_enabled) {
        learning_enabled_ = learning_enabled;
    }

    bool pbb_etree_enabled() const {
        return pbb_etree_enabled_;
    }

    void set_pbb_etree_enabled(bool pbb_etree_enabled) {
        pbb_etree_enabled_ = pbb_etree_enabled;
    }

    bool mvpn_registered() const {
        return mvpn_registered_;
    }

    void set_mvpn_registered(bool mvpn_registered) {
        mvpn_registered_ = mvpn_registered;
    }

    void incr_vn_count() {
        vn_count_++;
    }

    void decr_vn_count() {
        vn_count_--;
    }

    uint32_t vn_count() {
        return vn_count_;
    }

    uint32_t evpn_igmp_flags() const {
        return evpn_igmp_flags_;
    }

    void set_evpn_igmp_flags(uint32_t evpn_igmp_flags) {
        evpn_igmp_flags_ = evpn_igmp_flags;
    }

    MulticastGroupObject* GetDependentMG(uint32_t isid);
private:
    friend class MulticastHandler;
    std::string vrf_name_;
    Ip4Address grp_address_;
    std::string vn_name_;
    Ip4Address src_address_;
    uint32_t vxlan_id_;
    uint64_t peer_identifier_;
    bool deleted_;
    std::map<boost::uuids::uuid, MacAddress> local_olist_; /* UUID of local i/f
                                                              and its MAC */
    VnEntryConstRef vn_;

    DependencyRef<MulticastGroupObject, MulticastGroupObject> dependent_mg_;
    DEPENDENCY_LIST(MulticastGroupObject, MulticastGroupObject, mg_list_);

    bool pbb_vrf_;
    std::string pbb_vrf_name_;
    TunnelOlist fabric_olist_;
    const Peer *peer_;
    uint32_t fabric_label_;
    bool learning_enabled_;
    bool pbb_etree_enabled_;
    bool layer2_control_word_;
    BridgeDomainConstRef bridge_domain_;
    bool mvpn_registered_;
    uint32_t vn_count_;
    uint32_t evpn_igmp_flags_;
    DISALLOW_COPY_AND_ASSIGN(MulticastGroupObject);
};

class MulticastTEWalker : public AgentRouteWalker {
public:
    typedef DBTableWalker::WalkId RouteWalkerIdList[Agent::ROUTE_TABLE_MAX];
    MulticastTEWalker(const std::string &name, Agent *agent);
    virtual ~MulticastTEWalker();

    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    DISALLOW_COPY_AND_ASSIGN(MulticastTEWalker);
};

struct AgentRouteData;

/* Static class for handling multicast objects common functionalities */
class MulticastHandler {
public:
    static const uint32_t kMulticastTimeout = 5 * 60 * 1000;
    static const Ip4Address kBroadcast;
    typedef std::set<MulticastGroupObject *> MulticastGroupObjectList;
    typedef std::map<boost::uuids::uuid, MulticastGroupObjectList>
        VmMulticastGroupObjectList;
    typedef std::vector<std::string> ManagedPhysicalDevicesList;

    MulticastHandler(Agent *agent);
    virtual ~MulticastHandler() {
        assert(multicast_obj_list_.size() == 0);
    }

    MulticastGroupObject *CreateMulticastGroupObject(const string &vrf_name,
                                            const string &vn_name,
                                            const Ip4Address &src_addr,
                                            const Ip4Address &grp_addr,
                                            uint32_t vxlan_id);

    /* Called by XMPP to add ctrl node sent olist and label */
    void ModifyFabricMembers(const Peer *peer,
                             const std::string &vrf_name,
                             const Ip4Address &group,
                             const Ip4Address &source,
                             uint32_t source_label,
                             const TunnelOlist &olist,
                             uint64_t peer_identifier = 0);
    void ModifyEvpnMembers(const Peer *peer,
                             const std::string &vrf_name,
                             const Ip4Address &grp,
                             const Ip4Address &src,
                             const TunnelOlist &olist,
                             uint32_t ethernet_tag,
                             uint64_t peer_identifier = 0);
    /* Called as a result of XMPP message received with OLIST of
     * evpn endpoints with mpls or vxlan encap
     */
    void ModifyEvpnMembers(const Peer *peer,
                           const std::string &vrf_name,
                           const TunnelOlist &olist,
                           uint32_t ethernet_tag,
                           uint64_t peer_identifier = 0);
    void ModifyTorMembers(const Peer *peer,
                          const std::string &vrf_name,
                          const TunnelOlist &olist,
                          uint32_t ethernet_tag,
                          uint64_t peer_identifier = 0);
    void ModifyMvpnVrfRegistration(const Peer *peer,
                          const std::string &vrf_name,
                          const Ip4Address &group,
                          const Ip4Address &source,
                          uint64_t peer_identifier);

    //Registered for VN notification
    void ModifyVN(DBTablePartBase *partition, DBEntryBase *e);
    //Registered for VRF notification
    void ModifyVRF(DBTablePartBase *partition, DBEntryBase *e);
    void McastTableNotify(DBTablePartBase *partition, DBEntryBase *e);
    //Registered for VM notification
    void ModifyVmInterface(DBTablePartBase *partition, DBEntryBase *e);
    void NotifyPhysicalDevice(DBTablePartBase *partition, DBEntryBase *e);
    //Register VM and VN notification
    void Register();

    //Singleton object reference
    static MulticastHandler *GetInstance() {
        return obj_;
    };
    void TriggerLocalRouteChange(MulticastGroupObject *obj, const Peer *peer);
    void TriggerRemoteRouteChange(MulticastGroupObject *obj,
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
                                  uint32_t ethernet_tag);
    void HandleIpam(const VnEntry *vn);
    void HandleVxLanChange(const VnEntry *vn);
    void HandleVnParametersChange(DBTablePartBase *partition,
                                  DBEntryBase *e);
    //For test routines to clear all routes and mpls label
    void Shutdown();
    //Multicast obj list addition deletion
    MulticastGroupObject *FindFloodGroupObject(const std::string &vrf_name);
    MulticastGroupObject *FindActiveGroupObject(const std::string &vrf_name,
                                                const Ip4Address &dip);
    MulticastGroupObject *FindActiveGroupObject(const std::string &vrf_name,
                                    const Ip4Address &sip,
                                    const Ip4Address &dip);
    std::set<MulticastGroupObject *> &GetMulticastObjList() {
        return multicast_obj_list_;
    };
    MulticastGroupObject *FindGroupObject(const std::string &vrf_name,
                                          const Ip4Address &sip,
                                          const Ip4Address &dip);
    ComponentNHKeyList GetInterfaceComponentNHKeyList(MulticastGroupObject *obj,
                                                      uint8_t flags);
    void AddMulticastRoute(MulticastGroupObject *obj, const Peer *peer,
                                    uint32_t ethernet_tag,
                                    AgentRouteData *data,
                                    AgentRouteData *bridge_data);
    void DeleteMulticastRoute(const Peer *peer,
                                    const string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr,
                                    uint32_t ethernet_tag,
                                    COMPOSITETYPE comp_type);
    bool FlushPeerInfo(uint64_t peer_sequence);
    void DeleteBroadcast(const Peer *peer,
                         const std::string &vrf_name,
                         uint32_t ethernet_tag,
                         COMPOSITETYPE type);
    void DeleteMulticastObject(const std::string &vrf_name,
                               const Ip4Address &src_addr,
                               const Ip4Address &grp_addr);

    const Agent *agent() const {return agent_;}
    void Terminate();
    void AddBridgeDomain(DBTablePartBase *paritition,
                         DBEntryBase *e);
    const ManagedPhysicalDevicesList &physical_devices() const {
        return physical_devices_;
    }

    void AddLocalPeerRoute(MulticastGroupObject *sg_object);
    void DeleteLocalPeerRoute(MulticastGroupObject *sg_object);
    void CreateMulticastVrfSourceGroup(const std::string &vrf_name,
                                    const std::string &vn_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    void HandleRouteChangeAndMulticastObject(MulticastGroupObject *sg_object,
                                boost::uuids::uuid vm_itf_uuid);
    void DeleteMulticastVrfSourceGroup(const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    bool AddVmInterfaceToVrfSourceGroup(const std::string &vrf_name,
                                    const std::string &vn_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    void DeleteVmInterfaceFromVrfSourceGroup(const std::string &vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    void DeleteVmInterfaceFromVrfSourceGroup(const std::string &vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &grp_addr = Ip4Address());

    void AddVmInterfaceToSourceGroup(const std::string &mvpn_vrf_name,
                                    const std::string &vn_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    void DeleteVmInterfaceFromSourceGroup(const std::string &mvpn_vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);
    void DeleteVmInterfaceFromSourceGroup(const std::string &mvpn_vrf_name,
                                    const VmInterface *vm_itf,
                                    const Ip4Address &grp_addr);
    void DeleteVmInterfaceFromSourceGroup(const std::string &mvpn_vrf_name,
                                    const std::string &vm_vrf_name,
                                    const VmInterface *vm_itf);

    void SetEvpnMulticastSGFlags(const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr,
                                    uint32_t flags);
    uint32_t GetEvpnMulticastSGFlags(const std::string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);

    bool FilterVmi(const VmInterface *vmi);

    static void GetMulticastMacFromIp(const Ip4Address &ip, MacAddress &mac) {
        const Ip4Address::bytes_type &bytes_v4 = ip.to_bytes();
        MacAddress mac_address((unsigned int)0x01,
                            (unsigned int)0x00,
                            (unsigned int)0x5E,
                            (unsigned int)(bytes_v4.at(1)&0x7F),
                            (unsigned int)bytes_v4.at(2),
                            (unsigned int)bytes_v4.at(3));
        mac = mac_address;
    }

private:
    //operations on list of all objectas per group/source/vrf
    void AddToMulticastObjList(MulticastGroupObject *obj) {
        multicast_obj_list_.insert(obj);
    };
    //VM intf add-delete
    void DeleteVmInterface(const VmInterface *intf,
                           const std::string &vrf_name);
    void DeleteVmInterface(const VmInterface *intf,
                           MulticastIntfDBState *state);
    void AddVmInterfaceInFloodGroup(const VmInterface *vm_itf,
                                    MulticastIntfDBState *state);
    void AddVmInterfaceInFloodGroup(const VmInterface *vm_itf,
                                    const std::string &vrf_name);
    void Resync(MulticastGroupObject *obj);
    void DeleteEvpnPath(MulticastGroupObject *obj);

    //broadcast rt add /delete
    void AddL2BroadcastRoute(MulticastGroupObject *obj,
                             const std::string &vrf_name,
                             const std::string &vn_name,
                             const Ip4Address &addr,
                             uint32_t label,
                             int vxlan_id,
                             uint32_t ethernet_tag);
    void ChangeLearningMode(MulticastGroupObject *obj,
                            bool learning_enabled);
    void ChangePbbEtreeMode(MulticastGroupObject *obj,
                            bool pbb_etree_enabled);

    //VM itf to multicast ob
    void AddVmToMulticastObjMap(const boost::uuids::uuid &vm_itf_uuid,
                                MulticastGroupObject *obj) {

        if (this->vm_to_mcobj_list_[vm_itf_uuid].find(obj) ==
                      this->vm_to_mcobj_list_[vm_itf_uuid].end()) {
            this->vm_to_mcobj_list_[vm_itf_uuid].insert(obj);
        }
    };

    bool FindVmToMulticastObjMap(const boost::uuids::uuid &vm_itf_uuid,
                                 MulticastGroupObjectList &objList) {

        VmMulticastGroupObjectList::iterator vmi_it =
            vm_to_mcobj_list_.find(vm_itf_uuid);
        if (vmi_it == vm_to_mcobj_list_.end()) {
            return false;
        }

        objList = this->vm_to_mcobj_list_[vm_itf_uuid];
        return true;
    }

    void DeleteVmToMulticastObjMap(const boost::uuids::uuid &vm_itf_uuid,
                                   const MulticastGroupObject *obj) {
        VmMulticastGroupObjectList::iterator vmi_it =
            vm_to_mcobj_list_.find(vm_itf_uuid);
        if (vmi_it == vm_to_mcobj_list_.end()) {
            return;
        }

        MulticastGroupObjectList::iterator mc_it = vmi_it->second.begin();
        for (;mc_it != vmi_it->second.end(); mc_it++) {
            if (*mc_it == obj) {
                vmi_it->second.erase(mc_it);
                break;
            }
        }

        if (vmi_it->second.size() == 0) {
            vm_to_mcobj_list_.erase(vmi_it);
        }
    };

    std::set<MulticastGroupObject *> &
        GetVmToMulticastObjMap(const boost::uuids::uuid &uuid)
    {
        return vm_to_mcobj_list_[uuid];
    };

    MulticastDBState*
    CreateBridgeDomainMG(DBTablePartBase *p, BridgeDomainEntry *bd);
    void ResyncDependentVrf(MulticastGroupObject *obj);
    void UpdateReference(MulticastGroupObject *obj);
    static MulticastHandler *obj_;

    Agent *agent_;
    std::map<std::string, std::vector<VnIpam> > vrf_ipam_mapping_;
    //VN uuid to VRF name mapping
    std::map<boost::uuids::uuid, string> vn_vrf_mapping_;
    //VM uuid <-> VN uuid
    //List of all multicast objects(VRF/G/S)
    MulticastGroupObjectList multicast_obj_list_;
    //Reference mapping of VM to participating multicast object list
    VmMulticastGroupObjectList vm_to_mcobj_list_;

    DBTable::ListenerId vn_listener_id_;
    DBTable::ListenerId vrf_listener_id_;
    DBTable::ListenerId interface_listener_id_;
    DBTable::ListenerId bridge_domain_id_;
    DBTable::ListenerId physical_device_listener_id_;
    ManagedPhysicalDevicesList physical_devices_;
    AgentRouteWalkerPtr te_walker_;
    DISALLOW_COPY_AND_ASSIGN(MulticastHandler);
};

#endif /* multicast_agent_oper_hpp */
