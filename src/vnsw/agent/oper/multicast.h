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

using namespace boost::uuids;

extern SandeshTraceBufferPtr MulticastTraceBuf;

#define MCTRACE(obj, ...)                                                        \
do {                                                                             \
    Multicast##obj::TraceMsg(MulticastTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);                                                                 \

#define IS_BCAST_MCAST(grp)    ((grp.to_ulong() == 0xFFFFFFFF) || \
                               ((grp.to_ulong() & 0xF0000000) == 0xE0000000))

struct OlistTunnelEntry {
    OlistTunnelEntry() : label_(0), daddr_(0), tunnel_bmap_(0) { }
    OlistTunnelEntry(uint32_t label, const Ip4Address &addr,
                     TunnelType::TypeBmap bmap) : 
        label_(label), daddr_(addr), tunnel_bmap_(bmap) { }
    virtual ~OlistTunnelEntry() { }

    uint32_t label_;
    Ip4Address daddr_;
    TunnelType::TypeBmap tunnel_bmap_;
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
    };     
    MulticastGroupObject(const std::string &vrf_name, 
                         const Ip4Address &grp_addr,
                         const Ip4Address &src_addr) : 
        vrf_name_(vrf_name), grp_address_(grp_addr), src_address_(src_addr),
        evpn_mpls_label_(0), vxlan_id_(0), layer2_forwarding_(true),
        peer_identifier_(0), deleted_(false) {
        local_olist_.clear();
    };     
    virtual ~MulticastGroupObject() { };

    uint32_t evpn_mpls_label() const {return evpn_mpls_label_;}
    void set_evpn_mpls_label(uint32_t label) {evpn_mpls_label_ = label;}

    //Add local member is local VM in server.
    bool AddLocalMember(const uuid &intf_uuid) { 
        if (std::find(local_olist_.begin(), local_olist_.end(), intf_uuid) !=
            local_olist_.end()) {
            return false;
        }
        local_olist_.push_back(intf_uuid); 
        return true;
    };

    //Delete local member from VM list in server 
    bool DeleteLocalMember(const uuid &intf_uuid) { 
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
    const std::list<uuid> &GetLocalOlist() { return local_olist_; };
    const std::string &GetVnName() { return vn_name_; };
    bool IsDeleted() { return deleted_; };
    void Deleted(bool val) { deleted_ = val; };
    bool layer2_forwarding() const {return layer2_forwarding_;};
    void SetLayer2Forwarding(bool enable) {layer2_forwarding_ = enable;};
    bool CanUnsubscribe() const {return (deleted_ || !layer2_forwarding_);}
    void set_vxlan_id(int vxlan_id) {vxlan_id_ = vxlan_id;}
    int vxlan_id() const {return vxlan_id_;}
    void set_peer_identifier(uint64_t peer_id) {peer_identifier_ = peer_id;}
    uint64_t peer_identifier() {return peer_identifier_;}

private:

    std::string vrf_name_;
    Ip4Address grp_address_;
    std::string vn_name_;
    Ip4Address src_address_;
    uint32_t evpn_mpls_label_;
    int vxlan_id_;
    bool layer2_forwarding_;
    uint64_t peer_identifier_;
    bool deleted_;
    std::list<uuid> local_olist_; /* UUID of local i/f */

    friend class MulticastHandler;
    DISALLOW_COPY_AND_ASSIGN(MulticastGroupObject);
};

/* Static class for handling multicast objects common functionalities */
class MulticastHandler {
public:
    static const uint32_t kMulticastTimeout = 5 * 60 * 1000;
    MulticastHandler(Agent *agent);
    virtual ~MulticastHandler() { }

    /* Called by XMPP to add ctrl node sent olist and label */
    static void ModifyFabricMembers(const Peer *peer,
                                    const std::string &vrf_name,
                                    const Ip4Address &group,
                                    const Ip4Address &source, 
                                    uint32_t source_label,
                                    const TunnelOlist &olist,
                                    uint64_t peer_identifier = 0);
    /* Called as a result of XMPP message received with OLIST of
     * evpn endpoints with mpls or vxlan encap
     */
    static void ModifyEvpnMembers(const Peer *peer,
                                  const std::string &vrf_name,
                                  const TunnelOlist &olist,
                                  uint32_t ethernet_tag,
                                  uint64_t peer_identifier = 0);
    //Registered for VN notification
    static void ModifyVN(DBTablePartBase *partition, DBEntryBase *e);
    //Registered for VM notification
    static void ModifyVmInterface(DBTablePartBase *partition, DBEntryBase *e); 
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
                           const ComponentNHKeyList &comp_nh_list);
    void HandleIpam(const VnEntry *vn);
    void HandleFamilyConfig(const VnEntry *vn);
    void HandleVxLanChange(const VnEntry *vn);
    //For test routines to clear all routes and mpls label
    static void Shutdown();
    //Multicast obj list addition deletion
    MulticastGroupObject *FindFloodGroupObject(const std::string &vrf_name);
    MulticastGroupObject *FindActiveGroupObject(const std::string &vrf_name,
                                                const Ip4Address &dip);
    MulticastGroupObject *FindGroupObject(const std::string &vrf_name,
                                          const Ip4Address &dip);
    ComponentNHKeyList GetInterfaceComponentNHKeyList(MulticastGroupObject *obj,
                                                      uint8_t flags);
    bool FlushPeerInfo(uint64_t peer_sequence);
    void DeleteBroadcast(const Peer *peer,
                         const std::string &vrf_name,
                         uint32_t ethernet_tag);

    const Agent *agent() const {return agent_;}
    void Terminate();

private:
    //operations on list of all objectas per group/source/vrf
    void AddToMulticastObjList(MulticastGroupObject *obj) {
        multicast_obj_list_.insert(obj);
    };
    void DeleteMulticastObject(const std::string &vrf_name,
                               const Ip4Address &grp_addr);
    std::set<MulticastGroupObject *> &GetMulticastObjList() {
        return this->multicast_obj_list_;
    };

    //VM intf add-delete
    void DeleteVmInterface(const Interface *intf);
    void AddVmInterfaceInFloodGroup(const std::string &vrf_name, const uuid &itf_uuid, 
                                    const VnEntry *vn);

    //broadcast rt add /delete
    void AddL2BroadcastRoute(MulticastGroupObject *obj,
                             const std::string &vrf_name,
                             const std::string &vn_name,
                             const Ip4Address &addr,
                             uint32_t label,
                             int vxlan_id,
                             uint32_t ethernet_tag);

    //VM itf to multicast ob
    void AddVmToMulticastObjMap(const uuid &vm_itf_uuid, 
                                MulticastGroupObject *obj) {
        this->vm_to_mcobj_list_[vm_itf_uuid].push_back(obj);
    };
    void DeleteVmToMulticastObjMap(const uuid &vm_itf_uuid) { 
        if (this->vm_to_mcobj_list_[vm_itf_uuid].size() == 0) {
            std::map<uuid, std::list<MulticastGroupObject *> >::iterator it =
                this->vm_to_mcobj_list_.find(vm_itf_uuid);
            if (it != this->vm_to_mcobj_list_.end()) {
                this->vm_to_mcobj_list_.erase(it);
            }
        }
    };

    std::list<MulticastGroupObject *> &GetVmToMulticastObjMap(const uuid &uuid)
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
    DISALLOW_COPY_AND_ASSIGN(MulticastHandler);
};

#endif /* multicast_agent_oper_hpp */
