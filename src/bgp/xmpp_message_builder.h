/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_XMPP_MESSAGE_BUILDER_H_
#define SRC_BGP_XMPP_MESSAGE_BUILDER_H_

#include <pugixml/pugixml.hpp>

#include <string>
#include <vector>

#include "bgp/message_builder.h"
#include "bgp/bgp_ribout.h"
#include "bgp/extended-community/load_balance.h"

namespace autogen {
    class ItemType;
    class EnetItemType;
}
class Community;
class ExtCommunity;

class BgpXmppMessageBuilder : public MessageBuilder {
public:
    BgpXmppMessageBuilder();
    virtual Message *Create(int part_id, const RibOut *ribout,
                            bool cache_routes,
                            const RibOutAttr *roattr,
                            const BgpRoute *route) const;

private:
    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessageBuilder);
};

class BgpXmppMessage : public Message {
public:
    BgpXmppMessage(int part_id, const BgpTable *table, const RibOutAttr *roattr,
                   bool cache_routes);
    virtual ~BgpXmppMessage();

    static void Initialize();
    static void Terminate();

    void Start(const RibOutAttr *roattr, const BgpRoute *route);
    virtual void Finish();
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr);
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp);

private:
    static const size_t kMaxFromToLength = 192;
    static const uint32_t kMaxReachCount = 32;
    static const uint32_t kMaxUnreachCount = 256;

    class XmlWriter : public pugi::xml_writer {
    public:
        explicit XmlWriter(std::string *repr = NULL) : repr_(repr) { }
        virtual void write(const void *data, size_t size) {
            repr_->append(static_cast<const char*>(data), size);
        }

    private:
        std::string *repr_;
    };

    void EncodeNextHop(const BgpRoute *route,
                       const RibOutAttr::NextHop &nexthop,
                       autogen::ItemType *item);
    void AddIpReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddIpUnreach(const BgpRoute *route);
    bool AddInetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    bool AddInet6Route(const BgpRoute *route, const RibOutAttr *roattr);

    void EncodeEnetNextHop(const BgpRoute *route,
                           const RibOutAttr::NextHop &nexthop,
                           autogen::EnetItemType *item);
    void AddEnetReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddEnetUnreach(const BgpRoute *route);
    bool AddEnetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void AddMcastReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddMcastUnreach(const BgpRoute *route);
    bool AddMcastRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void ProcessCommunity(const Community *community);
    void ProcessExtCommunity(const ExtCommunity *ext_community);
    std::string GetVirtualNetwork(const RibOutAttr::NextHop &nexthop) const;
    std::string GetVirtualNetwork(const BgpRoute *route,
                                  const RibOutAttr *roattr) const;

    int part_id_;
    const BgpTable *table_;
    XmlWriter writer_;
    bool is_reachable_;
    bool cache_routes_;
    bool repr_valid_;
    uint32_t sequence_number_;
    std::vector<int> security_group_list_;
    std::vector<std::string> community_list_;
    LoadBalance::LoadBalanceAttribute load_balance_attribute_;
    static std::vector<std::string> repr_;
    static std::vector<pugi::xml_document *> doc_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessage);
};

#endif  // SRC_BGP_XMPP_MESSAGE_BUILDER_H_
