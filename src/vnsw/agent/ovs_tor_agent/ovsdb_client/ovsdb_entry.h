/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ENTRY_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ENTRY_H_

#include <boost/intrusive_ptr.hpp>

#include <db/db_entry.h>
#include <ksync/ksync_entry.h>

class KSyncObject;

namespace OVSDB {
class OvsdbObject;
class OvsdbDBObject;
class OvsdbClientIdl;

class OvsdbEntryBase {
public:
    virtual void Ack(bool success) = 0;

    // this API is called when the transaction ends up forming an
    // empty message, to provide success ACK event, this can be
    // overriden by the derived class to get triggers
    virtual void TxnDoneNoMessage() {}

    KSyncEntry::KSyncEvent ack_event() {return ack_event_;}

protected:
    friend class OvsdbClientIdl;
    KSyncEntry::KSyncEvent ack_event_;
};

class OvsdbEntry : public KSyncEntry, public OvsdbEntryBase {
public:
    OvsdbEntry(OvsdbObject *table);
    OvsdbEntry(OvsdbObject *table, uint32_t index);
    virtual ~OvsdbEntry();

    virtual bool Add();
    virtual bool Change();
    virtual bool Delete();

    struct ovsdb_idl_row *ovs_entry() {return ovs_entry_;}
    KSyncObject* GetObject();
    void Ack(bool success);

protected:
    OvsdbObject *table_;
    struct ovsdb_idl_row *ovs_entry_;

private:
    DISALLOW_COPY_AND_ASSIGN(OvsdbEntry);
};

class OvsdbDBEntry : public KSyncDBEntry, public OvsdbEntryBase {
public:
    OvsdbDBEntry(OvsdbDBObject *table);
    OvsdbDBEntry(OvsdbDBObject *table, struct ovsdb_idl_row *ovs_entry);
    virtual ~OvsdbDBEntry();

    // pre processing callback for add/change msg to take object reference
    virtual void PreAddChange() {}
    // post processing callback for delete msg to release object reference
    // it can result in another delete transaction, so should be triggered
    // after we are done with the current transaction
    virtual void PostDelete() {}
    // Encode add message for entry
    virtual void AddMsg(struct ovsdb_idl_txn *) {}
    // Encode change message for entry
    virtual void ChangeMsg(struct ovsdb_idl_txn *) {}
    // Encode delete message for entry
    virtual void DeleteMsg(struct ovsdb_idl_txn *) {}

    virtual void OvsdbChange() {}

    bool AllowDeleteStateComp() {return false;}
    virtual void NotifyAdd(struct ovsdb_idl_row *);
    virtual void NotifyDelete(struct ovsdb_idl_row *);

    virtual bool Add();
    virtual bool Change();
    virtual bool Delete();

    virtual bool IsDataResolved();
    bool IsDelAckWaiting();
    bool IsAddChangeAckWaiting();

    struct ovsdb_idl_row *ovs_entry() {return ovs_entry_;}

    OvsdbDBObject *table() { return table_;}

    KSyncObject* GetObject();
    virtual void Ack(bool success);

protected:
    // by default create transaction for all entries
    virtual bool IsNoTxnEntry() { return false; }

    OvsdbDBObject *table_;
    struct ovsdb_idl_row *ovs_entry_;

private:
    friend class OvsdbDBObject;
    DISALLOW_COPY_AND_ASSIGN(OvsdbDBEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ENTRY_H_

