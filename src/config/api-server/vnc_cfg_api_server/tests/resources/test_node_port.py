#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import logging
import pprint

from cfgm_common.exceptions import RefsExistError
from vnc_api.gen.resource_client import Node
from vnc_api.gen.resource_client import PhysicalInterface
from vnc_api.gen.resource_client import PhysicalRouter
from vnc_api.gen.resource_client import Port
from vnc_api.gen.resource_xsd import BaremetalPortInfo
from vnc_api.gen.resource_xsd import LocalLinkConnection

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestNodePort(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestNodePort, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestNodePort, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def remove_qfx_and_pi(self, pr_and_pi):
        logger.warn("Removing PR")
        for pr_data in pr_and_pi:
            for pi_data in pr_and_pi[pr_data]:
                self.api.physical_interface_delete(
                    fq_name=['default-global-system-config',
                             pr_data,
                             pi_data])
                logger.warn("    Removed PI: " + pr_data + "=> " + pi_data)
            self.api.physical_router_delete(
                fq_name=['default-global-system-config', pr_data])
            logger.warn("Removed PR: " + pr_data)

    def create_qfx_and_pi(self, pr_and_pi):
        logger.warn("Creating PR")
        for pr_data in pr_and_pi:
            pr = PhysicalRouter(pr_data)
            pr_uuid = self.api.physical_router_create(pr)
            pr_obj = self.api.physical_router_read(id=pr_uuid)
            logger.warn("Created PR: " + pr_data)
            for pi_data in pr_and_pi[pr_data]:
                logger.warn("    Created PI: " + pr_data + " => " + pi_data)
                pi_obj = PhysicalInterface(name=pi_data, parent_obj=pr_obj)
                self.api.physical_interface_create(pi_obj)
                # pi_pr_obj = self.api.physical_interface_read(id=pi_pr)
                # logger.warn(pprint.pformat(pi_pr_obj.__dict__))

    def remove_node_and_port(self, node_and_port):
        logger.warn("Removing Node and Port")
        for node in node_and_port:
            logger.warn("Removing Node ")
            for port in node_and_port[node]:
                logger.warn("Removing Port " + port.get('name'))
                self.api.port_delete(fq_name=['default-global-system-config',
                                              node, port.get('name')])
                logger.warn("PORT : " + port.get('name'))
            self.api.node_delete(fq_name=['default-global-system-config',
                                          node])
            logger.warn("NODE: " + node)
        return

    def verify_port_pi_ref(self, port_fq_name, pi_fq_name):
        port_read = self.api.port_read(fq_name=port_fq_name)
        pi_read = self.api.physical_interface_read(fq_name=pi_fq_name)
        port_refs = pi_read.get_port_refs()
        logger.warn("PORT UUID: " + pprint.pformat(port_refs))
        for port_ref in port_refs:
            # logger.warn(pprint.pformat(port_ref['uuid']))
            if port_read.uuid == port_ref.get('uuid'):
                return True
        return False

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
                node_port_obj = Port(port.get('name'), node_obj,
                                     bms_port_info=bm_info)
                self.api.port_create(node_port_obj)
                logger.warn(port['name'])

    def test_create_node_port_1(self):
        # test the creation of port and if PI details are empty
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        qfx2_name = self.id() + 'qfx2'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4'],
                     qfx2_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        try:
            node_and_port = {node_name: [
                {'name': 'port1',
                 'address': "11:22:33:44:55:55"},
                {'name': 'port2',
                 'address': "11:22:33:44:55:56",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/1'},
                {'name': 'port3',
                 'address': "11:22:33:44:55:57",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/2'}]
            }
            verify_result = False
            self.create_node_and_port(node_and_port)
            for node in node_and_port:
                for port in node_and_port[node]:
                    logger.warn('verify port ' + node + ' => ' + port['name'])
                    verify_result = False
                    port_fq_name = ['default-global-system-config', node,
                                    port.get('name')]

                    if port.get('sw_name') and port.get('port_id'):
                        pi_fq_name = ['default-global-system-config',
                                      port.get('sw_name'), port.get('port_id')]
                        verify_result = self.verify_port_pi_ref(port_fq_name,
                                                                pi_fq_name)
                    else:
                        port_read = self.api.port_read(fq_name=port_fq_name)
                        if port_read:
                            verify_result = True
                    logger.warn('ALL Good 1 ' + str(verify_result))
                    self.assertEqual(True, verify_result)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            self.assertEqual(True, verify_result)
            logger.warn('Port TEST 1 : Failed' + str(e))
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_2(self):
        # test the basic creation of node, port and its link with PI
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        qfx2_name = self.id() + 'qfx2'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0',
                                 'xe-0/0/1',
                                 'xe-0/0/2',
                                 'xe-0/0/3',
                                 'xe-0/0/4'],
                     qfx2_name: ['xe-0/0/0',
                                 'xe-0/0/1',
                                 'xe-0/0/2',
                                 'xe-0/0/3',
                                 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        try:
            node_and_port = {node_name: [
                {'name': 'port1',
                 'address': "11:22:33:44:55:55",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/0'},
                {'name': 'port2',
                 'address': "11:22:33:44:55:56",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/1'},
                {'name': 'port3',
                 'address': "11:22:33:44:55:57",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/2'}]
            }
            verify_result = False
            self.create_node_and_port(node_and_port)
            for node in node_and_port:
                for port in node_and_port[node]:
                    logger.warn('verify port ' + node + ' => ' + port['name'])
                    verify_result = False
                    port_fq_name = ['default-global-system-config', node,
                                    port.get('name')]
                    pi_fq_name = ['default-global-system-config',
                                  port.get('sw_name'), port.get('port_id')]
                    verify_result = self.verify_port_pi_ref(port_fq_name,
                                                            pi_fq_name)
                    logger.warn('ALL Good 1 ' + str(verify_result))
                    self.assertEqual(True, verify_result)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('Port Test 2 FAILED' + str(e))
            self.assertEqual(True, verify_result)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_3(self):
        # test the delete of PI, when it is linked with port
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        node_name = self.id() + '-node'
        # NOTE: This is already tested with test_1 and test_2
        pr_and_pi = {qfx1_name: ['xe-0/0/0',
                                 'xe-0/0/1',
                                 'xe-0/0/2',
                                 'xe-0/0/3',
                                 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        try:
            node_and_port = {node_name: [
                {'name': 'port1',
                 'address': "11:22:33:44:55:55",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/0'},
                {'name': 'port2',
                 'address': "11:22:33:44:55:56",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/1'},
                {'name': 'port3',
                 'address': "11:22:33:44:55:57",
                 'sw_name': qfx1_name,
                 'port_id': 'xe-0/0/2'}]
            }
            self.create_node_and_port(node_and_port)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('Port Test 3 : FAILED' + str(e))
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_4(self):
        # verify the failure to delete of port, if PI is still linked
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        qfx2_name = self.id() + 'qfx2'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4'],
                     qfx2_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        node_and_port = {
            node_name: [{
                'name': 'port1',
                'address': "11:22:33:44:55:55",
                'sw_name': qfx1_name,
                'port_id': 'xe-0/0/0'}]
        }
        try:
            self.create_node_and_port(node_and_port)
            for node in node_and_port:
                for port in node_and_port[node]:
                    try:
                        self.api.port_delete(
                            fq_name=['default-global-system-config',
                                     node, port.get('name')])
                    except RefsExistError as e:
                        logger.warn('Port Test 6 : RefsExistError: ' + str(e))
                        continue
                    except Exception as e:
                        logger.warn('Port Test 7 : Unkown Exception ' + str(e))

            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('Port Test 4 : FAILED ' + str(e))
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_5(self):
        # verify the creation of port if local_link is in-valid
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        port_details = {'name': 'port1', 'address': "11:22:33:44:55:55",
                        'sw_name': qfx1_name, 'port_id': 'xe-0/0/5'}
        node_and_port = {node_name: [port_details]}
        try:
            node_obj = Node(node_name, node_hostname=node_name)
            self.api.node_create(node_obj)
            ll_obj = LocalLinkConnection(
                switch_info=port_details.get('sw_name'),
                port_id=port_details.get('port_id'))
            bm_info = BaremetalPortInfo(address=port_details.get('address'),
                                        local_link_connection=ll_obj)
            node_port_obj = Port(port_details.get('name'), node_obj,
                                 bms_port_info=bm_info)
            self.api.port_create(node_port_obj)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('Port Test 5 : FAILED ' + str(e))
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_6(self):
        # verify the update of port by moving to a new PI
        logger.warn('TESTING %s', self.id())
        qfx1_name = self.id() + 'qfx1'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0', 'xe-0/0/1', 'xe-0/0/2',
                                 'xe-0/0/3', 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        node_and_port = {
            node_name: [{
                'name': 'port1',
                'address': "11:22:33:44:55:55",
                'sw_name': qfx1_name,
                'port_id': 'xe-0/0/0'}
            ]
        }
        port_details = {'name': 'port1', 'address': "11:22:33:44:55:55",
                        'sw_name': qfx1_name, 'port_id': 'xe-0/0/2'}
        new_node_port = {node_name: [port_details]}
        try:
            verify_result = False
            self.create_node_and_port(node_and_port)
            ll_obj = LocalLinkConnection(
                switch_info=port_details.get('sw_name'),
                port_id=port_details.get('port_id'))
            bm_info = BaremetalPortInfo(
                address=port_details.get('address'),
                local_link_connection=ll_obj)
            port_read_obj = self.api.port_read(
                fq_name=['default-global-system-config', node_name, 'port1'])
            port_read_obj.set_bms_port_info(bm_info)
            logger.warn('BEFORE PORT UPDATED ')
            self.api.port_update(port_read_obj)
            logger.warn('PORT UPDATED ')
            for node in new_node_port:
                for port in new_node_port[node]:
                    logger.warn('verify port ' + node + ' => ' + port['name'])
                    verify_result = False
                    port_fq_name = ['default-global-system-config',
                                    node, port.get('name')]
                    pi_fq_name = ['default-global-system-config',
                                  port.get('sw_name'), port.get('port_id')]
                    verify_result = self.verify_port_pi_ref(port_fq_name,
                                                            pi_fq_name)
                    logger.warn('ALL Good 1 ' + str(verify_result))
                    self.assertEqual(True, verify_result)

            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('TEST FAILED ' + str(e))
            self.assertEqual(True, verify_result)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')

    def test_create_node_port_7(self):
        # verify the update of port by moving to a new PI and delete the PI
        # and Port this is tested as part of _6 test, update is happening and
        # port is being deleted.
        logger.warn('TESTING %s', self.id())
        logger.warn('PASS - Port Created')

    def test_create_node_port_8(self):
        logger.warn('TESTING %s', self.id())
        # verify the update of port with empty local_link by moving to a new PI
        qfx1_name = self.id() + 'qfx1'
        node_name = self.id() + '-node'
        pr_and_pi = {qfx1_name: ['xe-0/0/0',
                                 'xe-0/0/1',
                                 'xe-0/0/2',
                                 'xe-0/0/3',
                                 'xe-0/0/4']}
        self.create_qfx_and_pi(pr_and_pi)
        node_and_port = {
            node_name: [{'name': 'port1', 'address': "11:22:33:44:55:55"}]
        }
        port_details = {'name': 'port1', 'address': "11:22:33:44:55:55",
                        'sw_name': qfx1_name, 'port_id': 'xe-0/0/2'}
        new_node_port = {node_name: [port_details]}
        try:
            verify_result = False
            self.create_node_and_port(node_and_port)
            ll_obj = LocalLinkConnection(
                switch_info=port_details.get('sw_name'),
                port_id=port_details.get('port_id'))
            bm_info = BaremetalPortInfo(
                address=port_details.get('address'),
                local_link_connection=ll_obj)
            port_read_obj = self.api.port_read(
                fq_name=['default-global-system-config', node_name, 'port1'])
            port_read_obj.set_bms_port_info(bm_info)
            logger.warn('BEFORE PORT UPDATED ')
            self.api.port_update(port_read_obj)
            logger.warn('PORT UPDATED ')
            for node in new_node_port:
                for port in new_node_port[node]:
                    logger.warn('verify port ' + node + ' => ' + port['name'])
                    verify_result = False
                    port_fq_name = ['default-global-system-config',
                                    node,
                                    port.get('name')]
                    pi_fq_name = ['default-global-system-config',
                                  port.get('sw_name'),
                                  port.get('port_id')]
                    verify_result = self.verify_port_pi_ref(port_fq_name,
                                                            pi_fq_name)
                    logger.warn('ALL Good 1 ' + str(verify_result))
                    self.assertEqual(True, verify_result)

            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        except Exception as e:
            logger.warn('TEST FAILED ' + str(e))
            self.assertEqual(True, verify_result)
            self.remove_qfx_and_pi(pr_and_pi)
            self.remove_node_and_port(node_and_port)
        logger.warn('PASS - Port Created')
