
from builtins import str
import logging
import pprint

from vnc_api.gen.resource_client import Card
from vnc_api.gen.resource_client import Hardware
from vnc_api.gen.resource_client import Node
from vnc_api.gen.resource_client import NodeProfile
from vnc_api.gen.resource_client import Port
from vnc_api.gen.resource_client import Tag
from vnc_api.gen.resource_xsd import BaremetalPortInfo
from vnc_api.gen.resource_xsd import InterfaceMapType
from vnc_api.gen.resource_xsd import LocalLinkConnection
from vnc_api.gen.resource_xsd import PortInfoType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestNodeProfile(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestNodeProfile, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestNodeProfile, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def print_node_profile(self, node_profile_uuid="", np_fq_name=[]):
        if node_profile_uuid:
            np_read = self.api.node_profile_read(id=node_profile_uuid)
        elif np_fq_name:
            np_read = self.api.node_profile_read(fq_name=np_fq_name)
        else:
            return

        # hw_read = self.api.hardware_read(fq_name=["test-card1"])
        # logger.warn( pprint.pformat(hw_read.__dict__))

        logger.warn("============ Node Profile Dict ===================")
        logger.warn(pprint.pformat(np_read.__dict__))
        hw_refs = np_read.get_hardware_refs()
        for hw_ref in hw_refs:
            hw_obj = self.api.hardware_read(id=hw_ref.get('uuid'))
            logger.warn(pprint.pformat(hw_obj.__dict__))
            card_refs = hw_obj.get_card_refs()
            for card_ref in card_refs:
                card_obj = self.api.card_read(id=card_ref.get('uuid'))
                logger.warn(pprint.pformat(card_obj.__dict__))
                port_map = card_obj.get_interface_map()
                port_info = port_map.get_port_info()
                for port in port_info:
                    logger.warn("============== Port Info =================")
                    logger.warn(pprint.pformat(port))

    def create_node_and_port(self, node_and_port):
        for node in node_and_port:
            node_obj = Node(node, node_hostname=node)
            self.api.node_create(node_obj)
            for port in node_and_port[node]:
                logger.warn(port['name'])
                ll_obj = None
                if port.get('sw_name') and port.get('port_id'):
                    ll_obj = LocalLinkConnection(
                        switch_info=port.get('sw_name'),
                        port_id=port.get('port_id'))
                bm_info = BaremetalPortInfo(address=port.get('address'),
                                            local_link_connection=ll_obj)
                node_port_obj = Port(port.get('name'),
                                     node_obj,
                                     bms_port_info=bm_info)
                self.api.port_create(node_port_obj)

    def remove_node_and_port(self, node_and_port):
        logger.warn("Removing Node and Port")
        for node in node_and_port:
            logger.warn("Removing Node ")
            port_groups = self.api.port_groups_list(
                parent_fq_name=['default-global-system-config', node])
            logger.warn(pprint.pformat(port_groups))
            for pg in port_groups['port-groups']:
                logger.warn('DELETING Port-Group : ' + str(pg['fq_name'][-1]))
                self.api.port_group_delete(fq_name=pg['fq_name'])

            for port in node_and_port[node]:
                logger.warn("Removing Port " + port.get('name'))
                self.api.port_delete(fq_name=['default-global-system-config',
                                              node, port.get('name')])
                logger.warn("PORT : " + port.get('name'))

            self.api.node_delete(fq_name=['default-global-system-config',
                                          node])
            logger.warn("NODE: " + node)
        return

    def create_tags(self):
        tag_list = {
            'provisioning': {'tag_type_name': 'label'},
            'tenant': {'tag_type_name': 'label'},
            'tenant1': {'tag_type_name': 'label'},
            'tenant2': {'tag_type_name': 'label'},
            'tenant3': {'tag_type_name': 'label'},
            'provisioning1': {'tag_type_name': 'label'},
            'control-data1': {'tag_type_name': 'label'},
            'control-data': {'tag_type_name': 'label'}}

        for tag in tag_list:
            tag_obj = Tag(tag_type_name=tag_list[tag]['tag_type_name'],
                          tag_value=tag)
            self.api.tag_create(tag_obj)
            tag_read_obj = self.api.tag_read(id=tag_obj.uuid)
            logger.warn("TAGS %s", pprint.pformat(tag_read_obj.__dict__))

    def create_node_profile(self, node_profile_data):
        for np in node_profile_data:
            hardware = node_profile_data[np]['hardware']
            interface_map = hardware['card']['interface-map']
            ifmap_list = []
            for iface in interface_map:
                logger.warn(iface)
                logger.warn(pprint.pformat(interface_map[iface]))
                port_info = PortInfoType(
                    name=iface,
                    type="xe",
                    port_speed=interface_map[iface].get('port_speed'),
                    labels=interface_map[iface].get('labels'),
                    port_group=interface_map[iface].get('port_group'))
                ifmap_list.append(port_info)
            iface_map = InterfaceMapType(port_info=ifmap_list)
            logger.warn("PORT-MPA %s", pprint.pformat(iface_map.__dict__))
            card_obj = Card(hardware['card'].get('name'),
                            interface_map=iface_map)
            self.api.card_create(card_obj)

            hw_obj = Hardware(hardware.get('name'))
            hw_obj.add_card(card_obj)
            self.api.hardware_create(hw_obj)

            node_profile_obj = NodeProfile(
                np,
                node_profile_vendor=node_profile_data[np].get(
                    'node_profile_vendor'),
                node_profile_device_family=node_profile_data[np].get(
                    'node_profile_device_family'))

            node_profile_obj.add_hardware(hw_obj)
            self.api.node_profile_create(node_profile_obj)
            self.print_node_profile(node_profile_uuid=node_profile_obj.uuid)

        return

    def test_create_node_profile(self):
        """Test node-profile association with Node.

        create node (node1), and ports.
        create node-profiles qfx1-np and qfx2-np
        create tags to be used
        associate node with qfx1-np, now node-ports should
        ref to tags from node-profile.
        assoicate node with qfx2-np, now node-ports should
        ref to new tags from node-profile.
        remove ref from node, tags from node-ports should
        be removed.
        remove ports and node, there should not be any error.
        """
        node_and_port = {
            'node1':
                [{'name': 'eth0',
                  'address': "11:22:33:44:55:55",
                  'sw_name': 'unit_test_qfx1',
                  'port_id': 'xe-0/0/0'},
                 {'name': 'eth1',
                  'address': "11:22:33:44:55:56",
                  'sw_name': 'unit_test_qfx1',
                  'port_id': 'xe-0/0/1'},
                 {'name': 'eth2',
                  'address': "11:22:33:44:55:57",
                  'sw_name': 'unit_test_qfx1',
                  'port_id': 'xe-0/0/2'}]}

        node_profile_data = {
            'qfx1-np': {
                'node_profile_vendor': 'Juniper',
                'node_profile_device_family': 'qfx',
                'hardware': {
                    'name': 'hw1',
                    'card': {
                        'name': 'card1',
                        'interface-map': {
                            'eth0': {
                                'labels': ["provisioning", "tenant"],
                                'port_group': 'bond0',
                                'port_speed': '10G'
                            },
                            'eth1': {
                                'labels': ["tenant"],
                                'port_group': 'bond0',
                                'port_speed': '10G'
                            },
                            'eth2': {
                                'labels': ["provisioning",
                                           "tenant",
                                           "control-data"],
                                'port_speed': '10G'
                            }
                        }
                    }
                }
            }
        }
        node_profile_data1 = {
            'qfx2-np': {
                'node_profile_vendor': 'Juniper',
                'node_profile_device_family': 'qfx',
                'hardware': {
                    'name': 'hw2',
                    'card': {
                        'name': 'card2',
                        'interface-map': {
                            'eth0': {
                                'labels': [
                                    "provisioning1",
                                    "tenant1"],
                                'port_group': 'bond1',
                                'port_speed': '10G'
                            },
                            'eth1': {
                                'labels': ["tenant2"],
                                'port_group': 'bond1',
                                'port_speed': '10G'
                            },
                            'eth2': {
                                'labels': [
                                    "provisioning1",
                                    "tenant3",
                                    "control-data1"],
                                'port_speed': '10G'
                            }
                        }
                    }
                }
            }
        }
        self.create_tags()
        self.create_node_profile(node_profile_data)
        self.create_node_profile(node_profile_data1)
        self.create_node_and_port(node_and_port)
        node_object = self.api.node_read(
            fq_name=['default-global-system-config', 'node1'])
        np_object = self.api.node_profile_read(
            fq_name=['default-global-system-config', 'qfx1-np'])
        np2_object = self.api.node_profile_read(
            fq_name=['default-global-system-config', 'qfx2-np'])
        logger.warn(pprint.pformat(node_object.__dict__))
        node_object.set_node_profile(np_object)
        self.api.node_update(node_object)
        node_object.set_node_profile(np2_object)
        self.api.node_update(node_object)

        for node in node_and_port:
            node_object_update = self.api.node_read(
                fq_name=['default-global-system-config', node])
            logger.warn(pprint.pformat(node_object_update.__dict__))
            for port in node_and_port[node]:
                port_obj = self.api.port_read(
                    fq_name=['default-global-system-config',
                             node,
                             port.get('name')])
                logger.warn(pprint.pformat(port_obj.__dict__))

        self.api.ref_update('node',
                            node_object.uuid,
                            'node-profile',
                            np2_object.uuid,
                            ['default-global-system-config', 'qfx2-np'],
                            'DELETE')

        for node in node_and_port:
            node_object_update = self.api.node_read(
                fq_name=['default-global-system-config', node])
            logger.warn(pprint.pformat(node_object_update.__dict__))
            for port in node_and_port[node]:
                port_obj = self.api.port_read(
                    fq_name=['default-global-system-config',
                             node,
                             port.get('name')])
                logger.warn("==============")
                logger.warn(pprint.pformat(port_obj.__dict__))
            port_groups = self.api.port_groups_list(
                parent_fq_name=['default-global-system-config', node])
            logger.warn('Port-Groups Printing ==============')
            logger.warn(pprint.pformat(port_groups))
            for pg in port_groups['port-groups']:
                logger.warn("==============")
                pg_obj = self.api.port_group_read(fq_name=pg['fq_name'])
                logger.warn(pprint.pformat(pg_obj.__dict__))

        self.remove_node_and_port(node_and_port)
        logger.warn('PASS - NodeProfile Created')
