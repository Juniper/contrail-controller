from __future__ import unicode_literals
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Base class of all exceptions in VNC

from vnc_api.exceptions import *


class DatabaseUnavailableError(ServiceUnavailableError):
    def __init__(self, db_type, code=None):
        self._db_type = db_type
        super(DatabaseUnavailableError, self).__init__(code)
    # end __init__

    def __str__(self):
        return 'Error accessing %s database due to: %s' \
               % (self._db_type, self._reason_code)
    # end __str__
# end class DatabaseUnavailableError


class MaxRabbitPendingError(VncError):

    def __init__(self, npending):
        self._npending = npending
    # end __init__

    def __str__(self):
        return 'Too many pending updates to RabbitMQ: %s' % (self._npending)
    # end __str__
# end class MaxRabbitPendingError


class ResourceExistsError(VncError):
    def __init__(self, eexists_fq_name, eexists_id, location=None):
        self._eexists_fq_name = eexists_fq_name
        self._eexists_id = eexists_id
        self._eexists_location = location
    # end __init__

    def __str__(self):
        if self._eexists_location:
            return 'FQ Name: %s exists already with ID: %s at %s' \
                % (self._eexists_fq_name, self._eexists_id,
                   self._eexists_location)
        else:
            return 'FQ Name: %s exists already with ID: %s' \
                % (self._eexists_fq_name, self._eexists_id)
    # end __str__
# end class ResourceExistsError


class ResourceExhaustionError(VncError):
    pass
# end class ResourceExhaustionError


class ResourceOutOfRangeError(VncError):
    def __init__(self, requested_id, min_range, max_range):
        self._requested_id = requested_id
        self._min_range = min_range
        self._max_range = max_range

    def __str__(self):
        return "Requested ID '%d' is out of the ID range [%d, %d]" % (
            self._requested_id, self._min_range, self._max_range)


class NoUserAgentKey(VncError):
    pass
# end class NoUserAgentKey


class UnknownAuthMethod(VncError):
    pass
# end class UnknownAuthMethod


class AmbiguousParentError(VncError):
    pass


class InvalidSessionID(VncError):
    pass
# end InvalidSessionID
