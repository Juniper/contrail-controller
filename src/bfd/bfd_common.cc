/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_common.h"

#include <string>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/optional.hpp>
#include <boost/assign/list_of.hpp>

namespace BFD {
typedef boost::bimap<BFDState, std::string> BFDStateNames;

static BFDStateNames kBFDStateNames =
        boost::assign::list_of<BFDStateNames::relation>
        (kAdminDown, "AdminDown")
        (kDown,      "Down")
        (kInit,      "Init")
        (kUp,        "Up");

std::ostream &operator<<(std::ostream &out, BFDState state) {
    try {
        out << kBFDStateNames.left.at(state);
    } catch (std::out_of_range &) {
        out << "Unknown";
    }

    return out;
}

boost::optional<BFDState> BFDStateFromString(const char *str) {
    try {
        return boost::optional<BFDState>(kBFDStateNames.right.at(str));
    } catch (std::out_of_range &) {}

    return boost::optional<BFDState>();
}

const int kMinimalPacketLength = 24;
const TimeInterval kIdleTxInterval = boost::posix_time::seconds(1);
boost::random::taus88 randomGen;
}  // namespace BFD
