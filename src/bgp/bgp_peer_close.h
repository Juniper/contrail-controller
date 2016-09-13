/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_CLOSE_H__
#define SRC_BGP_BGP_PEER_CLOSE_H__

#include <set>
#include <string>
#include <vector>

#include "bgp/ipeer.h"

class BgpNeighborResp;
class BgpPeerInfoData;
class PeerCloseManager;

class BgpPeerClose : public IPeerClose {
  public:
    typedef std::set<Address::Family> Families;

    explicit BgpPeerClose(BgpPeer *peer);
    virtual ~BgpPeerClose();
    virtual std::string ToString() const;
    virtual void CustomClose();
    virtual void GracefulRestartStale();
    virtual void LongLivedGracefulRestartStale();
    virtual void GracefulRestartSweep();
    virtual bool IsReady() const;
    virtual IPeer *peer() const;
    virtual void Close(bool non_graceful);
    virtual void Delete();
    virtual int GetGracefulRestartTime() const;
    virtual int GetLongLivedGracefulRestartTime() const;
    virtual void ReceiveEndOfRIB(Address::Family family);
    virtual const char *GetTaskName() const;
    virtual int GetTaskInstance() const;
    virtual void MembershipRequestCallbackComplete();
    virtual bool IsCloseGraceful() const;
    virtual bool IsCloseLongLivedGraceful() const;
    virtual void CloseComplete();
    virtual void GetGracefulRestartFamilies(Families *families) const;
    virtual void SetManager(PeerCloseManager *manager);

    void AddGRCapabilities(BgpProto::OpenMessage::OptParam *opt_param);
    bool SetGRCapabilities(BgpPeerInfoData *peer_info);
    void FillNeighborInfo(BgpNeighborResp *bnr) const;

private:
    bool IsGRReady() const;

    BgpPeer *peer_;
    uint64_t flap_count_;
    PeerCloseManager *manager_;
    std::vector<std::string> negotiated_families_;
    std::vector<std::string> gr_families_;
    std::vector<std::string> llgr_families_;
    BgpProto::OpenMessage::Capability::GR gr_params_;
    BgpProto::OpenMessage::Capability::LLGR llgr_params_;

    DISALLOW_COPY_AND_ASSIGN(BgpPeerClose);
};

#endif  // SRC_BGP_BGP_PEER_CLOSE_H__
