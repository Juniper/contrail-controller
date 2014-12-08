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
    MulticastDBState(const std::string &vrf_name, const Ip4Address &ip_addr) :
        vrf_name_(vrf_name), ip_addr_(ip_addr) { }

    std::string vrf_name_;
    Ip4Address ip_addr_;
};

typedef std::vector<OlistTunnelEntry> TunnelOlist;

class MulticastGroupObject {
public:
    MulticastGroupObject(const std::string &vrf_name, 
                         const Ip4Address &grp_addr,
                         const std::string &vn_name) :
        vrf_name_(vrf_name), grp_address_(grp_addr), vn_name_(vn_name),
        evpn_mpls_label_(0), vxlan_id_(0), layer2_forwarding_(true),
        peer_identifier_(0), deleted_(false) {
        boost::system::error_code ec;
        src_address_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
        local_olist_.clear();
        tor_olist_.clear();
    };     
    MulticastGroupObject(const std::string &vrf_name, 
                         const Ip4Address &grp_addr,
                         const Ip4Address &src_addr) : 
        vrf_name_(vrf_name), grp_address_(grp_addr), src_address_(src_addr),
        evpn_mpls_label_(0), vxlan_id_(0), layer2_forwarding_(true),
        peer_identifier_(0), deleted_(false) {
        local_olist_.clear();
        tor_olist_.clear();
    };     
    virtual ~MulticastGroupObject() { };

    bool CanBeDeleted() const;
    uint32_t evpn_mpls_label() const {return evpn_mpls_label_;}
    void set_evpn_mpls_label(uint32_t label) {evpn_mpls_label_ = label;}

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
    const std::string &vrf_name() { return vrf_name_; };
    const Ip4Address &GetGroupAddress() { return grp_address_; };
    const Ip4Address &GetSourceAddress() { return src_address_; };
    const std::list<boost::uuids::uuid> &GetLocalOlist() {
        return local_olist_;}
    const std::string &GetVnName() { return vn_name_; };
    bool IsDeleted() { return deleted_; };
    void Deleted(bool val) { deleted_ = val; };
    bool layer2_forwarding() const {return layer2_forwarding_;};
    void SetLayer2Forwarding(bool enable) {layer2_forwarding_ = enable;};
    bool CanUnsubscribe() const {return (deleted_ || !layer2_forwarding_);}
    void set_vxlan_id(uint32_t vxlan_id) {vxlan_id_ = vxlan_id;}
    uint32_t vxlan_id() const {return vxlan_id_;}
    void set_peer_identifier(uint64_t peer_id) {peer_identifier_ = peer_id;}
    uint64_t peer_identifier() {return peer_identifier_;}
    bool AddInTorList(const boost::uuids::uuid &device_uuid,
                      const Ip4Address &ip_addr,
                      uint32_t vxlan_id,
                      uint32_t tunnel_bmap);
    bool DeleteFromTorList(const boost::uuids::uuid &device_uuid,
                           const Ip4Address &ip_addr, uint32_t vxlan_id,
                           uint32_t tunnel_bmap, bool use_uuid_only);
    const OlistTunnelEntry *FindInTorListUsingUuid(const boost::uuids::uuid &device_uuid);
    const OlistTunnelEntry *FindInTorList(const boost::uuids::uuid &device_uuid,
                                          uint32_t vxlan_id,
                                          uint32_t tunnel_bmap);
    const TunnelOlist &tor_olist() const {return tor_olist_;}
    bool UpdateTorAddressInOlist(const boost::uuids::uuid &device_uuid,
                                 const Ip4Address &ip,
                                 uint32_t vxlan_id);

private:

    std::string vrf_name_;
    Ip4Address grp_address_;
    std::string vn_name_;
    Ip4Address src_address_;
    uint32_t evpn_mpls_label_;
    uint32_t vxlan_id_;
    bool layer2_forwarding_;
    uint64_t peer_identifier_;
    bool deleted_;
    std::list<boost::uuids::uuid> local_olist_; /* UUID of local i/f */
    TunnelOlist tor_olist_; 

    friend class MulticastHandler;
    DISALLOW_COPY_AND_ASSIGN(MulticastGroupObject);
};

/* Static class for handling multicast objects common functionalities */
class MulticastHandler {
public:
    static const uint32_t kMulticastTimeout = 5 * 60 * 1000;
    MulticastHandler(Agent *agent);
    virtual ~MulticastHandler() { }

    MulticastGroupObject *CreateMulticastGroupObject(const string &vrf_name,
                                                     const Ip4Address &ip_addr,
                                                     const string &vn_name,
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
    void ModifyTor(DBTablePartBase *partition, DBEntryBase *e);
    void ModifyPhysicalDevice(DBTablePartBase *partition, DBEntryBase *e);

    void HandleTorRoute(DBTablePartBase *partition,
                        DBEntryBase *device_vn_entry);
    void UpdatePhysicalDeviceAddressMap(const boost::uuids::uuid &uuid,
                                        const IpAddress &ip);
    void HandleTor(const VnEntry *vn); 
    void WalkDone();
    bool TorWalker(DBTablePartBase *partition, DBEntryBase *entry);

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
    void RebakeSubnetRoute(const Peer *peer,
                           const std::string &vrf_name,
                           uint32_t label,
                           uint32_t vxlan_id,
                           const std::string &vn_name,
                           bool del_op,
                           const ComponentNHKeyList &comp_nh_list,
                           COMPOSITETYPE comp_type);
    void HandleIpam(const VnEntry *vn);
    void HandleFamilyConfig(const VnEntry *vn);
    void HandleVxLanChange(const VnEntry *vn);
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
                         uint32_t ethernet_tag);
    void DeleteMulticastObject(const std::string &vrf_name,
                               const Ip4Address &grp_addr);

    const Agent *agent() const {return agent_;}
    void Terminate();

private:
    //operations on list of all objectas per group/source/vrf
    void AddToMulticastObjList(MulticastGroupObject *obj) {
        multicast_obj_list_.insert(obj);
    };
    //VM intf add-delete
    void DeleteVmInterface(const Interface *intf);
    void AddVmInterfaceInFloodGroup(const VmInterface *vm_itf);

    //broadcast rt add /delete
    void AddL2BroadcastRoute(MulticastGroupObject *obj,
                             const std::string &vrf_name,
                             const std::string &vn_name,
                             const Ip4Address &addr,
                             uint32_t label,
                             int vxlan_id,
                             uint32_t ethernet_tag);

    //VM itf to multicast ob
    void AddVmToMulticastObjMap(const boost::uuids::uuid &vm_itf_uuid, 
                                MulticastGroupObject *obj) {
        this->vm_to_mcobj_list_[vm_itf_uuid].push_back(obj);
    };
    void DeleteVmToMulticastObjMap(const boost::uuids::uuid &vm_itf_uuid) { 
        if (this->vm_to_mcobj_list_[vm_itf_uuid].size() == 0) {
            std::map<uuid, std::list<MulticastGroupObject *> >::iterator it =
                this->vm_to_mcobj_list_.find(vm_itf_uuid);
            if (it != this->vm_to_mcobj_list_.end()) {
                this->vm_to_mcobj_list_.erase(it);
            }
        }
    };

    std::list<MulticastGroupObject *> &
        GetVmToMulticastObjMap(const boost::uuids::uuid &uuid)
    {
        return this->vm_to_mcobj_list_[uuid];
    };

    static MulticastHandler *obj_;

    Agent *agent_;
    std::map<std::string, std::vector<VnIpam> > vrf_ipam_mapping_;
    //VN uuid to VRF name mapping
    std::map<uuid, string> vn_vrf_mapping_;
    //VM uuid <-> VN uuid
    //List of all multicast objects(VRF/G/S)
    std::set<MulticastGroupObject *> multicast_obj_list_;
    //Reference mapping of VM to participating multicast object list
    std::map<uuid, std::list<MulticastGroupObject *> > vm_to_mcobj_list_;

    DBTable::ListenerId vn_listener_id_;
    DBTable::ListenerId interface_listener_id_;
    DBTable::ListenerId physical_device_vn_listener_id_;
    DBTable::ListenerId physical_device_listener_id_;
    DBTableWalker::WalkId physical_device_vn_walker_id_;
    std::map<uuid, IpAddress> physical_device_uuid_addr_map_;
    DISALLOW_COPY_AND_ASSIGN(MulticastHandler);
};

#endif /* multicast_agent_oper_hpp */
