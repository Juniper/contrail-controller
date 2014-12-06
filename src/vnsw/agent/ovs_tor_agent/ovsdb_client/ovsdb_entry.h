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

class OvsdbEntryBase {
public:
    virtual void Ack(bool success) = 0;
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
    virtual void PostDelete() {}
    // Encode add message for entry
    virtual void AddMsg(struct ovsdb_idl_txn *) = 0;
    // Encode change message for entry
    virtual void ChangeMsg(struct ovsdb_idl_txn *) = 0;
    // Encode delete message for entry
    virtual void DeleteMsg(struct ovsdb_idl_txn *) = 0;

    virtual void OvsdbChange() {}

    bool AllowDeleteStateComp() {return false;}
    virtual void NotifyAdd(struct ovsdb_idl_row *);
    virtual void NotifyDelete();

    bool Add();
    bool Change();
    bool Delete();

    virtual bool IsDataResolved();
    bool IsDelAckWaiting();
    bool IsAddChangeAckWaiting();

    struct ovsdb_idl_row *ovs_entry() {return ovs_entry_;}
    void clear_ovs_entry() {ovs_entry_ = NULL;}

    KSyncObject* GetObject();
    void Ack(bool success);
protected:
    OvsdbDBObject *table_;
    struct ovsdb_idl_row *ovs_entry_;
private:
    friend class OvsdbDBObject;
    DISALLOW_COPY_AND_ASSIGN(OvsdbDBEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ENTRY_H_

