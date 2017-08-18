import sys
import time
import uuid
import tempfile

import gevent
from vnc_api import vnc_cgitb
from kube_manager.common import args as kube_args
sys.path.append("../../config/common/tests")
import test_common
sys.path.insert(0, '../../../../build/production/container/kube-manager')
from vnc_api.vnc_api import (
    Project, AddressType, SubnetType, PolicyRuleType, RefsExistError, PortType,
    PolicyEntriesType, IdPermsType, SecurityGroup, VirtualNetwork,
    VirtualNetworkType, NoIdError, VirtualMachine, VirtualMachineInterface,
    InstanceIp)


class KMTestCase(test_common.TestCase):

    DEFAULT_SECGROUP_DESCRIPTION = "Default security group"

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(KMTestCase, cls).setUpClass(extra_config_knobs=extra_config)

        test_common.ErrorInterceptingLogger.reset()

        cls._svc_mon_greenlet = gevent.spawn(
            test_common.launch_svc_monitor,
            cls._cluster_id,
            cls.__name__,
            cls._api_server_ip,
            cls._api_server_port,
            logger_class=
            test_common.ErrorInterceptingLogger.get_qualified_name())
        cls._st_greenlet = gevent.spawn(
            test_common.launch_schema_transformer,
            cls._cluster_id,
            cls.__name__,
            cls._api_server_ip,
            cls._api_server_port,
            extra_args="--logger_class {} ".format(
                test_common.ErrorInterceptingLogger.get_qualified_name()))
        test_common.wait_for_schema_transformer_up()

        cls.event_queue = gevent.queue.Queue()
        cls.spawn_kube_manager()

    @classmethod
    def spawn_kube_manager(cls, extra_args=()):
        kube_config = [
            ('DEFAULTS', 'log_file', 'contrail-kube-manager.log'),
            ('DEFAULTS', 'logger_class',
             test_common.ErrorInterceptingLogger.get_qualified_name()),
            ('VNC', 'vnc_endpoint_ip', cls._api_server_ip),
            ('VNC', 'vnc_endpoint_port', cls._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('VNC', 'cluster_id', cls._cluster_id),
            ('KUBERNETES', 'service_subnets', "10.96.0.0/12"),
            ('KUBERNETES', 'pod_subnets', "10.32.0.0/12"),
            ('KUBERNETES', 'cluster_name', "test-cluster"),
        ]
        kube_config.extend(extra_args)
        cls._km_greenlet = gevent.spawn(
            test_common.launch_kube_manager,
            cls.__name__,
            kube_config,
            True,
            cls.event_queue)
        test_common.wait_for_kube_manager_up()

    @classmethod
    def tearDownClass(cls):
        test_common.kill_svc_monitor(cls._svc_mon_greenlet)
        test_common.kill_schema_transformer(cls._st_greenlet)
        cls.kill_kube_manager()
        super(KMTestCase, cls).tearDownClass()

        exceptions = test_common.ErrorInterceptingLogger.get_exceptions()
        if exceptions:
            raise AssertionError(
                "Tracebacks found in logs:\n\n{}".format(
                    "\n\n".join(msg for msg, _, __ in exceptions)))

    @classmethod
    def kill_kube_manager(cls):
        test_common.kill_kube_manager(cls._km_greenlet)

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(KMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(KMTestCase, self).tearDown()

    def enqueue_event(self, event):
        self.event_queue.put(event)

    def wait_for_all_tasks_done(self):
        self.enqueue_idle_event()
        while self.event_queue.empty() is False:
            time.sleep(1)

    def enqueue_idle_event(self):
        idle_event = {'type': None,
                      'object': {'kind': 'Idle', 'metadata': {'name': None, 'uid': None}}}
        self.event_queue.put(idle_event)

    def generate_kube_args(self):
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

            args = kube_args.parse_args(["-c", conf.name])
            return args

    @staticmethod
    def create_add_namespace_event(name, uid):
        return KMTestCase.create_namespace_event(name, uid, type='ADDED')

    @staticmethod
    def create_delete_namespace_event(name, uid):
        return KMTestCase.create_namespace_event(name, uid, type='DELETED')

    @staticmethod
    def create_namespace_event(name, uid, type):
        return KMTestCase.create_event(
            kind='Namespace',
            spec={},
            meta={
                'name': name,
                'uid': uid},
            type=type)

    @staticmethod
    def create_event(kind, spec, meta, type):
        return {
            'type': type,
            'object': {
                'kind': kind,
                'spec': spec,
                'metadata': meta
            }
        }

    def create_project(self, name):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)

        try:
            uid = self._vnc_lib.project_create(proj_obj)
            if uid:
                proj_obj = self._vnc_lib.project_read(id=uid)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        return proj_obj

    def create_security_group(self, proj_obj):
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
                               description=KMTestCase.DEFAULT_SECGROUP_DESCRIPTION)
        sg_obj = SecurityGroup(name='default', parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
        self._vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())
        return sg_obj

    def create_network(self, name, proj_obj):
        vn = VirtualNetwork(
            name=name,
            parent_obj=proj_obj,
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())

        except NoIdError:
            # Virtual network does not exist. Create one.
            uid = self._vnc_lib.virtual_network_create(vn)
            vn_obj = self._vnc_lib.virtual_network_read(id=uid)
        return vn_obj

    def create_virtual_machine(self, name, vn, ipaddress):
        vm_instance = VirtualMachine(name)
        self._vnc_lib.virtual_machine_create(vm_instance)
        fq_name = [name, '0']
        vmi = VirtualMachineInterface(parent_type='virtual-machine', fq_name=fq_name)
        vmi.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        ip = InstanceIp(vm_instance.name + '.0')
        ip.set_virtual_machine_interface(vmi)
        ip.set_virtual_network(vn)
        ip.set_instance_ip_address(ipaddress)
        self._vnc_lib.instance_ip_create(ip)
        return vm_instance

    def vmi_clean(self, vm_instance):
        fq_name = vm_instance.fq_name
        fq_name.append('0')
        try:
            vmi = self._vnc_lib.virtual_machine_interface_read(fq_name=fq_name)
        except NoIdError:
            return

        ips = vmi.get_instance_ip_back_refs()
        for ref in ips:
            self._vnc_lib.instance_ip_delete(id=ref['uuid'])

        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)

    def delete_virtual_machine(self, vm_instance):
        self.vmi_clean(vm_instance)
        self._vnc_lib.virtual_machine_delete(id=vm_instance.uuid)
