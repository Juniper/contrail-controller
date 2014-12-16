#ifndef __AGENT_OPER_NEXTHOP_MANAGER_H__
#define __AGENT_OPER_NEXTHOP_MANAGER_H__

#include <queue>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include "db/db_table.h"
#include "cmn/agent_signal.h"
#include "db/db_entry.h"
#include "oper/nexthop.h"
#include "base/queue_task.h"
#include "oper/nexthop_server.h"

class DB;
class EventManager;

/*
 * Register for nexthop events from the global nexthop table.
 * Receive nexthop notifications from the nexthop table and send to
 * registered clients.
 */
class NexthopManager {
 public:

    NexthopManager(EventManager *evm);
    ~NexthopManager();

    void Initialize(DB *database, AgentSignal *signal);
    void Terminate();

 private:

    /*
     * Event observer for changes in the "db.nexthop.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    EventManager *evm_;
    DBTableBase *nh_table_;
    DBTableBase::ListenerId nh_listener_;
    NexthopDBServer *nh_server_;

    DISALLOW_COPY_AND_ASSIGN(NexthopManager);
};

#endif
