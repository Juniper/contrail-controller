/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_cmn_hpp
#define vnsw_agent_cmn_hpp

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/address.h>
#include <unistd.h>

#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <io/event_manager.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <db/db_table_walker.h>

#include <base/logging.h>
#include <base/task.h>
#include <base/task_trigger.h>
#include <base/task_annotations.h>
#include <base/dependency.h>
#include <base/lifetime.h>

#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>

#include <cmn/agent.h>
#include <cmn/agent_db.h>
#include <cmn/event_notifier.h>

static inline bool UnregisterDBTable(DBTable *table, 
                                     DBTableBase::ListenerId id) {
    table->Unregister(id);
    return true;
}

static inline TaskTrigger *SafeDBUnregister(DBTable *table,
                                            DBTableBase::ListenerId id) {
    TaskTrigger *trigger = 
           new TaskTrigger(boost::bind(&UnregisterDBTable, table, id),
               TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    trigger->Set();
    return trigger;
}

static inline void CfgUuidSet(uint64_t ms_long, uint64_t ls_long,
                              boost::uuids::uuid &u) {
    for (int i = 0; i < 8; i++) {
        u.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        u.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
}

static inline void CloseTaskFds(void) {
    int max_open_fds = sysconf(_SC_OPEN_MAX);
    int fd;
    for(fd = 3; fd < max_open_fds; fd++)
        close(fd);
}

extern SandeshTraceBufferPtr OperConfigTraceBuf;
extern bool GetBuildInfo(std::string &build_info_str);

#define OPER_IFMAP_TRACE(obj, ...)\
do {\
   Oper##obj::TraceMsg(OperConfigTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#define IFMAP_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::IFMAP_AGENT)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

#endif // vnsw_agent_cmn_hpp

