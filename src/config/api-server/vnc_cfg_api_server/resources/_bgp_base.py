#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#


def check_hold_time_in_range(hold_time):
    """RFC 4271 compliant."""
    if hold_time is None or hold_time == 0 or 2 < hold_time < 65536:
        return True, ''
    return (False, 'BGPaaS: Hold Time MUST be either zero '
                   'or at least three seconds')
