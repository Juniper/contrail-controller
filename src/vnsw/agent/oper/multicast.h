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

extern SandeshTraceBufferPtr MulticastTraceBuf;

#define MCTRACE(obj, ...)                                                        \
do {                                                                             \
    Multicast##obj::TraceMsg(MulticastTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);                                                                 \

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
        pbb_etree_enabled_(false), bridge_domain_(NULL) {
        boost::system::error_code ec;
        src_address_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
        local_olist_.clear();
    };     
    MulticastGroupObject(const std::string &vrf_name,
                         const Ip4Address &grp_addr,
                         const Ip4Address &src_addr) : 
        vrf_name_(vrf_name), grp_address_(grp_addr), src_address_(src_addr),
        vxlan_id_(0), peer_identifier_(0), deleted_(false), vn_(NULL),
        dependent_mg_(this, NULL), pbb_vrf_(false), pbb_vrf_name_(""),
        peer_(NULL), fabric_label_(0), learning_enabled_(false),
        pbb_etree_enabled_(false), bridge_domain_(NULL) {
        local_olist_.clear();
    };
    virtual ~MulticastGroupObject() { };

    bool CanBeDeleted() const;

    //Add local member is local VM in server.
    bool AddLocalMember(const boost::uuids::uuid &intf_uuid) { 
        if (std::find(local_olist_.begin(), local_olist_.end(), intf_uuid) !=
            local_olist_.end()) {
            return false;
        }
        local_olist_.push_back(intf_uuid); 
        return true;
    };

    //Delete local member from VM list in server 
    bool DeleteLocalMember(const boost::uuids::uuid &intf_uuid) { 
        std::list<uuid>::iterator it = std::find(local_olist_.begin(), 
                                                 local_olist_.end(), intf_uuid);
        if (it != local_olist_.end()) {
            local_olist_.erase(it); 
            return true;
        }
        return false;
    };
    uint32_t GetLocalListSize() { return local_olist_.size(); };

    //Labels for server + server list + ingress source label
    void FlushAllPeerInfo(const Agent *agent,
                          const Peer *peer,
                          uint64_t peer_identifier);

    //Gets
    const std::string &vrf_name() const { return vrf_name_; };
    const Ip4Address &GetGroupAddress() { return grp_address_; };
    const Ip4Address &GetSourceAddress() { return src_address_; };
    const std::list<boost::uuids::uuid> &GetLocalOlist() {
        return local_olist_;}
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
    std::list<boost::uuids::uuid> local_olist_; /* UUID of local i/f */
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
    DISALLOW_COPY_AND_ASSIGN(MulticastGroupObject);
};

/* Static class for handling multicast objects common functionalities */
class MulticastHandler {
public:
    static const uint32_t kMulticastTimeout = 5 * 60 * 1000;
    static const Ip4Address kBroadcast;
    typedef std::list<MulticastGroupObject *> MulticastGroupObjectList;
    typedef std::map<uuid, MulticastGroupObjectList> VmMulticastGroupObjectList;

    MulticastHandler(Agent *agent);
    virtual ~MulticastHandler() { }

    MulticastGroupObject *CreateMulticastGroupObject(const string &vrf_name,
                                                     const Ip4Address &ip_addr,
                                                     const VnEntry *vn,
                                                     uint32_t vxlan_id);

    /* Called by XMPP to add ctrl node sent olist and label */
    void ModifyFabricMembers(const Peer *peer,
                             const std::string &vrf_name,
                             const Ip4Address &group,
                             const Ip4Address &source, 
                             uint32_t source_label,
                             const TunnelOlist &olist,
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

    //Registered for VN notification
    void ModifyVN(DBTablePartBase *partition, DBEntryBase *e);
    //Registered for VM notification
    void ModifyVmInterface(DBTablePartBase *partition, DBEntryBase *e); 
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
    std::set<MulticastGroupObject *> &GetMulticastObjList() {
        return multicast_obj_list_;
    };
    MulticastGroupObject *FindGroupObject(const std::string &vrf_name,
                                          const Ip4Address &dip);
    ComponentNHKeyList GetInterfaceComponentNHKeyList(MulticastGroupObject *obj,
                                                      uint8_t flags);
    bool FlushPeerInfo(uint64_t peer_sequence);
    void DeleteBroadcast(const Peer *peer,
                         const std::string &vrf_name,
                         uint32_t ethernet_tag,
                         COMPOSITETYPE type);
    void DeleteMulticastObject(const std::string &vrf_name,
                               const Ip4Address &grp_addr);

    const Agent *agent() const {return agent_;}
    void Terminate();
    void AddBridgeDomain(DBTablePartBase *paritition,
                         DBEntryBase *e);

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
        this->vm_to_mcobj_list_[vm_itf_uuid].push_back(obj);
    };

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

    std::list<MulticastGroupObject *> &
        GetVmToMulticastObjMap(const boost::uuids::uuid &uuid)
    {
        return this->vm_to_mcobj_list_[uuid];
    };

    MulticastDBState*
    CreateBridgeDomainMG(DBTablePartBase *p, BridgeDomainEntry *bd);
    void ResyncDependentVrf(MulticastGroupObject *obj);
    void UpdateReference(MulticastGroupObject *obj);
    static MulticastHandler *obj_;

    Agent *agent_;
    std::map<std::string, std::vector<VnIpam> > vrf_ipam_mapping_;
    //VN uuid to VRF name mapping
    std::map<uuid, string> vn_vrf_mapping_;
    //VM uuid <-> VN uuid
    //List of all multicast objects(VRF/G/S)
    std::set<MulticastGroupObject *> multicast_obj_list_;
    //Reference mapping of VM to participating multicast object list
    VmMulticastGroupObjectList vm_to_mcobj_list_;

    DBTable::ListenerId vn_listener_id_;
    DBTable::ListenerId interface_listener_id_;
    DBTable::ListenerId bridge_domain_id_;
    DISALLOW_COPY_AND_ASSIGN(MulticastHandler);
};

#endif /* multicast_agent_oper_hpp */
