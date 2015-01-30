/*
 * NexthopDBServer class: represents the master server interface that
 *                        has a Nexthop DB store and a list of clients.
 */
#ifndef _AGENT_OPER_NEXTHOP_SERVER_H_
#define _AGENT_OPER_NEXTHOP_SERVER_H_

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
#include "nexthop_client.h"
#include "nexthop_entry.h"

/*
 * The main server object for serving nexthops to connected clients.
 */
class NexthopDBServer {
 public:

  typedef std::map<uint64_t, NexthopDBClient::ClientPtr> ClientDB;
  typedef std::map<uint64_t, NexthopDBClient::ClientPtr>::iterator
    ClientIterator;
  typedef std::map<std::string, NexthopDBEntry::NexthopPtr> NexthopDB;
  typedef std::map<std::string, NexthopDBEntry::NexthopPtr>::iterator
    NexthopIterator;

  NexthopDBServer(boost::asio::io_service &io);

  NexthopDBEntry::NexthopPtr FindOrCreateNexthop(std::string str);
  void FindAndRemoveNexthop(std::string str);
  void Run();
  void EventHandler(UnixDomainSocketServer *, UnixDomainSocketSession *,
                    UnixDomainSocketServer::Event);
  void RemoveClient(uint64_t);

 private:
  boost::asio::io_service &io_service_;
  UnixDomainSocketServer *io_server_;
  NexthopDB nexthop_table_;
  ClientDB client_table_;
  tbb::mutex mutex_;

  void AddClient(NexthopDBClient::ClientPtr cl);
  void TriggerClients();
  void AddNexthop(NexthopDBEntry::NexthopPtr nh);
  void RemoveNexthop(NexthopDBEntry::NexthopPtr nh);
};

#endif
