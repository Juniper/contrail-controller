/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_nexthop_hpp
#define vnsw_agent_nexthop_hpp

#include <netinet/in.h>
#include <net/ethernet.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <base/dependency.h>
#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/agent_types.h>

using namespace boost::uuids;

template <typename Member>
class MemberList {
public:
    const static uint32_t kInvalidIndex = 0xffff;
    MemberList(int max_size) : max_size_(max_size), free_index_(0), hash_id(0) {
    }

    MemberList():max_size_(64), free_index_(0), hash_id(0) {
    }

    ~MemberList() {
       for (uint32_t i = 0; i < mbr_list_.size(); i++) {
           if (mbr_list_[i]) {
               delete mbr_list_[i];
           }
       }
    }

    typedef typename std::vector<Member *>::iterator iterator;
    typedef typename std::vector<Member *>::const_iterator const_iterator;

    int insert(const Member &mbr) {
        if (mbr_list_.size() >= kInvalidIndex) {
            return kInvalidIndex;
        }

        if (mbr_list_.size() < free_index_ + 1) {
            mbr_list_.resize(free_index_ + 1);
            hash_table_.resize(free_index_ + 1);
        }

        Member *entry = new Member(mbr);
        mbr_list_[free_index_] = entry;
        UpdateFreeIndex();
        UpdateHashTable();
        return free_index_;
    }

    bool remove(const Member &mbr) {
       uint32_t i = 0;
       for (i = 0; i < mbr_list_.size(); i++) {
           if (mbr_list_[i] && *mbr_list_[i] == mbr) {
               delete mbr_list_[i];
               mbr_list_[i] = NULL;
               break;
           }
       }

       if (i == mbr_list_.size()) {
           return false;
       }
       UpdateFreeIndex();
       UpdateHashTable();
       return true;
    }

    bool remove(uint32_t index) {
        if (index >= mbr_list_.size()) {
            return false;
        }
        if (mbr_list_[index] != NULL) {
            delete mbr_list_[index];
            mbr_list_[index] = NULL;
            UpdateFreeIndex();
            UpdateHashTable();
        }
        return true;
    }

    void UpdateHashTable() {
        hash_table_.clear();
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i] == NULL) {
                hash_table_.push_back(0xffff);
                continue;
            }
            hash_table_.push_back(i);
        }
    }

    void UpdateFreeIndex() {
        uint32_t i;
        for (i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i] == NULL) {
                free_index_ = i;
                return;
            }
        }
        free_index_ = i;
    }

    void UpdateFreeIndex(uint32_t index) {
        if (index > free_index_) {
            return;
        }
        UpdateFreeIndex();
    }

    void replace(std::vector<Member> list) {
        //Add new elements, which are not presnet in member list
        typename std::vector<Member>::const_iterator it = list.begin();
        while (it != list.end()) {
            Member mem = *it;
            if (!Find(mem)) {
                insert(mem);
            }
            it++;
        }

        //Remove elements present member list, but not in new list
        iterator mbr_list_iterator = begin();
        while (mbr_list_iterator != end()) {
            const Member *member = *mbr_list_iterator;
            if (!member) {
                 mbr_list_iterator++;
                 continue;
            }
            it = list.begin();
            while (it != list.end()) {
                const Member *latest_member = &(*it);
                if (latest_member && *latest_member == *member) {
                    break;
                }
                it++;
            }
            if (it == list.end()) {
                remove(*member);
            }
            mbr_list_iterator++;
        }
    }

    void clear() {
        hash_table_.clear();
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i]) {
                delete mbr_list_[i];
            }
        }
        mbr_list_.clear();
        free_index_ = 0;
    }

    size_t HashTableSize() const {
        return hash_table_.size();
    }

    iterator begin() { return iterator(mbr_list_.begin());};
    iterator end() { return iterator(mbr_list_.end());};

    const_iterator begin() const {
        return const_iterator(mbr_list_.begin());
    }
    const_iterator end() const {
        return const_iterator(mbr_list_.end());
    }

    Member* Find(const Member &mem) const {
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i] && *mbr_list_[i] == mem) {
                return mbr_list_[i];
            }
        }
        return NULL;
    }

    Member* Find(const Member &mem, uint32_t &index) const{
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i] && *mbr_list_[i] == mem) {
                index = i;
                return mbr_list_[i];
            }
        }
        return NULL;
    }

    const Member* Get(uint32_t idx) const {
        return mbr_list_[idx];
    }

    size_t size() const {
        return mbr_list_.size();
    }

    uint32_t hash(size_t hash) const {
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (hash_table_[hash % hash_table_.size()] != 0xffff) {
                return hash_table_[hash % hash_table_.size()];
            }
            hash++;
        }
        return 0;
    }

    uint32_t count() const {
        int cnt = 0;
        for (uint32_t i = 0; i < mbr_list_.size(); i++) {
            if (mbr_list_[i] != NULL)
                cnt++;
        }

        return cnt;
    }

private:
    std::vector<Member *> mbr_list_;
    std::vector<uint32_t> hash_table_;
    uint32_t max_size_;
    uint32_t free_index_;
    uint32_t hash_id;
};

/////////////////////////////////////////////////////////////////////////////
// Class to manage supported tunnel-types 
/////////////////////////////////////////////////////////////////////////////
class TunnelType {
public:
    // Various tunnel-types supported
    enum Type {
        INVALID,
        MPLS_GRE,
        MPLS_UDP,
        VXLAN
    };
    // Bitmap of supported tunnel types
    typedef uint32_t TypeBmap;
    typedef std::list<Type> PriorityList;

    TunnelType(Type type) : type_(type) { }
    ~TunnelType() { }
    bool Compare(const TunnelType &rhs) const {
        return type_ == rhs.type_;
    }
    bool IsLess(const TunnelType &rhs) const {
        return type_ < rhs.type_;
    }

   std::string ToString() const {
       switch (type_) {
       case MPLS_GRE:
           return "MPLSoGRE";
       case MPLS_UDP:
           return "MPLSoUDP";
       case VXLAN:
           return "VXLAN";
       default:
           break;
       }
       return "UNKNOWN";
   }

    static std::string GetString(uint32_t type) {
        std::ostringstream tunnel_type;
        if (type & (1 << MPLS_GRE)) {
            tunnel_type << "MPLSoGRE ";
        }

        if (type & (1 << MPLS_UDP)) {
            tunnel_type << "MPLSoUDP ";
        }

        if (type & ( 1 << VXLAN)) {
            tunnel_type << "VxLAN";
        }
        return tunnel_type.str();
    }

    Type GetType() const {return type_;}
    void SetType(TunnelType::Type type) {type_ = type;}

    static void SetDefaultType(Type type) {default_type_ = type;}
    static Type ComputeType(TypeBmap bmap);
    static Type DefaultType() {return default_type_;}
    static TypeBmap DefaultTypeBmap() {return (1 << DefaultType());}
    static TypeBmap VxlanType() {return (1 << VXLAN);};
    static TypeBmap MplsType() {return ((1 << MPLS_GRE) | (1 << MPLS_UDP));};
    static TypeBmap AllType() {return ((1 << MPLS_GRE) | (1 << MPLS_UDP) | 
                                       (1 << VXLAN));}
    static TypeBmap GREType() {return (1 << MPLS_GRE);}
    static TypeBmap UDPType() {return (1 << MPLS_UDP);}
    static bool EncapPrioritySync(const std::vector<std::string> &cfg_list);
    static void DeletePriorityList();

private:
    Type type_;
    static PriorityList priority_list_;
    static Type default_type_;
};

/////////////////////////////////////////////////////////////////////////////
// Base class for NextHop. Implementation of specific NextHop must 
// derive from this class
/////////////////////////////////////////////////////////////////////////////
class NextHop : AgentRefCount<NextHop>, public AgentDBEntry {
public:
    enum Type {
        INVALID,
        DISCARD,
        RECEIVE,
        RESOLVE,
        ARP,
        VRF,
        INTERFACE,
        TUNNEL,
        MIRROR,
        COMPOSITE,
        VLAN
    };

    NextHop(Type type, bool policy) : 
        type_(type), valid_(true), policy_(policy) { };
    NextHop(Type type, bool valid, bool policy) : 
        type_(type), valid_(valid), policy_(policy) { };
    virtual ~NextHop() { };

    virtual std::string ToString() const { return "NH";}
    virtual bool Change(const DBRequest *req) { return false; }
    virtual void Delete(const DBRequest *req) {};
    virtual void SetKey(const DBRequestKey *key);
    virtual bool NextHopIsLess(const DBEntry &rhs) const = 0;
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool CanAdd() const = 0;
    virtual bool IsLess(const DBEntry &rhs) const {
        const NextHop &a = static_cast<const NextHop &>(rhs);
        if (type_ != a.type_) {
            return type_ < a.type_;
        }
        bool ret = NextHopIsLess(rhs);
        return ret;
    }

    virtual bool DeleteOnZeroRefCount() const {
        return false;
    }
    virtual void OnZeroRefCount() {};

    uint32_t GetRefCount() const {
        return AgentRefCount<NextHop>::GetRefCount();
    }

    Type GetType() const {return type_;}
    bool IsValid() const {return valid_;};
    bool PolicyEnabled() const {return policy_;};

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SetNHSandeshData(NhSandeshData &data) const;
    static void FillObjectLogIntf(const Interface *intf,
                                  NextHopObjectLogInfo &info);
    static void FillObjectLogMac(const unsigned char *m,
                                 NextHopObjectLogInfo &info);
protected:
    void FillObjectLog(AgentLogEvent::type event,
                       NextHopObjectLogInfo &info) const;
    Type type_;
    bool valid_;
    bool policy_;
private:
    DISALLOW_COPY_AND_ASSIGN(NextHop);
};

class NextHopData : public AgentData {
public:
    NextHopData() : AgentData() {};
    virtual ~NextHopData() {};
private:
    DISALLOW_COPY_AND_ASSIGN(NextHopData);
};

class NextHopKey : public AgentKey {
public:
    NextHopKey(NextHop::Type type, bool policy) :
        AgentKey(), type_(type), policy_(policy) { }
    virtual ~NextHopKey() { };

    virtual NextHop *AllocEntry() const = 0;
    virtual NextHopKey *Clone() const {return NULL;}
    virtual bool Compare(const NextHopKey &rhs) const {assert(0); return false;}

    void SetPolicy(bool policy) {
        policy_ = policy;
    };

    NextHop::Type GetType() const {return type_;}
    bool GetPolicy() const {return policy_;}
protected:
    friend class NextHop;;
    NextHop::Type type_;
    bool policy_;
private:
    DISALLOW_COPY_AND_ASSIGN(NextHopKey);
};

/////////////////////////////////////////////////////////////////////////////
// Discard NH definition
/////////////////////////////////////////////////////////////////////////////
class DiscardNHKey : public NextHopKey {
public:
    DiscardNHKey() : NextHopKey(NextHop::DISCARD, false) { };
    virtual ~DiscardNHKey() { };

    virtual NextHop *AllocEntry() const;
private:
    DISALLOW_COPY_AND_ASSIGN(DiscardNHKey);
};

class DiscardNHData : public NextHopData {
public:
    DiscardNHData() : NextHopData() {};
    virtual ~DiscardNHData() {};
private:
    DISALLOW_COPY_AND_ASSIGN(DiscardNHData);
};

class DiscardNH : public NextHop {
public:
    DiscardNH() : NextHop(DISCARD, true, false) { };
    virtual ~DiscardNH() { };

    virtual std::string ToString() const { return "DISCARD"; };
    // No change expected to Discard NH */
    virtual bool Change(const DBRequest *req) { return false; };
    virtual void Delete(const DBRequest *req) {};
    virtual bool NextHopIsLess(const DBEntry &rhs) const { return false; };
    virtual void SetKey(const DBRequestKey *key) { NextHop::SetKey(key); };
    virtual bool CanAdd() const;
    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr(new DiscardNHKey());
    };

    static void Create();

private:
    DISALLOW_COPY_AND_ASSIGN(DiscardNH);
};

/////////////////////////////////////////////////////////////////////////////
// Receive NH definition
/////////////////////////////////////////////////////////////////////////////
class ReceiveNHKey : public NextHopKey {
public:
    ReceiveNHKey(InterfaceKey *intf_key, bool policy) :
        NextHopKey(NextHop::RECEIVE, policy), intf_key_(intf_key) {
    }
    virtual ~ReceiveNHKey() { };
    virtual NextHop *AllocEntry() const;

private:
    friend class ReceiveNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveNHKey);
};

class ReceiveNHData : public NextHopData {
public:
    ReceiveNHData() : NextHopData() {};
    virtual ~ReceiveNHData() {};

private:
    friend class ReceiveNH;
    DISALLOW_COPY_AND_ASSIGN(ReceiveNHData);
};

class ReceiveNH : public NextHop {
public:
    ReceiveNH(Interface *intf, bool policy) : 
        NextHop(RECEIVE, true, policy), interface_(intf) { };
    virtual ~ReceiveNH() { };

    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const { return "Local Receive"; };
    // No change expected to Receive NH */
    virtual bool Change(const DBRequest *req) { return false;};
    virtual void Delete(const DBRequest *req) {};
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool CanAdd() const;
    bool NextHopIsLess(const DBEntry &rhs) const {
        const ReceiveNH &a = static_cast<const ReceiveNH &>(rhs);
        if (interface_.get() != a.interface_.get()) {
            return interface_.get() < a.interface_.get();
        }

        return policy_ < a.policy_;
    };

    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr
            (new ReceiveNHKey(dynamic_cast<InterfaceKey *>(interface_->GetDBRequestKey().release()),
                              policy_));
    };

    static void CreateReq(const string &interface);
    static void Create(NextHopTable *table, const string &interface);
    static void Delete(NextHopTable *table, const string &interface);
    const Interface *GetInterface() const {return interface_.get();};
private:
    InterfaceRef interface_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveNH);
};

/////////////////////////////////////////////////////////////////////////////
// Resolve NH definition
/////////////////////////////////////////////////////////////////////////////
class ResolveNHKey : public NextHopKey {
public:
    ResolveNHKey() : NextHopKey(NextHop::RESOLVE, false) { };
    virtual ~ResolveNHKey() { };

    virtual NextHop *AllocEntry() const;
private:
    friend class ResolveNH;
    DISALLOW_COPY_AND_ASSIGN(ResolveNHKey);
};

class ResolveNHData : public NextHopData {
public:
    ResolveNHData() : NextHopData() {};
    virtual ~ResolveNHData() { };

private:
    friend class ResolveNH;
    DISALLOW_COPY_AND_ASSIGN(ResolveNHData);
};

class ResolveNH : public NextHop {
public:
    ResolveNH() : NextHop(RESOLVE, true, false) { };
    virtual ~ResolveNH() { };

    virtual std::string ToString() const { return "Resolve"; };
    // No change expected to Resolve NH */
    virtual bool Change(const DBRequest *req) { return false;};
    virtual void Delete(const DBRequest *req) {};
    virtual void SetKey(const DBRequestKey *key) { NextHop::SetKey(key); };
    virtual bool CanAdd() const;
    virtual bool NextHopIsLess(const DBEntry &rhs) const {
        return false;
    };
    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr(new ResolveNHKey());
    };

    static void Create();
private:
    DISALLOW_COPY_AND_ASSIGN(ResolveNH);
};

/////////////////////////////////////////////////////////////////////////////
// ARP NH definition
/////////////////////////////////////////////////////////////////////////////
class ArpNHKey : public NextHopKey {
public:
    ArpNHKey(const string &vrf_name, const Ip4Address &ip) :
        NextHopKey(NextHop::ARP, false), vrf_key_(vrf_name), dip_(ip) {
    }
    virtual ~ArpNHKey() { };

    virtual NextHop *AllocEntry() const;
private:
    friend class ArpNH;
    VrfKey vrf_key_;
    Ip4Address dip_;
    DISALLOW_COPY_AND_ASSIGN(ArpNHKey);
};

class ArpNHData : public NextHopData {
public:
    ArpNHData() :
        NextHopData(), intf_key_(NULL),
        mac_(), resolved_(false), valid_(false) { };

    ArpNHData(const struct ether_addr &mac, InterfaceKey *intf_key,
              bool resolved) : NextHopData(), intf_key_(intf_key), mac_(mac),
        resolved_(resolved), valid_(true) {
    }
    virtual ~ArpNHData() { };

private:
    friend class ArpNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    struct ether_addr mac_;
    bool resolved_;
    bool valid_;
    DISALLOW_COPY_AND_ASSIGN(ArpNHData);
};

class ArpNH : public NextHop {
public:
    ArpNH(VrfEntry *vrf, const Ip4Address &ip) : 
        NextHop(ARP, false, false), vrf_(vrf), ip_(ip), interface_(), mac_() {};
    virtual ~ArpNH() { };

    virtual std::string ToString() { return "ARP"; }
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const struct ether_addr *GetMac() const {return &mac_;};
    const Interface *GetInterface() const {return interface_.get();};
    const uuid &GetIfUuid() const;
    const uint32_t vrf_id() const;
    const Ip4Address *GetIp() const {return &ip_;};
    const VrfEntry *GetVrf() const {return vrf_.get();};
    bool GetResolveState() const {return valid_;}
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
private:
    VrfEntryRef vrf_;
    Ip4Address ip_;
    InterfaceRef interface_;
    struct ether_addr mac_;
    DISALLOW_COPY_AND_ASSIGN(ArpNH);
};

/////////////////////////////////////////////////////////////////////////////
// Tunnel NH definition
/////////////////////////////////////////////////////////////////////////////
class TunnelNHKey : public NextHopKey {
public:
    TunnelNHKey(const string &vrf_name, const Ip4Address &sip,
                const Ip4Address &dip, bool policy, TunnelType type) :
        NextHopKey(NextHop::TUNNEL, policy), vrf_key_(vrf_name), sip_(sip),
        dip_(dip), tunnel_type_(type) { 
    };
    virtual ~TunnelNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new TunnelNHKey(vrf_key_.name_, sip_, dip_,
                               NextHopKey::GetPolicy(), tunnel_type_);
    }

    virtual bool Compare(const NextHopKey &rhs) const {
        const TunnelNHKey &key = static_cast<const TunnelNHKey &>(rhs);
        if (vrf_key_.Compare(key.vrf_key_) == false)
            return false;

        if (sip_ != key.sip_)
            return false;

        if (dip_ != key.dip_)
            return false;

        return tunnel_type_.Compare(key.tunnel_type_);
    }
private:
    friend class TunnelNH;
    VrfKey vrf_key_;
    Ip4Address sip_;
    Ip4Address dip_;
    TunnelType tunnel_type_;
    DISALLOW_COPY_AND_ASSIGN(TunnelNHKey);
};

class TunnelNHData : public NextHopData {
public:
    TunnelNHData() : NextHopData() {};
    virtual ~TunnelNHData() { };
private:
    friend class TunnelNH;
    DISALLOW_COPY_AND_ASSIGN(TunnelNHData);
};

/////////////////////////////////////////////////////////////////////////////
// Interface NH definition
/////////////////////////////////////////////////////////////////////////////
struct InterfaceNHFlags {
    enum Type {
        INET4 = 1,
        LAYER2 = 2,
        MULTICAST = 4
    };
};

class InterfaceNHKey : public NextHopKey {
public:
    InterfaceNHKey(InterfaceKey *intf, bool policy, uint8_t flags) :
        NextHopKey(NextHop::INTERFACE, policy), intf_key_(intf),
        flags_(flags) {
            //TODO evpn changes remove this, just extra check
            assert((flags != 0) || (flags == 1) ||
                   (flags == 5));
    }

    virtual ~InterfaceNHKey() {};
    const uuid &GetUuid() const {return intf_key_->uuid_;};

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        //TODO evpn changes remove this, just extra check
        assert((flags_ != 0) || (flags_ == 1) ||
               (flags_ == 5));
        return new InterfaceNHKey(intf_key_->Clone(), policy_, flags_);
    }
    virtual bool Compare(const NextHopKey &rhs) const {
        const InterfaceNHKey &key = static_cast<const InterfaceNHKey &>(rhs);
        if (intf_key_->Compare(*key.intf_key_.get()) == false)
            return false;

        return flags_ == key.flags_;
    }

private:
    friend class InterfaceNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    uint8_t flags_;
};

class InterfaceNHData : public NextHopData {
public:
    InterfaceNHData(const string vrf_name, const struct ether_addr &dmac) :
        NextHopData(), dmac_(dmac), vrf_key_(vrf_name) { }
    virtual ~InterfaceNHData() { }

private:
    friend class InterfaceNH;
    struct ether_addr dmac_;
    VrfKey vrf_key_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceNHData);
};

class InterfaceNH : public NextHop {
public:
    InterfaceNH(Interface *intf, bool policy, uint8_t flags) : 
        NextHop(INTERFACE, true, policy), interface_(intf),
        flags_(flags), dmac_() { };
    InterfaceNH(Interface *intf, bool policy) : 
        NextHop(INTERFACE, true, policy), interface_(intf),
        flags_(InterfaceNHFlags::INET4), dmac_() { };
    virtual ~InterfaceNH() { };

    virtual std::string ToString() const { 
        return "InterfaceNH : " + interface_->name();
    };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;
    virtual void SendObjectLog(AgentLogEvent::type event) const;

    const Interface *GetInterface() const {return interface_.get();};
    const struct ether_addr &GetDMac() const {return dmac_;};
    bool is_multicastNH() const { return flags_ & InterfaceNHFlags::MULTICAST; };
    bool IsLayer2() const { return flags_ & InterfaceNHFlags::LAYER2; };
    uint8_t GetFlags() const {return flags_;};
    const uuid &GetIfUuid() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};

    static void CreateMulticastVmInterfaceNH(const uuid &intf_uuid,
                                             const struct ether_addr &dmac, 
                                             const string &vrf_name);
    static void DeleteMulticastVmInterfaceNH(const uuid &intf_uuid);
    static void CreateL2VmInterfaceNH(const uuid &intf_uuid,
                                      const struct ether_addr &dmac, 
                                      const string &vrf_name);
    static void DeleteL2InterfaceNH(const uuid &intf_uuid);
    static void CreateL3VmInterfaceNH(const uuid &intf_uuid,
                                      const struct ether_addr &dmac, 
                                      const string &vrf_name);
    static void DeleteL3InterfaceNH(const uuid &intf_uuid);
    static void DeleteNH(const uuid &intf_uuid, bool policy, uint8_t flags);
    static void DeleteVmInterfaceNHReq(const uuid &intf_uuid);
    static void CreatePacketInterfaceNh(const string &ifname);
    static void DeleteHostPortReq(const string &ifname);
    static void CreateInetInterfaceNextHop(const string &ifname,
                                           const string &vrf_name);
    static void DeleteInetInterfaceNextHop(const string &ifname);

private:
    InterfaceRef interface_;
    uint8_t flags_;
    struct ether_addr dmac_;
    VrfEntryRef vrf_; 
    DISALLOW_COPY_AND_ASSIGN(InterfaceNH);
};

/////////////////////////////////////////////////////////////////////////////
// VRF NH definition
/////////////////////////////////////////////////////////////////////////////
class VrfNHKey : public NextHopKey {
public:
    VrfNHKey(const string &vrf_name, bool policy) :
        NextHopKey(NextHop::VRF, policy), vrf_key_(vrf_name), policy_(policy) {
    }
    virtual ~VrfNHKey() { }

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new VrfNHKey(vrf_key_.name_, policy_); 
    }
private:
    friend class VrfNH;
    VrfKey vrf_key_;
    bool policy_;
    DISALLOW_COPY_AND_ASSIGN(VrfNHKey);
};

class VrfNHData : public NextHopData {
public:
    VrfNHData() : NextHopData() { }
    virtual ~VrfNHData() { }
private:
    friend class VrfNH;
    DISALLOW_COPY_AND_ASSIGN(VrfNHData);
};

class VrfNH : public NextHop {
public:
    VrfNH(VrfEntry *vrf, bool policy) : 
        NextHop(VRF, true, policy), vrf_(vrf) { };
    virtual ~VrfNH() { };

    virtual std::string ToString() const { return "VrfNH"; };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    // No change expected for VRF Nexthop
    virtual bool Change(const DBRequest *req) {return false;};
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const VrfEntry *GetVrf() const {return vrf_.get();};
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }

private:
    VrfEntryRef vrf_;
    DISALLOW_COPY_AND_ASSIGN(VrfNH);
};

/////////////////////////////////////////////////////////////////////////////
// VLAN NH definition
/////////////////////////////////////////////////////////////////////////////
class VlanNHKey : public NextHopKey {
public:
    VlanNHKey(const uuid &vm_port_uuid, uint16_t vlan_tag) :
        NextHopKey(NextHop::VLAN, false), 
        intf_key_(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, vm_port_uuid,
                                     "")),
        vlan_tag_(vlan_tag) {
    }
    VlanNHKey(InterfaceKey *key, uint16_t vlan_tag) :
        NextHopKey(NextHop::VLAN, false), intf_key_(key), vlan_tag_(vlan_tag) {
    }
    
    virtual ~VlanNHKey() {}
    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new VlanNHKey(intf_key_->Clone(), vlan_tag_);
    }
    virtual bool Compare(const NextHopKey &rhs) const {
        const VlanNHKey &key = static_cast<const VlanNHKey &>(rhs);
        if (intf_key_->Compare(*key.intf_key_.get()) == false)
            return false;

        return vlan_tag_ == key.vlan_tag_;
    }
private:
    friend class VlanNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    uint16_t vlan_tag_;
    DISALLOW_COPY_AND_ASSIGN(VlanNHKey);
};

class VlanNHData : public NextHopData {
public:
    VlanNHData(const string vrf_name, const struct ether_addr &smac, 
               const struct ether_addr &dmac):
        NextHopData(), smac_(smac), dmac_(dmac), vrf_key_(vrf_name) {}
    virtual ~VlanNHData() { }
private:
    friend class VlanNH;
    struct ether_addr smac_;
    struct ether_addr dmac_;
    VrfKey vrf_key_;
    DISALLOW_COPY_AND_ASSIGN(VlanNHData);
};

class VlanNH : public NextHop {
public:
    VlanNH(Interface *intf, uint32_t vlan_tag):
        NextHop(VLAN, true, false), interface_(intf), vlan_tag_(vlan_tag),
        smac_(), dmac_(), vrf_(NULL) { };
    virtual ~VlanNH() { };

    bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    virtual void Delete(const DBRequest *req) {};
    virtual bool Change(const DBRequest *req);
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const Interface *GetInterface() const {return interface_.get();};
    uint16_t GetVlanTag() const {return vlan_tag_;};
    const uuid &GetIfUuid() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const struct ether_addr &GetSMac() const {return smac_;};
    const struct ether_addr &GetDMac() const {return dmac_;};
    static VlanNH *Find(const uuid &intf_uuid, uint16_t vlan_tag);

    static void Create(const uuid &intf_uuid, uint16_t vlan_tag, 
                          const std::string &vrf_name, const ether_addr &smac,
                          const ether_addr &dmac);
    static void Delete(const uuid &intf_uuid, uint16_t vlan_tag);

private:
    InterfaceRef interface_;
    uint16_t vlan_tag_;
    struct ether_addr smac_;
    struct ether_addr dmac_;
    VrfEntryRef vrf_; 
    DISALLOW_COPY_AND_ASSIGN(VlanNH);
};

//TODO Shift this to class CompositeNH
struct Composite {
    enum Type {
        FABRIC,
        L2COMP,
        L3COMP,
        MULTIPROTO,
        ECMP
    };
};

//TODO remove defines
#define COMPOSITETYPE Composite::Type
/////////////////////////////////////////////////////////////////////////////
// Component NH definition
/////////////////////////////////////////////////////////////////////////////
class CompositeNHKey : public NextHopKey {
public: 
    CompositeNHKey(const string &vrf_name, const Ip4Address &dip, 
                   const Ip4Address &sip, bool local, COMPOSITETYPE type) :
        NextHopKey(NextHop::COMPOSITE, false), vrf_key_(vrf_name), sip_(sip),
        dip_(dip), plen_(32), is_mcast_nh_(true), is_local_ecmp_nh_(local),
        comp_type_(type) {
    }

    CompositeNHKey(const string &vrf_name, const Ip4Address &dip, uint8_t plen,
            bool local):
        NextHopKey(NextHop::COMPOSITE, false), vrf_key_(vrf_name), sip_(0),
        dip_(dip), plen_(plen), is_mcast_nh_(false), is_local_ecmp_nh_(local),
        comp_type_(Composite::ECMP) {
    }

    virtual CompositeNHKey *Clone() const {
        if (is_mcast_nh_) {
            return new CompositeNHKey(vrf_key_.name_, dip_, sip_,
                                      is_local_ecmp_nh_, comp_type_);
        } else {
            return new CompositeNHKey(vrf_key_.name_, dip_, plen_,
                                      is_local_ecmp_nh_);
        }
    }

    virtual ~CompositeNHKey() { }
    virtual NextHop *AllocEntry() const;
private:
    friend class CompositeNH;
    VrfKey vrf_key_;
    Ip4Address sip_;
    Ip4Address dip_;
    uint8_t plen_;
    bool is_mcast_nh_;
    bool is_local_ecmp_nh_;
    COMPOSITETYPE comp_type_;
    DISALLOW_COPY_AND_ASSIGN(CompositeNHKey);
};

struct ComponentNH {
    ComponentNH(): label_(0), nh_(NULL) { }

    ComponentNH(uint32_t label, NextHop *nh): label_(label), nh_(nh) {
    }

    bool operator == (const ComponentNH &rhs) const {
        if (label_ == rhs.label_ && nh_.get() == rhs.nh_.get()) {
            return true;
        }

        return false;
    }

    std::string ToString() {
        return nh_->ToString();
    }

    void SetNH(NextHop *nh) {
        nh_ = nh;
    }

    const NextHop* GetNH() const {
        return nh_.get();
    }

    uint32_t label() const {
        return label_;
    }

    uint32_t label_;
    NextHopRef nh_;
};

class ComponentNHData {
public:
    ComponentNHData(const ComponentNHData &rhs) :
        label_(rhs.label_), nh_key_(rhs.nh_key_->Clone()) {
    }

    ComponentNHData(int label, const uuid &intf_uuid, uint8_t flags) :
        label_(label), 
        nh_key_(new InterfaceNHKey(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                      intf_uuid, ""), false,
                                   flags)) {
    }

    ComponentNHData(int label, const uuid &intf_uuid, uint16_t tag,
                    bool is_mcast) :
        label_(label), nh_key_(new VlanNHKey(intf_uuid, tag)) {
    }

    ComponentNHData(int label, const string &vrf_name, const Ip4Address &sip,
                    const Ip4Address &dip, bool policy,
                    TunnelType::TypeBmap bmap) :
        label_(label), nh_key_(new TunnelNHKey(vrf_name, sip, dip, policy,
                                               TunnelType::ComputeType(bmap))),
        type_bmap_(bmap) {
    }

    ComponentNHData(const string &vrf_name, const Ip4Address &dip,
                    const Ip4Address &sip, bool is_local, 
                    COMPOSITETYPE type) :
        label_(0), nh_key_(new CompositeNHKey(vrf_name, dip, sip, 
                                             is_local, type)) {
    }

    ComponentNHData(int label, NextHopKey *key) :
        label_(label), nh_key_(key->Clone()) {
    }

    virtual ~ComponentNHData() {
        if (nh_key_) {
            delete nh_key_;
        }
    }

    ComponentNHData& operator= (const ComponentNHData& rhs) {
        label_ = rhs.label_;
        if (nh_key_) {
            delete nh_key_;
        }
        nh_key_ = rhs.nh_key_->Clone();
        return *this;
    }

    bool operator == (const ComponentNHData &rhs) const {
        if (label_ != rhs.label_) {
            return false;
        }

        return nh_key_->Compare(*(rhs.nh_key_));
    }

    typedef std::vector<ComponentNHData> ComponentNHDataList;
    uint32_t label_;
    NextHopKey *nh_key_;
    TunnelType::TypeBmap type_bmap_;

private:
};

class CompositeNHData : public NextHopData {
public:
    enum operation {
        ADD,
        DELETE,
        REPLACE,
        REBAKE
    };
    CompositeNHData(operation op) : NextHopData(), op_(op) { };
    CompositeNHData(const std::vector<ComponentNHData> &data, operation op) : 
        NextHopData(), data_(data), op_(op) {};
private:
    friend class CompositeNH;
    std::vector<ComponentNHData> data_;
    operation op_;
    DISALLOW_COPY_AND_ASSIGN(CompositeNHData);
};

class CompositeNH : public NextHop {
public:
    typedef MemberList<ComponentNH> ComponentNHList;
    typedef DependencyList<CompositeNH, CompositeNH>::iterator iterator;
    typedef DependencyList<CompositeNH, CompositeNH>::const_iterator
        const_iterator;

    static const uint32_t kInvalidComponentNHIdx = 0xFFFFFFFF;
    CompositeNH(VrfEntry *vrf_, Ip4Address grp_addr, Ip4Address src_addr,
        COMPOSITETYPE type) : NextHop(COMPOSITE, true, false), vrf_(vrf_), 
        grp_addr_(grp_addr), plen_(32), src_addr_(src_addr),
        is_local_ecmp_nh_(false), local_comp_nh_(this), comp_type_(type) {
    };

    CompositeNH(VrfEntry *vrf_, Ip4Address dip, uint8_t plen, bool local_ecmp,
        COMPOSITETYPE type) : NextHop(COMPOSITE, true, true), vrf_(vrf_), 
        grp_addr_(dip), plen_(plen), src_addr_(0),
        is_local_ecmp_nh_(local_ecmp), 
        local_comp_nh_(this), comp_type_(type) {
    };

    virtual ~CompositeNH() { };

    virtual std::string ToString() const { return "Composite NH"; };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const Ip4Address &GetGrpAddr() const { return grp_addr_; };
    const Ip4Address &GetSrcAddr() const { return src_addr_; };
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const std::string &vrf_name() const {return GetVrf()->GetName();};
    //const uint32_t GetSubNHListCount() const { return olist_->size(); };
    //const std::list<NextHopRef> *GetSubNHList() const { return olist_; };
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    void Sync(bool deleted);
    static void CreateCompositeNH(const std::string vrf_name, 
                                  const Ip4Address ip,
                                  bool is_local_ecmp_nh,
                                  std::vector<ComponentNHData> comp_nh_list);
    static void CreateCompositeNH(const std::string vrf_name, 
                                  const Ip4Address source, 
                                  const Ip4Address group,
                                  COMPOSITETYPE comp_type,
                                  std::vector<ComponentNHData> comp_nh_list);
    static void CreateComponentNH(std::vector<ComponentNHData> comp_nh_list);
    static void AppendComponentNH(const std::string vrf_name, 
                                  const Ip4Address ip, uint8_t plen,
                                  bool is_local_ecmp_nh,
                                  ComponentNHData comp_nh);
    static void DeleteComponentNH(const std::string vrf_name, 
                                  const Ip4Address ip, uint8_t plen,
                                  bool is_local_ecmp_nh,
                                  ComponentNHData comp_nh);

    ComponentNHList::iterator begin() {
        return component_nh_list_.begin();
    }

    ComponentNHList::iterator end() {
        return component_nh_list_.end();
    }

    ComponentNHList::const_iterator begin() const {
        return component_nh_list_.begin();
    }

    ComponentNHList::const_iterator end() const {
        return component_nh_list_.end();
    }

    size_t ComponentNHCount() const {
        return component_nh_list_.HashTableSize();
    }
    uint32_t ActiveComponentNHCount() const {
        return component_nh_list_.count();
    }

    const ComponentNHList* GetComponentNHList() const {
        return &component_nh_list_;
    }

    const NextHop* GetNH(uint32_t idx) const {
        if (component_nh_list_.Get(idx)) {
            return component_nh_list_.Get(idx)->GetNH();
        } 
        return NULL;
    }
 
    COMPOSITETYPE CompositeType() const {
        return comp_type_;
    }

    bool IsEcmpNH() const {
        return (comp_type_ == Composite::ECMP);
    }

    bool IsMcastNH() const {
        return (comp_type_ != Composite::ECMP);
    }

    uint32_t GetRemoteLabel(Ip4Address ip) const;
    bool IsLocal() const {
        return is_local_ecmp_nh_;
    }

    const CompositeNH* GetLocalCompositeNH() const {
        return local_comp_nh_.get();
    }

    bool GetOldNH(const CompositeNHData *data, ComponentNH &);

    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    virtual void OnZeroRefCount() {
        component_nh_list_.clear();
    }
    uint32_t prefix_len() const { return plen_;}
    const NextHop* GetLocalNextHop() const;
private:
    VrfEntryRef vrf_;
    Ip4Address grp_addr_;
    uint8_t plen_;
    Ip4Address src_addr_;
    ComponentNHList component_nh_list_;
    bool is_local_ecmp_nh_;
    DependencyRef<CompositeNH, CompositeNH> local_comp_nh_;
    COMPOSITETYPE comp_type_;
    DEPENDENCY_LIST(CompositeNH, CompositeNH, remote_comp_nh_list_);
    DISALLOW_COPY_AND_ASSIGN(CompositeNH);
};

/////////////////////////////////////////////////////////////////////////////
// NextHop DBTable definition
/////////////////////////////////////////////////////////////////////////////
class NextHopTable : public AgentDBTable {
public:
    NextHopTable(DB *db, const std::string &name);
    virtual ~NextHopTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        NextHop *nh = static_cast<NextHop *>(entry);
        bool ret = nh->Change(req);
        nh->SendObjectLog(AgentLogEvent::CHANGE);
        return ret;
    }

    virtual bool Resync(DBEntry *entry, DBRequest *req) {
        NextHop *nh = static_cast<NextHop *>(entry);
        bool ret = nh->Change(req);
        nh->SendObjectLog(AgentLogEvent::RESYNC);
        return ret;
    }

    virtual void Delete(DBEntry *entry, const DBRequest *req) {
        NextHop *nh = static_cast<NextHop *>(entry);
        nh->Delete(req);
        nh->SendObjectLog(AgentLogEvent::DELETE);
    }

    static void Delete(NextHopKey *key) {
        DBRequest req;
        req.key.reset(key);
        req.data.reset(NULL);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    }

    virtual void OnZeroRefcount(AgentDBEntry *e);
    void Process(DBRequest &req);
    Interface *FindInterface(const InterfaceKey &key) const;
    VrfEntry *FindVrfEntry(const VrfKey &key) const;
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static NextHopTable *GetInstance() {return nexthop_table_;};

    void set_discard_nh(NextHop *nh) { discard_nh_ = nh; }
    NextHop *discard_nh() const {return discard_nh_;}

private:
    NextHop *AllocWithKey(const DBRequestKey *k) const;
    virtual std::auto_ptr<DBEntry> GetEntry(const DBRequestKey *key) const;

    NextHop *discard_nh_;
    static NextHopTable *nexthop_table_;
    DISALLOW_COPY_AND_ASSIGN(NextHopTable);
};
#endif // vnsw_agent_nexthop_hpp
