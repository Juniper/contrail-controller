/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __MAC_LEARNING_PROTO_H__
#define __MAC_LEARNING_PROTO_H__

#include <net/if.h>
#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"

class MacLearningTable;
class MacAgingTable;

class MacLearningProto : public Proto {
public:
    typedef boost::shared_ptr<MacLearningTable> MacLearningTablePtr;
    typedef std::pair<uint32_t, MacLearningTablePtr> MacLearningTablePair;
    typedef std::map<uint32_t, MacLearningTablePtr> MacLearningTableMap;

    typedef boost::shared_ptr<MacAgingTable> MacAgingTablePtr;
    typedef std::pair<uint32_t, MacAgingTablePtr> VrfMacAgingTablePair;
    typedef std::map<uint32_t, MacAgingTablePtr> VrfMacAgingTableMap;

    MacLearningProto(Agent *agent,
                     boost::asio::io_service &io);
    virtual ~MacLearningProto() {}

    virtual bool Validate(PktInfo *msg) { return true; }
    virtual ProtoHandler*
        AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                          boost::asio::io_service &io);
    MacLearningTable* Find(uint32_t index);

    void Delete(uint32_t index) {
        mac_learning_table_map_.erase(index);
    }
    bool Enqueue(PktInfoPtr msg);
    void Init();
    uint32_t Hash(uint32_t vrf_id, const MacAddress &mac);
    uint32_t size() {
        return mac_learning_table_map_.size();
    }
private:
    tbb::mutex::scoped_lock mutex_;
    MacLearningTableMap mac_learning_table_map_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningProto);
};
#endif
