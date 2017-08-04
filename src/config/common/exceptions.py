import json
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Base class of all exceptions in VNC

status_codes = {
    "ServiceUnavailableError": 503,
    "TimeOutError": 504,
    "BadRequest": 400,
    "NoIdError": 404,
    "NoUserAgentKey": 404,
    "RefsExistError": 409,
    "OverQuota": 412,
    "PermissionDenied": 403,
    "ResourceExistsError": 409
}

def get_status_code(error):
    try:
        return status_codes[error]
    except KeyError:
        return 500


class VncError(Exception):

    pass
# end class VncError

class HttpError(VncError):
    def __init__(self, status_code=500, content=None):
        self.status_code = status_code
        self.error = self.__class__.__name__
        if content is None:
            self.content = json.dumps(self.__dict__)
        else:
            self.content = content
        self.message = 'HTTP Status: %s Content: %s' % (status_code,
                self.content)
    # end __init__

    def __str__(self):
        return 'HTTP Status: %s Content: %s' % (self.status_code,
                self.content)
    # end __str__
# end HttpError

class ServiceUnavailableError(HttpError):

    def __init__(self, down_service):
        self.down_service = down_service
        self.error = self.__class__.__name__
        self.message = \
            'Service {} unavailable. Timed out.'.format(down_service)
        super(ServiceUnavailableError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Service {} unavailable. Timed out.'.format(self.down_service)

    # end __str__
# end class ServiceUnavailableError

class DatabaseUnavailableError(ServiceUnavailableError):

    def __init__(
        self,
        db_type,
        reason=None,
        expected_location=None,
        ):
        self.db_type = db_type
        self.reason = reason
        self.expected_location = expected_location
        self.error = self.__class__.__name__
        self.message = \
            '{} - {} Database expected running at {}. Failed due to {}'\
            .format(super(DatabaseUnavailableError, self).__str__(), db_type, \
                (expected_location if expected_location \
                is not None else 'Unknown'), (reason if reason \
                is not None else 'Unknown'))
        super(DatabaseUnavailableError, self).__init__('Database')

    # end __init__

    def __str__(self):
        return '{} - {} Database expected running at {}. Failed due to {}'\
                .format(super(DatabaseUnavailableError, self).__str__(), \
                self.db_type, (self.expected_location if self.expected_location \
                is not None else 'Unknown'), \
                (self.reason if self.reason is not None else 'Unknown'))


    # end __str__
# end class DatabaseUnavailableError

class TimeOutError(HttpError):

    def __init__(self, timeout_service=None):
        self.timeout_service = timeout_service
        self.error = self.__class__.__name__
        self.message = \
            'Service {} timed out'.format((timeout_service if timeout_service \
                is not None else 'API Server'))
        super(TimeOutError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Service {} timed out'.format(( \
            self.timeout_service \
            if self.timeout_service is not None \
            else 'API Server'))


    # end __str__
# end class TimeOutError

class BadRequest(HttpError):

    def __init__(self, reason=None):
        self.reason = reason
        self.message = \
            'Bad Request - Reason: {}'.foramt((reason if reason \
                is not None else 'Unknown'))
        self.error = self.__class__.__name__
        super(BadRequest, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Bad Request - Reason: {}'.foramt((self.reason if self.reason \
                is not None else 'Unknown'))


    # end __str__
# end class BadRequest

class BadHeader(BadRequest):

    def __init__(
        self,
        bad_parameter=None,
        bad_value=None,
        current_header=None,
        ):
        self.error = self.__class__.__name__
        self.bad_parameter = bad_parameter
        self.bad_value = bad_value
        self.current_header = current_header
        if bad_value is not None:
            self.message = \
                'Bad Request Header due to parameter {} value {}'.format(( \
                    bad_parameter \
                    if bad_parameter is not None \
                    else 'Unknown'), bad_value)
        else:
            self.message = \
                'Bad Request Header due to no value for parameter {}'.format(( \
                    bad_parameter \
                    if bad_parameter is not None \
                    else 'Unknown'))
        super(BadHeader, self).__init__("Bad Header")

    # end __init__

    def __str__(self):
        if self.bad_value is not None:
            return 'Bad Request Header due to parameter {} value {}'.format(( \
                self.bad_parameter \
                if self.bad_parameter is not None \
                else 'Unknown'), self.bad_value)
        else:
            return 'Bad Request Header due to no value for parameter {}'\
            .format((self.bad_parameter \
                if self.bad_parameter is not None \
                else 'Unknown'))


    # end __str__
# end class BadHeader

class BadParameter(BadRequest):

    def __init__(self, bad_property=None, bad_value=None):
        self.bad_property = bad_property
        self.bad_value = bad_value
        self.error = self.__class__.__name__
        if bad_value is not None:
            self.message = \
                'Bad request for value {} of property {}'.format( \
                    bad_value, (bad_property \
                    if bad_property is not None \
                    else 'Unknown'))
        else:
            self.message = \
                'Bad request for property {} with no value'.format(( \
                    bad_property \
                    if bad_property is not None \
                    else 'Unknown'))
        super(BadParameter, self).__init__("Bad Parameter")

    # end __init__

    def __str__(self):
        if self.bad_value is not None:
            return 'Bad request for value {} of property {}'.format( \
                self.bad_value, (self.bad_property \
                    if self.bad_property is not None \
                    else 'Unknown'))
        else:
            return 'Bad request for property {} with no value'.format(( \
                self.bad_property \
                if self.bad_property is not None \
                else 'Unknown'))


    # end __str__
# end class BadParameter

class AccessDeniedError(BadRequest):

    def __init__(self, object_type=None):
        self.object_type = object_type
        self.error = self.__class__.__name__
        self.message = \
            'Object {} is not visible by the user'.format(( \
                object_type \
                if object_type is not None \
                else 'Unknown'))
        super(AccessDeniedError, self).__init__("Access Denied Error")

    # end __init__

    def __str__(self):
        return 'Object {} is not visible by the user'.format(( \
            self.object_type \
            if self.object_type is not None \
            else 'Unknown'))


    # end __str__
# end class AccessDeniedError

class NoIdError(HttpError):

    def __init__(self, unknown_id, resource_type=None):
        self.unknown_id = unknown_id
        self.resource_type = resource_type
        self.error = self.__class__.__name__
        self.message = \
            'Unknown ID {} for {} resource type'.format(unknown_id, \
                (resource_type if resource_type \
                is not None else 'Unknown'))
        super(NoIdError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Unknown ID {} for {} resource type'.format(self.unknown_id, \
                (self.resource_type if self.resource_type \
                is not None else 'Unknown'))


    # end __str__
# end class NoIdError

class MaxRabbitPendingError(HttpError):

    def __init__(self, pending_updates):
        self.pending_updates = pending_updates
        self.error = self.__class__.__name__
        self.message = \
            'RabbitMQ update queue full due to {} pending update requsts'\
            .format(pending_updates)
        super(MaxRabbitPendingError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'RabbitMQ update queue full due to {} pending update requsts'\
        .format(self.pending_updates)


    # end __str__
# end class MaxRabbitPendingError

class ResourceExistsError(HttpError):

    def __init__(
        self,
        existing_resource_fq_name,
        existing_resource_id,
        existing_resource_location=None,
        ):
        self.existing_resource_fq_name = existing_resource_fq_name
        self.existing_resource_id = existing_resource_id
        self.existing_resource_location = existing_resource_location
        self.error = self.__class__.__name__
        self.message = \
            'FQ Name {} already exists with ID {} at {} location'.foramt( \
                existing_resource_fq_name, \
                existing_resource_id, \
                (existing_resource_location if existing_resource_location \
                is not None else 'Unknown'))
        super(ResourceExistsError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'FQ Name {} already exists with ID {} at {} location'.foramt( \
            self.existing_resource_fq_name, \
            self.existing_resource_id, \
            (self.existing_resource_location \
            if self.existing_resource_location is not None \
            else 'Unknown'))


    # end __str__
# end class ResourceExistsError

class ResourceTypeUnknownError(HttpError):

    def __init__(self, unknown_type):
        self.unknown_type = unknown_type
        self.error = self.__class__.__name__
        self.message = 'Object Type {} is unknown'.format(unknown_type)
        super(ResourceTypeUnknownError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Object Type {} is unknown'.format(self.unknown_type)


    # end __str__
# end class ResourceTypeUnknownError

class PermissionDenied(HttpError):

    def __init__(self, reason=None):
        self.reason = reason
        self.error = self.__class__.__name__
        self.message = \
            'Permission Denied due to {}'.format((reason if reason \
                is not None else 'Unknown'))
        super(PermissionDenied, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Permission Denied due to {}' \
        .format((self.reason if self.reason is not None else 'Unknown'))


    # end __str__
# end class PermissionDenied

class OverQuota(HttpError):

    def __init__(self):
        self.error = self.__class__.__name__
        self.message = 'Quota Exceeded'
        super(OverQuota, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Quota Exceeded'


    # end __str__
# end class OverQuota

class RefsExistError(HttpError):

    def __init__(
        self,
        existing_reference_name,
        existing_reference_id,
        existing_reference_location=None,
        ):
        self.existing_reference_name = existing_reference_name
        self.existing_reference_id = existing_reference_id
        self.existing_reference_location = existing_reference_location
        self.error = self.__class__.__name__
        self.message = \
            'Resource {} already exists with ID {} at {} location'\
            .foramt(existing_reference_name, \
            existing_reference_id, \
            (existing_reference_location if existing_reference_location \
            is not None else 'Unknown'))
        super(RefsExistError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Resource {} already exists with ID {} at {} location'.foramt( \
            self.existing_reference_name, \
            self.existing_reference_id, \
            (self.existing_reference_location \
            if self.existing_reference_location is not None \
            else 'Unknown'))


    # end __str__
# end class RefsExistError

class ResourceExhaustionError(VncError):

    def __init__(self, reason=None):
        self.error = self.__class__.__name__
        self.reason = (reason if reason is not None else 'Unknown')
        self.message = \
            'Resources Exhausted. Reason: {}'.foramt((reason if reason \
                is not None else 'Unknown'))
        super(ResourceExhaustionError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Resources Exhausted. Reason: {}'.foramt(self.reason)


    # end __str__
# end class ResourceExhaustionError

class NoUserAgentKey(VncError):

    def __init__(self):
        self.error = self.__class__.__name__
        self.message = 'No User Agent Key'
        super(NoUserAgentKey, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'No User Agent Key'


# end class NoUserAgentKey

class UnknownAuthMethod(VncError):

    def __init__(self, auth_method):
        self.auth_method = auth_method
        self.error = self.__class__.__name__
        self.message = \
            'Unknown Authenticaion Method {}'.format(auth_method)
        super(UnknownAuthMethod, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Unknown Authenticaion Method {}'.format(self.auth_method)


    # end __str__
# end class UnknownAuthMethod

class AmbiguousParentError(VncError):

    def __init__(self):
        self.error = self.__class__.__name__
        self.message = 'Ambiguos Patent in request'
        super(AmbiguousParentError, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Ambiguos Patent in request'


    # end __str__
# end AmbiguousParentError

class InvalidSessionID(VncError):

    def __init__(self):
        self.error = self.__class__.__name__
        self.message = 'Invalid Session ID'
        super(InvalidSessionID, self).__init__(get_status_code(self.error))

    # end __init__

    def __str__(self):
        return 'Invalid Session ID'


# end InvalidSessionID

class RequestSizeError(HttpError):

    pass


# end class RequestSizeError

