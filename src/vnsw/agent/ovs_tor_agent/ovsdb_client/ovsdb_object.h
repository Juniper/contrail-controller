/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_OBJECT_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_OBJECT_H_

#include <boost/intrusive_ptr.hpp>

#include <db/db_entry.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_object.h>

#include <ovsdb_client_idl.h>

namespace OVSDB {
class OvsdbDBEntry;

class OvsdbObject : public KSyncObject {
public:
    OvsdbObject(OvsdbClientIdl *idl);
    virtual ~OvsdbObject();

    KSyncEntry *FindActiveEntry(KSyncEntry *key);
    OvsdbClientIdl *client_idl() { return client_idl_;}
protected:
    OvsdbClientIdl *client_idl_;
private:
    DISALLOW_COPY_AND_ASSIGN(OvsdbObject);
};

class OvsdbDBObject : public KSyncDBObject {
public:
    OvsdbDBObject(OvsdbClientIdl *idl);
    OvsdbDBObject(OvsdbClientIdl *idl, DBTable *tbl);
    virtual ~OvsdbDBObject();

    void NotifyAddOvsdb(OvsdbDBEntry *key, struct ovsdb_idl_row *row);
    void NotifyDeleteOvsdb(OvsdbDBEntry *key);

    virtual void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *) = 0;
    virtual OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row) = 0;
    bool DBWalkNotify(DBTablePartBase *partition, DBEntryBase *entry);
    void DBWalkDone(DBTableBase *partition);

    OvsdbClientIdl *client_idl() { return client_idl_;}
protected:
    OvsdbClientIdl *client_idl_;
private:
    friend class OvsdbDBEntry;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbDBObject);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_OBJECT_H_

