/*
 * This file along with nexthop_server.[cc|h] and usock_server.[cc|h]
 * implement a generic server (within vrouter agent process) to send
 * notification about tunnel nexthops to interested clients. At the
 * moment of writing, the primary motivation is to send the tunnel
 * nexthop events to a SSL forwarding component so that SSL sessions
 * can be brought up between _this vrouter node_ and the other
 * nexthops.
 *
 * The server supports any number of clients. The communication occurs
 * over a UNIX socket. This is also a requirement since the clients
 * and server (vrouter agent) may operate in different network
 * namespaces. The data being sent follows JSON format, as follows:
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
 *      "next hop 1 IP address": {
 *          "action": <operation>
 *      },
 *      "next hop 2 IP address": {
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
 *
 * The division of labor between the files is the following:
 *
 *  nexthop_manager.[cc|h]: the interface with "vrouter agent" machine;
 *                          registration for db.nexthop.0 table.
 *  nexthop_server.[cc|h]: the server implementation. Keep track of
 *                         nexthops, their states, and clients. Send
 *                         nexthop events to clients.
 *  usock_server.[cc|h]: implement a UNIX domain socket interface using
 *                       boost::asio.
 */

#include <boost/filesystem.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <sys/wait.h>
#include "db/db.h"
#include "io/event_manager.h"
#include "cmn/agent_signal.h"
#include "oper/tunnel_nh.h"
#include "oper/nexthop_manager.h"

using boost::uuids::uuid;

NexthopManager::NexthopManager(EventManager *evm)
  : evm_(evm), nh_table_(NULL),
    nh_listener_(DBTableBase::kInvalidId)
{
}

NexthopManager::~NexthopManager()
{
}

void
NexthopManager::Initialize(DB *database, AgentSignal *signal)
{
  nh_table_ = database->FindTable("db.nexthop.0");
  assert(nh_table_);
  nh_listener_ = nh_table_->Register(
        boost::bind(&NexthopManager::EventObserver, this, _1, _2));

  nh_server_ = new NexthopDBServer(*(evm_->io_service()));
}

void
NexthopManager::Terminate()
{
  nh_table_->Unregister(nh_listener_);
  delete nh_server_;
}

void
NexthopManager::EventObserver(DBTablePartBase *db_part, DBEntryBase *entry)
{
  NextHop *nh = static_cast<NextHop *>(entry);
  if (nh->GetType() == NextHop::TUNNEL) {
    TunnelNH *tnh = (TunnelNH *) nh;
    if (nh->IsDeleted()) {
      nh_server_->FindAndRemoveNexthop(tnh->GetDip()->to_string().c_str());
    } else {
      nh_server_->FindOrCreateNexthop(tnh->GetDip()->to_string().c_str());
    }
  }
}
