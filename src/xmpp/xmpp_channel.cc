/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_channel.h"

using std::string;

namespace xmps {

string PeerIdToName(PeerId id) {
    switch (id) {
    case CONFIG:
        return "IFMap";
        break;
    case BGP:
        return "BGP";
        break;
    case DNS:
        return "DNS";
        break;
    case OTHER:
    default:
        return "OTHER";
        break;
    }
    return "OTHER";
}

}
