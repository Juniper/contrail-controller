#include <base/logging.h>
#include "rapidjson/document.h"
#include "rapidjson/filestream.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "nexthop_server.h"

const char  SERV_SOCKET[] = "/var/run/contrail-nhserv.socket";

NexthopDBClient::NexthopDBClient (UnixDomainSocketSession *session,
				  NexthopDBServer *server)
{
  session_ = session;
  server_ = server;
  session_->set_observer(boost::bind(&NexthopDBClient::EventHandler, this,
				     _1, _2));
}

void
NexthopDBClient::EventHandler (UnixDomainSocketSession *session,
			       UnixDomainSocketSession::Event event)
{
  if (event == UnixDomainSocketSession::WRITE_READY) {
    WriteMessage();
  } else if (event == UnixDomainSocketSession::CLOSE) {
    server_->RemoveClient(this);
  }
}

/* Caller should free the string being returned */
char *
NexthopDBClient::NextMessage ()
{
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);

  writer.StartObject();

  nexthop_t *tnh;
  size_t pdu_len = 0;

  CLIENT_NEXTHOP_FOREACH(tnh) {
    if ((pdu_len + NEXTHOP_DATA_LEN(tnh)) > kPDUDataLen) {
      break;
    }

    pdu_len += NEXTHOP_DATA_LEN(tnh);
    CLIENT_NEXTHOP_CONSUME(tnh);

    writer.String(tnh->nexthop_string);
    writer.StartObject();
    const char *action = (tnh->state == NEXTHOP_STATE_DELETED) ? "del" : "add";
    writer.String("action");
    writer.String(action);
    writer.EndObject();
  }

  writer.EndObject();

  /* Nothing to consume? */
  if (!pdu_len) {
    return NULL;
  }

  char *out_data = strdup(s.GetString());
  assert(pdu_len >= strlen(out_data));
  return out_data;
}

void
NexthopDBClient::WriteMessage ()
{
  char *cp = NULL;
  while (1) {
    if (!session_->write_ready()) {
      break;
    }

    cp = NextMessage();
    if (!cp) {
      break;
    }

    session_->WriteData(cp);
    free(cp);
  }
}

NexthopDBServer::NexthopDBServer (boost::asio::io_service &io)
  : io_service_(io)
{
  std::remove(SERV_SOCKET);
  io_server_ = new UnixDomainSocketServer(io_service_, SERV_SOCKET);

  signals_ = new boost::asio::signal_set(io_service_, SIGUSR1);
  signals_->async_wait(boost::bind(&NexthopDBServer::SignalHandler, this,
				   boost::asio::placeholders::error,
				   boost::asio::placeholders::signal_number));

  timer_ = new boost::asio::deadline_timer(io_service_,
					   boost::posix_time::seconds(5));
  timer_->async_wait(boost::bind(&NexthopDBServer::TimerHandler, this,
				 boost::asio::placeholders::error));
  TAILQ_INIT(&nexthop_list_);
  io_server_->set_observer(boost::bind(&NexthopDBServer::EventHandler, this,
				       _1, _2, _3));
}

void
NexthopDBServer::Run ()
{
  io_service_.run();
}

void
NexthopDBServer::EventHandler (UnixDomainSocketServer *server,
			       UnixDomainSocketSession *session,
			       UnixDomainSocketServer::Event event)
{
  if (event == UnixDomainSocketServer::NEW_SESSION) {
    NexthopDBClient *cl = new NexthopDBClient(session, this);
    AddClient(cl);
  } else if (event == UnixDomainSocketServer::DELETE_SESSION) {
    NexthopDBClient *cl = client_table_[session->session_id()];
    if (cl) {
      RemoveClient(cl);
    }
  }
}

void
NexthopDBServer::AddClient (NexthopDBClient *cl)
{
  /* Add client to the client table */
  assert(client_table_[cl->session_->session_id()] == NULL);
  client_table_[cl->session_->session_id()] = cl;
  TAILQ_INSERT_HEAD(&nexthop_list_, &(cl->client_node_), list_next);
  cl->client_node_.object = cl;
  cl->client_node_.is_client = true;
  LOG(DEBUG, "New client: " << cl->session_->session_id());
}

void
NexthopDBServer::RemoveClient (NexthopDBClient *cl)
{
  LOG(DEBUG, "Removing client: " << cl->session_->session_id());
  client_table_.erase(cl->session_->session_id());
  TAILQ_REMOVE(&nexthop_list_, &(cl->client_node_), list_next);
  delete cl;
}

void
NexthopDBServer::TriggerClients ()
{
  for (ClientIterator iter = client_table_.begin();
       iter != client_table_.end(); ++iter) {
    iter->second->WriteMessage();
  }
}

nexthop_t *
NexthopDBServer::FindOrCreateNexthop (const char *nh_str)
{
  /*
   * Does the nexthop exist? If so, return.
   */
  if (nexthop_table_[nh_str] != NULL) {
    return nexthop_table_[nh_str];
  }

  nexthop_t *nh = (nexthop_t *)calloc(1, sizeof(*nh));
  assert(nh);
  nh->nexthop_string = strdup(nh_str);
  assert(nh->nexthop_string);
  nexthop_table_[nh->nexthop_string] = nh;

  /*
   * Add the nexthop to the tail of the announce list and trigger
   * clients so they are notified of the new nexthop.
   */
  TAILQ_INSERT_TAIL(&nexthop_list_, &(nh->nexthop_node), list_next);
  nh->nexthop_node.object = nh;
  TriggerClients();
  return nh;
}

void
NexthopDBServer::FindAndRemoveNexthop (const char *str)
{
  if (nexthop_table_[str] == NULL) {
    return;
  }
  nexthop_t *nh = nexthop_table_[str];
  RemoveNexthop(nh);
  TriggerClients();
}

void
NexthopDBServer::RemoveNexthop (nexthop_t *nh)
{
  assert(nh);
  nh->state = NEXTHOP_STATE_DELETED;
  nexthop_table_.erase(nh->nexthop_string);
  if (NEXTHOP_IN_LIST(nh)) {
    TAILQ_REMOVE(&nexthop_list_, &(nh->nexthop_node), list_next);
  }
  TAILQ_INSERT_TAIL(&nexthop_list_, &(nh->nexthop_node), list_next);
}

void
NexthopDBServer::FreeNexthop (nexthop_t *nh)
{
  assert(nh->state = NEXTHOP_STATE_DELETED);
  if (NEXTHOP_IN_LIST(nh)) {
    TAILQ_REMOVE(&nexthop_list_, &(nh->nexthop_node), list_next);
  }
  free(nh);
}

void
NexthopDBServer::SignalHandler (const boost::system::error_code& error,
				int signal_number)
{
  signals_->async_wait(boost::bind(&NexthopDBServer::SignalHandler, this,
				   boost::asio::placeholders::error,
				   boost::asio::placeholders::signal_number));
}

void
NexthopDBServer::TimerHandler (const boost::system::error_code& error)
{
  list_entry_t *ent, *tent;
  nexthop_t *tnh;

  /*
   * Go through the nexthop list and free all nexthops that have been deleted.
   * Stop the moment we encounter a client - that means that the client has
   * not processed the nexthops in front of it - so even if a nexthop in
   * front of it was marked deleted, we can't free it.
   */
  LISTENT_FOREACH(ent, tent) {
    if (ent->is_client) {
      break;
    } else {
      tnh = (nexthop_t *)ent->object;
      if (tnh->state == NEXTHOP_STATE_DELETED) {
	FreeNexthop(tnh);
      }
    }
  }

  timer_->expires_at(timer_->expires_at() + boost::posix_time::seconds(5));
  timer_->async_wait(boost::bind(&NexthopDBServer::TimerHandler, this,
				 boost::asio::placeholders::error));
}

