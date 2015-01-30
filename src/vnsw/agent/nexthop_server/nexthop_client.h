/*
 * NexthopDBClient class: represents a client connecting to the nexthop-server
 *                        interface for nexthop notifications.
 */
#ifndef _AGENT_OPER_NEXTHOP_CLIENT_H_
#define _AGENT_OPER_NEXTHOP_CLIENT_H_

#include <cstdio>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <tbb/mutex.h>
#include "io/usock_server.h"
#include "nexthop_entry.h"

class NexthopDBServer;

/*
 * Class to represent each client that connects to nexthop-server to
 * receive nexthop notifications.
 */
class NexthopDBClient {//: public boost::enable_shared_from_this<NexthopDBClient> {
 public:

  typedef boost::shared_ptr<NexthopDBClient> ClientPtr;
  typedef std::vector<NexthopDBEntry::NexthopPtr> NexthopList;
  typedef std::vector<NexthopDBEntry::NexthopPtr>::iterator
    NexthopListIterator;

  NexthopDBClient(UnixDomainSocketSession *session, NexthopDBServer *server);
  ~NexthopDBClient();

  void AddNexthop(NexthopDBEntry::NexthopPtr nh);
  void RemoveNexthop(NexthopDBEntry::NexthopPtr nh);

  /*
   * Handle events from the underlying (unix socket) session to this client
   */
  void EventHandler(UnixDomainSocketSession *, UnixDomainSocketSession::Event);

 protected:
  friend class NexthopDBServer;

 private:
  UnixDomainSocketSession *session_;
  NexthopDBServer *server_;

  /*
   * Each client object maintains its own vector of nexthops that need to be
   * dispatched. The nexthop pointer is removed from the vector after the
   * dispatch.
   */
  NexthopList nexthop_list_;

  void WriteMessage();
  uint8_t *NextMessage(int *length);
};

#endif
