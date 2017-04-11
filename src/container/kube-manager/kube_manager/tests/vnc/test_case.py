import sys
import time
import os

sys.path.append("../../config/common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/container/kube-manager')

import tempfile
from vnc_api.vnc_api import *
from cfgm_common import vnc_cgitb
from kube_manager.kube_manager import *
from kube_manager.common import args as kube_args

class KMTestCase(test_common.TestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(KMTestCase, cls).setUpClass(extra_config_knobs=extra_config)

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(KMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        self._svc_mon_greenlet = gevent.spawn(test_common.launch_svc_monitor,
            self.id(), self._api_server_ip, self._api_server_port)

        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port, extra_config_knobs)

        kube_config = [
            ('DEFAULTS', 'log_file', 'contrail-kube-manager.log'),
            ('VNC', 'vnc_endpoint_ip', self._api_server_ip),
            ('VNC', 'vnc_endpoint_port', self._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('KUBERNETES', 'service_subnets', "10.96.0.0/12"),
            ('KUBERNETES', 'pod_subnets', "10.32.0.0/12"),
            ('KUBERNETES', 'cluster_name', "test-cluster"),
        ]
        self.event_queue = Queue()
        self._km_greenlet = gevent.spawn(test_common.launch_kube_manager,
            self.id(), kube_config, True, self.event_queue)

    def tearDown(self):
        test_common.kill_svc_monitor(self._svc_mon_greenlet)
        test_common.kill_schema_transformer(self._st_greenlet)
        test_common.kill_kube_manager(self._km_greenlet)
        super(KMTestCase, self).tearDown()

    def enqueue_event(self, event):
        self.event_queue.put(event)
        while self.event_queue.empty() is False:
            time.sleep(1)


    def generate_kube_args(self):
        args_str = ""
        kube_config = [
            ('DEFAULTS', 'log_file', 'contrail-kube-manager.log'),
            ('VNC', 'vnc_endpoint_ip', self._api_server_ip),
            ('VNC', 'vnc_endpoint_port', self._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('KUBERNETES', 'service_subnets', "10.96.0.0/12"),
            ('KUBERNETES', 'pod_subnets', "10.32.0.0/12"),
        ]
        vnc_cgitb.enable(format='text')

        with tempfile.NamedTemporaryFile() as conf, tempfile.NamedTemporaryFile() as logconf:
            cfg_parser = test_common.generate_conf_file_contents(kube_config)
            cfg_parser.write(conf)
            conf.flush()

            cfg_parser = test_common.generate_logconf_file_contents()
            cfg_parser.write(logconf)
            logconf.flush()

            args_str = ["-c", conf.name]
            args = kube_args.parse_args(args_str)
            return args

    def create_add_namespace_event(self, name, uuid):
        event = {}
        object = {}
        object['kind'] = 'Namespace'
        object['spec'] = {}
        object['metadata'] = {}
        object['metadata']['name'] = name
        object['metadata']['uid'] = uuid
        event['type'] = 'ADDED'
        event['object'] = object
        return event

    def create_delete_namespace_event(self, name, uuid):
        event = {}
        object = {}
        object['kind'] = 'Namespace'
        object['spec'] = {}
        object['metadata'] = {}
        object['metadata']['name'] = name
        object['metadata']['uid'] = uuid
        event['type'] = 'DELETED'
        event['object'] = object
        return event

    def create_project(self, name):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)

        try:
            uuid = self._vnc_lib.project_create(proj_obj)
            if uuid:
                proj_obj = self._vnc_lib.project_read(id=uuid)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        return proj_obj

    def create_security_group(self, proj_obj):
        DEFAULT_SECGROUP_DESCRIPTION = "Default security group"
        def _get_rule(ingress, sg, prefix, ethertype):
            sgr_uuid = str(uuid.uuid4())
            if sg:
                addr = AddressType(
                    security_group=proj_obj.get_fq_name_str() + ':' + sg)
            elif prefix:
                addr = AddressType(subnet=SubnetType(prefix, 0))
            local_addr = AddressType(security_group='local')
            if ingress:
                src_addr = addr
                dst_addr = local_addr
            else:
                src_addr = local_addr
                dst_addr = addr
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                  protocol='any',
                                  src_addresses=[src_addr],
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=[dst_addr],
                                  dst_ports=[PortType(0, 65535)],
                                  ethertype=ethertype)
            return rule

        rules = []
        rules.append(_get_rule(True, 'default', None, 'IPv4'))
        rules.append(_get_rule(True, 'default', None, 'IPv6'))
        sg_rules = PolicyEntriesType(rules)

        # create security group
        id_perms = IdPermsType(enable=True,
                               description=DEFAULT_SECGROUP_DESCRIPTION)
        sg_obj = SecurityGroup(name='default', parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
        self._vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())
        return sg_obj

    def create_network(self, name, proj_obj):
        vn = VirtualNetwork(name=name, parent_obj=proj_obj,
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())

        except NoIdError:
            # Virtual network does not exist. Create one.
            uuid = self._vnc_lib.virtual_network_create(vn)
            vn_obj = self._vnc_lib.virtual_network_read(id=uuid)
        return vn_obj

    def create_virtual_machine(self, name, vn, ipaddress):
        vm_instance = VirtualMachine(name)
        self._vnc_lib.virtual_machine_create(vm_instance)
        fq_name = [name]
        fq_name.append('0')
        vmi = VirtualMachineInterface(parent_type = 'virtual-machine', fq_name = fq_name)
        vmi.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        ip = InstanceIp(vm_instance.name + '.0')
        ip.set_virtual_machine_interface(vmi)
        ip.set_virtual_network(vn)
        ip.set_instance_ip_address(ipaddress)
        uuid = self._vnc_lib.instance_ip_create(ip)
        return vm_instance

    def vmi_clean(self, vm_instance):
        fq_name = vm_instance.fq_name
        fq_name.append('0')
        try:
            vmi = self._vnc_lib.virtual_machine_interface_read(fq_name = fq_name)
        except NoIdError:
            return

        ips = vmi.get_instance_ip_back_refs()
        for ref in ips:
            self._vnc_lib.instance_ip_delete(id = ref['uuid'])

        self._vnc_lib.virtual_machine_interface_delete(id = vmi.uuid)

    def delete_virtual_machine(self, vm_instance):
        self.vmi_clean(vm_instance)
        self._vnc_lib.virtual_machine_delete(id = vm_instance.uuid)
