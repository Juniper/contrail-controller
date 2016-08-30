/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_h
#define ctrlplane_db_h

#include <map>
#include <vector>

#include <boost/function.hpp>
#include <tbb/mutex.h>

#include "base/util.h"

class DBGraph;
class DBPartition;
class DBTableBase;
class DBTableWalker;
class DBTableWalkMgr;

// A database is a collection of tables.
// The storage is implemented by a set of shards (DB Partitions).
class DB {
public:
    typedef boost::function<DBTableBase *(DB *, const std::string &)>
        CreateFunction;
    typedef std::map<std::string, DBTableBase *> TableMap;
    typedef TableMap::iterator iterator;
    typedef TableMap::const_iterator const_iterator;

    DB(int task_id = -1);
    ~DB();

    // Get the partition with the specified id.
    DBPartition *GetPartition(int index);
    const DBPartition *GetPartition(int index) const;

    void AddTable(DBTableBase *tbl_base);

    // Table creation.
    DBTableBase *CreateTable(const std::string &name);
    DBTableBase *FindTable(const std::string &name);
    iterator FindTableIter(const std::string &name);
    void RemoveTable(DBTableBase *tbl_base);

    // Table walker
    DBTableWalker *GetWalker() {
        return walker_.get();
    }
    DBTableWalkMgr *GetWalkMgr() {
        return walk_mgr_.get();
    }

    DBGraph *GetGraph(const std::string &name);
    void SetGraph(const std::string &name, DBGraph *graph);
    void SetQueueDisable(bool disable);

    static void SetPartitionCount(int partition_count);
    static int PartitionCount();
    static void RegisterFactory(const std::string &prefix,
                                CreateFunction create_fn);
    static void ClearFactoryRegistry();

    void Clear();
    bool IsDBQueueEmpty() const;

    iterator begin() { return tables_.begin(); }
    iterator end() { return tables_.end(); }
    iterator lower_bound(const std::string &name) {
        return tables_.lower_bound(name);
    }
    const_iterator const_begin() { return tables_.begin(); }
    const_iterator const_end() { return tables_.end(); }
    const_iterator const_lower_bound(const std::string &name) {
        return tables_.lower_bound(name);
    }

    int task_id() const { return task_id_; }

private:
    typedef std::map<std::string, CreateFunction> FactoryMap;
    typedef std::map<std::string, DBGraph *> GraphMap;

    static int partition_count_;
    static FactoryMap *factories();

    tbb::mutex mutex_;
    int task_id_;
    std::vector<DBPartition *> partitions_;
    TableMap tables_;
    GraphMap graph_map_;
    std::auto_ptr<DBTableWalker> walker_;
    std::auto_ptr<DBTableWalkMgr> walk_mgr_;

    DISALLOW_COPY_AND_ASSIGN(DB);
};

#endif
