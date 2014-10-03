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
    MirrorEntryData(const std::string vrf_name, const Ip4Address &sip, 
                    const uint16_t sport, const Ip4Address &dip, 
                    const uint16_t dport): vrf_name_(vrf_name), 
            sip_(sip), sport_(sport), dip_(dip), dport_(dport) { };
    std::string vrf_name_;
    Ip4Address sip_;
    uint16_t sport_;
    Ip4Address dip_;
    uint16_t dport_;
};

class MirrorEntry : AgentRefCount<MirrorEntry>, public AgentDBEntry {
public:
    MirrorEntry(std::string analyzer_name) : 
           analyzer_name_(analyzer_name), vrf_(NULL), nh_(NULL) { };
    virtual ~MirrorEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual bool Change(const DBRequest *req) {return false;};
    virtual void Delete(const DBRequest *req) {};

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
    const Ip4Address *GetSip() const {return &sip_;}
    uint16_t GetSPort() const {return sport_;}
    const Ip4Address *GetDip() const {return &dip_;}
    uint16_t GetDPort() const {return dport_;}
    const NextHop *GetNH() const {return nh_.get();}

private:
    std::string analyzer_name_;
    VrfEntryRef vrf_;
    Ip4Address sip_;
    uint16_t sport_;
    Ip4Address dip_;
    uint16_t dport_;
    NextHopRef nh_;
    friend class MirrorTable;
};

class MirrorTable : public AgentDBTable {
public:
    const static unsigned bufLen = 512;
    MirrorTable(DB *db, const std::string &name) : AgentDBTable(db, name) {
    }
    virtual ~MirrorTable();
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req) { }
    VrfEntry *FindVrfEntry(const std::string &vrf_name) const;
    static void AddMirrorEntry(const std::string &analyzer_name,
                               const std::string &vrf_name,
                               const Ip4Address &sip, uint16_t sport, 
                               const Ip4Address &dip, uint16_t dport);
    static void DelMirrorEntry(const std::string &analyzer_name);
    virtual void OnZeroRefcount(AgentDBEntry *e);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static MirrorTable *GetInstance() {return mirror_table_;}
    void MirrorSockInit(void);
    void ReadHandler(const boost::system::error_code& error, size_t bytes);

private:
    std::auto_ptr<boost::asio::ip::udp::socket> udp_sock_;
    static MirrorTable *mirror_table_;
    char rx_buff_[bufLen];
};
#endif
