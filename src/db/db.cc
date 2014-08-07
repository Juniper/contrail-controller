/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_walker.h"
#include "tbb/task_scheduler_init.h"

using namespace std;

int DB::partition_count_;

// factory map is declared as a local static variable in order to avoid
// static initialization order dependencies.
DB::FactoryMap *DB::factories() {
    static FactoryMap factory_map;
    return &factory_map;
}

void DB::RegisterFactory(const std::string &prefix, CreateFunction create_fn) {
    DB::factories()->insert(make_pair(prefix, create_fn));
}

void DB::ClearFactoryRegistry() {
    DB::factories()->clear();
}

int DB::PartitionCount() {

    // Initialize static partition_count_.
    if (!partition_count_) {
        partition_count_ = TaskScheduler::GetInstance()->HardwareThreadCount();
    }
    return partition_count_;
}

DB::DB() : walker_(new DBTableWalker()) {
    for (int i = 0; i < PartitionCount(); i++) {
        partitions_.push_back(new DBPartition(i));
    }
}

DB::~DB() {
    Clear();
}

DBPartition *DB::GetPartition(int index) {
    return partitions_[index];
}

const DBPartition *DB::GetPartition(int index) const {
    return partitions_[index];
}

DBTableBase *DB::FindTable(const string &name) {
    TableMap::iterator loc = tables_.find(name);
    if (loc != tables_.end()) {
        DBTableBase *tbl_base = loc->second;
        return tbl_base;
    }
    return NULL;
}

void DB::AddTable(DBTableBase *tbl_base) {
    pair<TableMap::iterator, bool> result =
            tables_.insert(make_pair(tbl_base->name(), tbl_base));
    assert(result.second);
}

void DB::RemoveTable(DBTableBase *tbl_base) {
    tables_.erase(tbl_base->name());
}

bool DB::IsDBQueueEmpty() const {
    for (int i = 0; i < PartitionCount(); i++) {
        if (!GetPartition(i)->IsDBQueueEmpty()) return false;
    }

    return true;
}

DBTableBase *DB::CreateTable(const string &name) {
    FactoryMap *factory_map = factories();
    string prefix = name;
    while (prefix.size()) {
        FactoryMap::iterator loc = factory_map->find(prefix);
        if (loc != factory_map->end()) {
            DBTableBase *tbl_base = (loc->second)(this, name);
            tables_.insert(make_pair(name, tbl_base));
            return tbl_base;
        }
        size_t index = prefix.find('.');
        if (index == string::npos) {
            break;
        }
        if (index == (prefix.length()-1)) {
            break;
        }
        prefix = prefix.substr(index+1);
    }
    return NULL;
}

DBGraph *DB::GetGraph(const std::string &name) {
    GraphMap::iterator loc = graph_map_.find(name);
    if (loc != graph_map_.end()) {
        return loc->second;
    }
    return NULL;
}

void DB::SetGraph(const std::string &name, DBGraph *graph) {
    pair<GraphMap::iterator, bool> result =
        graph_map_.insert(make_pair(name, graph));
    assert(result.second);
}

void DB::Clear() {
    STLDeleteElements(&tables_);
    STLDeleteValues(&partitions_);
}
