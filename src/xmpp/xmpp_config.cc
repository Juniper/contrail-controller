/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_config.h"

#include "base/logging.h"
#include <boost/algorithm/string.hpp>

using namespace boost::asio::ip;
using namespace boost::property_tree;
using namespace std;

XmppChannelConfig::XmppChannelConfig(bool isClient) :
     ToAddr(""), FromAddr(""), NodeAddr(""), logUVE(false), auth_enabled(false),
     path_to_server_cert(""), path_to_server_priv_key(""), path_to_ca_cert(""),
     tcp_hold_time(XmppChannelConfig::kTcpHoldTime), gr_helper_disable(false),
     dscp_value(0), isClient_(isClient)  {
}

int XmppChannelConfig::CompareTo(const XmppChannelConfig &rhs) const {
    int res;

    if (endpoint < rhs.endpoint) return -1;
    if (endpoint > rhs.endpoint) return 1;
    res = name.compare(rhs.name);
    if (res) return res;

    return 0;
}

void XmppConfigManager::PeerConfigDiff(PeerDiffObserver obs) const {
    const XmppConfigData::XmppChannelConfigMap &n_current = 
        current_->neighbors();
    if (future_.get() == NULL) {
        return;
    }
    const XmppConfigData::XmppChannelConfigMap &n_future = future_->neighbors();

    XmppConfigData::XmppChannelConfigMap::const_iterator icurr = 
        n_current.begin();
    XmppConfigData::XmppChannelConfigMap::const_iterator inext = 
        n_future.begin();

    while (icurr != n_current.end() && inext != n_future.end()) {
        if (icurr->first < inext->first) {
            obs(DF_DELETE, icurr->second, NULL);
            ++icurr;
        } else if (icurr->first > inext->first) {
            obs(DF_ADD, NULL, inext->second);
            ++inext;
        } else {
            int cmp = icurr->second->CompareTo(*inext->second);
            if (cmp != 0) {
                obs(DF_CHANGE, icurr->second, inext->second);
            }
            ++icurr;
            ++inext;
        }
    }
    for (; icurr != n_current.end(); ++icurr) {
        obs(DF_DELETE, icurr->second, NULL);
    }
    for (; inext != n_future.end(); ++inext) {
        obs(DF_ADD, NULL, inext->second);
    }
}

void XmppConfigManager::SetFuture(const XmppConfigData *future) {
    future_.reset(future);
}

void XmppConfigManager::AcceptFuture() {
    current_.reset(future_.release());
}

void XmppConfigManager::Terminate() {
    current_.reset();
    future_.reset();
}

XmppConfigManager::XmppConfigManager() : current_(new XmppConfigData()) {
}
