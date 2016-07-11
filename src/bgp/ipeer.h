/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_IPEER_H_
#define SRC_BGP_IPEER_H_

#include "bgp/bgp_proto.h"
#include "net/address.h"
#include "tbb/atomic.h"

class DBTablePartBase;
class BgpPath;
class BgpRoute;
class BgpServer;
class BgpTable;
class IPeer;
class PeerCloseManager;

class IPeerUpdate {
public:
    virtual ~IPeerUpdate() { }
    // Printable name
    virtual std::string ToString() const = 0;

    // Send an update.
    // Returns true if the peer can send additional messages.
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) = 0;

    // Flush any accumulated updates.
    // Returns true if the peer can send additional messages.
    virtual bool FlushUpdate() { return true; }
};

class IPeerDebugStats {
public:
    struct ProtoStats {
        ProtoStats() {
            total = 0;
            open = 0;
            keepalive = 0;
            notification = 0;
            update = 0;
            close = 0;
        }
        tbb::atomic<uint64_t> total;
        tbb::atomic<uint64_t> open;
        tbb::atomic<uint64_t> keepalive;
        tbb::atomic<uint64_t> notification;
        tbb::atomic<uint64_t> update;
        tbb::atomic<uint64_t> close;
    };

    struct ErrorStats {
        ErrorStats() {
            connect_error = 0;
            connect_timer = 0;
            hold_timer = 0;
            open_error = 0;
            update_error = 0;
        }
        tbb::atomic<uint64_t> connect_error;
        tbb::atomic<uint64_t> connect_timer;
        tbb::atomic<uint64_t> hold_timer;
        tbb::atomic<uint64_t> open_error;
        tbb::atomic<uint64_t> update_error;
    };

    struct RxErrorStats {
        RxErrorStats() {
            inet6_bad_xml_token_count = 0;
            inet6_bad_prefix_count = 0;
            inet6_bad_nexthop_count = 0;
            inet6_bad_afi_safi_count = 0;
        }
        tbb::atomic<uint64_t> inet6_bad_xml_token_count;
        tbb::atomic<uint64_t> inet6_bad_prefix_count;
        tbb::atomic<uint64_t> inet6_bad_nexthop_count;
        tbb::atomic<uint64_t> inet6_bad_afi_safi_count;
    };

    struct RxRouteStats {
        RxRouteStats() {
            total_path_count = 0;
            primary_path_count = 0;
        }
        tbb::atomic<uint64_t> total_path_count;
        tbb::atomic<uint64_t> primary_path_count;
    };

    struct UpdateStats {
        UpdateStats() {
            end_of_rib = 0;
            total = 0;
            reach = 0;
            unreach = 0;
        }
        tbb::atomic<uint64_t> end_of_rib;
        tbb::atomic<uint64_t> total;
        tbb::atomic<uint64_t> reach;
        tbb::atomic<uint64_t> unreach;
    };

    struct SocketStats {
        SocketStats() {
            calls = 0;
            bytes = 0;
            blocked_count = 0;
            blocked_duration_usecs = 0;
        }
        tbb::atomic<uint64_t> calls;
        tbb::atomic<uint64_t> bytes;
        tbb::atomic<uint64_t> blocked_count;
        tbb::atomic<uint64_t> blocked_duration_usecs;
    };

    virtual ~IPeerDebugStats() { }

    // Reset all counters
    virtual void Clear() = 0;
    // Printable name
    virtual std::string ToString() const = 0;
    // Previous State of the peer
    virtual std::string last_state() const = 0;
    // Last state change At
    virtual std::string last_state_change_at() const = 0;
    // Last error on this peer
    virtual std::string last_error() const = 0;
    // Last Event on this peer
    virtual std::string last_event() const = 0;
    // When was the Last
    virtual std::string last_flap() const = 0;
    // Total number of flaps
    virtual uint64_t num_flaps() const = 0;

    virtual void GetRxProtoStats(ProtoStats *stats) const = 0;
    virtual void GetRxRouteUpdateStats(UpdateStats *stats) const = 0;
    virtual void GetRxSocketStats(SocketStats *stats) const = 0;
    virtual void GetRxErrorStats(RxErrorStats *stats) const = 0;
    virtual void GetRxRouteStats(RxRouteStats *stats) const = 0;

    virtual void GetTxProtoStats(ProtoStats *stats) const = 0;
    virtual void GetTxRouteUpdateStats(UpdateStats *stats) const = 0;
    virtual void GetTxSocketStats(SocketStats *stats) const = 0;

    virtual void UpdateTxReachRoute(uint64_t count) = 0;
    virtual void UpdateTxUnreachRoute(uint64_t count) = 0;
};

class IPeerClose {
public:
    typedef std::set<Address::Family> Families;

    virtual ~IPeerClose() { }
    // Printable name
    virtual std::string ToString() const = 0;
    virtual PeerCloseManager *close_manager() = 0;
    virtual void Close(bool non_graceful) = 0;
    virtual bool IsCloseGraceful() const = 0;
    virtual bool IsCloseLongLivedGraceful() const = 0;
    virtual void CustomClose() = 0;
    virtual void CloseComplete() = 0;
    virtual void Delete() = 0;
    virtual void GracefulRestartStale() = 0;
    virtual void LongLivedGracefulRestartStale() = 0;
    virtual void GracefulRestartSweep() = 0;
    virtual void GetGracefulRestartFamilies(Families *) const = 0;
    virtual const int GetGracefulRestartTime() const = 0;
    virtual const int GetLongLivedGracefulRestartTime() const = 0;
    virtual bool IsReady() const = 0;
    virtual IPeer *peer() const = 0;
    virtual void ReceiveEndOfRIB(Address::Family family) = 0;
    virtual void MembershipRequestCallbackComplete() = 0;
    virtual const char *GetTaskName() const = 0;
    virtual int GetTaskInstance() const = 0;
};

class IPeer : public IPeerUpdate {
public:
    virtual ~IPeer() { }
    // Printable name
    virtual std::string ToString() const = 0;
    virtual std::string ToUVEKey() const = 0;
    virtual BgpServer *server() = 0;
    virtual BgpServer *server() const = 0;
    virtual IPeerClose *peer_close() = 0;
    virtual IPeerDebugStats *peer_stats() = 0;
    virtual const IPeerDebugStats *peer_stats() const = 0;
    virtual bool IsReady() const = 0;
    virtual bool IsXmppPeer() const = 0;
    //
    // Whether the peer must register to a table
    // before it could push the route to VRF.
    // Control is mostly required in MockPeer in unit tests.
    //
    virtual bool IsRegistrationRequired() const = 0;
    virtual void Close(bool non_graceful) = 0;
    virtual BgpProto::BgpPeerType PeerType() const = 0;
    virtual uint32_t bgp_identifier() const = 0;
    virtual const std::string GetStateName() const = 0;
    virtual void UpdateTotalPathCount(int count) const = 0;
    virtual int GetTotalPathCount() const = 0;
    virtual void UpdatePrimaryPathCount(int count) const = 0;
    virtual int GetPrimaryPathCount() const = 0;
    virtual void MembershipRequestCallback(BgpTable *table) = 0;
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) = 0;
    virtual bool CanUseMembershipManager() const = 0;
};

#endif  // SRC_BGP_IPEER_H_
