/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>

#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
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
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    Sync();
    msg_len = AddMsg(msg, KSYNC_DEFAULT_MSG_SIZE);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::ADD_ACK);
    return false;
}

bool KSyncNetlinkEntry::Change() {
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    if (Sync() == false) {
        free(msg);
        return true;
    }

    msg_len = ChangeMsg(msg, KSYNC_DEFAULT_MSG_SIZE);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::CHANGE_ACK);
    return false;
}

bool KSyncNetlinkEntry::Delete() {
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    msg_len = DeleteMsg(msg, KSYNC_DEFAULT_MSG_SIZE);
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
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    msg_len = AddMsg(msg, KSYNC_DEFAULT_MSG_SIZE); 
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::ADD_ACK);
    return false;
}

bool KSyncNetlinkDBEntry::Change() {
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    msg_len = ChangeMsg(msg, KSYNC_DEFAULT_MSG_SIZE);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::CHANGE_ACK);
    return false;
}

bool KSyncNetlinkDBEntry::Delete() {
    char        *msg = (char *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    int         msg_len;

    msg_len = DeleteMsg(msg, KSYNC_DEFAULT_MSG_SIZE);
    if (msg_len == 0) {
        free(msg);
        return true;
    }
    KSyncSock   *sock = KSyncSock::Get(0);
    sock->SendAsync(this, msg_len, msg, KSyncEntry::DEL_ACK);
    return false;
}

