import sys
import json
import uuid
import logging

from testtools.matchers import Equals, Contains, Not
from testtools import content, content_type

from vnc_api.vnc_api import *

sys.path.append('../common/tests')
from test_utils import *
import test_common

import test_case

logger = logging.getLogger(__name__)


class NetworkKVPTest(test_case.ResouceDriverNetworkTestCase):
    def setUp(self):
        super(NetworkKVPTest, self).setUp()
        domain_name = 'my-domain'
        proj_name = 'my-proj'
        domain = Domain(domain_name)
        self._vnc_lib.domain_create(domain)
        self._dom_obj = self._vnc_lib.domain_read(id=domain.uuid)
        print 'Created domain ' + domain.uuid

        project = Project(proj_name, domain)
        self._vnc_lib.project_create(project)
        print 'Created Project ' + project.uuid
        self._proj_obj = self._vnc_lib.project_read(id=project.uuid)

        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        self._ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)
        print 'Created network ipam '+ipam.uuid

    def tearDown(self):
        print 'Delete the ipam ' + self._ipam_obj.uuid
        self._vnc_lib.network_ipam_delete(id=self._ipam_obj.uuid)
        print 'Delete the project ' + self._proj_obj.uuid
        self._vnc_lib.project_delete(id=self._proj_obj.uuid)
        print 'Delete the domain ' + self._dom_obj.uuid
        self._vnc_lib.domain_delete(id=self._dom_obj.uuid)
        self._dom_obj = None
        self._proj_obj = None
        self._ipam_obj = None
        super(NetworkKVPTest, self).tearDown()

    def test_network_create(self):
        subnet = '192.168.1.0'
        prefix = 24
        subnet_1 = '10.1.1.0'
        vn_name = 'my-fe'
        print 'Test network create with ipam: Creating network with name ' + vn_name + ' subnets ' + subnet + ', ' + subnet_1
        vn = VirtualNetwork(vn_name, self._proj_obj)
        ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet, prefix))
        ipam_sn_2 = IpamSubnetType(subnet=SubnetType(subnet_1, prefix))
        vn.add_network_ipam(self._ipam_obj, VnSubnetsType([ipam_sn_1, ipam_sn_2]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)
        print 'network created with uuid ' + net_obj.uuid
        # Ensure only TWO entry in KV 
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 4)

        # Ensure only one entry in KV 
        key = net_obj.uuid + " " + subnet +'/'+str(prefix)
        subnet_uuid = self._vnc_lib.kv_retrieve(key)
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        key = net_obj.uuid + " " + subnet_1 +'/'+str(prefix)
        subnet_uuid = self._vnc_lib.kv_retrieve(key)
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        # Test delete
        print 'Deleting network with name ' + vn_name 
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        # Ensure all entries are gone
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 0)

        # Test update
        vn_name = 'my-be'
        subnet_2 = '9.1.1.0'
        print 'Test network update: Creating network with name ' + vn_name + ' subnets ' + subnet_2
        vn = VirtualNetwork(vn_name, self._proj_obj)
        ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet_2, prefix))
        vn.add_network_ipam(self._ipam_obj, VnSubnetsType([ipam_sn_1]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)
        print 'network created with uuid ' + net_obj.uuid
        # Ensure only ONE entry in KV 
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 2)
        key = net_obj.uuid + " " + subnet_2 +'/'+str(prefix)
        subnet_uuid = self._vnc_lib.kv_retrieve(key)
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        subnet_3 = '8.1.1.0'
        print 'Update the network with new subnet ' + subnet_3
        ipam_sn_2 = IpamSubnetType(subnet=SubnetType(subnet_3, prefix))
        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                ipam_ref['attr'].set_ipam_subnets([ipam_sn_2])
                net_obj._pending_field_updates.add('network_ipam_refs')
        self._vnc_lib.virtual_network_update(net_obj)
        # Ensure only ONE entry in KV 
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 2)
        key = net_obj.uuid + " " + subnet_3 +'/'+str(prefix)
        subnet_uuid = self._vnc_lib.kv_retrieve(key)
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        key = net_obj.uuid + " " + subnet_2 +'/'+str(prefix)
        try:
            subnet_uuid = self._vnc_lib.kv_retrieve(key)
        except:
            subnet_uuid = None
            pass
        self.assertEqual(subnet_uuid, None)
        # Test delete
        print 'Delete the network ' + vn_name
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 0)

        # Test ref_update
        vn_name = 'my-ref'
        print 'Test network update with ref_update: Creating network with name ' + vn_name
        vn = VirtualNetwork(vn_name, self._proj_obj)
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)
        print 'network created with uuid ' + net_obj.uuid

        # Ensure only ONE entry in KV 
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 0)

        subnet_3 = '99.1.1.0'
        print 'ref_update with subnet ' + subnet_3
        ipam_sn_3 = IpamSubnetType(subnet=SubnetType(subnet_3, prefix))
        self._vnc_lib.ref_update('virtual-network', net_obj.uuid, 'network_ipam_refs', None, self._ipam_obj.get_fq_name(), 'ADD', VnSubnetsType([ipam_sn_3]))
        # Ensure only TWO entries in KV 
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 2)
        key = net_obj.uuid + " " + subnet_3 +'/'+str(prefix)
        subnet_uuid = self._vnc_lib.kv_retrieve(key)
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        print 'ref_update to remove the ipam ref'
        self._vnc_lib.ref_update('virtual-network', net_obj.uuid, 'network_ipam_refs', None, self._ipam_obj.get_fq_name(), 'DELETE')
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 0)

        # Test delete
        print 'Delete the network ' + vn_name
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        kvp = self._vnc_lib.kv_retrieve()
        self.assertEqual(len(kvp), 0)
# end class NetworkKVPTest
