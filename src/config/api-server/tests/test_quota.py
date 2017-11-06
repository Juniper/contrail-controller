#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import sys
import uuid
import logging

from testtools.matchers import MismatchError
from testtools import ExpectedException

from vnc_api.vnc_api import *
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestQuota(test_case.ApiServerTestCase):

    def __init__(self, *args, **kwargs):
        super(TestQuota, self).__init__(*args, **kwargs)
        self._port_quota = 3
        self._fip_quota = 3

    def test_create_vmi_with_quota_in_parallels(self, project=None):
        vn_name = 'test-net' + str(uuid.uuid4())
        if project is None:
            proj_name = 'admin' + self.id()
            kwargs = {'quota':{'virtual_machine_interface': self._port_quota}}
            project = Project(proj_name, **kwargs)
            self._vnc_lib.project_create(project)
        vn = VirtualNetwork(vn_name, project)
        self._vnc_lib.virtual_network_create(vn)
        vmi_list=[]
        for i in xrange(self._port_quota+1):
            vmi_list.insert(i,VirtualMachineInterface(str(uuid.uuid4()), project))
            vmi_list[i].uuid = vmi_list[i].name
            vmi_list[i].set_virtual_network(vn)
        result = {'ok': 0, 'exception': 0}
        def create_port(vmi):
            try:
                self._vnc_lib.virtual_machine_interface_create(vmi)
            except OverQuota:
                result['exception'] +=1
                return
            result['ok'] +=1
        threads_vmi = [gevent.spawn(create_port, vmi_list[i]) for i in xrange(self._port_quota+1)]
        gevent.joinall(threads_vmi)
        self.assertEqual(result['ok'], self._port_quota)
        self.assertEqual(result['exception'], 1)
        # Cleanup
        # Also test for zk counter delete functionality
        for vmi in vmi_list:
            try:
                self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
            except NoIdError:
                pass

    def test_create_fip_with_quota_in_parallels(self):
        proj_name = 'admin' + self.id()
        vn_name = 'test-net'
        kwargs = {'quota':{'floating_ip': self._fip_quota}}
        project = Project(proj_name, **kwargs)
        self._vnc_lib.project_create(project)
        ipam_obj = NetworkIpam('ipam-%s' %(self.id()), project)
        self._vnc_lib.network_ipam_create(ipam_obj)
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        vn = VirtualNetwork('vn-%s' %(self.id()), project)
        vn.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        fip_pool_obj = FloatingIpPool(
            'fip-pool-%s' %(self.id()), parent_obj=vn)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip=[]
        for i in xrange(self._fip_quota+1):
            fip.append(FloatingIp(str(uuid.uuid4()), fip_pool_obj))
            fip[i].add_project(project)
            fip[i].uuid = fip[i].name
        result = {'ok': 0, 'exception': 0}
        def create_fip(fip):
            try:
                self._vnc_lib.floating_ip_create(fip)
            except OverQuota:
                result['exception'] +=1
                return
            result['ok'] +=1
        threads_fip = [gevent.spawn(create_fip, fip[i]) for i in xrange(self._fip_quota+1)]
        gevent.joinall(threads_fip)
        self.assertEqual(result['ok'], self._fip_quota)
        self.assertEqual(result['exception'], 1)
    # end test_create_fip_with_quota_in_parallels

    def test_update_quota_and_create_resource_negative(self):
        proj_name = 'admin' + self.id()
        kwargs = {'quota':{'virtual_machine_interface': self._port_quota}}
        project = Project(proj_name, **kwargs)
        self._vnc_lib.project_create(project)
        # Creating 4 vmis should fail as the quota is set to 3
        self.test_create_vmi_with_quota_in_parallels(project=project)

        # Update quota of vmi from 3->4
        kwargs = {'virtual_machine_interface': 4}
        quota = QuotaType(**kwargs)
        project.set_quota(quota)
        self._vnc_lib.project_update(project)
        with ExpectedException(MismatchError) as e:
            # Creating 4 vmis should be a success now
            # Expect MismatchError on asserEqual(result['exception'], 1)
            self.test_create_vmi_with_quota_in_parallels(project=project)
    # end test_update_quota_and_create_resource_negative
# class TestQuota


class TestGlobalQuota(test_case.ApiServerTestCase):

    global_quotas = {'security_group_rule' : 5}

    def __init__(self, *args, **kwargs):
        super(TestGlobalQuota, self).__init__(*args, **kwargs)

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = []
        for k, v in cls.global_quotas.items():
            extra_config_knobs.append(('QUOTA', k, v))
        kwargs.update({'extra_config_knobs' : extra_config_knobs})
        super(TestGlobalQuota, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestGlobalQuota, cls).tearDownClass(*args, **kwargs)

    def test_security_group_rule_global_quota(self):
        logger.info("Test#1: Create a security group with one rule")
        sg_name = '%s-first-sg' % self.id()
        sg_obj = SecurityGroup(sg_name)
        rule = {'port_min': 1,
                'port_max': 1,
                'direction': 'egress',
                'ip_prefix': None,
                'protocol': 'any',
                'ether_type': 'IPv4'}
        sg_rule = self._security_group_rule_build(
                rule, "default-domain:default-project:%s" % sg_name)
        self._security_group_rule_append(sg_obj, sg_rule)
        self._vnc_lib.security_group_create(sg_obj)
        # make sure expected number of rules are present
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self.assertEqual(1,
                len(sg_obj.get_security_group_entries().get_policy_rule()))
        quota_counters = self._server_info['api_server'].quota_counter
        quota_counter_keys = quota_counters.keys()
        # make sure one counter is initialized
        self.assertEqual(len(quota_counter_keys), 1)
        # make sure sgr quota counter is incremented
        sgr_quota_counter = quota_counters[quota_counter_keys[0]]
        self.assertEqual(sgr_quota_counter.value, 1)

        logger.info("Test#2: Update sg with rules one less than quota limit.")
        sg_name = '%s-sg' % self.id()
        sg_obj = SecurityGroup(sg_name)
        self._vnc_lib.security_group_create(sg_obj)
        for i in range(2, self.global_quotas['security_group_rule']):
            rule = {'port_min': i,
                    'port_max': i,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, sg_rule)
        self._vnc_lib.security_group_update(sg_obj)
        # make sure expected number of rules are present
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self.assertEqual(3,
                len(sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is incremented
        self.assertEqual(sgr_quota_counter.value, 4)

        logger.info("Test#3: Try updating sg with two rules; one more than quota limit.")
        sg_rules = []
        for i in range(self.global_quotas['security_group_rule'],
                self.global_quotas['security_group_rule'] + 2):
            rule = {'port_min': i,
                    'port_max': i,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, sg_rule)
            sg_rules.append(sg_rule)
        with ExpectedException(OverQuota) as e:
            self._vnc_lib.security_group_update(sg_obj)
        # make sure sgr quota counter is not incremented
        self.assertEqual(sgr_quota_counter.value, 4)

        logger.info("Test#4: Error during SG update with rule.")
        api_server = self._server_info['api_server']
        orig_dbe_update = api_server._db_conn.dbe_update
        try:
            def err_dbe_update(*args, **kwargs):
                return False, (501, "Fake Not Implemented")
            api_server._db_conn.dbe_update = err_dbe_update
            # Read the latest sb obj
            sg_obj = self._vnc_lib.security_group_read(
                    ["default-domain", "default-project", sg_name])
            port = self.global_quotas['security_group_rule']
            rule = {'port_min': port,
                    'port_max': port,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            last_sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, last_sg_rule)
            self._vnc_lib.security_group_update(sg_obj)
        except Exception as e:
            # Make sure expected fake return code is received
            self.assertEqual(e.status_code, 501)
        finally:
            api_server._db_conn.dbe_update = orig_dbe_update
        # make sure expected number of rules are present
        new_sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self.assertEqual(3,
                len(new_sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is unchanged
        self.assertEqual(sgr_quota_counter.value, 4)

        logger.info("Test#5: Update sg with one rule to reach quota limit.")
        # Read the latest sb obj
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        port = self.global_quotas['security_group_rule']
        rule = {'port_min': port,
                'port_max': port,
                'direction': 'egress',
                'ip_prefix': None,
                'protocol': 'any',
                'ether_type': 'IPv4'}
        last_sg_rule = self._security_group_rule_build(
                rule, "default-domain:default-project:%s" % sg_name)
        self._security_group_rule_append(sg_obj, last_sg_rule)
        self._vnc_lib.security_group_update(sg_obj)
        # make sure expected number of rules are present
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self.assertEqual(4,
                len(sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is incremented
        self.assertEqual(sgr_quota_counter.value, 5)

        logger.info("Test#6: Try updating sg with one rule more than quota limit.")
        with ExpectedException(OverQuota) as e:
            port = self.global_quotas['security_group_rule'] + 1
            rule = {'port_min': port,
                    'port_max': port,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, sg_rule)
            self._vnc_lib.security_group_update(sg_obj)
        # make sure sgr quota counter is not incremented
        self.assertEqual(sgr_quota_counter.value, 5)

        logger.info("Test#7: Creating one more security group object with rule entries.")
        new_sg_name = '%s-one_more_sg' % self.id()
        new_sg_obj = SecurityGroup(new_sg_name)
        with ExpectedException(OverQuota) as e:
            port = self.global_quotas['security_group_rule'] + 1
            rule = {'port_min': port,
                    'port_max': port,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            new_sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % new_sg_name)
            self._security_group_rule_append(new_sg_obj, sg_rule)
            self._vnc_lib.security_group_create(new_sg_obj)
        # make sure sgr quota counter is not incremented
        self.assertEqual(sgr_quota_counter.value, 5)

        logger.info("Test#7: Remove the last rule from SG")
        # Read the latest sb obj
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self._security_group_rule_remove(sg_obj, last_sg_rule)
        self._vnc_lib.security_group_update(sg_obj)
        # make sure expected number of rules are present
        sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", sg_name])
        self.assertEqual(3,
                len(sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is decremented
        self.assertEqual(sgr_quota_counter.value, 4)

        logger.info("Test#8: Error during SG create with rule.")
        api_server = self._server_info['api_server']
        orig_dbe_create = api_server._db_conn.dbe_create
        try:
            def err_dbe_create(*args, **kwargs):
                return False, (501, "Fake Not Implemented")
            api_server._db_conn.dbe_create = err_dbe_create
            self._vnc_lib.security_group_create(new_sg_obj)
        except Exception as e:
            # Make sure expected fake return code is received
            self.assertEqual(e.status_code, 501)
        finally:
            api_server._db_conn.dbe_create = orig_dbe_create
        # make sure SG is not created
        with ExpectedException(NoIdError) as e:
            self._vnc_lib.security_group_read(
                    ["default-domain", "default-project", new_sg_name])
        # make sure sgr quota counter is unchanged
        self.assertEqual(sgr_quota_counter.value, 4)

        logger.info("Test9: Create new SG with one rule.")
        self._vnc_lib.security_group_create(new_sg_obj)
        # make sure expected number of rules are present
        new_sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", new_sg_name])
        self.assertEqual(1,
                len(new_sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is incremented
        self.assertEqual(sgr_quota_counter.value, 5)

        logger.info("Test#10: Error during SG delete with rule.")
        api_server = self._server_info['api_server']
        orig_dbe_delete = api_server._db_conn.dbe_delete
        try:
            def err_dbe_delete(*args, **kwargs):
                return False, (501, "Fake Not Implemented")
            api_server._db_conn.dbe_delete = err_dbe_delete
            self._vnc_lib.security_group_delete(
                     ["default-domain", "default-project", new_sg_name])
        except Exception as e:
            # Make sure expected fake return code is received
            self.assertEqual(e.status_code, 501)
        finally:
            api_server._db_conn.dbe_delete = orig_dbe_delete
        # make sure expected number of rules are present
        new_sg_obj = self._vnc_lib.security_group_read(
                ["default-domain", "default-project", new_sg_name])
        self.assertEqual(1,
                len(new_sg_obj.get_security_group_entries().get_policy_rule()))
        # make sure sgr quota counter is unchanged
        self.assertEqual(sgr_quota_counter.value, 5)
    #end TestGlobalQuota
