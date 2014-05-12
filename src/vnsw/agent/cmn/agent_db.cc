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

bool AgentDBEntry::CanDelete(DBRequest *req) {
    return true;
}

void AgentDBEntry::PostAdd() {
}

AgentDBEntry *AgentDBTable::FindActiveEntry(const DBEntry *key) {
    AgentDBEntry *entry = static_cast<AgentDBEntry *> (Find(key));
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
    DBTablePartition::Add(entry);
    static_cast<AgentDBEntry *>(entry)->PostAdd();
}

void AgentDBTablePartition::Remove(DBEntryBase *entry) {
    AgentDBEntry *agent = static_cast<AgentDBEntry *>(entry);
    if (agent->GetRefCount() != 0) {
        agent->ClearOnRemoveQ();
        return;
    }
    DBTablePartition::Remove(entry);
}

void AgentDBTable::Input(DBTablePartition *partition, DBClient *client,
                         DBRequest *req) {
    AgentKey *key = static_cast<AgentKey *>(req->key.get());

    if (key->sub_op_ == AgentKey::ADD_DEL_CHANGE) {
        if (req->oper == DBRequest::DB_ENTRY_DELETE) {
            AgentDBEntry *entry =
                static_cast<AgentDBEntry *>(partition->Find(key));
            if (entry && !entry->CanDelete(req))
                return;
        }
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

void AgentDBTable::Process(DBRequest &req) {
    CHECK_CONCURRENCY("db::DBTable");
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}
