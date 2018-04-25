/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#ifndef __XMPP_STR_H__
#define __XMPP_STR_H__


// STREAM strings
//

#define sXMPP_STREAM_O              "stream:stream"
#define sXMPP_STREAM_FEATURES_O     "<stream:features>"
#define sXMPP_STREAM_FEATURES_C     "</stream:features>"
#define sXMPP_STREAM_ERROR_O        "<stream:error>"
#define sXMPP_STREAM_ERROR_C        "</stream:error>"
#define sXMPP_IQ_KEY                "iq"
#define sXMPP_IQ                    "<iq"
#define sXMPP_MESSAGE_KEY           "message"
#define sXMPP_MESSAGE               "<message"
#define sXMPP_STREAM_STARTTLS_O     "<starttls"
#define sXMPP_STREAM_FAILURE_O      "<failure"
#define sXMPP_STREAM_PROCEED_O      "<proceed"
#define sXMPP_REQUIRED_O            "<required"


#define sXMPP_VERSION_1_GLOBAL      "<?xml version='1.0'?>"
#define sXMPP_VERSION_1             "version='1.0'"
#define sXMPP_JABBER_CLIENT         "xmlns='jabber:client'"
#define sXMPP_JABBER_SERVER         "xmlns='jabber:server'"
#define sXMPP_LANG_EN               "xml:lang='en'"
#define sXMPP_STREAM_NS             "xmlns:stream='http://etherx.jabber.org/streams'"
#define sXMPP_STREAM_NS_TLS         "urn:ietf:params:xml:ns:xmpp-tls"
#define sXMPP_BIND_NS               "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/>"
#define sXMPP_STREAM_ERROR_NS       "xmlns='urn:ietf:params:xml:ns:xmpp-streams'"


//Encoded stream messages
#define sXMPP_STREAM_OPEN            "<?xml version='1.0'?><stream:stream from='dummycl' to='dummyserver' version='1.0' xml:lang='en' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'/>"
#define sXMPP_STREAM_RESP            "<?xml version='1.0'?><stream:stream from='dummyserver' to='dummycl' id='++123' version='1.0' xml:lang='en' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'/>"
#define sXMPP_STREAM_FEATURE_TLS    "<stream:features><starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'><required/></starttls></stream:features>"
#define sXMPP_STREAM_START_TLS       "<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>"
#define sXMPP_STREAM_PROCEED_TLS     "<proceed xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>"

#define sXMPP_WHITESPACE             "Ȁ" //unicode U+0200 as whitespace
// Whitespace characters allowed as fillers between xmpp messages.
#define sXMPP_VALIDWS                " \n\r\tȀ"

#define sXMPP_CHAT_MSG             "<message from='fake-from' to='fake-to' type='chat'> <body> msg </body> <subject> log </subject> </message>"
#define sXMPP_STREAM_START         "<?xml version=\"1.0\"?><stream:stream"
#define sXMPP_STREAM_START_S       "<stream:stream"
#define sXML_STREAM_C               "/stream:stream"

// Regexp
//#define rXMPP_MESSAGE              "<.*[\\s\\n\\t\\r]"
#define rXMPP_MESSAGE              "<(iq|message)"
#define rXMPP_STREAM_START         "<?.*?>*[\\s\\n\\t\\r]*<stream:stream"
#define rXMPP_STREAM_END           "http://etherx.jabber.org/streams[\"'][\\s\\t\\r\\n]*>"
#define rXMPP_STREAM_FEATURES      "<stream:features"
#define rXMPP_STREAM_STARTTLS      "<starttls"
#define rXMPP_STREAM_PROCEED       "<proceed"
#define rXMPP_STREAM_STANZA_END    "[\\s\\t\\r\\n]*/>"

#define rXMPP_STREAM_START_FEATURES "<?.*?>*[\\s\\n\\t\\r]*<(stream:stream|stream:features)"
#endif // __XMPP_STR_H__
