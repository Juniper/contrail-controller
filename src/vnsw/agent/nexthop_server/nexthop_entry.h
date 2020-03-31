/*
 * A simple representation of a nexthop entry: the nexthop string and
 * its current state in the Nexthop DB. Used to send nexthop notifications
 * to registered clients.
 */
#ifndef _AGENT_NHS_NEXTHOP_ENTRY_H_
#define _AGENT_NHS_NEXTHOP_ENTRY_H_

#include <array>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <cstdio>
#include <iostream>
#include <tbb/mutex.h>

class NexthopDBEntry {

    static const int kNexthopEntryOverhead = 32;

 public:

    typedef boost::shared_ptr<NexthopDBEntry> NexthopPtr;

    enum NexthopDBEntryState {
        NEXTHOP_STATE_CLEAN,
        NEXTHOP_STATE_MARKED,
        NEXTHOP_STATE_DELETED
    };

    NexthopDBEntry(const std::string& nh)
      : nexthop_string_(nh) {}

    ~NexthopDBEntry() {
    }

    int EncodedLength() {
        return (nexthop_string_.length() + kNexthopEntryOverhead);
    }

    void set_state (NexthopDBEntryState state) {
        state_ = state;
    }

    NexthopDBEntryState state() {
        return state_;
    }

    std::string& nexthop_string() {
        return nexthop_string_;
    }

    bool operator== (NexthopDBEntry& other)
    {
        return (nexthop_string_ == other.nexthop_string());
    }

 private:
    std::string nexthop_string_;
    NexthopDBEntryState state_;
};

#endif
