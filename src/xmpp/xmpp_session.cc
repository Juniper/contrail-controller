/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_session.h"

#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_server.h"
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
const boost::regex XmppSession::stream_features_patt_(rXMPP_STREAM_FEATURES);
const boost::regex XmppSession::starttls_patt_(rXMPP_STREAM_STARTTLS);
const boost::regex XmppSession::proceed_patt_(rXMPP_STREAM_PROCEED);
const boost::regex XmppSession::end_patt_(rXMPP_STREAM_STANZA_END);

XmppSession::XmppSession(XmppConnectionManager *manager, SslSocket *socket,
    bool async_ready)
    : SslSession(manager, socket, async_ready),
      manager_(manager),
      connection_(NULL),
      tag_known_(0),
      task_instance_(-1),
      stats_(XmppStanza::RESERVED_STANZA, XmppSession::StatsPair(0, 0)),
      keepalive_probes_(kSessionKeepaliveProbes) {
    buf_.reserve(kMaxMessageSize);
    offset_ = buf_.begin();
    stream_open_matched_ = false;
}

XmppSession::~XmppSession() {
    set_observer(NULL);
    connection_ = NULL;
}

void XmppSession::SetConnection(XmppConnection *connection) {
    assert(connection);
    connection_ = connection;
    task_instance_ = connection_->GetTaskInstance();
}

//
// Dissociate the connection from the this XmppSession.
// Do not invalidate the task_instance since it can be used to spawn an
// io::ReaderTask while this method is being executed.
//
void XmppSession::ClearConnection() {
    connection_ = NULL;
}

//
// Concurrency: called in the context of bgp::Config task.
//
// Process write ready callback.
//
void XmppSession::ProcessWriteReady() {
    if (!connection_)
        return;
    connection_->WriteReady();
}

//
// Concurrency: called in the context of io thread.
//
// Handle write ready callback.
//
// Enqueue session to the XmppConnectionManager. The session is added to a
// WorkQueue gets processed in the context of bgp::Config task. Doing this
// ensures that we don't access the XmppConnection while the XmppConnection
// is trying to clear our back pointer to it.
//
// We can ignore any errors since the StateMachine will get informed of the
// TcpSession close independently and react to it.
//
void XmppSession::WriteReady(const boost::system::error_code &error) {
    if (error)
        return;
    manager_->EnqueueSession(this);
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

boost::system::error_code XmppSession::EnableTcpKeepalive(int hold_time) {
    char *keepalive_time_str = getenv("TCP_KEEPALIVE_SECONDS");
    if (keepalive_time_str) {
        hold_time = strtoul(keepalive_time_str, NULL, 0) * 3;
        if (!hold_time)
            return boost::system::error_code();
    }

    if (hold_time <= 9) {
        hold_time = 9; // min hold-time in secs.
    }
    hold_time = ((hold_time > 18)? hold_time/2 : hold_time);
    keepalive_idle_time_ = hold_time/3;
    keepalive_interval_ =
        ((hold_time - keepalive_idle_time_)/keepalive_probes_);
    tcp_user_timeout_ = (hold_time * 1000); // msec

    return (SetSocketKeepaliveOptions(keepalive_idle_time_,
                                      keepalive_interval_,
                                      keepalive_probes_,
                                      tcp_user_timeout_));
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
    xmsm::XmOpenConfirmState oc_state =
        connection->GetStateMcOpenConfirmState();

    if (NewBuf) {
        const uint8_t *cp = BufferData(buffer);
        // TODO Avoid this copy
        std::string str(cp, cp + BufferSize(buffer));
        XmppSession::SetBuf(str);
    }

    int m = -1;
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
            // Note, these are client only states
            if (!stream_open_matched_) {
                m = MatchRegex(tag_known_ ? stream_res_end_:stream_patt_);
                if ((m == 0) && (tag_known_)) {
                    stream_open_matched_ = true;
                }
            } else {
                m = MatchRegex(tag_known_ ? tag_to_pattern(begin_tag_.c_str()):
                                            stream_features_patt_);
            }
        } else if ((state == xmsm::OPENCONFIRM) && !(IsSslDisabled())) {
            if (connection->IsClient()) {
                if (oc_state == xmsm::OPENCONFIRM_FEATURE_NEGOTIATION) {
                    m = MatchRegex(tag_known_ ? end_patt_: proceed_patt_);
                    if ((m == 0) && (tag_known_)) {
                        // set the flag, as we do not want OnRead function to
                        // read any more data from basic socket.
                        SetSslHandShakeInProgress(true);
                    }
                } else if (oc_state == xmsm::OPENCONFIRM_FEATURE_SUCCESS) {
                    m = MatchRegex(tag_known_ ? stream_res_end_:stream_patt_);
                } else {
                    m = MatchRegex(tag_known_ ? tag_to_pattern(begin_tag_.c_str()):
                                                stream_features_patt_);
                }
            } else {
                if (oc_state == xmsm::OPENCONFIRM_FEATURE_SUCCESS) {
                    m = MatchRegex(tag_known_ ? stream_res_end_:stream_patt_);
                } else {
                    m = MatchRegex(tag_known_ ? end_patt_:starttls_patt_);
                    if ((m == 0) && (tag_known_)) {
                        SetSslHandShakeInProgress(true);
                    }
                }
            }
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
            // Ensure we have not reached the end
            if (buf_.begin() == offset_) { // xml.size() == 0
                buf_.clear();
                break;
            }

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
