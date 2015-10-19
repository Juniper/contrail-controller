/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_

#include <base/lifetime.h>
#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_connection_state.h>

namespace OVSDB {
class HaStaleL2RouteTable;
class HaStaleL2RouteEntry;
class HaStaleVnTable;
class ConnectionStateEntry;

class HaStaleDevVnTable : public OvsdbDBObject {
public:
    static const uint32_t kStaleTimerJobInterval = 100; // in millisecond
    static const uint32_t kNumEntriesPerIteration = 100; // in millisecond

    typedef boost::function<void(void)> StaleClearL2EntryCb;
    struct StaleL2Entry {
        StaleL2Entry(uint64_t t, HaStaleL2RouteEntry *e)
            : time_stamp(t), entry(e) {
        }

        uint64_t time_stamp;
        HaStaleL2RouteEntry *entry;

        bool operator <(const StaleL2Entry &rhs) const {
            if (time_stamp != rhs.time_stamp) {
                return time_stamp < rhs.time_stamp;
            }

            return entry < rhs.entry;
        }
    };
    typedef std::map<StaleL2Entry, StaleClearL2EntryCb> CbMap;
    HaStaleDevVnTable(Agent *agent, OvsPeerManager *manager,
                      ConnectionStateEntry *state,
                      std::string &dev_name);
    virtual ~HaStaleDevVnTable();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);

    Agent *agent() const;
    void DeleteTableDone();
    virtual void EmptyTable();

    void VnReEvalEnqueue(const boost::uuids::uuid &vn_uuid);
    bool VnReEval(const boost::uuids::uuid &vn_uuid);

    OvsPeer *route_peer() const;
    const std::string &dev_name() const;
    ConnectionStateEntry *state() const;
    uint64_t time_stamp() const { return time_stamp_;}

    void StaleClearAddEntry(uint64_t time_stamp, HaStaleL2RouteEntry *entry,
                            StaleClearL2EntryCb cb);
    void StaleClearDelEntry(uint64_t time_stamp, HaStaleL2RouteEntry *entry);

private:
    friend class HaStaleDevVnEntry;
    void OvsdbNotify(OvsdbClientIdl::Op op, struct ovsdb_idl_row *row) {}
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row) {return NULL;}

    bool StaleClearTimerCb();

    Agent *agent_;
    OvsPeerManager *manager_;
    std::auto_ptr<OvsPeer> route_peer_;
    std::string dev_name_;
    ConnectionStateEntryPtr state_;
    // Additionaly listen to VN table for VN-VRF link updates
    HaStaleVnTable *vn_table_;
    // entries for which the timer is running to clear stale entries
    CbMap stale_l2_entry_map_;
    // time stamp represented by iteration count of interval
    // kStaleTimerJobInterval should always be non-zero and start with 1
    uint64_t time_stamp_;
    Timer *stale_clear_timer_;
    // reeval queue used to trigger updates on change in VN-VRF link
    // this is required VN object cannot trigger a re-eval on dev-vn
    // object in line as dev-vn object needs to check for resolution
    // of vn object.
    WorkQueue<boost::uuids::uuid> *vn_reeval_queue_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleDevVnTable);
};

class HaStaleDevVnEntry : public OvsdbDBEntry {
public:
    HaStaleDevVnEntry(OvsdbDBObject *table, const boost::uuids::uuid &vn_uuid);
    ~HaStaleDevVnEntry();

    bool Add();
    bool Change();
    bool Delete();

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Ha Stale Dev VN Entry";}
    KSyncEntry* UnresolvedReference();

    void TriggerAck(HaStaleL2RouteTable *table);

    HaStaleL2RouteTable *l2_table() const {return l2_table_;}
    const boost::uuids::uuid &vn_uuid() const { return vn_uuid_; }

    Agent *agent() const;
    OvsPeer *route_peer() const;
    const std::string &dev_name() const;
    ConnectionStateEntry *state() const;
    IpAddress dev_ip() const;
    const std::string &vn_name() const;
    uint32_t vxlan_id() const;

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    friend class HaStaleDevVnTable;
    boost::uuids::uuid vn_uuid_;
    HaStaleL2RouteTable *l2_table_;
    // hold pointer to old l2_table which is already
    // scheduled for deletion, this delete can be
    // triggered due to delete or change of entry
    // so we hold the ksync state to wait for an Ack
    // on table cleanup and move the ksync state
    // machine when HaStaleL2RouteTable triggerack
    // at the time of table delete complete
    HaStaleL2RouteTable *old_l2_table_;
    AgentRouteTable *oper_bridge_table_;
    IpAddress dev_ip_;
    std::string vn_name_;
    uint32_t vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleDevVnEntry);
};

};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_

