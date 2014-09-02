/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_proto.h"
#include <iostream>
#include <string>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_str.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_message_sandesh_types.h"
#include "sandesh/xmpp_trace_sandesh_types.h"

using namespace std;

auto_ptr<XmlBase> XmppProto::open_doc_(AllocXmppXmlImpl(sXMPP_STREAM_OPEN));

XmppStanza::XmppStanza() {
}

XmppProto::XmppProto() { 
}

XmppProto::~XmppProto() {
}

int XmppProto::EncodeStream(const XmppStreamMessage &str, string &to,
                            string &from, uint8_t *buf, size_t size) {
    int len = 0;

    if (str.strmtype == XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER) {
        len = EncodeOpen(buf, to, from);
    }

    if (str.strmtype == XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER_RESP) {
        len = EncodeOpenResp(buf, to, from);
    }

    return len;
}

int XmppProto::EncodeStream(const XmppStanza::XmppMessage &str, uint8_t *buf, 
                            size_t size) {
    int ret = 0;

    if (str.type == XmppStanza::WHITESPACE_MESSAGE_STANZA) {
        return EncodeWhitespace(buf);
    }

    return ret;
}

int XmppProto::EncodeMessage(XmlBase *dom, uint8_t *buf, size_t size) {
    int len = dom->WriteDoc(buf);

    return len;
}

int XmppProto::EncodePresence(uint8_t *buf, size_t size) {
    return 0;
}

int XmppProto::EncodeIq(const XmppStanza::XmppMessageIq *iq, 
                        XmlBase *doc, uint8_t *buf, size_t size) {
    auto_ptr<XmlBase> send_doc_(AllocXmppXmlImpl());

    // create
    send_doc_->LoadDoc("");
    send_doc_->AddNode("iq", "");

    switch(iq->stype) {
        case XmppStanza::XmppMessageIq::GET: 
            send_doc_->AddAttribute("type", "get");
            break;
        case XmppStanza::XmppMessageIq::SET: 
            send_doc_->AddAttribute("type", "set");
            break;
        case XmppStanza::XmppMessageIq::RESULT: 
            send_doc_->AddAttribute("type", "result");
            break;
        case XmppStanza::XmppMessageIq::ERROR: 
            send_doc_->AddAttribute("type", "error");
            break;
        default:
            break;
    }
    send_doc_->AddAttribute("from", iq->from);
    send_doc_->AddAttribute("to", iq->to);
    send_doc_->AddAttribute("id", "id1");

    send_doc_->AddChildNode("pubsub", "");
    send_doc_->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");

    send_doc_->AppendDoc("pubsub", doc);

    //Returns byte encoded in the doc
    int len = send_doc_->WriteDoc(buf);

    return len;
}

int XmppProto::EncodeWhitespace(uint8_t *buf) {
    string str(sXMPP_WHITESPACE);
     
    int len = str.size();
    if (len > 0) {
        memcpy(buf, str.data(), len);
    }

    return len;
}

int XmppProto::EncodeOpenResp(uint8_t *buf, string &to, string &from) {

    auto_ptr<XmlBase> resp_doc(XmppStanza::AllocXmppXmlImpl(sXMPP_STREAM_RESP));

    if (resp_doc.get() == NULL) {
        return 0;
    }

    SetTo(to, resp_doc.get()); 
    SetFrom(from, resp_doc.get()); 

    //Returns byte encoded in the doc
    int len = resp_doc->WriteRawDoc(buf);
    if (len > 0) {
        string doc(buf, buf+len);

        boost::algorithm::ireplace_last(doc, "/", " ");

        memcpy(buf, doc.c_str(), len);
    }
    return len;
}

int XmppProto::EncodeOpen(uint8_t *buf, string &to, string &from) {

    if (open_doc_.get() ==  NULL) {
        return 0;
    }

    SetTo(to, open_doc_.get()); 
    SetFrom(from, open_doc_.get()); 

    //Returns byte encoded in the doc
    int len = open_doc_->WriteRawDoc(buf);
    if (len > 0) {
        string doc(buf, buf+len);

        boost::algorithm::ireplace_last(doc, "/", " ");

        memcpy(buf, doc.c_str(), len);
    }
    return len;
}

XmppStanza::XmppMessage *XmppProto::Decode(const string &ts) {
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    if (impl.get() == NULL) {
        return NULL;
    }

    XmppStanza::XmppMessage *msg = DecodeInternal(ts, impl.get());
    if (!msg) {
        return NULL;
    }

    // transfer ownership
    msg->dom = impl;

    return msg;
}

XmppStanza::XmppMessage *XmppProto::DecodeInternal(const string &ts, 
                                                   XmlBase *impl) {
    XmppStanza::XmppMessage *ret = NULL;

    string ns(sXMPP_STREAM_O);
    string ws(sXMPP_WHITESPACE);
    string iq(sXMPP_IQ_KEY);

    if (ts.find(sXMPP_IQ) != string::npos) {
        string ts_tmp = ts;

        if (impl->LoadDoc(ts) == -1) {
            XMPP_WARNING(XmppIqMessageParseFail);
            assert(false);
            goto done;
        }

        XmppStanza::XmppMessageIq *msg = new XmppStanza::XmppMessageIq;
        impl->ReadNode(iq);
        msg->to = XmppProto::GetTo(impl); 
        msg->from = XmppProto::GetFrom(impl); 
        msg->id = XmppProto::GetId(impl); 
        msg->iq_type = XmppProto::GetType(impl); 
        // action is subscribe,publish,collection
        const char *action = XmppProto::GetAction(impl, msg->iq_type);
        if (action) {
            msg->action = action;
        }
        if (XmppProto::GetNode(impl, msg->action)) {
            msg->node = XmppProto::GetNode(impl, msg->action);
        }
        //associate or dissociate collection node
        if (msg->action.compare("collection") == 0) {
            if (XmppProto::GetAsNode(impl)) {
                msg->as_node = XmppProto::GetAsNode(impl);
                msg->is_as_node = true;
            } else if (XmppProto::GetDsNode(impl)) {
                msg->as_node = XmppProto::GetDsNode(impl);
                msg->is_as_node = false;
            }
        }

        msg->dom.reset(impl);

        ret = msg;

        XMPP_UTDEBUG(XmppIqMessageProcess, msg->node, msg->action, 
                   msg->from, msg->to, msg->id, msg->iq_type);
        goto done;

    } else if (ts.find(sXMPP_MESSAGE) != string::npos) {

        if (impl->LoadDoc(ts) == -1) {
            XMPP_WARNING(XmppChatMessageParseFail);
            goto done;
        }
        XmppStanza::XmppMessage *msg = new XmppStanza::XmppChatMessage(
                                           STATE_NONE); 
        impl->ReadNode(sXMPP_MESSAGE_KEY);

        msg->to = XmppProto::GetTo(impl);
        msg->from = XmppProto::GetFrom(impl);
        ret = msg;

        XMPP_UTDEBUG(XmppChatMessageProcess, msg->type, msg->from, msg->to);
        goto done;

    } else if (ts.find(sXMPP_STREAM_O) != string::npos) {

        // ensusre stream open is at the beginning of the message
        string ts_tmp = ts;
        ts_tmp.erase(std::remove(ts_tmp.begin(), ts_tmp.end(), '\n'), ts_tmp.end());

        if ((ts_tmp.compare(0, strlen(sXMPP_STREAM_START), 
             sXMPP_STREAM_START) != 0) && 
            (ts_tmp.compare(0, strlen(sXMPP_STREAM_START_S), 
             sXMPP_STREAM_START_S) != 0)) {
            XMPP_WARNING(XmppBadMessage, "Open message not at the beginning.");
            goto done;
        }

        // check if the buf is xmpp open or response message
        // As end tag will be missing we need to modify the 
        // string for stream open, else dom decoder will fail 
        boost::algorithm::replace_last(ts_tmp, ">", "/>");
        if (impl->LoadDoc(ts_tmp) == -1) {
            XMPP_WARNING(XmppBadMessage, "Open message parse failed.");
            goto done;
        }

        XmppStanza::XmppStreamMessage *strm = 
            new XmppStanza::XmppStreamMessage();
        strm->strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
        impl->ReadNode(ns);
        strm->to = XmppProto::GetTo(impl); 
        strm->from = XmppProto::GetFrom(impl); 

        ret = strm;

        XMPP_UTDEBUG(XmppRxOpenMessage, strm->from, strm->to);

    } else if (ts.find_first_of(sXMPP_VALIDWS) != string::npos) {

        XmppStanza::XmppMessage *msg = 
            new XmppStanza::XmppMessage(WHITESPACE_MESSAGE_STANZA);
        return msg;
    }

done:

    return ret;
}

int XmppProto::SetTo(string &to, XmlBase *doc) {
    if (!doc) return -1;

    string ns(sXMPP_STREAM_O);
    doc->ReadNode(ns);
    doc->ModifyAttribute("to", to);
 
    return 0;
}

int XmppProto::SetFrom(string &from, XmlBase *doc) {
    if (!doc) return -1;

    string ns(sXMPP_STREAM_O);
    doc->ReadNode(ns);
    doc->ModifyAttribute("from", from);
    return 0;
}

const char *XmppProto::GetTo(XmlBase *doc) {
    if (!doc) return NULL;

    string tmp("to");
    return doc->ReadAttrib(tmp);
}

const char *XmppProto::GetFrom(XmlBase *doc) {
    if (!doc) return NULL;

    string tmp("from");
    return doc->ReadAttrib(tmp);
}

const char *XmppProto::GetId(XmlBase *doc) {
    if (!doc) return NULL;

    string tmp("id");
    return doc->ReadAttrib(tmp);
}

const char *XmppProto::GetType(XmlBase *doc) {
    if (!doc) return NULL;

    string tmp("type");
    return doc->ReadAttrib(tmp);
}

const char *XmppProto::GetAction(XmlBase *doc, const string &str) {
    if (!doc) return NULL;

    if (str.compare("set") == 0) {
        doc->ReadNode("pubsub");
        return(doc->ReadChildNodeName());
    } else if (str.compare("get") == 0) {
    }

    return(NULL);
}

const char *XmppProto::GetNode(XmlBase *doc, const string &str) {
    if (!doc) return NULL;

    if (!str.empty()) {
        return(doc->ReadAttrib("node"));
    }

    return(NULL);
}

const char *XmppProto::GetAsNode(XmlBase *doc) {
    if (!doc) return NULL;

    const char *node = doc->ReadNode("associate");
    if (node != NULL) {
        return(doc->ReadAttrib("node"));
    }

    return(NULL);
}

const char *XmppProto::GetDsNode(XmlBase *doc) {
    if (!doc) return NULL;

    const char *node = doc->ReadNode("dissociate");
    if (node != NULL) {
        return(doc->ReadAttrib("node"));
    }

    return(NULL);
}
