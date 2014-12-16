#ifndef _AGENT_OPER_NEXTHOP_SERV_H_
#define _AGENT_OPER_NEXTHOP_SERV_H_

#include <cstdio>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>

#include "queue.h"
#include "usock_server.h"

class NexthopDBServer;

/*
 * List entry structure to track send-state of nexthops to clients.
 * Both the nexthop and client objects embed this structure.
 */
typedef struct list_entry {
  TAILQ_ENTRY(list_entry) list_next;
  void *object;
  bool is_client;
} list_entry_t;

/*
 * Class to represent each client that connects to vrouter-agent to
 * receive nexthop notifications.
 */
class NexthopDBClient {
 public:
  NexthopDBClient(UnixDomainSocketSession *session, NexthopDBServer *server);

  void EventHandler(UnixDomainSocketSession *, UnixDomainSocketSession::Event);

 protected:
  friend class NexthopDBServer;

 private:
  UnixDomainSocketSession *session_;
  NexthopDBServer *server_;
  list_entry_t client_node_;
  char *name_;

  void WriteMessage();

  char *NextMessage();

#define CLIENT_NEXTHOP_FOREACH(nh)				   \
  for (list_entry_t *entry = TAILQ_NEXT(&client_node_, list_next); \
       entry; entry = TAILQ_NEXT(entry, list_next))		   \
    if (!entry->is_client && (nh = (nexthop_t *) entry->object))

#define CLIENT_NEXTHOP_CONSUME(nh)					\
  TAILQ_REMOVE(&(server_->nexthop_list_), &client_node_, list_next);	\
  if (nh) {								\
    TAILQ_INSERT_AFTER(&(server_->nexthop_list_), &(nh->nexthop_node),	\
		       &client_node_, list_next);			\
  } else {								\
    TAILQ_INSERT_TAIL(&(server_->nexthop_list_), &client_node_, list_next); \
  }
};

enum {
  NEXTHOP_STATE_CLEAN,
  NEXTHOP_STATE_MARKED,
  NEXTHOP_STATE_DELETED
};

/*
 * A passive struct to represent each nexthop.
 */
typedef struct nexthop {
  char *nexthop_string;
  list_entry_t nexthop_node;
  int state;
} nexthop_t;

#define NEXTHOP_IN_LIST(nh)						\
  (TAILQ_NEXT(&(nh->nexthop_node), list_next) != NULL ||		\
   TAILQ_PREV(&(nh->nexthop_node), nexthoplist, list_next) != NULL)


#define NEXTHOP_DATA_OVERHEAD 32
#define NEXTHOP_DATA_LEN(nh) \
  (strlen((nh)->nexthop_string) + NEXTHOP_DATA_OVERHEAD)


/*
 * The main server object for serving nexthops to connected clients.
 */
class NexthopDBServer {
 public:

  typedef std::map<uint64_t, NexthopDBClient *>::iterator ClientIterator;

  NexthopDBServer(boost::asio::io_service &io);

  nexthop_t *FindOrCreateNexthop(const char *str);
  void FindAndRemoveNexthop (const char *str);
  void Run();
  void EventHandler(UnixDomainSocketServer *, UnixDomainSocketSession *,
		    UnixDomainSocketServer::Event);

 protected:
  friend class NexthopDBClient;

 private:
  TAILQ_HEAD(nexthoplist, list_entry) nexthop_list_;
  boost::asio::io_service &io_service_;
  UnixDomainSocketServer *io_server_;
  boost::asio::signal_set *signals_;
  boost::asio::deadline_timer *timer_;
  std::map<std::string, nexthop_t *> nexthop_table_;
  std::map<uint64_t, NexthopDBClient *> client_table_;

  void AddClient(NexthopDBClient *cl);
  void RemoveClient(NexthopDBClient *cl);
  void TriggerClients();
  void SignalHandler(const boost::system::error_code& error,
		     int signal_number);
  void TimerHandler(const boost::system::error_code& error);
  void RemoveNexthop(nexthop_t *);
  void FreeNexthop(nexthop_t *);

#define LISTENT_FOREACH(ent, tent)				       \
  TAILQ_FOREACH_SAFE(ent, &nexthop_list_, list_next, tent)

#define NEXTHOP_FOREACH(nh)						    \
  for (list_entry_t *tent, *ent = TAILQ_FIRST(&nexthop_list_); \
       (ent) && (tent = TAILQ_NEXT(ent, list_next), 1); ent = tent) \
    if (!ent->is_client && (nh = (nexthop_t *) ent->object))
};

#endif
