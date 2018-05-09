/*
 * Each object of NexthopDBClient class interacts with the master
 * instance of NexthopDBServer and to the underlying (Unix socket) session
 * connected to the client process:
 *
 *                {RemoveNexthop()}
 *                {AddNexthop()}             {Send()}
 * [NexthopDBServer] ------ [NexthopDBClient] ----- [UnixDomainSocketSession]
 *                     1:n                     1:1
 */
#include <base/logging.h>
#include "nexthop_server/nexthop_server.h"
#include <pthread.h>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

NexthopDBClient::NexthopDBClient(UnixDomainSocketSession *session,
                                 NexthopDBServer *server)
  :  nexthop_list_()
{
    server_ = server;
    session_ = session;
    session_->set_observer(boost::bind(&NexthopDBClient::EventHandler, this,
                                       _1, _2));
}

NexthopDBClient::~NexthopDBClient()
{
    for (NexthopListIterator iter = nexthop_list_.begin();
         iter != nexthop_list_.end(); ++iter) {
        nexthop_list_.erase (iter);
    }
}

void
NexthopDBClient::EventHandler(UnixDomainSocketSession *session,
                              UnixDomainSocketSession::Event event)
{
    if (event == UnixDomainSocketSession::READY) {
        WriteMessage();
    } else if (event == UnixDomainSocketSession::CLOSE) {
        LOG (DEBUG, "[NexthopServer] Client " << session->session_id() <<
             " closed session");
        server_->RemoveClient(session->session_id());
    }
}

void
NexthopDBClient::AddNexthop(NexthopDBEntry::NexthopPtr nh)
{
    nexthop_list_.push_back(nh);
}

void
NexthopDBClient::RemoveNexthop(NexthopDBEntry::NexthopPtr nh)
{
    for (NexthopListIterator iter = nexthop_list_.begin();
         iter != nexthop_list_.end(); ++iter) {
        if (*(*iter) == *nh) {
            iter = nexthop_list_.erase(iter);
            break;
        }
    }
}

bool
NexthopDBClient::FindNexthop(NexthopDBEntry::NexthopPtr nh)
{
    for (NexthopListIterator iter = nexthop_list_.begin();
         iter != nexthop_list_.end(); ++iter) {
        if (*(*iter) == *nh) {
            return true;
        }
    }
    return false;
}

/* Caller should free the returned buffer */
uint8_t *
NexthopDBClient::NextMessage(int *data_len)
{
    contrail_rapidjson::StringBuffer s;
    contrail_rapidjson::Writer <contrail_rapidjson::StringBuffer> writer(s);

    writer.StartObject();

    int pdu_len = 0;

    for (NexthopListIterator iter = nexthop_list_.begin();
         iter != nexthop_list_.end();) {
        NexthopDBEntry::NexthopPtr tnh = *iter;

        if ((pdu_len + tnh->EncodedLength()) >
            UnixDomainSocketSession::kPDUDataLen) {
            break;
        }

        writer.String((tnh->nexthop_string()).c_str());
        writer.StartObject();
        const char *action = (tnh->state() ==
             NexthopDBEntry::NEXTHOP_STATE_DELETED) ? "del" : "add";
        writer.String("action");
        writer.String(action);
        writer.EndObject();

        pdu_len += tnh->EncodedLength();

        iter = nexthop_list_.erase(iter);
    }

    writer.EndObject();

    /* Nothing to consume? */
    if (!pdu_len) {
        return NULL;
    }

    const char *nhdata = s.GetString();
    int nhlen = strlen(nhdata);
    u_int8_t *out_data = new u_int8_t[nhlen + 4];
    out_data[0] = (unsigned char) (nhlen >> 24);
    out_data[1] = (unsigned char) (nhlen >> 16);
    out_data[2] = (unsigned char) (nhlen >> 8);
    out_data[3] = (unsigned char) nhlen;
    memcpy(&out_data[4], nhdata, nhlen);
    *data_len = nhlen + 4;
    return out_data;
}

void
NexthopDBClient::WriteMessage()
{
    uint8_t *data = NULL;
    int data_len;

    while(1) {
        data_len = 0;
        data = NextMessage(&data_len);
        if (!data || !data_len) {
            break;
        }

        /*
         * Send always succeeds
         */
        session_->Send(data, data_len);
        delete [] data;
    }
}
