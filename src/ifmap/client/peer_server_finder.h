/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_peer_server_finder_h
#define ctrlplane_peer_server_finder_h

#include <string>
#include <vector>
#include <tbb/mutex.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include "base/util.h"

class DiscoveryServiceClient;
class IFMapDSPeerInfo;
class IFMapManager;

struct DSResponse;

struct PeerIFMapServerInfo {
public:
    PeerIFMapServerInfo() : in_use(false) {
    }
    PeerIFMapServerInfo(std::string ihost, std::string iport,
                        bool iuse = false) :
        host(ihost), port(iport), in_use(iuse) {
    }
    bool IsSamePeer(const PeerIFMapServerInfo &peer) const {
        if ((host.compare(peer.host) == 0) && (port.compare(peer.port) == 0)) {
            return true;
        } else {
            return false;
        }
    }
    std::string ToString() {
        return in_use ? (host + ":" + port + ":in_use") :
                        (host + ":" + port + ":not in_use");
    }
    std::string host;
    std::string port;
    bool in_use;
};

class PeerIFMapServerFinder {
public:
    typedef std::vector<DSResponse>::iterator DsRespIterator;
    typedef std::vector<PeerIFMapServerInfo>::iterator PeerIterator;

    PeerIFMapServerFinder(IFMapManager *ifmap_manager,
                          DiscoveryServiceClient *client,
                          const std::string &url);
    PeerIFMapServerFinder(IFMapManager *ifmap_manager, const std::string &url);
    virtual ~PeerIFMapServerFinder() { }

    bool PeerDown();
    void GetAllDSPeerInfo(IFMapDSPeerInfo *ds_peer_info);
    const std::string &get_url() { return url_; }
    bool get_init_done() { return init_done_; }
    uint64_t get_peer_ifmap_ds_resp_count() {
        return peer_ifmap_ds_resp_count_;
    }
    uint64_t get_using_non_ds_peer_count() { return using_non_ds_peer_count_; }
    uint64_t get_no_best_peer_count() { return no_best_peer_count_; }
    const std::string &get_service_name() { return service_name_; }
    std::string get_static_peer() {
        if (static_peer_.host.size()) {
            return static_peer_.host + ":" + static_peer_.port;
        } else {
            return std::string("");
        }
    }
    std::string get_current_peer() {
        return current_peer_.host + ":" + current_peer_.port;
    }
    void ProcPeerIFMapDSResp(std::vector<DSResponse> response);
    size_t get_peer_ifmap_servers_count() { return peer_ifmap_servers_.size(); }
    bool HostPortInDSDb(const std::string &host, unsigned short port);

protected:
    virtual bool DSExists() { return ((ds_client_ == NULL) ? false : true); }

private:
    // url_ is set only when the host/port are statically configured
    bool IsConfigStatic() { return !url_.empty(); }
    void PeerIFMapDSSubscribe();
    void UpdatePeerIFMapServers(std::vector<DSResponse> &response);
    bool ValidStaticConfig() { return !url_.empty(); }
    bool DSPeerExists();
    void RetrieveStaticHostPort(const std::string& url);
    bool GetBestPeer(PeerIFMapServerInfo *srv_info);
    bool DSPeerAvailable(size_t *index);
    bool PeerExistsInDb(const PeerIFMapServerInfo &current, size_t *index);
    void incr_peer_ifmap_ds_resp_count() { ++peer_ifmap_ds_resp_count_; }
    void incr_using_non_ds_peer_count() { ++using_non_ds_peer_count_; }
    void incr_no_best_peer_count() { ++no_best_peer_count_; }
    void set_last_response_at_to_now() {
        last_response_at_ = UTCTimestampUsec();
    }
    std::string last_response_at_str() {
        return last_response_at_ ?
            (duration_usecs_to_string(UTCTimestampUsec() - last_response_at_)) :
            boost::lexical_cast<std::string>(0);
    }

    IFMapManager *ifmap_manager_;
    DiscoveryServiceClient *ds_client_;
    std::string url_;
    std::string service_name_;
    PeerIFMapServerInfo static_peer_;
    PeerIFMapServerInfo current_peer_;
    std::vector<PeerIFMapServerInfo> peer_ifmap_servers_;
    tbb::mutex ds_mutex_; // protect peer_ifmap_servers_
    bool init_done_;
    uint64_t peer_ifmap_ds_resp_count_;
    uint64_t last_response_at_;
    uint64_t using_non_ds_peer_count_;
    uint64_t no_best_peer_count_;
};

#endif // ctrlplane_peer_server_finder_h
