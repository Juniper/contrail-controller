/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mirror_table_hpp
#define vnsw_agent_mirror_table_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

struct MirrorEntryKey : public AgentKey {
    MirrorEntryKey(const std::string analyzer_name)
            : analyzer_name_(analyzer_name) {
    }
    std::string analyzer_name_;
};

struct MirrorEntryData : public AgentData {
    enum MirrorEntryFlags {
        DynamicNH_With_JuniperHdr = 1,
        DynamicNH_Without_JuniperHdr = 2,
        StaticNH_With_JuniperHdr = 3,
        StaticNH_Without_JuniperHdr = 4

    };

    MirrorEntryData(const std::string vrf_name, const IpAddress &sip,
                    const uint16_t sport, const IpAddress &dip,
                    const uint16_t dport, uint8_t mirror_flags,
                    uint32_t vni, const MacAddress &mac, bool createdvrf):
            vrf_name_(vrf_name), sip_(sip), sport_(sport), dip_(dip),
            dport_(dport), mirror_flags_(mirror_flags), vni_(vni),
            mac_(mac), createdvrf_(createdvrf),
            nic_assisted_mirroring_(false) { }

    MirrorEntryData(bool nic_assisted_mirroring,
            uint16_t nic_assisted_mirroring_vlan):
            nic_assisted_mirroring_(nic_assisted_mirroring),
            nic_assisted_mirroring_vlan_(nic_assisted_mirroring_vlan) {  }


    std::string vrf_name_;
    IpAddress sip_;
    uint16_t sport_;
    IpAddress dip_;
    uint16_t dport_;
    uint8_t mirror_flags_;
    uint32_t vni_;
    MacAddress mac_; // can be type of vtep mac or analyzer-mac based on type NH
    bool createdvrf_;
    bool nic_assisted_mirroring_;
    uint16_t nic_assisted_mirroring_vlan_;
};

class MirrorEntry : AgentRefCount<MirrorEntry>, public AgentDBEntry {
public:
    MirrorEntry(std::string analyzer_name) :
           analyzer_name_(analyzer_name), vrf_(NULL, this), nh_(NULL),
           vrf_name_(""), createdvrf_(false) { };
    virtual ~MirrorEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual bool Change(const DBRequest *req) {return false;};

    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    virtual std::string ToString() const { return "MirrorEntry";};

    uint32_t GetRefCount() const {
        return AgentRefCount<MirrorEntry>::GetRefCount();
    }
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void set_mirror_entrySandeshData(MirrorEntrySandeshData &data) const;
    uint32_t vrf_id() const;
    const VrfEntry *GetVrf() const;
    const std::string GetAnalyzerName() const {return analyzer_name_;}
    const IpAddress *GetSip() const {return &sip_;}
    uint16_t GetSPort() const {return sport_;}
    const IpAddress *GetDip() const {return &dip_;}
    uint16_t GetDPort() const {return dport_;}
    const NextHop *GetNH() const {return nh_.get();}
    const std::string vrf_name() const {return vrf_name_;}
    uint32_t GetVni() const {return vni_;}
    uint8_t GetMirrorFlag() const {return mirror_flags_;}
    const MacAddress *GetMac() const { return &mac_;}
    bool GetCreatedVrf() const {return createdvrf_;}
    uint16_t nic_assisted_mirroring_vlan() const {
        return nic_assisted_mirroring_vlan_;}
    bool nic_assisted_mirroring() const {
        return nic_assisted_mirroring_;
    }
    void set_mirror_index(uint32_t index) {mirror_index_ = index;}
    uint32_t mirror_index() const { return mirror_index_; }
private:
    std::string analyzer_name_;
    VrfEntryRef vrf_;
    IpAddress sip_;
    uint16_t sport_;
    IpAddress dip_;
    uint16_t dport_;
    NextHopRef nh_;
    std::string vrf_name_;
    uint8_t mirror_flags_;
    uint32_t vni_;
    MacAddress mac_; // can be type of vtep mac or analyzer-mac based on type NH
    // this vrf will be created if this mirror vrf is not known to compute node
    // add it subscribes for the route information to get down loaded
    bool createdvrf_;
    bool nic_assisted_mirroring_;
    uint16_t nic_assisted_mirroring_vlan_;
    uint32_t mirror_index_;
    friend class MirrorTable;
};

class MirrorTable : public AgentDBTable {
public:
    const static unsigned bufLen = 512;
    const static uint8_t kInvalidIndex = 0xFF;
    typedef std::vector<MirrorEntry *> MirrorEntryList;
    typedef std::map<std::string , MirrorEntryList> VrfMirrorEntryList;
    typedef std::pair<std::string , MirrorEntryList> VrfMirrorEntry;

    MirrorTable(DB *db, const std::string &name) : AgentDBTable(db, name) {
    }
    virtual ~MirrorTable();
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *request);
    virtual bool Resync(DBEntry *entry, const DBRequest *req) {
        bool ret = OnChange(entry, req);
        return ret;
    }
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    VrfEntry *FindVrfEntry(const std::string &vrf_name) const;
    static void AddMirrorEntry(const std::string &analyzer_name,
                               const std::string &vrf_name,
                               const IpAddress &sip, uint16_t sport,
                               const IpAddress &dip, uint16_t dport);
    static void AddMirrorEntry(const std::string &analyzer_name,
                               const std::string &vrf_name,
                               const IpAddress &sip, uint16_t sport,
                               const IpAddress &dip, uint16_t dport,
                               uint32_t vni, uint8_t mirror_flag,
                               const MacAddress &mac);
    static void AddMirrorEntry(const std::string &analyzer_name,
                               uint32_t nic_assisted_mirroring_vlan);
    static void DelMirrorEntry(const std::string &analyzer_name);
    virtual void OnZeroRefcount(AgentDBEntry *e);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static MirrorTable *GetInstance() {return mirror_table_;}
    void MirrorSockInit(void);
    void ReadHandler(const boost::system::error_code& error, size_t bytes);
    void AddUnresolved(MirrorEntry *entry);
    void RemoveUnresolved(MirrorEntry *entry);
    void AddResolvedVrfMirrorEntry(MirrorEntry *entry);
    void DeleteResolvedVrfMirrorEntry(MirrorEntry *entry);
    void ResyncMirrorEntry(VrfMirrorEntryList &list, const VrfEntry *vrf);
    void ResyncResolvedMirrorEntry(const VrfEntry *vrf);
    void ResyncUnresolvedMirrorEntry(const VrfEntry *vrf);
    void Add(VrfMirrorEntryList &vrf_entry_map, MirrorEntry *entry);
    void Delete(VrfMirrorEntryList &vrf_entry_map, MirrorEntry *entry);
    void VrfListenerInit();
    void VrfNotify(DBTablePartBase *root, DBEntryBase *entry);
    void Shutdown();
    void Initialize();
    bool OnChange(MirrorEntry *mirror_entry);
    struct MirrorVrfState : DBState {
        MirrorVrfState() : DBState(), seen_(false),
        bridge_rt_table_listener_id_(DBTableBase::kInvalidId) {}
        bool seen_;
        DBTableBase::ListenerId bridge_rt_table_listener_id_;
    };
    void BridgeRouteTableNotify(DBTablePartBase *partition, DBEntryBase *e);
    void UnRegisterBridgeRouteTableListener(const VrfEntry *entry,
                                            MirrorVrfState *state);
    bool UnresolvedMirrorVrf(const VrfEntry *vrf, VrfMirrorEntryList &list);
    MirrorEntry* GetMirrorEntry(VrfEntry *vrf, const MacAddress & mac,
                                VrfMirrorEntryList &list);
    static MirrorEntryData::MirrorEntryFlags
        DecodeMirrorFlag (const std::string &nh_mode, bool juniper_header);
    void DeleteMirrorVrf(MirrorEntry *entry);
    bool IsConfigured();
private:
    std::auto_ptr<boost::asio::ip::udp::socket> udp_sock_;
    static MirrorTable *mirror_table_;
    char rx_buff_[bufLen];
    VrfMirrorEntryList unresolved_entry_list_;
    VrfMirrorEntryList resolved_entry_list_;
    DBTableBase::ListenerId vrf_listener_id_;
};
#endif
