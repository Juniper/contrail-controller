import gc
import sys
import time
import uuid
import tempfile

import gevent
from greenlet import greenlet
from cfgm_common import vnc_cgitb
sys.path.append("../../config/common/tests")
import test_common
sys.path.insert(0, '../../../../build/production/container/kube-manager')
from vnc_api.vnc_api import (
    Project, AddressType, SubnetType, PolicyRuleType, RefsExistError, PortType,
    PolicyEntriesType, IdPermsType, SecurityGroup, VirtualNetwork,
    VirtualNetworkType, NoIdError, VirtualMachine, VirtualMachineInterface,
    InstanceIp, NetworkIpam, IpamSubnets, IpamSubnetType, VnSubnetsType,
    VirtualRouter)
from kube_manager.common import args as kube_args
from kube_manager.vnc import vnc_kubernetes
from kube_manager.vnc import vnc_kubernetes_config as vnc_kube_config
from kube_manager.vnc.config_db import (
    VirtualMachineInterfaceKM, InstanceIpKM, VirtualMachineKM, VirtualRouterKM)


class KMTestCase(test_common.TestCase):

    DEFAULT_SECGROUP_DESCRIPTION = "Default security group"

    @classmethod
    def setUpClass(cls, extra_config_knobs=None, kube_args=()):
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

        cls.vnc_kubernetes_config_dict = None
        cls.event_queue = gevent.queue.Queue()
        cls.spawn_kube_manager(extra_args=kube_args)
        cls.kubernetes_node_ip = '192.168.0.1'

    @classmethod
    def spawn_kube_manager(cls, extra_args=()):
        kube_config = [
            ('DEFAULTS', 'log_file', 'contrail-kube-manager.log'),
            ('DEFAULTS', 'logger_class',
             test_common.ErrorInterceptingLogger.get_qualified_name()),
            ('DEFAULTS', 'nested_mode', '0'),
            ('DEFAULTS', 'kube_timer_interval', '0'),
            ('VNC', 'vnc_endpoint_ip', cls._api_server_ip),
            ('VNC', 'vnc_endpoint_port', cls._api_server_port),
            ('VNC', 'db_driver', 'cassandra'),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('VNC', 'cluster_id', cls._cluster_id),
            ('KUBERNETES', 'service_subnets', "10.96.0.0/12"),
            ('KUBERNETES', 'ip_fabric_subnets', "10.64.0.0/12"),
            ('KUBERNETES', 'pod_subnets', "10.32.0.0/12"),
            ('KUBERNETES', 'cluster_name', cls.cluster_name()),
        ]
        kube_config.extend(extra_args)
        cls._km_greenlet = gevent.spawn(
            test_common.launch_kube_manager,
            cls.__name__,
            kube_config,
            True,
            cls.event_queue,
            cls.vnc_kubernetes_config_dict)
        test_common.wait_for_kube_manager_up()

    @classmethod
    def tearDownClass(cls):
        test_common.kill_svc_monitor(cls._svc_mon_greenlet)
        test_common.kill_schema_transformer(cls._st_greenlet)
        cls.kill_kube_manager()
        super(KMTestCase, cls).tearDownClass()

        # Kill all remaining greenlets except for the main one
        gevent.killall(
            x for x in gc.get_objects()
            if isinstance(x, gevent.Greenlet) and x != greenlet.getcurrent())

        exceptions = test_common.ErrorInterceptingLogger.get_exceptions()
        if exceptions:
            raise AssertionError(
                "Tracebacks found in logs (count={}):\n\n{}".format(
                    len(exceptions),
                    "\n\n".join(msg for msg, _, __ in exceptions)))

    @classmethod
    def kill_kube_manager(cls):
        cls.vnc_kubernetes_config_dict = \
            vnc_kube_config.VncKubernetesConfig.vnc_kubernetes_config
        test_common.kill_kube_manager(cls._km_greenlet)

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(KMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(KMTestCase, self).tearDown()

    def enqueue_event(self, event):
        self.event_queue.put(event)

    def wait_for_all_tasks_done(self, sleep_interval=1):
        self.enqueue_idle_event()
        while self.event_queue.empty() is False:
            time.sleep(sleep_interval)

    def enqueue_idle_event(self):
        idle_event = {'type': None,
                      'object': {
                          'kind': 'Idle', 'metadata': {
                              'name': None, 'uid': None
                              }
                          }
                     }
        self.event_queue.put(idle_event)

    def generate_kube_args(self):
        kube_config = [
            ('DEFAULTS', 'log_file', 'contrail-kube-manager.log'),
            ('VNC', 'vnc_endpoint_ip', self._api_server_ip),
            ('VNC', 'vnc_endpoint_port', self._api_server_port),
            ('VNC', 'db_driver', 'cassandra'),
            ('VNC', 'cassandra_server_list', '10.0.0.0:9160'),
            ('KUBERNETES', 'service_subnets', '10.96.0.0/12'),
            ('KUBERNETES', 'ip_fabric_subnets', "10.64.0.0/12"),
            ('KUBERNETES', 'pod_subnets', '10.32.0.0/12'),
        ]
        vnc_cgitb.enable(format='text')

        with tempfile.NamedTemporaryFile() as conf,\
            tempfile.NamedTemporaryFile() as logconf:
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
        return KMTestCase.create_namespace_event(name, uid, 'ADDED')

    @staticmethod
    def create_delete_namespace_event(name, uid):
        return KMTestCase.create_namespace_event(name, uid, 'DELETED')

    @staticmethod
    def create_namespace_event(name, uid, event_type):
        return KMTestCase.create_event(
            kind='Namespace',
            spec={},
            meta={
                'name': name,
                'uid': uid},
            event_type=event_type)

    @staticmethod
    def create_event(kind, spec, meta, event_type):
        return {
            'type': event_type,
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
        id_perms = IdPermsType(
            enable=True,
            description=KMTestCase.DEFAULT_SECGROUP_DESCRIPTION)
        sg_obj = SecurityGroup(name='default', parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
        self._vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())
        return sg_obj

    def _create_network_ipam(self, name, network_type, subnet, proj_obj,
                             vn_obj=None):
        ipam_obj = NetworkIpam(name=name, parent_obj=proj_obj)
        pfx, pfx_len = subnet.split('/')
        ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
        if network_type == 'flat-subnet':
            ipam_obj.set_ipam_subnet_method('flat-subnet')
            ipam_obj.set_ipam_subnets(IpamSubnets([ipam_subnet]))
        try:
            self._vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
        if vn_obj:
            if network_type == 'flat-subnet':
                vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))
            else:
                vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([ipam_subnet]))
        return ipam_obj

    def create_pod_service_network(self, pod_vn_name, service_vn_name, \
            proj_obj, pod_subnet, service_subnet):
        pod_vn = self.create_network(pod_vn_name, \
            proj_obj, pod_subnet, 'pod-ipam');
        service_vn = self.create_network(service_vn_name, \
            proj_obj, service_subnet, 'service-ipam');
        return pod_vn, service_vn

    def create_network(self, name, proj_obj, subnet, ipam_name):
        vn = VirtualNetwork(
            name=name, parent_obj=proj_obj,
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='user-defined-subnet-only')

        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())
        except NoIdError:
            # Virtual network does not exist. Create one.
            vn_uuid = self._vnc_lib.virtual_network_create(vn)
            vn_obj = self._vnc_lib.virtual_network_read(id=vn_uuid)

        ipam_obj = self._create_network_ipam(ipam_name, 'flat-subnet',
                                                 subnet, proj_obj, vn_obj)
        try:
            self._vnc_lib.virtual_network_update(vn_obj)
        except Exception as e:
            self.logger.error("%s - failed to update virtual network %s %s. %s"
                              % (self._name, vn_obj.uuid, str(vn_obj.fq_name),
                                 str(e)))

        vn_obj = self._vnc_lib.virtual_network_read(
            fq_name=vn_obj.get_fq_name())
        #kube = vnc_kubernetes.VncKubernetes.get_instance()
        #kube._create_cluster_service_fip_pool(vn_obj, pod_ipam_obj)

        return vn_obj

    @classmethod
    def create_virtual_router(cls, name, node_ip=None):
        vrouter_node_ip = node_ip if node_ip else cls.get_kubernetes_node_ip()
        vrouter_obj = VirtualRouter(
            name,
            virtual_router_ip_address=vrouter_node_ip)
        try:
            vrouter_obj = cls._vnc_lib.virtual_router_read(
                fq_name=vrouter_obj.get_fq_name())
        except NoIdError:
            cls._vnc_lib.virtual_router_create(vrouter_obj)
            VirtualRouterKM.locate(vrouter_obj.uuid)
        return vrouter_obj

    @classmethod
    def delete_virtual_router(cls, vrouter_uuid):
        try:
            cls._vnc_lib.virtual_router_delete(id=vrouter_uuid)
        except NoIdError:
            pass
        VirtualRouterKM.delete(vrouter_uuid)

    def create_virtual_machine(self, name, vn, ipaddress):
        vm = VirtualMachine(name)
        self._vnc_lib.virtual_machine_create(vm)
        VirtualMachineKM.locate(vm.uuid)

        vmi = VirtualMachineInterface(
            parent_type='virtual-machine', fq_name=[name, '0'])
        vmi.set_virtual_machine(vm)
        vmi.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        VirtualMachineInterfaceKM.locate(vmi.uuid)

        ip = InstanceIp(vm.name + '.0')
        ip.set_virtual_machine_interface(vmi)
        ip.set_virtual_network(vn)
        ip.set_instance_ip_address(ipaddress)
        self._vnc_lib.instance_ip_create(ip)
        InstanceIpKM.locate(ip.uuid)

        return vm, vmi, ip

    def vmi_clean(self, vm_instance):
        for vmi in vm_instance.get_virtual_machine_interface_back_refs() or ():
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi['uuid'])
            for ref in vmi.get_instance_ip_back_refs() or ():
                self._vnc_lib.instance_ip_delete(id=ref['uuid'])
            self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)

    def delete_virtual_machine(self, vm_instance=None, vm_id=None):
        if vm_instance is None:
            vm_instance = self._vnc_lib.virtual_machine_read(id=vm_id)

        self.vmi_clean(vm_instance)
        self._vnc_lib.virtual_machine_delete(id=vm_instance.uuid)

    @classmethod
    def get_kubernetes_node_ip(cls):
        return cls.kubernetes_node_ip

    @staticmethod
    def cluster_name():
        return "cluster"
