_RESOURCES_KEY = "resources"
_KIND_KEY = "kind"
_NAME_KEY = "name"
_DATA_KEY = "data"

class CreateAcceptRequest:
    def __init__(
            self,
            res_type,
            res_name,
            expected_payload,
            throw_http_error,
            response=None
        ):
        self.resource_type = res_type
        self.resource_name = res_name
        self.expected_payload = expected_payload
        self.throw_http_error = throw_http_error
        self.response = response

    def __str__(self):
        return """Resource Type: '{}'
            Resource Name: '{}'
            Expected Payload: '{}'
            Command Response Code: '{}'
            Command Response Content: '{}'
            """.format(
                self.resource_type,
                self.resource_name,
                self.expected_payload,
                self.throw_http_error,
                self.response
            )


class GetAcceptRequest:
    def __init__(
            self,
            res_type,
            throw_http_error,
            response=None
        ):
        self.resource_type = res_type
        self.throw_http_error = throw_http_error
        self.response = response

    def __str__(self):
        return """Resource Type: '{}'
            Command Response Code: '{}'
            Command Response Content: '{}'
            """.format(
                self.resource_type,
                self.throw_http_error,
                self.response
            )


class FakeCCClient:
    """Fake Contrail Command Client.

    This Client allows user to specify the list of expected requests.
    For creating/getting resources method the client will look up for
    the specified kind and name within list of accepted requests.
    The workflow looks like this:

        1. CC Client's Create resource function is called,
        2. Resource name and kind are extracted from input payload,
        3. CC Client finds first request from accept_requests list that
           matches previously extracted kind and name.
           If there is no such request in the list then client raises an
           exception,
        4. CC Client compares input payload with previously found request.
           If input payload lacks fields from the request or some of them
           are different then it raises an exception,
        5. CC Client returns specified response code and content that
           corresponds with previously found request,
        6. CC Client removes previously found request from the accept list,

    At the end of the test an accept_requests list should be empty.
    Otherwise test will raise an error as not all requests appeared.
    """

    def __init__(self, accept_requests):
        if accept_requests is None:
            accept_requests = []
        self.__validate(accept_requests)
        self.__accept_requests = accept_requests

    def __validate(self, accept_requests):
        if not isinstance(accept_requests, list):
            raise self.InvalidTestConfigurationException

        for ar in accept_requests:
            if not (isinstance(ar, CreateAcceptRequest) or isinstance(ar, GetAcceptRequest)):
                raise self.InvalidTestConfigurationException


    def create_cc_resource(self, res_request_payload):
        self.__validate_request_payload(res_request_payload)

        res_name = self.__get_resource_name(res_request_payload)
        res_kind = self.__get_resource_kind(res_request_payload)

        for i, r in enumerate(self.__accept_requests):
            if not isinstance(r, CreateAcceptRequest):
                continue

            if r.resource_name != res_name or r.resource_type != res_kind:
                continue

            self.__compare_payload(r.expected_payload, res_request_payload)

            self.__accept_requests.pop(i)

            if r.throw_http_error:
                raise self.HTTPError

            return r.response

        raise self.UnhandledCreateRequestException(res_request_payload)


    def __compare_payload(self, expected, actual):
        if not all(item in expected.items() for item in actual.items()):
            raise self.UnequalPayloadException(
                "Expected: '{}'. Got: '{}'".format(expected, actual)
            )


    def __validate_request_payload(self, request_payload):
        if not isinstance(request_payload, dict):
            raise self.InvalidPayloadFormException(
                "Request payload is not a dict"
            )
        if not _RESOURCES_KEY in request_payload:
            raise self.InvalidPayloadFormException(
                "Expected dict with '{}' key. Got '{}'".format(
                    _RESOURCES_KEY, request_payload
                )
            )
        resources = request_payload[_RESOURCES_KEY]
        if resources is None:
            raise self.InvalidPayloadFormException(
                "Empty value for '{}' key".format(_RESOURCES_KEY)
            )
        if not isinstance(resources, list):
            raise self.InvalidPayloadFormException(
                "Expected list value for '{}' key. Got: '{}'".format(
                    _RESOURCES_KEY, resources
                )
            )
        if len(resources) != 1:
            raise self.InvalidPayloadFormException(
                "Expected one resource. Got {} within payload '{}'".format(
                    len(resources), request_payload
                )
            )
        resource_payload = resources[0]
        if not _KIND_KEY in resource_payload:
            raise self.MissingFieldsException(
                "Missing '{}' key in payload: '{}'".format(
                    _KIND_KEY, request_payload
                )
            )
        if not _DATA_KEY in resource_payload:
            raise self.MissingFieldsException(
                "Missing '{}' key in payload: '{}'".format(
                    _DATA_KEY, request_payload
                )
            )
        if not isinstance(resource_payload[_DATA_KEY], dict):
            raise self.InvalidPayloadFormException(
                "Expected dict value for key '{}'. Got: '{}'".format(
                    _DATA_KEY, resource_payload[_DATA_KEY]
                )
            )
        if not _NAME_KEY in resource_payload[_DATA_KEY]:
            raise self.MissingFieldsException(
                "Missing '{}' key in payload: '{}'".format(
                    _NAME_KEY, request_payload
                )
            )

    def __get_resource_name(self, request_payload):
        return request_payload[_RESOURCES_KEY][0][_DATA_KEY][_NAME_KEY]

    def __get_resource_kind(self, request_payload):
        return request_payload[_RESOURCES_KEY][0][_KIND_KEY]

    def get_cc_resource(self, kind):
        for i, r in enumerate(self.__accept_requests):
            if (not isinstance(r, GetAcceptRequest)) or r.resource_type != kind:
                continue

            self.__accept_requests.pop(i)

            if r.throw_http_error:
                raise self.HTTPError

            return r.response

        raise self.UnhandledGetRequestException(kind)

    def assert_results(self):
        if len(self.__accept_requests) == 0:
            return
        raise self.MissingFunctionCallsException(
            "Missing {} function calls: {}".format(
                len(self.__accept_requests), self.__accept_requests,
            )
        )

    class HTTPError(Exception):
        pass

    class MissingFieldsException(Exception):
        pass

    class InvalidTestConfigurationException(Exception):
        pass

    class InvalidPayloadFormException(Exception):
        pass

    class UnhandledCreateRequestException(Exception):
        pass

    class UnhandledGetRequestException(Exception):
        pass

    class UnequalPayloadException(Exception):
        pass

    class MissingFunctionCallsException(Exception):
        pass
