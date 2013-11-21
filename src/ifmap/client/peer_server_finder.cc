/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "peer_server_finder.h"
#include "ifmap_manager.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "discovery_client.h"

#include "base/task_annotations.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_syslog_types.h"
#include "ifmap/ifmap_log.h"

PeerIFMapServerFinder::PeerIFMapServerFinder(IFMapManager *ifmap_manager,
                DiscoveryServiceClient *client, const std::string &url) :
        ifmap_manager_(ifmap_manager), ds_client_(client), url_(url),
        init_done_(false), peer_ifmap_ds_resp_count_(0), last_response_at_(0),
        using_non_ds_peer_count_(0), no_best_peer_count_(0) {

    if (ValidStaticConfig()) {
        RetrieveStaticHostPort(url);
        ifmap_manager_->Start(static_peer_.host, static_peer_.port);
        current_peer_ = static_peer_;
        init_done_ = true;
    } else if (DSExists()) {
        service_name_ = 
            (g_vns_constants.ModuleNames.find(Module::IFMAP_SERVER))->second;
        PeerIFMapDSSubscribe();
    }
}

// Used for testing purposes only.
PeerIFMapServerFinder::PeerIFMapServerFinder(IFMapManager *ifmap_manager,
                                             const std::string &url) :
        ifmap_manager_(ifmap_manager), ds_client_(NULL), url_(url),
        init_done_(false), peer_ifmap_ds_resp_count_(0), last_response_at_(0),
        using_non_ds_peer_count_(0), no_best_peer_count_(0) {

    if (ValidStaticConfig()) {
        RetrieveStaticHostPort(url);
        ifmap_manager_->Start(static_peer_.host, static_peer_.port);
        current_peer_ = static_peer_;
        init_done_ = true;
    }
}

void PeerIFMapServerFinder::PeerIFMapDSSubscribe() {

    if (DSExists()) {
        uint8_t num_instances = 2;
        ds_client_->Subscribe(service_name_, num_instances,
            boost::bind(&PeerIFMapServerFinder::ProcPeerIFMapDSResp, this, _1));
    }
}

// 'index' gives the next available peer only if return value is true
bool PeerIFMapServerFinder::DSPeerAvailable(size_t *index) {
    bool exists = false;
    if (peer_ifmap_servers_.empty()) {
        exists = false;
    } else {
        size_t sz = peer_ifmap_servers_.size();
        size_t i;
        // Find the location of the current peer
        for (i = 0; i < sz; ++i) {
            if (peer_ifmap_servers_[i].in_use) {
                peer_ifmap_servers_[i].in_use = false;
                break;
            }
        }
        // If none of the peer's are in use, return first
        // else return the next (next of last is first) peer in the list
        size_t next = (i == sz) ? 0 : ((i == (sz - 1)) ?  0 : (i + 1));

        *index = next;
        peer_ifmap_servers_[next].in_use = true;
        exists = true;
    }
    return exists;
}

// srv_info has the available peer to be used only if return value is true
// Policy:
// 1. static config gets first preference.
// 2. if static config does not exist and DS is configured, use the DS-db to
// get the appropriate peer
bool PeerIFMapServerFinder::GetBestPeer(PeerIFMapServerInfo *srv_info) {
    bool is_valid = false;

    if (ValidStaticConfig()) {
        *srv_info = static_peer_;
        is_valid = true;
    } else if (DSExists()) {
        size_t index;
        if (DSPeerAvailable(&index)) {
            *srv_info = peer_ifmap_servers_[index];
            is_valid = true;
        }
    }

    if (is_valid) {
        current_peer_ = *srv_info;
    } else {
        incr_no_best_peer_count();
    }
    return is_valid;
}

void PeerIFMapServerFinder::UpdatePeerIFMapServers(
                                        std::vector<DSResponse> &response) {
    CHECK_CONCURRENCY("http client");
    std::string response_str;
    peer_ifmap_servers_.clear();
    for (DsRespIterator iter = response.begin(); iter != response.end();
         ++iter) {
        DSResponse resp = *iter;

        std::string host = resp.ep.address().to_string();
        std::string port = boost::lexical_cast<std::string>(resp.ep.port());
        PeerIFMapServerInfo info(host, port, false);
        peer_ifmap_servers_.push_back(info);

        response_str += host + ":" + port + ", ";
    }
    IFMAP_DEBUG(IFMapDSResp, "current_peer is " + current_peer_.ToString(), 
                "DSResp is " + response_str);
}

// Runs in the context of "http client" task
void PeerIFMapServerFinder::ProcPeerIFMapDSResp(
                                        std::vector<DSResponse> response) {
    CHECK_CONCURRENCY("http client");
    tbb::mutex::scoped_lock lock(ds_mutex_);
    incr_peer_ifmap_ds_resp_count();
    set_last_response_at_to_now();

    PeerIFMapServerInfo old_peer = current_peer_;
    UpdatePeerIFMapServers(response);

    size_t index;
    if (PeerExistsInDb(old_peer, &index)) {
        // If the current peer is still a valid peer, continue using it
        peer_ifmap_servers_[index].in_use = true;
    } else {
        // If the current peer is not a valid peer anymore, get the 'first'
        // available peer and trigger a new connection to this new peer.
        PeerIFMapServerInfo srv_info;
        bool valid = GetBestPeer(&srv_info);
        IFMAP_DEBUG(IFMapBestPeer, "DSResp", srv_info.ToString(), valid);
        if (valid) {
            if (!old_peer.IsSamePeer(srv_info)) {
                if (init_done_) {
                    ifmap_manager_->ResetConnection(srv_info.host,
                                                    srv_info.port);
                } else {
                    ifmap_manager_->Start(srv_info.host, srv_info.port);
                    init_done_ = true;
                }
            }
        } else {
            // Since we dont have any usable peers, continue using the current
            // one although its not in the 
            incr_using_non_ds_peer_count();
        }
    }
}

// 'index' gives the available peer to be used only if return value is true
// Caller must take ds_mutex_
bool PeerIFMapServerFinder::PeerExistsInDb(const PeerIFMapServerInfo &current,
                                           size_t *index) {
    bool exists = false;
    for (size_t i = 0; i < peer_ifmap_servers_.size(); ++i) {
        if (current.IsSamePeer(peer_ifmap_servers_[i])) {
            exists = true;
            *index = i;
            break;
        }
    }
    return exists;
}

bool PeerIFMapServerFinder::HostPortInDSDb(const std::string &host,
                                           unsigned short port) {
    PeerIFMapServerInfo srv_info(host, boost::lexical_cast<std::string>(port));
    size_t index;
    tbb::mutex::scoped_lock lock(ds_mutex_);
    return PeerExistsInDb(srv_info, &index);
}

bool PeerIFMapServerFinder::PeerDown() {
    PeerIFMapServerInfo srv_info;
    tbb::mutex::scoped_lock lock(ds_mutex_);
    // Get the 'next' best available peer
    bool is_valid = GetBestPeer(&srv_info);
    IFMAP_DEBUG(IFMapBestPeer, "PeerDown", srv_info.ToString(), is_valid);
    if (is_valid) {
        ifmap_manager_->ResetConnection(srv_info.host, srv_info.port);
    }
    return is_valid;
}

// url must be of the format https://host:port
// Assuming this has been verified while reading command line params 
void PeerIFMapServerFinder::RetrieveStaticHostPort(const std::string& url) {
    std::string res1("https://");

    // find the second : in the string
    size_t pos2 = url.find(":", res1.length());

    static_peer_.host = url.substr(res1.length(), pos2 - res1.length());
    // get port starting with the byte after the second colon until the end.
    static_peer_.port = url.substr(pos2 + 1);
    // setting in_use field is not needed

    IFMAP_DEBUG(IFMapUrlInfo, "IFMap server host is", static_peer_.host,
                "and port is", static_peer_.port);
}

void PeerIFMapServerFinder::GetAllDSPeerInfo(IFMapDSPeerInfo *ds_peer_info) {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    std::vector<IFMapDSPeerInfoEntry> info_list;

    tbb::mutex::scoped_lock lock(ds_mutex_);
    size_t sz = peer_ifmap_servers_.size();
    info_list.reserve(sz);
    IFMapDSPeerInfoEntry entry;
    for (size_t i = 0; i < sz; ++i) {
        entry.host = peer_ifmap_servers_[i].host;
        entry.port = peer_ifmap_servers_[i].port;
        entry.in_use = peer_ifmap_servers_[i].in_use;
        info_list.push_back(entry);
    }
    ds_peer_info->set_num_peers(sz);
    ds_peer_info->set_ds_peer_list(info_list);
    ds_peer_info->set_service_name(get_service_name());
    ds_peer_info->set_static_peer(get_static_peer());
    std::string mode;
    if (ValidStaticConfig()) {
        mode = " (Static)";
    } else if (DSExists()) {
        mode = " (DS)";
    } else {
        mode = " (None)";
    }
    std::string current_peer = get_current_peer() + mode;
    ds_peer_info->set_current_peer(current_peer);
    ds_peer_info->set_ds_response_count(get_peer_ifmap_ds_resp_count());
    ds_peer_info->set_last_response_ago(last_response_at_str());
    ds_peer_info->set_using_non_ds_peer_count(get_using_non_ds_peer_count());
    ds_peer_info->set_no_best_peer_count(get_no_best_peer_count());
}

