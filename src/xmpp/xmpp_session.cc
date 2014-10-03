/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_session.h"

#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_state_machine.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/xmpp_trace_sandesh_types.h"

using namespace std;

using boost::asio::mutable_buffer;

const boost::regex XmppSession::patt_(rXMPP_MESSAGE);
const boost::regex XmppSession::stream_patt_(rXMPP_STREAM_START);
const boost::regex XmppSession::stream_res_end_(rXMPP_STREAM_END);
const boost::regex XmppSession::whitespace_(sXMPP_WHITESPACE);

const std::string XmppStream::close_string = sXML_STREAM_C;

XmppSession::XmppSession(TcpServer *server, Socket *socket, bool async_ready)
        : TcpSession(server, socket, async_ready), connection_(NULL), 
          buf_(""), offset_(), tag_known_(0), 
          stats_(XmppStanza::RESERVED_STANZA, XmppSession::StatsPair(0,0)) {

    buf_.reserve(kMaxMessageSize);
    offset_ = buf_.begin();
}


XmppSession::~XmppSession() {
    set_observer(NULL);
    connection_ = NULL;
}

void XmppSession::WriteReady(const boost::system::error_code &error) {
    if (connection_)
        connection_->WriteReady(error);
}

XmppSession::StatsPair XmppSession::Stats(unsigned int type) const {
    assert (type < (unsigned int)XmppStanza::RESERVED_STANZA);
    return stats_[type];
}

void XmppSession::IncStats(unsigned int type, uint64_t bytes) {
    assert (type < (unsigned int)XmppStanza::RESERVED_STANZA);
    stats_[type].first++;
    stats_[type].second += bytes;
}

boost::regex XmppSession::tag_to_pattern(const char *tag) {
    std::string token("</");
    token += ++tag;
    token += "[\\s\\t\\r\\n]*>";

    boost::regex exp(token.c_str());
    return exp;
}

void XmppSession::SetBuf(const std::string &str) {
    if (buf_.empty()) {
        ReplaceBuf(str);
    } else {
        int pos = offset_ - buf_.begin();
        buf_ += str;
        offset_ = buf_.begin() + pos;
    }
}

void XmppSession::ReplaceBuf(const std::string &str) {
    buf_ = str;
    buf_.reserve(kMaxMessageSize+8);
    offset_ = buf_.begin();
}

bool XmppSession::LeftOver() const {
    if (buf_.empty())
        return false;
    return (buf_.end() != offset_);
}

// Match a pattern in the buffer. Partially matched string is
// kept in buf_ for use in conjucntion with next buffer read.
int XmppSession::MatchRegex(const boost::regex &patt) {

    std::string::const_iterator end = buf_.end();

    if (regex_search(offset_, end, res_, patt, 
                     boost::match_default | boost::match_partial) == 0) {
        return -1;
    }
    if(res_[0].matched == false) {
        // partial match
        offset_ = res_[0].first;
        return 1;
    } else {
        begin_tag_ = string(res_[0].first, res_[0].second);
        offset_ = res_[0].second;
        return 0; 
    }
}

bool XmppSession::Match(Buffer buffer, int *result, bool NewBuf) {
    const XmppConnection *connection = this->Connection();

    if (connection == NULL) {
        return true;
    }

    xmsm::XmState state = connection->GetStateMcState();

    if (NewBuf) {
        const uint8_t *cp = BufferData(buffer);
        // TODO Avoid this copy
        std::string str(cp, cp + BufferSize(buffer));
        XmppSession::SetBuf(str);
    }

    int m;
    *result = 0;
    do {
        if (!tag_known_) {
            // check for whitespaces
            size_t pos = buf_.find_first_not_of(sXMPP_VALIDWS);
            if (pos != 0) {
                if (pos == string::npos) pos = buf_.size();
                offset_ = buf_.begin() + pos;
                return false;
            }
        }

        if (state == xmsm::ACTIVE || state == xmsm::IDLE) {
            m = MatchRegex(tag_known_ ? stream_res_end_:stream_patt_);
        } else if (state == xmsm::CONNECT || state == xmsm::OPENSENT) { 
            m = MatchRegex(tag_known_ ? stream_res_end_:stream_patt_);
        } else if (state == xmsm::OPENCONFIRM || state == xmsm::ESTABLISHED) {
            m = MatchRegex(tag_known_ ? tag_to_pattern(begin_tag_.c_str()):patt_);
        }

        if (m == 0) { // full match
            *result = 0; 
            tag_known_ ^= 1; 
            if (!tag_known_) {
                // Found well formed xml
                return false;
            }
        } else if (m == -1) { // no match
            return true;
        } else {
            return true; // partial. read more
        }
    } while (true);

    return true;
}

// Read the socket stream and send messages to the connection object.
// The buffer is copied to local string for regex match. 
// TODO Code need to change st Match() is done on buffer itself.
void XmppSession::OnRead(Buffer buffer) {
    if (this->Connection() == NULL || !connection_) {
        // Connection is deleted. Session is being deleted as well
        // Drop the packet.
        ReleaseBuffer(buffer);
        return;
    }

    if (connection_->disable_read()) {
        ReleaseBuffer(buffer);

        // Reset the hold timer as we did receive some thing from the peer
        connection_->state_machine()->StartHoldTimer();
        return;
    }

    int result = 0;
    bool more = Match(buffer, &result, true);
    do {
        if (more == false) {
            if (result < 0) {
                // TODO generate error, close connection.
                break;
            }
            // We got good match. Process the message
            std::string::const_iterator st = buf_.begin();
            std::string xml = string(st, offset_);

            //
            // XXX Connection gone ?
            //
            if (!connection_) break;
            connection_->ReceiveMsg(this, xml);

        } else {
            // Read more data. Either we have partial match
            // or no match but in this state we need to keep
            // reading data.
            break;
        }

        if (LeftOver()) {
            std::string::const_iterator st = buf_.end();
            ReplaceBuf(string(offset_, st));
            more = Match(buffer, &result, false);
        } else {
            // No more data in the Buffer
            buf_.clear();
            break;
        }
    } while (true);

    ReleaseBuffer(buffer);
    return;
}
