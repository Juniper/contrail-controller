/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_index_vector_resource_hpp
#define vnsw_agent_index_vector_resource_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <cmn/index_vector.h>


template<typename Object>
// This Class is to do Index Allocation.
class IndexVectorResource {
public:
    IndexVectorResource() {
    }
    virtual ~IndexVectorResource() {
    }

    //index allocation from index table
    uint32_t AllocIndex(Object entry) {
        return index_table_.Insert(entry);
    }

    uint32_t InsertAtIndex(uint32_t index, Object entry) {
        return index_table_.InsertAtIndex(index, entry);
    }

    void UpdateIndex(uint32_t index, Object entry) {
        return index_table_.Update((index), entry);
    }
    void FreeIndex(uint32_t index) {
        index_table_.Remove(index);
    }

    Object FindIndex(size_t index) {
        return index_table_.At(index);
    }

private:
    IndexVector<Object> index_table_;
};
#endif //index_vector_resource
