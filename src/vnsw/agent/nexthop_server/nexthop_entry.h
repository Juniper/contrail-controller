/*
 */
#ifndef _AGENT_OPER_NEXTHOP_ENTRY_H_
#define _AGENT_OPER_NEXTHOP_ENTRY_H_

#include <cstdio>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <tbb/mutex.h>

/*
 * A simple representation of a nexthop entry: the nexthop string and
 * its current state in the DB.
 */
class NexthopDBEntry {

  static const int kNexthopEntryOverhead = 32;

 public:

  typedef boost::shared_ptr<NexthopDBEntry> NexthopPtr;

  enum NexthopDBEntryState {
    NEXTHOP_STATE_CLEAN,
    NEXTHOP_STATE_MARKED,
    NEXTHOP_STATE_DELETED
  };

  NexthopDBEntry(std::string& nh)
    : nexthop_string_(nh) {}

  ~NexthopDBEntry() {
    LOG(DEBUG, "[NexthopServer] nexthop " << nexthop_string_ << " deleted");
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
