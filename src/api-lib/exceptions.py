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
        return 'Service unavailable time out due to: %s' % (
                str(self._reason_code))
    # end __str__
# end class ServiceUnavailableError


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
        return self.content
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


class ResourceTypeUnknownError(VncError):
    def __init__(self, obj_type):
        self._unknown_type = obj_type
    # end __init__

    def __str__(self):
        return 'Unknown object type: %s' % (self._unknown_type)
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


class HttpError(VncError):

    def __init__(self, status_code, content):
        self.status_code = status_code
        self.content = content
    # end __init__

    def __str__(self):
        return 'HTTP Status: %s Content: %s' % (self.status_code, self.content)
    # end __str__
# end class HttpError


class RequestSizeError(VncError):
    pass
# end class RequestSizeError


class AuthFailed(VncError):
    pass
# end class AuthFailed
