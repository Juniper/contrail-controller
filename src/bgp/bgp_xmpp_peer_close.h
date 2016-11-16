/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_XMPP_PEER_CLOSE_H_
#define SRC_BGP_BGP_XMPP_PEER_CLOSE_H_

#include <set>
#include <string>

#include "bgp/ipeer.h"

class BgpPath;
class BgpXmppChannel;
class PeerCloseManager;

class BgpXmppPeerClose : public IPeerClose {
public:
    typedef std::set<Address::Family> Families;

    explicit BgpXmppPeerClose(BgpXmppChannel *channel);
    virtual ~BgpXmppPeerClose();
    virtual bool IsReady() const;
    virtual IPeer *peer() const;
    virtual int GetGracefulRestartTime() const;
    virtual int GetLongLivedGracefulRestartTime() const;
    virtual void GracefulRestartStale();
    virtual void LongLivedGracefulRestartStale();
    virtual void GracefulRestartSweep();
    virtual bool IsCloseGraceful() const;
    virtual bool IsCloseLongLivedGraceful() const;
    virtual void GetGracefulRestartFamilies(Families *families) const;
    virtual void ReceiveEndOfRIB(Address::Family family);
    virtual void MembershipRequestCallbackComplete();
    virtual const char *GetTaskName() const;
    virtual int GetTaskInstance() const;
    virtual void CustomClose();
    virtual void CloseComplete();
    virtual void Delete();
    virtual void Close(bool graceful);
    virtual PeerCloseManager *GetManager() const;
    virtual void UpdateRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const;

private:
    BgpXmppChannel *channel_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppPeerClose);
};

#endif  // SRC_BGP_BGP_XMPP_PEER_CLOSE_H_
