/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __control_node_mock__
#define __control_node_mock__

#include <pugixml/pugixml.hpp>

namespace pugi {
class xml_document;
class xml_node;
}

class EventManager;

namespace test {

class ControlNodeMock {
public:

    typedef struct NHEntry_ {
        std::string nh;
        int label;
    } NHEntry;
 
    typedef struct RouteEntry_ {
        std::string address;
        std::string vn;
        std::vector<NHEntry> nh_list_;
    } RouteEntry;

    typedef struct VrfEntry_ {
        std::string name;
        bool subscribed;
        std::map<std::string, RouteEntry *> route_list_;
    } VrfEntry;

    ControlNodeMock(EventManager *evm, std::string address);
    ~ControlNodeMock();
    void AddRoute(std::string vrf, std::string address, std::string nh, int, std::string vn);
    void DeleteRoute(std::string vrf, std::string address, std::string nh, 
                     int label, std::string vn);
    void SubscribeVrf(const std::string &vrf);
    void UnSubscribeVrf(const std::string &vrf);
    int GetServerPort() { return server_port_; }
    bool IsEstablished();
    void Shutdown();
    void Clear();

private: 
    pugi::xml_node AddXmppHdr();
    void SendUpdate(xmps::PeerId id);
    void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    void XmppChannelEvent(XmppChannel *channel, xmps::PeerState state);
    RouteEntry* InsertRoute(std::string &vrf, std::string &address, 
            std::string &nh, int label, std::string &vn);
    RouteEntry* RemoveRoute(std::string &vrf, std::string &address, 
            std::string &nh, int label, std::string &vn, bool &send_delete);
    void GetRoutes(std::string vrf, const XmppStanza::XmppMessage *msg);
    void WriteReadyCb(const boost::system::error_code &ec);
    VrfEntry* AddVrf(const std::string &vrf);
    VrfEntry* GetVrf(const std::string &vrf);
    void SendRoute(std::string vrf, RouteEntry *rt, bool add);

    std::vector<VrfEntry *> vrf_list_;
    EventManager *evm_;
    std::string address_;
    int server_port_;
    XmppServer *xs;
    XmppChannel *channel_;
    pugi::xml_document xdoc_;
};

} //name space test

#endif /* #define __control_node_mock__ */

