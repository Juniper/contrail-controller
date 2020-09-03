import unittest

from import_server import FilterModule

# green_node = {
#     "name": "green-node",
#     "node_type": "ovs-compute",
#     "ports": [green_port, blue_port]
# }

# blue_node = {

# }

# green_port = {

# }

# blue_port = {
    
# }

# {
#             "nodes": [
#                 {
#                     "name": "node-1",
#                     "node_type": "ovs-compute",
#                     "ports": [
#                         {
#                             "name": "ens224",
#                             "mac_address": "00:0c:29:13:37:bb",
#                             "switch_name": "VM283DD71D00"
#                             "tags": [
#                                 "tagA",
#                                 "tagB"
#                             ]
#                     ]   }
#                 }
#             ]
#         }

RESOURCE_TYPE_NODE = "node"
RESOURCE_TYPE_PORT = "port"
RESOURCE_TYPE_TAG = "tag"

RESPONSE_CODE_CREATED = 200
RESPONSE_CODE_ALREADY_EXISTS = 409

def __get_node_response_content(name):
    return {
        "data": {
            "display_name": name,
            "fq_name": ["default-global-system-config", name],
            "name": name,
            "parent_type": "global-system-config",
            "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001",
            "uuid": "f7524a15-e4ce-41c6-b45b-259924e6b2a6",
            "to": ["default-global-system-config", name]
        },
        "kind": "node",
        "operation": "CREATE"
    }

def __get_port_response_content(name):


class TestImportNodes:
    def test_none(self):
        fm = FilterModule()
        client = FakeCCClient()

        fm.import_nodes(None, client)

    def test_empty_dict(self):
        fm = FilterModule()
        client = FakeCCClient()

        fm.import_nodes({}, client)

    def test_single_node_without_ports(self):
        test_configuration = [
            {
                RESOURCE_TYPE: RESOURCE_TYPE_NODE,
                RESOURCE_NAME: "green-node",
                EXPECTED_PAYLOAD: __get_node_response_content("green-node"),
                COMMAND_RESPONSE_CODE: 
            }
        ]

        data = {
            "nodes": [{
                "name": "node-1",
                "node_type": "ovs-compute",
            }]
        }

        fm = FilterModule()
        client = FakeCCClient()

        fm.import_nodes(data, client)

    def test_single_node_with_empty_ports_list(self):
        pass

    def test_single_node_with_single_port(self):
        pass

    def test_single_node_with_multiple_ports(self):
        pass

    def test_single_node_with_single_port_with_empty_tag_list(self):
        pass

    def test_single_node_with_single_port_with_single_nonexisting_tag(self):
        pass

    def test_single_node_with_single_port_with_single_existing_tag(self):
        pass

    def test_single_node_with_single_port_with_multiple_tags(self):
        pass

    def test_single_node_with_multiple_ports_with_multiple_tags(self):
        pass

    def test_mupliple_nodes_without_ports(self):
        pass

    def test_mupliple_nodes_with_ports_and_tags(self):
        pass

