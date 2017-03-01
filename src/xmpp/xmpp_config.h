/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CONFIG_H__
#define __XMPP_CONFIG_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include "base/util.h"

// Internal representation of Peer configuration
class XmppChannelConfig {
public:
    explicit XmppChannelConfig(bool isClient = false);

    std::string ToAddr;
    std::string FromAddr;
    std::string NodeAddr; // pubsub node
    std::string name;
    boost::asio::ip::tcp::endpoint endpoint;
    boost::asio::ip::tcp::endpoint local_endpoint;
    bool logUVE;
    bool auth_enabled;
    std::string path_to_server_cert;
    std::string path_to_server_priv_key;
    std::string path_to_ca_cert;
    int tcp_hold_time;
    bool gr_helper_disable;
    uint8_t dscp_value;

    int CompareTo(const XmppChannelConfig &rhs) const;
    static int const default_client_port = 5269;
    static int const default_server_port = 5222;
    static int const kTcpHoldTime = 30; //in secs

    bool ClientOnly() const { return isClient_; }

private:
    bool isClient_;
};

class XmppConfigData {
public:
    typedef boost::ptr_map<boost::asio::ip::tcp::endpoint, XmppChannelConfig>
        XmppChannelConfigMap;

    XmppConfigData() {
    }

    void AddXmppChannelConfig(XmppChannelConfig *channel) {
        neighbors_.insert(channel->endpoint, channel);
    }

    XmppChannelConfig &GetXmppChannelConfig(const boost::asio::ip::tcp::endpoint ep) {
        return neighbors_[ep];
    }

    const XmppChannelConfigMap &neighbors() const { return neighbors_; }

    void Clear() {
        neighbors_.clear();
    }

private:
    XmppChannelConfigMap neighbors_;
    DISALLOW_COPY_AND_ASSIGN(XmppConfigData);
};

class XmppConfigManager {
public:
    enum DiffType {
        DF_NONE,
        DF_ADD,
        DF_CHANGE,
        DF_DELETE
    };

    XmppConfigManager();

    typedef boost::function<void(DiffType, const XmppChannelConfig *,
                                 const XmppChannelConfig *)> PeerDiffObserver;

    // stage 1: build the future internal representation
    bool ParseConfig(const std::string &config, XmppConfigData *);

    // stage 2: generate a delta
    void PeerConfigDiff(PeerDiffObserver obs) const;

    // stage 3: commit
    void AcceptFuture();

    // testing API -- begin --
    void SetFuture(const XmppConfigData *future);
    // testing API -- end --

    void Terminate();
private:
    std::auto_ptr<const XmppConfigData> current_;
    std::auto_ptr<const XmppConfigData> future_;
    DISALLOW_COPY_AND_ASSIGN(XmppConfigManager);
};

#endif
