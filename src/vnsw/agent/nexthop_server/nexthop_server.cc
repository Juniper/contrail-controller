/*
 * NexthopDBServer maintains two primary data structures:
 * (1) nexthop_table: a std::map DB of nexthop entries (NexthopDBEntry)
 *     keyed through a string representation of the nexthop address.
 * (2) client_table: a std::map DB of client entries (NexthopDBClient)
 *     keyed through the session_id of the underlying session
 *     (UnixDomainSocketSession).
 */
#include <base/logging.h>
#include "nexthop_client.h"
#include "nexthop_server.h"
#include <pthread.h>
#include "rapidjson/document.h"
#include "rapidjson/filestream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

NexthopDBServer::NexthopDBServer(boost::asio::io_service &io,
                                 const std::string &path)
  : io_service_(io), endpoint_path_(path), nexthop_table_(), client_table_()
{
    std::remove(endpoint_path_.c_str());
    io_server_.reset(new UnixDomainSocketServer(&io_service_, endpoint_path_));
    io_server_->set_observer(boost::bind(&NexthopDBServer::EventHandler, this,
                                         _1, _2, _3));
}

void
NexthopDBServer::Run()
{
    io_service_.run();
}

void
NexthopDBServer::EventHandler(UnixDomainSocketServer * server,
                              UnixDomainSocketSession * session,
                              UnixDomainSocketServer::Event event)
{
    tbb::mutex::scoped_lock lock(mutex_);
    if (event == UnixDomainSocketServer::NEW_SESSION) {
        NexthopDBClient::ClientPtr cl(new NexthopDBClient(session, this));
        AddClient(cl);
    } else if (event == UnixDomainSocketServer::DELETE_SESSION) {
        NexthopDBClient::ClientPtr cl = client_table_[session->session_id()];
        if (cl) {
            RemoveClient(session->session_id());
        }
    }
}

void
NexthopDBServer::AddClient(NexthopDBClient::ClientPtr cl)
{
    /* Add client to the client table */
    assert (client_table_[cl->session_->session_id()] == NULL);
    client_table_[cl->session_->session_id()] = cl;

    /* Build the client's nexthop list */
    for (NexthopIterator iter = nexthop_table_.begin();
         iter != nexthop_table_.end(); ++iter) {
        if (iter->second) {
            cl->AddNexthop(iter->second);
        }
    }

    cl->WriteMessage();
    LOG (DEBUG, "[NexthopServer] New client: " << cl->session_->session_id());
}

void
NexthopDBServer::RemoveClient(uint64_t session_id)
{
    LOG (DEBUG, "[NexthopServer] Remove client " << session_id);
    client_table_.erase(session_id);
}

void
NexthopDBServer::TriggerClients()
{
    for (ClientIterator iter = client_table_.begin();
         iter != client_table_.end(); ++iter) {
        iter->second->WriteMessage();
    }
}

void
NexthopDBServer::AddNexthop(NexthopDBEntry::NexthopPtr nh)
{
    for (ClientIterator iter = client_table_.begin();
         iter != client_table_.end(); ++iter) {
        iter->second->AddNexthop(nh);
    }
}

NexthopDBEntry::NexthopPtr
NexthopDBServer::FindOrCreateNexthop(const std::string &nh_str)
{
    tbb::mutex::scoped_lock lock (mutex_);

    /*
     * Does the nexthop exist? If so, return.
     */
    if (nexthop_table_[nh_str] != NULL) {
        return nexthop_table_[nh_str];
    }

    NexthopDBEntry::NexthopPtr nh(new NexthopDBEntry(nh_str));
    nexthop_table_[nh_str] = nh;

    /*
     * Add the nexthop to the tail of each client's announce list and trigger
     * clients so they are notified of the new nexthop.
     */
    AddNexthop(nh);
    TriggerClients();
    return nh;
}

void
NexthopDBServer::FindAndRemoveNexthop(const std::string &str)
{
    tbb::mutex::scoped_lock lock(mutex_);

    if (nexthop_table_[str] == NULL) {
        return;
    }
    NexthopDBEntry::NexthopPtr nh = nexthop_table_[str];
    RemoveNexthop(nh);
    nexthop_table_.erase(str);
    TriggerClients();
}

void
NexthopDBServer::RemoveNexthop(NexthopDBEntry::NexthopPtr nh)
{
    nh->set_state(NexthopDBEntry::NEXTHOP_STATE_DELETED);
    for (ClientIterator iter = client_table_.begin();
         iter != client_table_.end(); ++iter) {
        if (!iter->second->FindNexthop(nh)) {
            iter->second->AddNexthop(nh);
        }
    }
}
