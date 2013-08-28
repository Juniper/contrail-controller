/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_index_table_h 
#define ctrlplane_ksync_index_table_h 

#include <boost/dynamic_bitset.hpp>

class KSyncIndexTable {
public:
    KSyncIndexTable() { };

    KSyncIndexTable(unsigned int count) : table_(count) {
        table_.set();
    };

    ~KSyncIndexTable() {
        //assert(table_.count() == table_.size());
        table_.clear();
    };

    size_t Alloc() {
        size_t index = table_.find_first();
        assert(index != table_.npos);
        table_.set(index, 0);
        return index;
    };

    void Free(size_t index) {
        assert(index < table_.size());
        assert(table_[index] == 0);
        table_.set(index);
    };

private:
    typedef boost::dynamic_bitset<> Bitmap;
    Bitmap  table_;
};

#endif // ctrlplane_ksync_index_table_h
