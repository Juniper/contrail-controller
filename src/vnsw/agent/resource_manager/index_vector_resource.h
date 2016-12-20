/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_manager_hpp
#define vnsw_agent_resource_manager_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

using namespace boost::uuids;
using namespace std;

template<typename Object>
class IndexVectorResource {
public:    
    IndexVectorResource(Agent *agent) : agent_(agent) {
    }
    virtual ~IndexVectorResource();    

    //index allocation from index table
    uint32_t AllocIndex() {
        return index_table_.Insert(NULL);
    }

    uint32_t InsertAtIndex(uint32_t index, Object *entry) {
        return index_table_.InsertAtIndex(index, entry);
    }

    void UpdateIndex(uint32_t index, Object *entry) {
        return index_table_.Update((index), entry);
    }
    void FreeIndex(uint32_t index) {
        index_table_.Remove(index);
    }
    Object *FindIndex(size_t index) {
        return index_table_.At(index);
    }

private:    
    IndexVector<Object> index_table_;
    const Agent *agent_;
};
#endif //index_vector_resource
