#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Base class of all exceptions in VNC


class VncError(Exception):
    pass
# end class VncError

class ServiceUnavailableError(VncError):
    def __init__(self, code):
        self._reason_code = code
    # end __init__

    def __str__(self):
        return 'Service unavailable time out due to: %s' % (str(self._reason_code))
    # end __str__
# end class ServiceUnavailableError

class DatabaseUnavailableError(ServiceUnavailableError):
    def __init__(self, db_type, code=None):
        self._db_type = db_type
        super(DatabaseUnavailableError, self).__init__(code)
    # end __init__

    def __str__(self):
        return 'Error accessing %s database due to: %s' \
               %(self._db_type, self._reason_code)
    # end __str__
# end class DatabaseUnavailableError

class TimeOutError(VncError):
    def __init__(self, code):
        self._reason_code = code
    # end __init__

    def __str__(self):
        return 'Timed out due to: %s' % (str(self._reason_code))
    # end __str__
# end class TimeOutError


class BadRequest(Exception):
    def __init__(self, status_code, content):
        self.status_code = status_code
        self.content = content
    # end __init__

    def __str__(self):
        return 'HTTP Status: %s Content: %s' % (self.status_code, self.content)
    # end __str__
# end class BadRequest


class NoIdError(VncError):

    def __init__(self, unknown_id):
        self._unknown_id = unknown_id
    # end __init__

    def __str__(self):
        return 'Unknown id: %s' % (self._unknown_id)
    # end __str__
# end class NoIdError


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

class ResourceTypeUnknownError(VncError):
    def __init__(self, obj_type):
        self._unknown_type = obj_type
    # end __init__

    def __str__(self):
        return 'Unknown object type: %s' %(self._unknown_type)
    # end __str__
# end class ResourceTypeUnknownError

class PermissionDenied(VncError):
    pass
# end class PermissionDenied

class OverQuota(VncError):
    pass
# end class OverQuota

class RefsExistError(VncError):
    pass
# end class RefsExistError


class ResourceExhaustionError(VncError):
    pass
# end class ResourceExhaustionError


class NoUserAgentKey(VncError):
    pass
# end class NoUserAgentKey


class UnknownAuthMethod(VncError):
    pass
# end class UnknownAuthMethod


class HttpError(VncError):

    def __init__(self, status_code, content):
        self.status_code = status_code
        self.content = content
    # end __init__

    def __str__(self):
        return 'HTTP Status: %s Content: %s' % (self.status_code, self.content)
    # end __str__
# end class HttpError


class AmbiguousParentError(VncError):
    pass


class InvalidSessionID(VncError):
    pass
# end InvalidSessionID
