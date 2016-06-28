/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_db_hpp
#define vnsw_agent_db_hpp

#include <cmn/agent_cmn.h>

class Agent;
class AgentDBEntry;
class AgentDBTable;
class AgentDBState;
class AgentSandesh;
class AgentSandeshArguments;
class AgentSandesh;
typedef class boost::shared_ptr<AgentSandesh> AgentSandeshPtr;

/////////////////////////////////////////////////////////////////////////////
// Refcount class for AgentDBEntry
/////////////////////////////////////////////////////////////////////////////
template <class Derived>
class AgentRefCount {
public:
    friend void intrusive_ptr_add_ref(const Derived* p) {
        const AgentRefCount *entry = (const AgentRefCount *) (p);
        if (entry->refcount_.fetch_and_increment() == 0) {
            p->SetRefState();
        }
    }

    friend void intrusive_ptr_release(const Derived* p) {
        const AgentRefCount *entry = (const AgentRefCount *) (p);
        if (entry->refcount_.fetch_and_decrement() == 1) {
            p->ClearRefState();
        }
    }

    friend void intrusive_ptr_add_back_ref(const IntrusiveReferrer ref,
                                           const Derived* p) {
        const AgentRefCount *entry = (const AgentRefCount *) (p);
        tbb::mutex::scoped_lock lock(entry->back_ref_mutex_);
        entry->back_ref_set_.insert(ref);
    }

    friend void intrusive_ptr_del_back_ref(const IntrusiveReferrer ref,
                                           const Derived* p) {
        const AgentRefCount *entry = (const AgentRefCount *) (p);
        tbb::mutex::scoped_lock lock(entry->back_ref_mutex_);
        entry->back_ref_set_.erase(ref);
    }

    uint32_t GetRefCount() const {return refcount_;};
protected:
    AgentRefCount() {refcount_ = 0;}
    AgentRefCount(const AgentRefCount&) { refcount_ = 0; }
    AgentRefCount& operator=(const AgentRefCount&) { return *this; }
    virtual ~AgentRefCount() {assert(refcount_ == 0);};
    void swap(AgentRefCount&) {};

    mutable tbb::mutex back_ref_mutex_;
    mutable std::set<IntrusiveReferrer> back_ref_set_;

private:
    mutable tbb::atomic<uint32_t> refcount_;
};

/////////////////////////////////////////////////////////////////////////////
// DBState for AgentDBEntry
/////////////////////////////////////////////////////////////////////////////
struct AgentDBState : DBState {
    AgentDBState(const AgentDBEntry *entry) : DBState(), entry_(entry) { };
    const AgentDBEntry *entry_;
};

/////////////////////////////////////////////////////////////////////////////
// VNSwitch DB Table Partition class
/////////////////////////////////////////////////////////////////////////////
class AgentDBTablePartition: public DBTablePartition {
public:
    AgentDBTablePartition(DBTable *parent, int index) : 
        DBTablePartition(parent, index) { };
    virtual ~AgentDBTablePartition() {};
    virtual void Add(DBEntry *entry);
    virtual void Remove(DBEntryBase *entry);

private:
    DISALLOW_COPY_AND_ASSIGN(AgentDBTablePartition);
};

/////////////////////////////////////////////////////////////////////////////
// VNSwitch DB Entry Key base class. Defines additional operations on DBEntry
/////////////////////////////////////////////////////////////////////////////
struct AgentKey : public DBRequestKey {
    typedef enum {
        // Add/Delete/Change a entry
        ADD_DEL_CHANGE,
        // Change an entry if its already present and not in deleted state
        // Its a no-op if entry is not present or is in deleted state
        RESYNC,
    } DBSubOperation;

    AgentKey() : DBRequestKey(), sub_op_(ADD_DEL_CHANGE) { };
    AgentKey(DBSubOperation sub_op) : DBRequestKey(), sub_op_(sub_op) { };
    virtual ~AgentKey() { };

    uint8_t sub_op_;
};

/////////////////////////////////////////////////////////////////////////////
// VNSwitch DB Entry Data base class
/////////////////////////////////////////////////////////////////////////////
struct AgentData : public DBRequestData {
    AgentData() : DBRequestData() { };
    virtual ~AgentData() { };
};

/////////////////////////////////////////////////////////////////////////////
// VNSwitch DB Entry base class. Supports
// 1. Reference counting with boost Intrusive pointer
/////////////////////////////////////////////////////////////////////////////
class AgentDBEntry : public DBEntry {
public:
    AgentDBEntry() : DBEntry(), flags_(0) {};
    virtual ~AgentDBEntry() {};
    virtual uint32_t GetRefCount() const = 0;

    typedef boost::intrusive_ptr<AgentDBEntry> AgentDBEntyRef;
    void SetRefState() const;
    void ClearRefState() const;
    bool IsActive() const;

    virtual void PostAdd();
    virtual bool DBEntrySandesh(Sandesh *resp, std::string &name) const = 0;
private:
    friend class AgentDBTable;
    uint8_t flags_;
    DISALLOW_COPY_AND_ASSIGN(AgentDBEntry);
};

/////////////////////////////////////////////////////////////////////////////
// VNSwitch DB Table base class
/////////////////////////////////////////////////////////////////////////////
class AgentDBTable : public DBTable {
public:
    static const int kPartitionCount = 1;
    AgentDBTable(DB *db, const std::string &name) : 
        DBTable(db, name), ref_listener_id_(-1), agent_(NULL),
        OperDBTraceBuf(SandeshTraceBufferCreate(("Oper " + name), 5000)) {
        ref_listener_id_ = Register(boost::bind(&AgentDBTable::Notify,
                                                this, _1, _2));
    };

    AgentDBTable(DB *db, const std::string &name, bool del_on_zero_refcount) : 
        DBTable(db, name), ref_listener_id_(-1) , agent_(NULL),
        OperDBTraceBuf(SandeshTraceBufferCreate(("Oper " + name), 5000)) {
        ref_listener_id_ = Register(boost::bind(&AgentDBTable::Notify,
                                                this, _1, _2));
    };

    virtual ~AgentDBTable() { };

    virtual int PartitionCount() const { return kPartitionCount; }
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    virtual DBEntry *CfgAdd(DBRequest *req) {return NULL;};
    virtual bool Resync(DBEntry *entry, const DBRequest *req) {return false;};

    /*
     * Clear all entries on a table. Requires the table to have no listeners.
     * Used in process shutdown.
     */
    virtual void Clear();

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &uuid) {
        assert(0);
        return false;
    }
    virtual bool IFLinkToReq(IFMapLink *link, IFMapNode *node,
                             const std::string &peer_name, IFMapNode *peer,
                             DBRequest &req) {
        assert(0);
        return false;
    }
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &id);

    virtual DBTablePartition *AllocPartition(int index) {
        return new AgentDBTablePartition(this, index);
    };
    virtual void OnZeroRefcount(AgentDBEntry *e) {};
    virtual void NotifyEntry(DBEntryBase *entry);

    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context) {
        return AgentSandeshPtr();
    }

    // Dummy notification
    void Notify(DBTablePartBase *partition, DBEntryBase *entry) {
    };

    DBTableBase::ListenerId GetRefListenerId() const {return ref_listener_id_;};
    AgentDBEntry *FindActiveEntry(const DBEntry *key);
    AgentDBEntry *FindActiveEntryNoLock(const DBEntry *key);
    AgentDBEntry *FindActiveEntry(const DBRequestKey *key);
    AgentDBEntry *FindActiveEntryNoLock(const DBRequestKey *key);
    AgentDBEntry *Find(const DBEntry *key, bool ret_del);
    AgentDBEntry *Find(const DBRequestKey *key, bool ret_del);
    virtual bool CanNotify(IFMapNode *dbe) {
        return true;
    }
    virtual void Process(DBRequest &req);
    virtual bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u) {
        assert(0);
        return false;
    }

    void set_agent(Agent *agent) { agent_ = agent; }
    Agent *agent() const { return agent_; }

    void Flush(DBTableWalker *walker);
    SandeshTraceBufferPtr GetOperDBTraceBuf() const {return OperDBTraceBuf;}
private:
    AgentDBEntry *Find(const DBEntry *key);
    AgentDBEntry *Find(const DBRequestKey *key);

    DBTableBase::ListenerId ref_listener_id_;
    Agent *agent_;
    SandeshTraceBufferPtr OperDBTraceBuf;
    DISALLOW_COPY_AND_ASSIGN(AgentDBTable);
};

#define OPER_TRACE(obj, ...)\
do {\
   Oper##obj::TraceMsg(GetOperDBTraceBuf(), __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#define OPER_TRACE_ENTRY(obj, table, ...)\
do {\
   Oper##obj::TraceMsg(table->GetOperDBTraceBuf(),\
                       __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#endif // vnsw_agent_db_hpp
