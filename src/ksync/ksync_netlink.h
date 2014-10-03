/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_entry_netlink_h
#define ctrlplane_ksync_entry_netlink_h

#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

class KSyncObject;

// Implementation of KSyncEntry with Netlink ASIO as backend to send message
// Use this class in cases where KSyncEntry state-machine should be controlled
// by application
//
// Replaces virtual functions Add with AddMsg, Change with ChangeMsg and Delete
// with DeleteMsg. Uses KSyncSock in backend to send/receives netlink message
// to kernel
class KSyncNetlinkEntry : public KSyncEntry {
public:
    KSyncNetlinkEntry() : KSyncEntry() { };
    KSyncNetlinkEntry(uint32_t index) : KSyncEntry(index) { };
    virtual ~KSyncNetlinkEntry() { };

    // Generate netlink add message for the object
    virtual int AddMsg(char *msg, int len) = 0;
    // Generate netlink change message for the object
    virtual int ChangeMsg(char *msgi, int len) = 0;
    // Generate netlink delete message for the object
    virtual int DeleteMsg(char *msg, int len) = 0;

    bool Add();
    bool Change();
    bool Delete();
    virtual bool Sync() = 0;
    virtual bool AllowDeleteStateComp() {return true;}
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncNetlinkEntry);
};

// Implementation of KSyncDBEntry with Netlink ASIO as backend to send message
// Use this class in cases where KSyncEntry state-machine should be managed
// by DBTable notification.
// Applications are not needed to generate any events to the state-machine
//
// Replaces virtual functions Add with AddMsg, Change with ChangeMsg and Delete
// with DeleteMsg. Uses KSyncSock in backend to send/receives netlink message
// to kernel
class KSyncNetlinkDBEntry : public KSyncDBEntry {
public:
    KSyncNetlinkDBEntry() : KSyncDBEntry() { };
    KSyncNetlinkDBEntry(uint32_t index) : KSyncDBEntry(index) { };
    virtual ~KSyncNetlinkDBEntry() { };

    // Generate netlink add message for the object
    virtual int AddMsg(char *msg, int len) = 0;
    // Generate netlink change message for the object
    virtual int ChangeMsg(char *msg, int len) = 0;
    // Generate netlink delete message for the object
    virtual int DeleteMsg(char *msg, int len) = 0;

    bool Add();
    bool Change();
    bool Delete();
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncNetlinkDBEntry);
};

#endif //ctrlplane_ksync_entry_netlink_h
