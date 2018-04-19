/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#endif

#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <sandesh/sandesh_trace.h>

#include "ksync_index.h"
#include "ksync_entry.h"
#include "ksync_object.h"
#include "ksync_sock.h"
#include "ksync_types.h"
#include "ksync_netlink.h"

///////////////////////////////////////////////////////////////////////////////
// KSyncNetlinkEntry routines
///////////////////////////////////////////////////////////////////////////////
bool KSyncNetlinkEntry::Add() {
    Sync();
    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = AddMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::ADD_ACK);
    return false;
}

bool KSyncNetlinkEntry::Change() {
    if (Sync() == false) {
        return true;
    }

    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = ChangeMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::CHANGE_ACK);
    return false;
}

bool KSyncNetlinkEntry::Delete() {
    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = DeleteMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::DEL_ACK);
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// KSyncNetlinkDBEntry routines
///////////////////////////////////////////////////////////////////////////////
bool KSyncNetlinkDBEntry::Add() {
    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = AddMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::ADD_ACK);
    return false;
}

bool KSyncNetlinkDBEntry::Change() {
    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = ChangeMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::CHANGE_ACK);
    return false;
}

bool KSyncNetlinkDBEntry::Delete() {
    int len = MsgLen();
    char *msg = (char *)malloc(len);
    int  msg_len = DeleteMsg(msg, len);
    assert(msg_len <= len);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::DEL_ACK);
    return false;
}
