/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_STREAM_H__
#define __XMPP_STREAM_H__

class XmppStream {

public:
    struct XmppStrMessage {
        enum XmppStreamMsgType {
            NONE = 0,
            INIT_STREAM_HEADER = 1,
            INIT_STREAM_HEADER_RESP = 2,
            FEATURE_SASL = 3,
            FEATURE_TLS = 4,
            FEAUTRE_COMPRESS_LZW = 5,
            CLOSE_STREAM = 6
        };

        enum XmppStreamSASL {

        };

        XmppStreamMsgType type;
        
    };
};

