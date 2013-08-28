/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_STANZA_H__
#define __XMPP_STANZA_H__

#include "xmpp/xmpp_str.h"
#include "xml/xml_base.h"

class XmppStanza  {
public:
    enum XmppMessageType {
        INVALID = 0,
        STREAM_HEADER = 1,
        MESSAGE_STANZA = 2,
        IQ_STANZA = 3,
        WHITESPACE_MESSAGE_STANZA = 4,
        RESERVED_STANZA = 5
    };

    enum XmppStanzaErrorType {
        NONE = 0,
        BAD_REQUEST = 1,
        CONFLICT = 2,
        FEATURE_NOT_IMPLEMENTED = 3,
        FORBIDDEN = 4,
        GONE = 5,
        INTERNAL_SERVER_ERROR = 6,
        ITEMS_NOT_FOUND = 7,
        JID_MALFORMED = 8,
        NOT_ACCEPTABLE = 9,
        NOT_ALLOWED = 10,
        NOT_AUTHORIZED = 11,
        POLICY_VIOLATION = 12,
        RECIPIENT_UNAVAILABLE = 13,
        REDIRECT = 14,
        REGISTRATION_REQUIRED = 15,
        REMOTE_SERVER_NOT_FOUND = 16,
        REMOTE_SERVER_TIMEOUT = 17,
        RESOURCE_CONSTRAINT = 18,
        SERVICE_UNAVAILABLE = 19,
        SUBSCRIPTION_REQUIRED = 20,
        UNDEFINED_CONDITION = 21,
        UNEXPECTED_REQUEST = 22,
        APPLICATION_SEPCIFIC_CONDITION = 23
    };

    class XmppMessage {
    public:
        explicit XmppMessage(XmppMessageType type) : 
            type(type), from(""), to("") {
        }
        virtual ~XmppMessage(){ }
        XmppMessageType type;
        XmppStanzaErrorType error;
        std::string from;
        std::string to;
        std::auto_ptr<XmlBase> dom;

        bool IsValidType(XmppMessageType type) const {
            return (type > INVALID && type < RESERVED_STANZA);
        };
    };

    struct XmppStreamMessage : XmppMessage {
        XmppStreamMessage() : XmppMessage(STREAM_HEADER) {
        }

        enum XmppStreamMsgType {
            STREAM_NONE = 0,
            INIT_STREAM_HEADER = 1,
            INIT_STREAM_HEADER_RESP = 2,
            FEATURE_SASL = 3,
            FEATURE_TLS = 4,
            FEAUTRE_COMPRESS_LZW = 5,
            CLOSE_STREAM = 6 
        };

        enum XmppStreamSASL {
        };

        XmppStreamMsgType strmtype;
    };

    enum XmppMessageStateType {
        STATE_NONE = 0, STATE_ACTIVE = 1, STATE_COMPOSING = 2, 
        STATE_PAUSED = 3, STATE_INACTIVE = 4, STATE_GONE = 5
    };


    struct XmppChatMessage : public XmppMessage {
        explicit XmppChatMessage(XmppMessageStateType stype): 
            XmppMessage(MESSAGE_STANZA), state(stype) {
        };
        enum XmppMessageSubtype {
            NORMAL = 1,
            CHAT = 2,
            GROUPCHAT = 3,
            HEADLINE = 4, 
            ERROR = 5
        };

        XmppMessageStateType state;
        XmppMessageSubtype stype;
    };

    struct XmppMessagePresence : public XmppMessage {
        enum XmppPresenceSubtype {
            SUBSCRIBED = 1,
            PROBE = 2,
            UNAVAILABLE = 3
        };

        enum XmppPresenceShowType {
            CHAT = 1, AWAY = 2, XA = 3, DND = 4
        };

        XmppPresenceSubtype stype;
        XmppPresenceShowType show;
        std::string timestamp;
        std::string statusDescr;
        int16_t priority;
    };

    struct XmppMessageIq : public XmppMessage {
        XmppMessageIq() : XmppMessage(IQ_STANZA) {
        }
        enum XmppIqSubtype {
            GET = 1,
            SET = 2,
            RESULT = 3,
            ERROR = 4
        };

        enum XmppIqErrorType {
            AUTH = 1, CANCEL = 2, CONTINUE = 3, MODIFY = 4, WAIT = 5
        };

        XmppIqSubtype stype;
        std::string node;
        std::string id;
        std::string iq_type;
        std::string action;
        std::string as_node;
        bool is_as_node;
    };

    XmppStanza();

    static XmlBase *AllocXmppXmlImpl(const char *doc = NULL) {
        XmlBase *tmp = XmppXmlImplFactory::Instance()->GetXmlImpl();
        BOOST_ASSERT(tmp);
        if (doc) tmp->LoadDoc(doc);
        return tmp;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(XmppStanza);
};

class XmppProto : public XmppStanza {
public:

    static XmppStanza::XmppMessage *Decode(const std::string &ts);
    static int EncodeStream(const XmppStreamMessage &str, std::string &to, 
                            std::string &from, uint8_t *data, size_t size);
    static int EncodeStream(const XmppMessage &str, uint8_t *data, size_t size);
    static int EncodeMessage(const XmppChatMessage *, uint8_t *data, size_t size);
    static int EncodeMessage(XmlBase *, uint8_t *data, size_t size);
    static int EncodePresence(uint8_t *data, size_t size);
    static int EncodeIq(const XmppMessageIq *iq, XmlBase *doc, 
                        uint8_t *data, size_t size);

private:
    static int EncodeOpen(uint8_t *data, std::string &to, std::string &from);
    static int EncodeOpenResp(uint8_t *data, std::string &to, std::string &from);
    static int EncodeWhitespace(uint8_t *data);
    static int SetTo(std::string &to, XmlBase *doc);
    static int SetFrom(std::string &from, XmlBase *doc);

    static const char *GetId(XmlBase *doc);
    static const char *GetType(XmlBase *doc);
    static const char *GetTo(XmlBase *doc);
    static const char *GetFrom(XmlBase *doc);
    static const char *GetAction(XmlBase *doc, const std::string &str);
    static const char *GetNode(XmlBase *doc, const std::string &str);
    static const char *GetAsNode(XmlBase *doc);
    static const char *GetDsNode(XmlBase *doc);

    static XmppStanza::XmppMessage *DecodeInternal(const std::string &ts,
                                                   XmlBase *impl); 

    static std::auto_ptr<XmlBase> open_doc_;

    XmppProto();
    ~XmppProto();

    DISALLOW_COPY_AND_ASSIGN(XmppProto);
};

#endif // __XMPP_STANZA_H__
