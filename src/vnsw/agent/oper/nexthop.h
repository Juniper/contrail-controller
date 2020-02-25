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
#include <oper/ecmp_load_balance.h>

class NextHopKey;
class MplsLabel;

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
        VXLAN,
        NATIVE,
        MPLS_OVER_MPLS
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
       case NATIVE:
           return "Native";
       case MPLS_OVER_MPLS:
           return "MPLSoMPLS";
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

        if (type & (1 << NATIVE)) {
            tunnel_type << "Underlay";
        }

        if (type & (1 << MPLS_OVER_MPLS)) {
            tunnel_type << "MPLSoMPLS";
        }

        return tunnel_type.str();
    }

    Type GetType() const {return type_;}
    void SetType(TunnelType::Type type) {type_ = type;}

    static void SetDefaultType(Type type) {default_type_ = type;}
    static Type ComputeType(TypeBmap bmap);
    static Type DefaultMplsComputeType();
    static Type DefaultType() {return default_type_;}
    static TypeBmap DefaultTypeBmap() {return (1 << DefaultType());}
    static TypeBmap VxlanType() {return (1 << VXLAN);};
    static TypeBmap MplsType() {return ((1 << MPLS_GRE) | (1 << MPLS_UDP));};
    static TypeBmap MplsoMplsType() {return (1 << MPLS_OVER_MPLS);};
    static TypeBmap GetTunnelBmap(TunnelType::Type type) {
        if (type == MPLS_GRE || type == MPLS_UDP)
            return TunnelType::MplsType();
        if (type == VXLAN)
            return TunnelType::VxlanType();
        return TunnelType::AllType();
    }
    static TypeBmap AllType() {return ((1 << MPLS_GRE) | (1 << MPLS_UDP) |
                                       (1 << VXLAN));}
    static TypeBmap GREType() {return (1 << MPLS_GRE);}
    static TypeBmap UDPType() {return (1 << MPLS_UDP);}
    static TypeBmap NativeType() {return (1 << NATIVE);}
    static TypeBmap MPLSType() {return (1 << MPLS_OVER_MPLS);}
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
        L2_RECEIVE,
        RECEIVE,
        RESOLVE,
        ARP,
        VRF,
        INTERFACE,
        TUNNEL,
        MIRROR,
        COMPOSITE,
        VLAN,
        PBB
    };

    NextHop(Type type, bool policy) :
        type_(type), valid_(true), policy_(policy), id_(kInvalidIndex),
        mpls_label_(), learning_enabled_(false), etree_leaf_(false) {}
    NextHop(Type type, bool valid, bool policy) :
        type_(type), valid_(valid), policy_(policy), id_(kInvalidIndex),
        mpls_label_(), learning_enabled_(false), etree_leaf_(false) {}
    virtual ~NextHop();

    virtual std::string ToString() const { return "NH";}
    virtual void Add(Agent *agent, const DBRequest *req);
    virtual bool ChangeEntry(const DBRequest *req) = 0;
    virtual void Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual void SetKey(const DBRequestKey *key);
    virtual bool NextHopIsLess(const DBEntry &rhs) const = 0;
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool CanAdd() const = 0;
    virtual bool IsLess(const DBEntry &rhs) const {
        const NextHop &a = static_cast<const NextHop &>(rhs);
        if (type_ != a.type_) {
            return type_ < a.type_;
        }
        if (policy_ != a.policy_) {
            return policy_ < a.policy_;
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

    void ResetMplsRef() {
        if (mpls_label_.get() != NULL) {
            mpls_label_.reset();
        }
    }

    Type GetType() const {return type_;}
    bool IsValid() const {return valid_;};
    bool PolicyEnabled() const {return policy_;};
    uint32_t id() const { return id_;}
    void set_id(uint32_t index) { id_ = index;}

    void set_etree_leaf(bool val) {
        etree_leaf_ = val;
    }

    bool etree_leaf() const {
        return etree_leaf_;
    }

    void set_learning_flag(bool val) {
        learning_enabled_ = val;
    }

    bool learning_enabled() const {
        return learning_enabled_;
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SetNHSandeshData(NhSandeshData &data) const;
    static void FillObjectLogIntf(const Interface *intf,
                                  NextHopObjectLogInfo &info);
    static void FillObjectLogMac(const unsigned char *m,
                                 NextHopObjectLogInfo &info);
    bool NexthopToInterfacePolicy() const;
    const MplsLabel *mpls_label() const {
        return mpls_label_.get();
    }

    virtual bool MatchEgressData(const NextHop *nh) const = 0;
    MplsLabel *AllocateLabel(Agent *agent, const NextHopKey *key);
    virtual bool NeedMplsLabel() = 0;
    void PostAdd();
    void EnqueueResync() const;
protected:
    void FillObjectLog(AgentLogEvent::type event,
                       NextHopObjectLogInfo &info) const;
    Type type_;
    bool valid_;
    bool policy_;
    uint32_t id_;
    MplsLabelRef mpls_label_;
    bool learning_enabled_;
    bool etree_leaf_;
private:
    DISALLOW_COPY_AND_ASSIGN(NextHop);
};

class NextHopData : public AgentData {
public:
    NextHopData() : AgentData(), learning_enabled_(false), etree_leaf_(false) {};
    NextHopData(bool learning_enabled, bool etree_leaf):
        learning_enabled_(learning_enabled), etree_leaf_(etree_leaf) {}
    virtual ~NextHopData() {};
protected:
    bool learning_enabled_;
    bool etree_leaf_;
    DISALLOW_COPY_AND_ASSIGN(NextHopData);
};

class NextHopKey : public AgentKey {
public:
    NextHopKey(NextHop::Type type, bool policy) :
        AgentKey(), type_(type), policy_(policy) { }
    virtual ~NextHopKey() { };

    virtual NextHop *AllocEntry() const = 0;
    virtual NextHopKey *Clone() const = 0;
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        assert(0);
        return false;
    }
    bool IsEqual(const NextHopKey &rhs) const {
        if (type_ != rhs.type_) {
            return false;
        }
        if (policy_ != rhs.policy_) {
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
        if (policy_ != rhs.policy_) {
            return policy_;
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
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        // There is single DiscardNH. There is no field to compare
        return false;
    }

    virtual NextHopKey *Clone() const { return new DiscardNHKey(); }
private:

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
    virtual bool ChangeEntry(const DBRequest *req) { return false; };
    virtual void Delete(const DBRequest *req) {};
    virtual bool NextHopIsLess(const DBEntry &rhs) const { return false; };
    virtual void SetKey(const DBRequestKey *key) { NextHop::SetKey(key); };
    virtual bool CanAdd() const;
    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr(new DiscardNHKey());
    };

    virtual bool MatchEgressData(const NextHop *nh) const {
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }
    static void Create();

private:
    DISALLOW_COPY_AND_ASSIGN(DiscardNH);
};

/////////////////////////////////////////////////////////////////////////////
// Bridge Receive NH definition
/////////////////////////////////////////////////////////////////////////////
class L2ReceiveNHKey : public NextHopKey {
public:
    L2ReceiveNHKey() : NextHopKey(NextHop::L2_RECEIVE, false) { }
    virtual ~L2ReceiveNHKey() { }
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        // There is single Bridge Receive NH. There is no field to compare
        return false;
    }
    virtual NextHopKey *Clone() const { return new L2ReceiveNHKey(); }

private:

    virtual NextHop *AllocEntry() const;
private:
    DISALLOW_COPY_AND_ASSIGN(L2ReceiveNHKey);
};

class L2ReceiveNHData : public NextHopData {
public:
    L2ReceiveNHData() : NextHopData() {};
    virtual ~L2ReceiveNHData() {};
private:
    DISALLOW_COPY_AND_ASSIGN(L2ReceiveNHData);
};

class L2ReceiveNH : public NextHop {
public:
    L2ReceiveNH() : NextHop(L2_RECEIVE, true, false) { };
    virtual ~L2ReceiveNH() { };

    virtual std::string ToString() const { return "L2-Receive"; };
    // No change expected to Discard NH */
    virtual bool ChangeEntry(const DBRequest *req) { return false; };
    virtual void Delete(const DBRequest *req) {};
    virtual bool NextHopIsLess(const DBEntry &rhs) const { return false; };
    virtual void SetKey(const DBRequestKey *key) { NextHop::SetKey(key); };
    virtual bool CanAdd() const;
    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr(new L2ReceiveNHKey());
    };

    virtual bool MatchEgressData(const NextHop *nh) const {
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }
    static void Create();

private:
    DISALLOW_COPY_AND_ASSIGN(L2ReceiveNH);
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
    virtual NextHopKey *Clone() const {
        return new ReceiveNHKey(intf_key_->Clone(), policy_);
    }

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
    virtual bool ChangeEntry(const DBRequest *req) { return false;};
    virtual void Delete(const DBRequest *req) {};
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool CanAdd() const;
    virtual bool NextHopIsLess(const DBEntry &rhs) const {
        const ReceiveNH &a = static_cast<const ReceiveNH &>(rhs);
        return interface_.get() < a.interface_.get();
    };

    virtual KeyPtr GetDBRequestKey() const {
        return DBEntryBase::KeyPtr
            (new ReceiveNHKey(dynamic_cast<InterfaceKey *>(interface_->GetDBRequestKey().release()),
                              policy_));
    };

    static void CreateReq(const string &interface);
    static void Create(NextHopTable *table, const Interface *intf,
                       bool policy);
    static void Delete(NextHopTable *table, const Interface *intf,
                       bool policy);
    const Interface *GetInterface() const {return interface_.get();};

    virtual bool MatchEgressData(const NextHop *nh) const {
        return false;
    }

    virtual bool NeedMplsLabel() { return false; }
private:
    InterfaceRef interface_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveNH);
};

/////////////////////////////////////////////////////////////////////////////
// Resolve NH definition
/////////////////////////////////////////////////////////////////////////////
class ResolveNHKey : public NextHopKey {
public:
    ResolveNHKey(const InterfaceKey *intf_key, bool policy) :
        NextHopKey(NextHop::RESOLVE, policy),
        intf_key_(intf_key->Clone()) { };
    virtual ~ResolveNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new ResolveNHKey(intf_key_->Clone(), policy_);
    }
private:
    friend class ResolveNH;
    boost::scoped_ptr<const InterfaceKey> intf_key_;
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
    ResolveNH(const Interface *intf, bool policy) :
        NextHop(RESOLVE, true, policy), interface_(intf) { };
    virtual ~ResolveNH() { };

    virtual std::string ToString() const { return "Resolve"; };
    // No change expected to Resolve NH */
    virtual bool ChangeEntry(const DBRequest *req) { return false;};
    virtual void Delete(const DBRequest *req) {};
    virtual void SetKey(const DBRequestKey *key) { NextHop::SetKey(key); };
    virtual bool CanAdd() const;
    virtual bool NextHopIsLess(const DBEntry &rhs) const {
        const ResolveNH &a = static_cast<const ResolveNH &>(rhs);
        return interface_.get() < a.interface_.get();
    };
    virtual KeyPtr GetDBRequestKey() const {
        boost::scoped_ptr<InterfaceKey> intf_key(
            static_cast<InterfaceKey *>(interface_->GetDBRequestKey().release()));
        return DBEntryBase::KeyPtr(new ResolveNHKey(intf_key.get(), policy_));
    };
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    static void Create(const InterfaceKey *intf, bool policy);
    static void CreateReq(const InterfaceKey *intf, bool policy);
    const Interface* get_interface() const { return interface_.get();}

    virtual bool MatchEgressData(const NextHop *nh) const {
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }

private:
    InterfaceConstRef interface_;
    DISALLOW_COPY_AND_ASSIGN(ResolveNH);
};

/////////////////////////////////////////////////////////////////////////////
// ARP NH definition
/////////////////////////////////////////////////////////////////////////////
class ArpNHKey : public NextHopKey {
public:
    ArpNHKey(const string &vrf_name, const Ip4Address &ip, bool policy) :
        NextHopKey(NextHop::ARP, policy), vrf_key_(vrf_name), dip_(ip) {
    }
    virtual ~ArpNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new ArpNHKey(vrf_key_.name_, dip_, policy_);
    }
private:
    friend class ArpNH;
    VrfKey vrf_key_;
    Ip4Address dip_;
    DISALLOW_COPY_AND_ASSIGN(ArpNHKey);
};

class ArpNHData : public NextHopData {
public:
    ArpNHData(InterfaceKey *intf_key) :
        NextHopData(), intf_key_(intf_key),
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
        NextHop(ARP, false, false), vrf_(vrf, this), ip_(ip), interface_(), mac_() {};
    virtual ~ArpNH() { };

    virtual std::string ToString() { return "ARP"; }
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const MacAddress &GetMac() const {return mac_;};
    const Interface *GetInterface() const {return interface_.get();};
    const boost::uuids::uuid &GetIfUuid() const;
    const uint32_t vrf_id() const;
    const Ip4Address *GetIp() const {return &ip_;};
    const VrfEntry *GetVrf() const {return vrf_.get();};
    bool GetResolveState() const {return valid_;}
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }

    virtual bool MatchEgressData(const NextHop *nh) const {
        const ArpNH *arp_nh = dynamic_cast<const ArpNH *>(nh);
        if (arp_nh && vrf_ == arp_nh->vrf_ && ip_ == arp_nh->ip_) {
            return true;
        }
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }

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
    TunnelNHKey(const string &vrf_name,
                const Ip4Address &sip,
                const Ip4Address &dip,
                bool policy,
                TunnelType type,
                const MacAddress &rewrite_dmac = MacAddress()) :
        NextHopKey(NextHop::TUNNEL, policy), vrf_key_(vrf_name), sip_(sip),
        dip_(dip), tunnel_type_(type), rewrite_dmac_(rewrite_dmac) {
    };
    virtual ~TunnelNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new TunnelNHKey(vrf_key_.name_, sip_, dip_,
                               NextHopKey::GetPolicy(), tunnel_type_,
                               rewrite_dmac_);
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

        if (rewrite_dmac_ != key.rewrite_dmac_) {
            return rewrite_dmac_ < key.rewrite_dmac_;
        }

        return tunnel_type_.IsLess(key.tunnel_type_);
    }
    void set_tunnel_type(TunnelType tunnel_type) {
        tunnel_type_ = tunnel_type;
    }
    const Ip4Address dip() const {
        return dip_;
    }
protected:
    friend class TunnelNH;
    VrfKey vrf_key_;
    Ip4Address sip_;
    Ip4Address dip_;
    TunnelType tunnel_type_;
    MacAddress rewrite_dmac_;
private:
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
// Labelled Tunnel NH definition
/////////////////////////////////////////////////////////////////////////////
class LabelledTunnelNHKey : public TunnelNHKey {
public:
    LabelledTunnelNHKey(const string &vrf_name,
                const Ip4Address &sip,
                const Ip4Address &dip,
                bool policy,
                TunnelType type,
                const MacAddress &rewrite_dmac = MacAddress(),
                uint32_t label = 3) :
        TunnelNHKey(vrf_name, sip, dip, policy, type, rewrite_dmac),
        transport_mpls_label_(label) {
    };
    virtual ~LabelledTunnelNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const LabelledTunnelNHKey &key = static_cast<const LabelledTunnelNHKey &>(rhs);
            if (vrf_key_.IsEqual(key.vrf_key_) == false) {
                return vrf_key_.IsLess(key.vrf_key_);
            }

            if (sip_ != key.sip_) {
                return sip_ < key.sip_;
            }

            if (dip_ != key.dip_) {
                return dip_ < key.dip_;
            }

            if (rewrite_dmac_ != key.rewrite_dmac_) {
                return rewrite_dmac_ < key.rewrite_dmac_;
            }
            return (transport_mpls_label_ < key.transport_mpls_label_);
    }
    virtual NextHopKey *Clone() const {
        return new LabelledTunnelNHKey(vrf_key_.name_, sip_, dip_,
                               NextHopKey::GetPolicy(), tunnel_type_,
                               rewrite_dmac_, transport_mpls_label_);
    }
private:
    uint32_t transport_mpls_label_;
    friend class LabelledTunnelNH;
    DISALLOW_COPY_AND_ASSIGN(LabelledTunnelNHKey);
};

class LabelledTunnelNHData : public TunnelNHData {
public:
    LabelledTunnelNHData() : TunnelNHData() {};
    virtual ~LabelledTunnelNHData() { };
private:
    friend class LabelledTunnelNH;
    DISALLOW_COPY_AND_ASSIGN(LabelledTunnelNHData);
};

class PBBNHKey : public NextHopKey {
public:
    PBBNHKey(const string &vrf_name, const MacAddress &dest_bmac, uint32_t isid):
        NextHopKey(NextHop::PBB, false), vrf_key_(vrf_name),
        dest_bmac_(dest_bmac), isid_(isid) {
    };
    virtual ~PBBNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new PBBNHKey(vrf_key_.name_, dest_bmac_, isid_);
    }

    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const PBBNHKey &key = static_cast<const PBBNHKey &>(rhs);
        if (vrf_key_.IsEqual(key.vrf_key_) == false) {
            return vrf_key_.IsLess(key.vrf_key_);
        }

        if (dest_bmac_ != key.dest_bmac_) {
            return dest_bmac_ < key.dest_bmac_;
        }

        return isid_ < key.isid_;
    }

    const MacAddress dest_bmac() const {
        return dest_bmac_;
    }
private:
    friend class PBBNH;
    VrfKey vrf_key_;
    MacAddress dest_bmac_;
    uint32_t isid_;
    uint32_t label_;
    NextHopConstRef nh_;
    DISALLOW_COPY_AND_ASSIGN(PBBNHKey);
};

class PBBNHData : public NextHopData {
public:
    PBBNHData() : NextHopData() {};
    virtual ~PBBNHData() { };
private:
    friend class PBBNH;
    DISALLOW_COPY_AND_ASSIGN(PBBNHData);
};

class PBBNH : public NextHop {
public:
    PBBNH(VrfEntry *vrf, const MacAddress &dmac, uint32_t isid);
    virtual ~PBBNH();

    virtual std::string ToString() const {
        return "PBB to " + dest_bmac_.ToString();
    }
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const uint32_t vrf_id() const;
    const VrfEntry *vrf() const {return vrf_.get();};
    const MacAddress dest_bmac() const {return dest_bmac_;};
    const uint32_t isid() const { return isid_;};
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }

    virtual bool MatchEgressData(const NextHop *nh) const {
        const PBBNH *pbb_nh = dynamic_cast<const PBBNH *>(nh);
        if (pbb_nh && vrf_ == pbb_nh->vrf_ && dest_bmac_ == pbb_nh->dest_bmac_) {
            return true;
        }
        return false;
    }

    uint32_t label() const {
        return label_;
    }

    const NextHop *child_nh() const {
        return child_nh_.get();
    }
    virtual bool NeedMplsLabel() { return false; }
private:
    VrfEntryRef vrf_;
    MacAddress dest_bmac_;
    uint32_t isid_;
    uint32_t label_;
    NextHopConstRef child_nh_;
    DISALLOW_COPY_AND_ASSIGN(PBBNH);
};


/////////////////////////////////////////////////////////////////////////////
// Interface NH definition
/////////////////////////////////////////////////////////////////////////////
struct InterfaceNHFlags {
    enum Type {
        INVALID,
        INET4 = 1,
        BRIDGE = 2,
        MULTICAST = 4,
        INET6 = 8,
        VXLAN_ROUTING = 16
    };
};

class InterfaceNHKey : public NextHopKey {
public:
    InterfaceNHKey(InterfaceKey *intf, bool policy, uint8_t flags,
                   const MacAddress &mac) :
        NextHopKey(NextHop::INTERFACE, policy), intf_key_(intf),
        flags_(flags), dmac_(mac) {
            //TODO evpn changes remove this, just extra check
            assert((flags != (InterfaceNHFlags::INVALID)) ||
                    (flags == (InterfaceNHFlags::INET4)) ||
                    (flags_ == (InterfaceNHFlags::INET6)) ||
                    (flags_ == (InterfaceNHFlags::VXLAN_ROUTING)) ||
                    (flags ==
                     (InterfaceNHFlags::INET4|InterfaceNHFlags::MULTICAST)));
    }

    virtual ~InterfaceNHKey() {};
    const boost::uuids::uuid &GetUuid() const {return intf_key_->uuid_;};
    const std::string& name() const { return intf_key_->name_;};
    const Interface::Type &intf_type() const {return intf_key_->type_;}
    const InterfaceKey *intf_key() const { return intf_key_.get(); }
    void set_flags(uint8_t flags) {flags_ = flags;}
    const uint8_t &flags() const { return flags_; }
    const MacAddress &dmac() const { return dmac_; }

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        //TODO evpn changes remove this, just extra check
        assert((flags_ != (InterfaceNHFlags::INVALID)) ||
                (flags_ == (InterfaceNHFlags::INET4)) ||
                (flags_ == (InterfaceNHFlags::INET6)) ||
                (flags_ == (InterfaceNHFlags::VXLAN_ROUTING)) ||
                (flags_ ==
                 (InterfaceNHFlags::INET4|InterfaceNHFlags::MULTICAST)));
        return new InterfaceNHKey(intf_key_->Clone(), policy_, flags_, dmac_);
    }
    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const InterfaceNHKey &key = static_cast<const InterfaceNHKey &>(rhs);
        if (intf_key_->IsEqual(*key.intf_key_.get()) == false) {
            return intf_key_->IsLess(*key.intf_key_.get());
        }

        if (flags_ != key.flags_) {
            return flags_ < key.flags_;
        }

        return dmac_ < key.dmac_;
    }

private:
    friend class InterfaceNH;
    boost::scoped_ptr<InterfaceKey> intf_key_;
    uint8_t flags_;
    MacAddress dmac_;
};

class InterfaceNHData : public NextHopData {
public:
    InterfaceNHData(const string vrf_name) :
        NextHopData(), vrf_key_(vrf_name), layer2_control_word_(false) { }
    InterfaceNHData(const string vrf_name, bool learning_enabled, bool etree_leaf,
                    bool layer2_control_word):
        NextHopData(learning_enabled, etree_leaf), vrf_key_(vrf_name),
        layer2_control_word_(layer2_control_word) {}
    virtual ~InterfaceNHData() { }

private:
    friend class InterfaceNH;
    VrfKey vrf_key_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceNHData);
};

class InterfaceNH : public NextHop {
public:
    InterfaceNH(Interface *intf, bool policy, uint8_t flags,
                const MacAddress &mac) :
        NextHop(INTERFACE, true, policy), interface_(intf),
        flags_(flags), dmac_(mac), vrf_(NULL, this),
        delete_on_zero_refcount_(false) { };
    InterfaceNH(Interface *intf, bool policy, const MacAddress &mac) :
        NextHop(INTERFACE, true, policy), interface_(intf),
        flags_(InterfaceNHFlags::INET4), dmac_(mac), vrf_(NULL, this),
        delete_on_zero_refcount_(false) {};
    virtual ~InterfaceNH() { };

    virtual std::string ToString() const {
        return "InterfaceNH : " + interface_->name();
    };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;
    virtual bool NeedMplsLabel();
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;

    const Interface *GetInterface() const {return interface_.get();};
    const MacAddress &GetDMac() const {return dmac_;};
    bool IsVxlanRouting() const {
        return flags_ & InterfaceNHFlags::VXLAN_ROUTING;
    }
    bool is_multicastNH() const { return flags_ & InterfaceNHFlags::MULTICAST; };
    bool IsBridge() const { return flags_ & InterfaceNHFlags::BRIDGE; };
    uint8_t GetFlags() const {return flags_;};
    const boost::uuids::uuid &GetIfUuid() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};

    static void CreateMulticastVmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                             const MacAddress &dmac,
                                             const string &vrf_name,
                                             const string &intf_name);
    static void DeleteMulticastVmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                             const MacAddress &dmac,
                                             const std::string &intf_name);
    static void CreateL2VmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                      const MacAddress &dmac,
                                      const string &vrf_name,
                                      bool learning_enabled,
                                      bool etree_leaf,
                                      bool layer2_control_word,
                                      const std::string &intf_name);
    static void DeleteL2InterfaceNH(const boost::uuids::uuid &intf_uuid,
                                    const MacAddress &mac,
                                    const std::string &intf_name);
    static void CreateL3VmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                      const MacAddress &dmac,
                                      const string &vrf_name,
                                      bool learning_enabled,
                                      const std::string &intf_name);
    static void DeleteL3InterfaceNH(const boost::uuids::uuid &intf_uuid,
                                    const MacAddress &mac,
                                    const std::string &intf_name);
    static void DeleteNH(const boost::uuids::uuid &intf_uuid,
                         bool policy, uint8_t flags,
                         const MacAddress &mac, const std::string &intf_name);
    static void CreatePacketInterfaceNh(Agent *agent, const string &ifname);
    static void CreateInetInterfaceNextHop(const string &ifname,
                                           const string &vrf_name,
                                           const MacAddress &mac);
    static void DeleteInetInterfaceNextHop(const string &ifname,
                                           const MacAddress &mac);
    static void CreatePhysicalInterfaceNh(const string &ifname,
                                          const MacAddress &mac);
    static void DeletePhysicalInterfaceNh(const string &ifname,
                                          const MacAddress &mac);
    virtual bool DeleteOnZeroRefCount() const {
        return delete_on_zero_refcount_;
    }

    void set_delete_on_zero_refcount(bool val) {
        delete_on_zero_refcount_ = val;
    }

    virtual bool MatchEgressData(const NextHop *nh) const {
        const InterfaceNH *intf_nh =
            dynamic_cast<const InterfaceNH *>(nh);
        if (intf_nh && interface_ == intf_nh->interface_) {
            return true;
        }
        return false;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

private:
    InterfaceRef interface_;
    uint8_t flags_;
    MacAddress dmac_;
    VrfEntryRef vrf_;
    bool delete_on_zero_refcount_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceNH);
};

/////////////////////////////////////////////////////////////////////////////
// VRF NH definition
/////////////////////////////////////////////////////////////////////////////
class VrfNHKey : public NextHopKey {
public:
    VrfNHKey(const string &vrf_name, bool policy, bool bridge_nh) :
        NextHopKey(NextHop::VRF, policy), vrf_key_(vrf_name), policy_(policy),
        bridge_nh_(bridge_nh) {
    }
    virtual ~VrfNHKey() { }

    virtual bool NextHopKeyIsLess(const NextHopKey &rhs) const {
        const VrfNHKey &key = static_cast<const VrfNHKey &>(rhs);
        if (vrf_key_.IsEqual(key.vrf_key_) == false) {
            return vrf_key_.IsLess(key.vrf_key_);
        }

        if (policy_ != key.policy_) {
            return policy_ < key.policy_;
        }
        return bridge_nh_ < key.bridge_nh_;
    }

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new VrfNHKey(vrf_key_.name_, policy_, bridge_nh_);
    }
    const std::string &GetVrfName() const { return vrf_key_.name_; }
    const bool &GetBridgeNh() const { return bridge_nh_; }

private:
    friend class VrfNH;
    VrfKey vrf_key_;
    bool policy_;
    bool bridge_nh_;
    DISALLOW_COPY_AND_ASSIGN(VrfNHKey);
};

class VrfNHData : public NextHopData {
public:
    VrfNHData(bool flood_unknown_unicast, bool learning_enabled,
              bool layer2_control_word):
              NextHopData(learning_enabled, true),
              flood_unknown_unicast_(flood_unknown_unicast),
              layer2_control_word_(layer2_control_word) {}
    virtual ~VrfNHData() { }
private:
    friend class VrfNH;
    bool flood_unknown_unicast_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(VrfNHData);
};

class VrfNH : public NextHop {
public:
    VrfNH(VrfEntry *vrf, bool policy, bool bridge_nh_):
        NextHop(VRF, true, policy), vrf_(vrf, this), bridge_nh_(bridge_nh_),
        flood_unknown_unicast_(false) {}
    virtual ~VrfNH() { };

    virtual std::string ToString() const { return "VrfNH"; };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    // No change expected for VRF Nexthop
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req) {};
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const VrfEntry *GetVrf() const {return vrf_.get();};
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    bool bridge_nh() const { return bridge_nh_; }
    bool flood_unknown_unicast() const {
        return flood_unknown_unicast_;
    }

    virtual bool MatchEgressData(const NextHop *nh) const {
        const VrfNH *vrf_nh = dynamic_cast<const VrfNH *>(nh);
        if (vrf_nh && vrf_ == vrf_nh->vrf_) {
            return true;
        }
        return false;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }
    virtual bool NeedMplsLabel() { return true; }

private:
    VrfEntryRef vrf_;
    // NH created by VXLAN
    bool bridge_nh_;
    bool flood_unknown_unicast_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(VrfNH);
};

/////////////////////////////////////////////////////////////////////////////
// VLAN NH definition
/////////////////////////////////////////////////////////////////////////////
class VlanNHKey : public NextHopKey {
public:
    VlanNHKey(const boost::uuids::uuid &vm_port_uuid, uint16_t vlan_tag) :
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
    const uint16_t vlan_tag() const { return vlan_tag_; }
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
        smac_(), dmac_(), vrf_(NULL, this) { };
    virtual ~VlanNH() { };

    bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    virtual void Delete(const DBRequest *req) {};
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool CanAdd() const;

    const Interface *GetInterface() const {return interface_.get();};
    uint16_t GetVlanTag() const {return vlan_tag_;};
    const boost::uuids::uuid &GetIfUuid() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const MacAddress &GetSMac() const {return smac_;};
    const MacAddress &GetDMac() const {return dmac_;};
    static VlanNH *Find(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag);

    static void Create(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag,
                          const std::string &vrf_name, const MacAddress &smac,
                          const MacAddress &dmac);
    static void Delete(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag);
    static void CreateReq(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag,
                          const std::string &vrf_name, const MacAddress &smac,
                          const MacAddress &dmac);
    static void DeleteReq(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag);

    virtual bool MatchEgressData(const NextHop *nh) const {
        const VlanNH *vlan_nh = dynamic_cast<const VlanNH *>(nh);
        if (vlan_nh && interface_ == vlan_nh->interface_) {
            return true;
        }
        return false;
    }
    virtual bool NeedMplsLabel() { return true; }

private:
    InterfaceRef interface_;
    uint16_t vlan_tag_;
    MacAddress smac_;
    MacAddress dmac_;
    VrfEntryRef vrf_;
    DISALLOW_COPY_AND_ASSIGN(VlanNH);
};

/////////////////////////////////////////////////////////////////////////////
// Component NH definition
/////////////////////////////////////////////////////////////////////////////
//TODO Shift this to class CompositeNH
struct Composite {
    enum Type {
        INVALID,
        FABRIC,
        L3FABRIC,
        L2COMP,
        L3COMP,
        MULTIPROTO,
        ECMP,
        L2INTERFACE,
        L3INTERFACE,
        LOCAL_ECMP,
        EVPN,
        TOR,
        LU_ECMP // label unicast ecmp
    };
};
//TODO remove defines
#define COMPOSITETYPE Composite::Type

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
    ComponentNHKey(int label, const boost::uuids::uuid &intf_uuid,
                   uint8_t flags, const MacAddress &mac):
        label_(label),
        nh_key_(new InterfaceNHKey(
                    new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                    false, flags, mac)) {
    }
    ComponentNHKey(int label, uint8_t tag, const boost::uuids::uuid &intf_uuid):
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

            validate_mcast_src_ = true;
    }

    CompositeNHKey(COMPOSITETYPE type, bool validate_mcast_src, bool policy,
                   const ComponentNHKeyList &component_nh_key_list,
                   const std::string &vrf_name) :
        NextHopKey(NextHop::COMPOSITE, policy),
        composite_nh_type_(type), validate_mcast_src_(validate_mcast_src),
        component_nh_key_list_(component_nh_key_list), vrf_key_(vrf_name){
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
    bool Reorder(Agent *agent, uint32_t label, const NextHop *nh);
    void CreateTunnelNH(Agent *agent);
    void CreateTunnelNHReq(Agent *agent);
    void ChangeTunnelType(TunnelType::Type tunnel_type);
    COMPOSITETYPE composite_nh_type() const {return composite_nh_type_;}
    bool validate_mcast_src() const {return validate_mcast_src_;}

    void ReplaceLocalNexthop(const ComponentNHKeyList &new_comp_nh);
private:
    friend class CompositeNH;
    bool ExpandLocalCompositeNH(Agent *agent);
    void insert(ComponentNHKeyPtr nh_key);
    void erase(ComponentNHKeyPtr nh_key);
    bool find(ComponentNHKeyPtr nh_key);

    COMPOSITETYPE composite_nh_type_;
    bool validate_mcast_src_;
    ComponentNHKeyList component_nh_key_list_;
    VrfKey vrf_key_;
    DISALLOW_COPY_AND_ASSIGN(CompositeNHKey);
};

class CompositeNHData : public NextHopData {
public:
    CompositeNHData() : NextHopData(), pbb_nh_(false),
        layer2_control_word_(false) {}
    CompositeNHData(bool pbb_nh, bool learning_enabled, bool layer2_control_word) :
        NextHopData(learning_enabled, true), pbb_nh_(pbb_nh),
        layer2_control_word_(layer2_control_word) {}
private:
    friend class CompositeNH;
    bool pbb_nh_;
    bool layer2_control_word_;
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
        component_nh_key_list_(component_nh_key_list), vrf_(vrf, this),
        pbb_nh_(false) {
        validate_mcast_src_= true;
        comp_ecmp_hash_fields_.AllocateEcmpFields();
    }

    CompositeNH(COMPOSITETYPE type, bool validate_mcast_src, bool policy,
        const ComponentNHKeyList &component_nh_key_list, VrfEntry *vrf):
        NextHop(COMPOSITE, policy), composite_nh_type_(type),
        validate_mcast_src_(validate_mcast_src),
        component_nh_key_list_(component_nh_key_list), vrf_(vrf, this),
        pbb_nh_(false) {
        comp_ecmp_hash_fields_.AllocateEcmpFields();
    }

    virtual ~CompositeNH() { };
    virtual std::string ToString() const { return "Composite NH"; };
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
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

    uint32_t PickMember(uint32_t seed, uint32_t affinity_index,
                        bool ingress) const;
    const NextHop* GetNH(uint32_t idx) const {
        if (idx >= component_nh_list_.size()) {
            return NULL;
        }
        if (component_nh_list_[idx].get() == NULL) {
            return NULL;
        }
        return (*component_nh_list_[idx]).nh();
    }

    COMPOSITETYPE composite_nh_type() const {
       return composite_nh_type_;
    }

    void set_validate_mcast_src(bool validate_mcast_src) {
       validate_mcast_src_ = validate_mcast_src;
    }

    bool validate_mcast_src() const {
       return validate_mcast_src_;
    }

    bool GetOldNH(const CompositeNHData *data, ComponentNH &);

    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    virtual void OnZeroRefCount() {
        return;
    }
    ComponentNHKeyList AddComponentNHKey(ComponentNHKeyPtr component_nh_key,
                                         bool &comp_nh_policy) const;
    ComponentNHKeyList DeleteComponentNHKey(ComponentNHKeyPtr
                                            component_nh_key,
                                            bool &comp_nh_new_policy) const;
    bool UpdateComponentNHKey(uint32_t label, NextHopKey *nh_key,
        ComponentNHKeyList &component_nh_key_list, bool &comp_nh_policy) const;
    const ComponentNHList& component_nh_list() const {
        return component_nh_list_;
    }
    const ComponentNHKeyList& component_nh_key_list() const {
        return component_nh_key_list_;
    }
    const VrfEntry* vrf() const {
        return vrf_.get();
    }
   uint32_t hash(uint32_t seed, bool ingress) const {
       size_t size = component_nh_list_.size();
       if (size == 0) {
           return kInvalidComponentNHIdx;
       }
       uint32_t idx = seed % size;
       while (component_nh_list_[idx].get() == NULL ||
              component_nh_list_[idx]->nh() == NULL ||
              component_nh_list_[idx]->nh()->IsActive() == false ||
              (ingress == false &&
               component_nh_list_[idx]->nh()->GetType() == NextHop::TUNNEL)) {
           idx = (idx + 1) % size;
           if (idx == seed % size) {
               idx = kInvalidComponentNHIdx;
               break;
           }
       }
       return idx;
   }
   bool HasVmInterface(const VmInterface *vmi) const;
   bool GetIndex(ComponentNH &nh, uint32_t &idx) const;
   const ComponentNH* Get(uint32_t idx) const {
       return component_nh_list_[idx].get();
   }
   CompositeNH* ChangeTunnelType(Agent *agent, TunnelType::Type type) const;
   const NextHop *GetLocalNextHop() const;

   virtual bool MatchEgressData(const NextHop *nh) const {
       return false;
   }
   virtual bool NeedMplsLabel() { return false; }
   uint8_t EcmpHashFieldInUse() const {
        return comp_ecmp_hash_fields_.HashFieldsToUse();
   }
   EcmpHashFields& CompEcmpHashFields() { return comp_ecmp_hash_fields_; }
   void UpdateEcmpHashFieldsUponRouteDelete(Agent *agent, const string &vrf_name);
   bool pbb_nh() const {
       return pbb_nh_;
   }

   bool layer2_control_word() const {
       return layer2_control_word_;
   }

   const Interface *GetFirstLocalEcmpMemberInterface() const;

private:
    void CreateComponentNH(Agent *agent, TunnelType::Type type) const;
    void ChangeComponentNHKeyTunnelType(ComponentNHKeyList &component_nh_list,
                                        TunnelType::Type type) const;
    COMPOSITETYPE composite_nh_type_;
    // For relaxing source check in vrouter for mcast data packets
    // in R5.1 where support is for source outside contrail for <*,G>.
    bool validate_mcast_src_;
    ComponentNHKeyList component_nh_key_list_;
    ComponentNHList component_nh_list_;
    VrfEntryRef vrf_;
    EcmpHashFields comp_ecmp_hash_fields_;
    bool pbb_nh_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(CompositeNH);
};

/////////////////////////////////////////////////////////////////////////////
// NextHop DBTable definition
/////////////////////////////////////////////////////////////////////////////
class NextHopTable : public AgentDBTable {
public:
    static const uint32_t kRpfDisableIndex = 0;
    static const uint32_t kRpfDiscardIndex = 2;

    NextHopTable(DB *db, const std::string &name);
    virtual ~NextHopTable();

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Resync(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);

    virtual void OnZeroRefcount(AgentDBEntry *e);
    void Process(DBRequest &req);
    Interface *FindInterface(const InterfaceKey &key) const;
    VrfEntry *FindVrfEntry(const VrfKey &key) const;
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static NextHopTable *GetInstance() {return nexthop_table_;};

    void set_discard_nh(NextHop *nh) { discard_nh_ = nh; }
    NextHop *discard_nh() const {return discard_nh_;}

    void set_l2_receive_nh(NextHop *nh) { l2_receive_nh_ = nh; }
    NextHop *l2_receive_nh() const {return l2_receive_nh_;}
    // NextHop index managing routines
    void FreeInterfaceId(size_t index) { index_table_.Remove(index); }
    NextHop *FindNextHop(size_t index);
    uint32_t ReserveIndex();
    void CheckVrNexthopLimit();
    uint32_t NhIndexCount() { return index_table_.InUseIndexCount(); }
private:
    NextHop *AllocWithKey(const DBRequestKey *k) const;
    virtual std::auto_ptr<DBEntry> GetEntry(const DBRequestKey *key) const;

    NextHop *discard_nh_;
    NextHop *l2_receive_nh_;
    IndexVector<NextHop *> index_table_;
    static NextHopTable *nexthop_table_;
    DISALLOW_COPY_AND_ASSIGN(NextHopTable);
};
#endif // vnsw_agent_nexthop_hpp
