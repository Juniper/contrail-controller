/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_nexthop_hpp
#define vnsw_agent_nexthop_hpp

#include <netinet/in.h>
#include <net/ethernet.h>

#include <cmn/agent_cmn.h>
#include <agent_types.h>

#include <oper/interface_common.h>
#include <oper/vrf.h>

using namespace boost::uuids;
using namespace std;

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
    static const uint32_t kInvalidIndex = 0xFFFFFFFF;
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
        type_(type), valid_(true), policy_(policy), id_(kInvalidIndex) {}
    NextHop(Type type, bool valid, bool policy) : 
        type_(type), valid_(valid), policy_(policy), id_(kInvalidIndex) {}
    virtual ~NextHop();

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
    uint32_t id() const { return id_;}
    void set_id(uint32_t index) { id_ = index;}

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
    uint32_t id_;
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
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        assert(0);
        return false;
    }
    bool IsEqual(const NextHopKey &rhs) const {
        if (type_ != rhs.type_) {
            return false;
        }
        if (NextHopKeyIsLess(rhs) == false &&
            rhs.NextHopKeyIsLess(*this) == false) {
            return true;
        }
        return false;
    }

    void SetPolicy(bool policy) {
        policy_ = policy;
    };

    NextHop::Type GetType() const {return type_;}
    bool GetPolicy() const {return policy_;}
    bool IsLess(const NextHopKey &rhs) const {
        if (type_ != rhs.type_) {
            return type_ < rhs.type_;
        }
        return NextHopKeyIsLess(rhs);
    }
protected:
    friend class NextHop;
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

    ArpNHData(const MacAddress &mac, InterfaceKey *intf_key,
              bool resolved) : NextHopData(), intf_key_(intf_key), mac_(mac),
        resolved_(resolved), valid_(true) {
    }
    virtual ~ArpNHData() { };

private:
    friend class ArpNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    MacAddress mac_;
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

    const MacAddress &GetMac() const {return mac_;};
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
    MacAddress mac_;
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

    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const TunnelNHKey &key = static_cast<const TunnelNHKey &>(rhs);
        if (vrf_key_.IsEqual(key.vrf_key_) == false) {
            return vrf_key_.IsLess(key.vrf_key_);
        }

        if (sip_ != key.sip_) {
            return sip_ < key.sip_;
        }

        if (dip_ != key.dip_) {
            return dip_ < key.dip_;
        }

        return tunnel_type_.IsLess(key.tunnel_type_);
    }
    void set_tunnel_type(TunnelType tunnel_type) {
        tunnel_type_ = tunnel_type;
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
        INVALID,
        INET4 = 1,
        LAYER2 = 2,
        MULTICAST = 4,
        INET6 = 8
    };
};

class InterfaceNHKey : public NextHopKey {
public:
    InterfaceNHKey(InterfaceKey *intf, bool policy, uint8_t flags) :
        NextHopKey(NextHop::INTERFACE, policy), intf_key_(intf),
        flags_(flags) {
            //TODO evpn changes remove this, just extra check
            assert((flags != (InterfaceNHFlags::INVALID)) ||
                    (flags == (InterfaceNHFlags::INET4)) ||
                    (flags_ == (InterfaceNHFlags::INET6)) ||
                    (flags ==
                     (InterfaceNHFlags::INET4|InterfaceNHFlags::MULTICAST)));
    }

    virtual ~InterfaceNHKey() {};
    const uuid &GetUuid() const {return intf_key_->uuid_;};
    const std::string& name() const { return intf_key_->name_;};

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        //TODO evpn changes remove this, just extra check
        assert((flags_ != (InterfaceNHFlags::INVALID)) ||
                (flags_ == (InterfaceNHFlags::INET4)) ||
                (flags_ == (InterfaceNHFlags::INET6)) ||
                (flags_ ==
                 (InterfaceNHFlags::INET4|InterfaceNHFlags::MULTICAST)));
        return new InterfaceNHKey(intf_key_->Clone(), policy_, flags_);
    }
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const InterfaceNHKey &key = static_cast<const InterfaceNHKey &>(rhs);
        if (intf_key_->IsEqual(*key.intf_key_.get()) == false) {
            return intf_key_->IsLess(*key.intf_key_.get());
        }

        return flags_ < key.flags_;
    }

private:
    friend class InterfaceNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    uint8_t flags_;
};

class InterfaceNHData : public NextHopData {
public:
    InterfaceNHData(const string vrf_name, const MacAddress &dmac) :
        NextHopData(), dmac_(dmac), vrf_key_(vrf_name) { }
    virtual ~InterfaceNHData() { }

private:
    friend class InterfaceNH;
    MacAddress dmac_;
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
    const MacAddress &GetDMac() const {return dmac_;};
    bool is_multicastNH() const { return flags_ & InterfaceNHFlags::MULTICAST; };
    bool IsLayer2() const { return flags_ & InterfaceNHFlags::LAYER2; };
    uint8_t GetFlags() const {return flags_;};
    const uuid &GetIfUuid() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};

    static void CreateMulticastVmInterfaceNH(const uuid &intf_uuid,
                                             const MacAddress &dmac,
                                             const string &vrf_name);
    static void DeleteMulticastVmInterfaceNH(const uuid &intf_uuid);
    static void CreateL2VmInterfaceNH(const uuid &intf_uuid,
                                      const MacAddress &dmac,
                                      const string &vrf_name);
    static void DeleteL2InterfaceNH(const uuid &intf_uuid);
    static void CreateL3VmInterfaceNH(const uuid &intf_uuid,
                                      const MacAddress &dmac,
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
    MacAddress dmac_;
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
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const VlanNHKey &key = static_cast<const VlanNHKey &>(rhs);
        if (intf_key_->IsEqual(*key.intf_key_.get()) == false) {
            return intf_key_->IsLess(*key.intf_key_.get());
        }

        return vlan_tag_ < key.vlan_tag_;
    }
    const boost::uuids::uuid& GetUuid() const {return intf_key_->uuid_;}
    const std::string& name() const { return intf_key_->name_;}
private:
    friend class VlanNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    uint16_t vlan_tag_;
    DISALLOW_COPY_AND_ASSIGN(VlanNHKey);
};

class VlanNHData : public NextHopData {
public:
    VlanNHData(const string vrf_name, const MacAddress &smac,
               const MacAddress &dmac):
        NextHopData(), smac_(smac), dmac_(dmac), vrf_key_(vrf_name) {}
    virtual ~VlanNHData() { }
private:
    friend class VlanNH;
    MacAddress smac_;
    MacAddress dmac_;
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
    const MacAddress &GetSMac() const {return smac_;};
    const MacAddress &GetDMac() const {return dmac_;};
    static VlanNH *Find(const uuid &intf_uuid, uint16_t vlan_tag);

    static void Create(const uuid &intf_uuid, uint16_t vlan_tag,
                          const std::string &vrf_name, const MacAddress &smac,
                          const MacAddress &dmac);
    static void Delete(const uuid &intf_uuid, uint16_t vlan_tag);
    static void CreateReq(const uuid &intf_uuid, uint16_t vlan_tag,
                          const std::string &vrf_name, const MacAddress &smac,
                          const MacAddress &dmac);
    static void DeleteReq(const uuid &intf_uuid, uint16_t vlan_tag);

private:
    InterfaceRef interface_;
    uint16_t vlan_tag_;
    MacAddress smac_;
    MacAddress dmac_;
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
        ECMP,
        L2INTERFACE,
        L3INTERFACE,
        LOCAL_ECMP,
        EVPN
    };
};

//TODO remove defines
#define COMPOSITETYPE Composite::Type
/////////////////////////////////////////////////////////////////////////////
// Component NH definition
/////////////////////////////////////////////////////////////////////////////
class ComponentNH {
public:
    ComponentNH(uint32_t label, const NextHop *nh):
        label_(label), nh_(nh) {}
    ComponentNH():label_(0), nh_(NULL) {}

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

    const NextHop* nh() const {
        return nh_.get();
    }

    uint32_t label() const {
        return label_;
    }
private:
    uint32_t label_;
    NextHopConstRef nh_;
    DISALLOW_COPY_AND_ASSIGN(ComponentNH);
};

typedef boost::shared_ptr<const ComponentNH> ComponentNHPtr;
typedef std::vector<ComponentNHPtr> ComponentNHList;

class ComponentNHKey;
typedef boost::shared_ptr<const ComponentNHKey> ComponentNHKeyPtr;
typedef std::vector<ComponentNHKeyPtr> ComponentNHKeyList;

class ComponentNHKey {
public:
    ComponentNHKey(int label, std::auto_ptr<const NextHopKey> key) :
        label_(label), nh_key_(key) { }
    ComponentNHKey(int label, Composite::Type type, bool policy,
                   const ComponentNHKeyList &component_nh_list,
                   const std::string &vrf_name);
    ComponentNHKey(int label, const uuid &intf_uuid, uint8_t flags):
        label_(label), 
        nh_key_(new InterfaceNHKey(
                    new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                    false, flags)) {
    }
    ComponentNHKey(int label, uint8_t tag, const uuid &intf_uuid):
        label_(label), nh_key_(new VlanNHKey(intf_uuid, tag)) {
    }

    ComponentNHKey(int label, const string &vrf_name, const Ip4Address &sip,
            const Ip4Address &dip, bool policy, TunnelType::TypeBmap bmap) :
        label_(label), nh_key_(new TunnelNHKey(vrf_name, sip, dip, policy,
                                               TunnelType::ComputeType(bmap))) {
    }

    virtual ~ComponentNHKey() { }

    bool operator == (const ComponentNHKey &rhs) const {
        if (label_ != rhs.label_) {
            return false;
        }
        return nh_key_->IsEqual(*(rhs.nh_key_.get()));
    }

    uint32_t label() const { return label_; }
    const NextHopKey* nh_key() const { return nh_key_.get(); }
private:
    uint32_t label_;
    std::auto_ptr<const NextHopKey> nh_key_;
    DISALLOW_COPY_AND_ASSIGN(ComponentNHKey);
};

class CompositeNHKey : public NextHopKey {
public:
    CompositeNHKey(COMPOSITETYPE type, bool policy,
                   const ComponentNHKeyList &component_nh_key_list,
                   const std::string &vrf_name) :
        NextHopKey(NextHop::COMPOSITE, policy),
        composite_nh_type_(type), component_nh_key_list_(component_nh_key_list),
        vrf_key_(vrf_name){
    }

    virtual CompositeNHKey *Clone() const;

    virtual ~CompositeNHKey() {
    }
    virtual NextHop *AllocEntry() const;
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const;

    ComponentNHKeyList::const_iterator begin() const {
        return component_nh_key_list_.begin();
    }

    ComponentNHKeyList::const_iterator end() const {
        return component_nh_key_list_.end();
    }

    const ComponentNHKeyList& component_nh_key_list() const {
        return component_nh_key_list_;
    }
    void Reorder(Agent *agent, uint32_t label, const NextHop *nh);
    void CreateTunnelNH(Agent *agent);
    void CreateTunnelNHReq(Agent *agent);
    void ChangeTunnelType(TunnelType::Type tunnel_type);
private:
    friend class CompositeNH;
    void ExpandLocalCompositeNH(Agent *agent);
    void insert(ComponentNHKeyPtr nh_key);
    void erase(ComponentNHKeyPtr nh_key);
    bool find(ComponentNHKeyPtr nh_key);

    COMPOSITETYPE composite_nh_type_;
    ComponentNHKeyList component_nh_key_list_;
    VrfKey vrf_key_;
    DISALLOW_COPY_AND_ASSIGN(CompositeNHKey);
};

class CompositeNHData : public NextHopData {
public:
    CompositeNHData() : NextHopData() { }
private:
    DISALLOW_COPY_AND_ASSIGN(CompositeNHData);
};

//Composite NH
//* Key of composite NH is list of component NH key(mpls label + Nexthop Key)
//* In data part we maintain list of component NH(mpls label + Nexthop reference)
//* In case of ECMP composite NH ordering of component NH is important, since
//  flows would be pointing to one of component NH, and any change in
//  composite NH should not disturb flow which have been already setup.
//  If one of the component NH gets deleted, then a empty component NH
//  would be installed, which would resulting in kernel trapping flow
//  which are pointing to that component NH
//* In case of multicast composite NH ordering of the component NH is not
//  important
class CompositeNH : public NextHop {
public:
    static const uint32_t kInvalidComponentNHIdx = 0xFFFFFFFF;
    CompositeNH(COMPOSITETYPE type, bool policy,
        const ComponentNHKeyList &component_nh_key_list, VrfEntry *vrf):
        NextHop(COMPOSITE, policy), composite_nh_type_(type),
        component_nh_key_list_(component_nh_key_list), vrf_(vrf) {
    }

    virtual ~CompositeNH() { };
    virtual std::string ToString() const { return "Composite NH"; };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    virtual void SendObjectLog(AgentLogEvent::type event) const;
    ComponentNHList::const_iterator begin() const {
        return component_nh_list_.begin();
    }

    ComponentNHList::const_iterator end() const {
        return component_nh_list_.end();
    }

    size_t ComponentNHCount() const {
        return component_nh_list_.size();
    }
    uint32_t ActiveComponentNHCount() const {
        uint32_t idx = 0;
        uint32_t active_count = 0;
        while (idx < component_nh_list_.size()) {
            if (component_nh_list_[idx].get() != NULL) {
                active_count++;
            }
            idx++;
        }
        return active_count;
    }

    const NextHop* GetNH(uint32_t idx) const {
        if (idx >= component_nh_list_.size()) {
            assert(0);
        }
        if (component_nh_list_[idx].get() == NULL) {
            return NULL;
        }
        return (*component_nh_list_[idx]).nh();
    }

    COMPOSITETYPE composite_nh_type() const {
       return composite_nh_type_;
    }

    bool GetOldNH(const CompositeNHData *data, ComponentNH &);

    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    virtual void OnZeroRefCount() {
        return;
    }
    uint32_t GetRemoteLabel(const Ip4Address &ip) const;
    ComponentNHKeyList AddComponentNHKey(ComponentNHKeyPtr
                                         component_nh_key) const;
    ComponentNHKeyList DeleteComponentNHKey(ComponentNHKeyPtr
                                            component_nh_key) const;
    ComponentNHList& component_nh_list() {
        return component_nh_list_;
    }
    const ComponentNHKeyList& component_nh_key_list() const {
        return component_nh_key_list_;
    }
    const VrfEntry* vrf() const {
        return vrf_.get();
    }
   uint32_t hash(uint32_t seed) const {
       uint32_t idx = seed % component_nh_list_.size();
       while (component_nh_list_[idx].get() == NULL) {
           idx = (idx + 1) % component_nh_list_.size();
           if (idx == seed % component_nh_list_.size()) {
               idx = 0xffff;
               break;
           }
       }
       return idx;
   }
   bool GetIndex(ComponentNH &nh, uint32_t &idx) const;
   const ComponentNH* Get(uint32_t idx) const {
       return component_nh_list_[idx].get();
   }
   CompositeNH* ChangeTunnelType(Agent *agent, TunnelType::Type type) const;
private:
    void CreateComponentNH(Agent *agent, TunnelType::Type type) const;
    void ChangeComponentNHKeyTunnelType(ComponentNHKeyList &component_nh_list,
                                        TunnelType::Type type) const;
    COMPOSITETYPE composite_nh_type_;
    ComponentNHKeyList component_nh_key_list_;
    ComponentNHList component_nh_list_;
    VrfEntryRef vrf_;
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
        Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    }

    virtual void OnZeroRefcount(AgentDBEntry *e);
    void Process(DBRequest &req);
    Interface *FindInterface(const InterfaceKey &key) const;
    VrfEntry *FindVrfEntry(const VrfKey &key) const;
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static NextHopTable *GetInstance() {return nexthop_table_;};

    void set_discard_nh(NextHop *nh) { discard_nh_ = nh; }
    NextHop *discard_nh() const {return discard_nh_;}
    // NextHop index managing routines
    void FreeInterfaceId(size_t index) { index_table_.Remove(index); }
    NextHop *FindNextHop(size_t index);

private:
    NextHop *AllocWithKey(const DBRequestKey *k) const;
    virtual std::auto_ptr<DBEntry> GetEntry(const DBRequestKey *key) const;

    NextHop *discard_nh_;
    IndexVector<NextHop> index_table_;
    static NextHopTable *nexthop_table_;
    DISALLOW_COPY_AND_ASSIGN(NextHopTable);
};
#endif // vnsw_agent_nexthop_hpp
