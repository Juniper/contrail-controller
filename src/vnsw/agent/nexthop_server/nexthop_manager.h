/*
 * A generic server interface to send notification about tunnel nexthops
 * to interested clients. The server supports any number of clients. The
 * communication occurs over a UNIX socket. The message is really a JSON
 * frame:
 *
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                             Length                            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                                                               |
       |                       JSON text (variable)                    |
       |                                                               |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * where the JSON text follows the format:
 *
 *    {
 *      "<next hop 1 IP address>": {
 *          "action": <operation>
 *      },
 *      "<next hop 2 IP address>": {
 *          "action": <operation>
 *      },
 *      ...
 *    }
 *
 * <operation> can take one of the two values: "add" or "del", indicating
 * whether the next hop has been added or deleted.
 *
 * One can test a sample client by running:
 *     sudo socat -s - UNIX-CONNECT:/var/run/contrail-nhserv.socket
 */
#ifndef __AGENT_NHS_NEXTHOP_MANAGER_H__
#define __AGENT_NHS_NEXTHOP_MANAGER_H__

#include "base/queue_task.h"
#include <boost/asio.hpp>
#include "cmn/agent_signal.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "nexthop_server.h"
#include "oper/nexthop.h"
#include <queue>

class DB;
class EventManager;

/*
 * Register for nexthop events from the global nexthop table.
 * Receive nexthop notifications from the nexthop table and send to
 * registered clients.
 */
class NexthopManager
{
 public:

    NexthopManager(EventManager *evm, const std::string &endpoint);
    ~NexthopManager();

    void Initialize(DB *database);
    void Terminate();

 private:

    /*
     * Event observer for changes in the "db.nexthop.0" table.
     */
    void EventObserver(DBTablePartBase * db_part, DBEntryBase * entry);

    EventManager *evm_;
    DBTableBase *nh_table_;
    DBTableBase::ListenerId nh_listener_;

    boost::scoped_ptr<NexthopDBServer> nh_server_;
    DISALLOW_COPY_AND_ASSIGN(NexthopManager);
};

#endif
