/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_cmn.h"
#include "agent_db.h"
#include <cfg/cfg_init.h>

void AgentDBEntry::SetRefState() const {
    AgentDBTable *table = static_cast<AgentDBTable *>(get_table());
    // Force calling SetState on a const object.
    // Ideally, SetState should be 'const method' and StateMap mutable
    AgentDBEntry *entry = (AgentDBEntry *)this;
    entry->SetState(table, table->GetRefListenerId(), new AgentDBState(this));
}

void AgentDBEntry::ClearRefState() const {
    AgentDBTable *table = static_cast<AgentDBTable *>(get_table());
    // Force calling SetState on a const object.
    // Ideally, ClearState should be 'const method' and StateMap mutable
    AgentDBEntry *entry = (AgentDBEntry *)this;
    table->OnZeroRefcount(entry);
    delete(entry->GetState(table, table->GetRefListenerId()));
    entry->ClearState(table, table->GetRefListenerId());
}

bool AgentDBEntry::IsActive() const {
    return !IsDeleted();
}

DBState *AgentDBEntry::GetAgentDBEntryState(int listener_id) {
    DBState *state = dynamic_cast<DBState *>
        (GetState(get_table(), listener_id));
    return state;
}

const DBState *AgentDBEntry::GetAgentDBEntryState(int listener_id) const {
    const DBState *state = dynamic_cast<const DBState *>
        (GetState(get_table(), listener_id));
    return state;
}

void AgentDBEntry::AllocateResources(ResourceManager *resource_manager) {
}

void AgentDBEntry::FreeResources(ResourceManager *resource_manager) {
}

void AgentDBEntry::PostAdd() {
}

static bool FlushNotify(DBTablePartBase *partition, DBEntryBase *e) {
    if (e->IsDeleted()) {
        return true;
    }

    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key = e->GetDBRequestKey();
    (static_cast<AgentDBTable *>(e->get_table()))->Process(req);
    return true;
}

static void FlushWalkDone(DBTable::DBTableWalkRef walk_ref,
                          DBTableBase *partition) {
    AgentDBTable *table = static_cast<AgentDBTable *>(partition);
    table->ReleaseWalker(walk_ref);
    table->reset_flush_walk_ref();
}

AgentDBTable::AgentDBTable(DB *db, const std::string &name) :
    DBTable(db, name), ref_listener_id_(-1), agent_(NULL),
    OperDBTraceBuf(SandeshTraceBufferCreate(("Oper " + name), 5000)),
    flush_walk_ref_() {
    ref_listener_id_ = Register(boost::bind(&AgentDBTable::Notify,
                                            this, _1, _2));
};

AgentDBTable::AgentDBTable(DB *db, const std::string &name,
                           bool del_on_zero_refcount) :
    DBTable(db, name), ref_listener_id_(-1) , agent_(NULL),
    OperDBTraceBuf(SandeshTraceBufferCreate(("Oper " + name), 5000)),
    flush_walk_ref_() {
    ref_listener_id_ = Register(boost::bind(&AgentDBTable::Notify,
                                            this, _1, _2));
};

AgentDBTable::~AgentDBTable() {
    assert(HasWalkers() == false);
}

void AgentDBTable::NotifyEntry(DBEntryBase *e) {
    agent_->ConcurrencyCheck();
    DBTablePartBase *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(e));
    tpart->Notify(e);
}

void AgentDBTable::reset_flush_walk_ref() {
    flush_walk_ref_ = NULL;
}

AgentDBEntry *AgentDBTable::FindActiveEntryNoLock(const DBEntry *key) {
    AgentDBEntry *entry = static_cast<AgentDBEntry *> (FindNoLock(key));
    if (entry && (entry->IsActive() == false)) {
        return NULL;
    }
    return entry;
}

AgentDBEntry *AgentDBTable::FindActiveEntry(const DBEntry *key) {
    AgentDBEntry *entry = static_cast<AgentDBEntry *> (Find(key));
    if (entry && (entry->IsActive() == false)) {
        return NULL;
    }
    return entry;
}

AgentDBEntry *AgentDBTable::FindActiveEntryNoLock(const DBRequestKey *key) {
    AgentDBEntry *entry = static_cast<AgentDBEntry *>(FindNoLock(key));
    if (entry && (entry->IsActive() == false)) {
        return NULL;
    }
    return entry;
}

AgentDBEntry *AgentDBTable::FindActiveEntry(const DBRequestKey *key) {
    AgentDBEntry *entry = static_cast<AgentDBEntry *>(Find(key));
    if (entry && (entry->IsActive() == false)) {
        return NULL;
    }
    return entry;
}

AgentDBEntry *AgentDBTable::Find(const DBEntry *key, bool ret_del) {
    if (ret_del) {
        return Find(key);
    } else {
        return FindActiveEntry(key);
    }
}

AgentDBEntry *AgentDBTable::Find(const DBRequestKey *key, bool ret_del) {
    if (ret_del) {
        return Find(key);
    } else {
        return FindActiveEntry(key);
    }
}

AgentDBEntry *AgentDBTable::Find(const DBEntry *key) {
    return static_cast<AgentDBEntry *> (DBTable::Find(key));
}

AgentDBEntry *AgentDBTable::Find(const DBRequestKey *key) {
    return static_cast<AgentDBEntry *>(DBTable::Find(key));
}

void AgentDBTablePartition::Add(DBEntry *entry) {
    entry->set_table_partition(static_cast<DBTablePartBase *>(this));
    Agent *agent = (static_cast<AgentDBTable *>(parent()))->agent();
    if (agent) {
        (static_cast<AgentDBEntry *>(entry))->AllocateResources
            (agent->resource_manager());
    }
    DBTablePartition::Add(entry);
    static_cast<AgentDBEntry *>(entry)->PostAdd();
}

void AgentDBTablePartition::Remove(DBEntryBase *entry) {
    AgentDBEntry *agent_dbentry = static_cast<AgentDBEntry *>(entry);
    if (agent_dbentry->GetRefCount() != 0) {
        agent_dbentry->ClearOnRemoveQ();
        return;
    }
    Agent *agent = (static_cast<AgentDBTable *>(parent()))->agent();
    if (agent) {
        (static_cast<AgentDBEntry *>(entry))->FreeResources
            (agent->resource_manager());
    }
    DBTablePartition::Remove(entry);
}

bool AgentDBTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &id) {
    id = boost::uuids::nil_uuid();
    return false;
}

void AgentDBTable::Input(DBTablePartition *partition, DBClient *client,
                         DBRequest *req) {
    AgentKey *key = static_cast<AgentKey *>(req->key.get());

    if (key->sub_op_ == AgentKey::ADD_DEL_CHANGE) {
        DBTable::Input(partition, client, req);
        return;
    }

    AgentDBEntry *entry = static_cast<AgentDBEntry *>(partition->Find(key));
    if (entry && (entry->IsActive() == false)) {
        return;
    }

    // Dont create an entry on RESYNC sub_op
    if (key->sub_op_ == AgentKey::RESYNC) {
        if (entry == NULL) {
            return;
        }
        if (Resync(entry, req)) {
            partition->Change(entry);
        }
        return;
    }
    return;
}

void AgentDBTable::Clear() {
    Unregister(ref_listener_id_);
    assert(!HasListeners());
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        GetTablePartition(0));

    DBEntryBase *next = NULL;
    for (DBEntryBase *entry = partition->GetFirst(); entry; entry = next) {
        next = partition->GetNext(entry);
        if (entry->IsDeleted()) {
            continue;
        }
        partition->Delete(entry);
    }
}

void AgentDBTable::Process(DBRequest &req) {
    agent_->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

void AgentDBTable::Flush() {
    flush_walk_ref_ = AllocWalker(boost::bind(FlushNotify, _1, _2),
                                  boost::bind(FlushWalkDone, _1, _2));
    WalkAgain(flush_walk_ref_);
}
