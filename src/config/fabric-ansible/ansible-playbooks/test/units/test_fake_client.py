import unittest

from fake_cc_client import FakeCCClient, CreateAcceptRequest, GetAcceptRequest

def prepare_create_payload(kind, name):
    return {
        "resources": [
            {
                "kind": kind,
                "data": {
                    "name": name
                }
            }
        ]
    }

class TestFakeCClient(unittest.TestCase):
    def test_constructor_with_none_argument(self):
        FakeCCClient(None)

    def test_constructor_with_invalid_argument(self):
        self.assertRaises(
            FakeCCClient.InvalidTestConfigurationException,
            FakeCCClient, {}
        )

    def test_constructor_with_empty_list(self):
        FakeCCClient([])

    def test_constructor_with_list_containing_invalid_argument(self):
        self.assertRaises(
            FakeCCClient.InvalidTestConfigurationException,
            FakeCCClient, [{}]
        )

    def test_constructor_with_create_accept_request(self):
        FakeCCClient([CreateAcceptRequest("node", "nodeA", "{}", False)])

    def test_constructor_with_get_accept_request(self):
        FakeCCClient([GetAcceptRequest("node", False)])

    def test_create_without_accept_requests(self):
        client = FakeCCClient(None)
        self.assertRaises(
            FakeCCClient.UnhandledCreateRequestException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_get_without_accept_requests(self):
        client = FakeCCClient(None)
        self.assertRaises(
            FakeCCClient.UnhandledGetRequestException,
            client.get_cc_resource, "node"
        )

    def test_create_with_unequal_resource_type(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="port",
                expected_payload="{}",
                throw_http_error=False
            )
        ])
        self.assertRaises(
            FakeCCClient.UnhandledCreateRequestException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_get_with_unequal_resource_type(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False)
        ])
        self.assertRaises(
            FakeCCClient.UnhandledGetRequestException,
            client.get_cc_resource, "node"
        )

    def test_create_with_unequal_resource_name(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeY",
                res_type="node",
                expected_payload="{}",
                throw_http_error=False
            )
        ])
        self.assertRaises(
            FakeCCClient.UnhandledCreateRequestException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_create_with_proper_request(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            )
        ])
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))

    def test_get_with_proper_request(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False)
        ])
        client.get_cc_resource("port")

    def test_create_with_unequal_resource_payload(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload={},
                throw_http_error=False
            )
        ])
        self.assertRaises(
            FakeCCClient.UnequalPayloadException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_create_twice_with_single_expected_request(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            )
        ])
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))
        self.assertRaises(
            FakeCCClient.UnhandledCreateRequestException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_get_twice_with_single_expected_request(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False)
        ])
        client.get_cc_resource("port")
        self.assertRaises(
            FakeCCClient.UnhandledGetRequestException,
            client.get_cc_resource, "port"
        )

    def test_create_twice(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            ),
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            )
        ])
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))

    def test_get_twice(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False),
            GetAcceptRequest(res_type="port", throw_http_error=False)
        ])
        client.get_cc_resource("port")
        client.get_cc_resource("port")

    def test_two_different_creates(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            ),
            CreateAcceptRequest(
                res_name="nodeY",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeY"),
                throw_http_error=False
            )
        ])
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))
        client.create_cc_resource(prepare_create_payload("node", "nodeY"))

    def test_two_different_gets(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False),
            GetAcceptRequest(res_type="node", throw_http_error=False)
        ])
        client.get_cc_resource("port")
        client.get_cc_resource("node")

    def test_two_different_creates_in_different_order(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            ),
            CreateAcceptRequest(
                res_name="nodeY",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeY"),
                throw_http_error=False
            )
        ])
        client.create_cc_resource(prepare_create_payload("node", "nodeY"))
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))

    def test_two_different_gets_in_different_order(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False),
            GetAcceptRequest(res_type="node", throw_http_error=False)
        ])
        client.get_cc_resource("node")
        client.get_cc_resource("port")

    def test_get_with_create_accept_request(self):
        client = FakeCCClient([
            CreateAcceptRequest(
                res_name="nodeY",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeY"),
                throw_http_error=False
            )
        ])
        self.assertRaises(
            FakeCCClient.UnhandledGetRequestException,
            client.get_cc_resource, "node"
        )

    def test_create_with_get_accept_request(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="node", throw_http_error=False)
        ])
        self.assertRaises(
            FakeCCClient.UnhandledCreateRequestException,
            client.create_cc_resource, prepare_create_payload("node", "nodeX")
        )

    def test_get_and_create(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="node", throw_http_error=False),
            CreateAcceptRequest(
                res_name="nodeX",
                res_type="node",
                expected_payload=prepare_create_payload("node", "nodeX"),
                throw_http_error=False
            )
        ])
        client.get_cc_resource("node")
        client.create_cc_resource(prepare_create_payload("node", "nodeX"))

    def test_assert_results(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False),
            GetAcceptRequest(res_type="node", throw_http_error=False)
        ])
        client.get_cc_resource("node")
        client.get_cc_resource("port")
        client.assert_results()

    def test_assert_results_with_missing_request(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=False),
            GetAcceptRequest(res_type="node", throw_http_error=False)
        ])
        client.get_cc_resource("node")
        self.assertRaises(
            FakeCCClient.MissingFunctionCallsException,
            client.assert_results
        )

    def test_http_error(self):
        client = FakeCCClient([
            GetAcceptRequest(res_type="port", throw_http_error=True)
        ])
        self.assertRaises(
            FakeCCClient.HTTPError,
            client.get_cc_resource, "port"
        )

    def test_response(self):
        expected_response = "test123"
        client = FakeCCClient([
            GetAcceptRequest(
                res_type="port",
                throw_http_error=False,
                response=expected_response
            )
        ])
        response = client.get_cc_resource("port")
        self.assertEqual(response, expected_response)

    def test_response_order(self):
        expected_response_a = "test123"
        expected_response_b = "test456"

        client = FakeCCClient([
            GetAcceptRequest(
                res_type="port",
                throw_http_error=False,
                response=expected_response_a
            ),
                        GetAcceptRequest(
                res_type="port",
                throw_http_error=False,
                response=expected_response_b
            )
        ])
        response_a = client.get_cc_resource("port")
        self.assertEqual(response_a, expected_response_a)

        response_b = client.get_cc_resource("port")
        self.assertEqual(response_b, expected_response_b)



