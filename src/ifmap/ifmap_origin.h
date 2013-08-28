/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_origin_h
#define ctrlplane_ifmap_origin_h

struct IFMapOrigin {
    enum Origin {
        UNKNOWN,
        LOCAL,
        MAP_SERVER,
        XMPP,
    };
    IFMapOrigin() : origin(UNKNOWN) { }
    IFMapOrigin(Origin in_origin) : origin(in_origin) { }
    bool operator==(const IFMapOrigin &rhs) const {
        return origin == rhs.origin;
    }
    void set_origin(Origin in_origin) { origin = in_origin; }
    std::string ToString() {
        if (origin == UNKNOWN) {
            return "Unknown";
        } else if (origin == LOCAL) {
            return "Local";
        } else if (origin == MAP_SERVER) {
            return "MapServer";
        } else if (origin == XMPP) {
            return "Xmpp";
        } else {
            return "NotSet";
        }
    }
    bool IsOriginXmpp() {
        return ((origin == XMPP) ? true : false);
    }

    Origin origin;
};

#endif
