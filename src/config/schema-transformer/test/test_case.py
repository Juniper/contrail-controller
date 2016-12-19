import sys
from gevent import sleep

sys.path.append("../common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/config/schema-transformer/')

from vnc_api.vnc_api import *


def retry_exc_handler(tries_remaining, exception, delay):
    print >> sys.stderr, "."


def retries(max_tries, delay=1, backoff=1, exceptions=(Exception,),
            hook=retry_exc_handler):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = range(max_tries)
            tries.reverse()
            for tries_remaining in tries:
                try:
                   return func(*args, **kwargs)
                except exceptions as e:
                    if tries_remaining > 0:
                        if hook is not None:
                            hook(tries_remaining, e, mydelay)
                        sleep(mydelay)
                        mydelay = mydelay * backoff
                    else:
                        raise
                else:
                    break
        return f2
    return dec


class VerifyCommon(object):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def wait_to_get_object(self, obj_class, obj_name):
        if obj_name not in obj_class._dict:
            raise Exception('%s not found' % obj_name)

    @retries(5)
    def wait_to_delete_object(self, obj_class, obj_name):
        if obj_name in obj_class._dict:
            raise Exception('%s still found' % obj_name)


class STTestCase(test_common.TestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(STTestCase, cls).setUpClass(extra_config_knobs=extra_config)

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(STTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        self._svc_mon_greenlet = gevent.spawn(test_common.launch_svc_monitor,
            self.id(), self._api_server_ip, self._api_server_port)
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port, extra_config_knobs)

    def tearDown(self):
        self.check_ri_is_deleted(fq_name=['default-domain', 'default-project', 'svc-vn-left', 'svc-vn-left'])
        self.check_ri_is_deleted(fq_name=['default-domain', 'default-project', 'svc-vn-right', 'svc-vn-right'])
        test_common.kill_svc_monitor(self._svc_mon_greenlet)
        test_common.kill_schema_transformer(self._st_greenlet)
        super(STTestCase, self).tearDown()

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

    def frame_rule_addresses(self, addr):
        addrs = [addr] if isinstance(addr, dict) else addr
        rule_kwargs = {}
        for addr in addrs:
            if addr["type"] == "vn":
                vn = addr["value"]
                rule_kwargs.update({'virtual_network' : vn.get_fq_name_str()})
            elif addr["type"] == "cidr_list":
                subnets = []
                for cidr in addr["value"]:
                    pfx, pfx_len = cidr.split('/')
                    subnets.append(SubnetType(pfx, int(pfx_len)))
                rule_kwargs.update({'subnet_list' : subnets})
            else:
                cidr = addr["value"].split('/')
                pfx = cidr[0]
                pfx_len = int(cidr[1])
                rule_kwargs.update({'subnet' : SubnetType(pfx, pfx_len)})
        rule_addr = AddressType(**rule_kwargs)
        return rule_addr

    def get_service_list(self, rule):
        service_name_list = []
        service_list = rule.get('service_list', None)
        if service_list:
            src_addresses = [rule['src']] if isinstance(rule['src'], dict) else rule['src']
            dst_addresses = [rule['dst']] if isinstance(rule['dst'], dict) else rule['dst']
            vn1_name = [addr["value"].get_fq_name_str()\
                        for addr in src_addresses if addr["type"] == "vn"][0]
            vn2_name = [addr["value"].get_fq_name_str()\
                        for addr in dst_addresses if addr["type"] == "vn"][0]
            for service in service_list:
                service_name_list.append(self._create_service(
                    [('left', vn1_name), ('right', vn2_name)], service,
                    rule['auto_policy'], **rule['service_kwargs']))
        return service_name_list

    def get_mirror_service(self, rule):
        mirror_si = None
        mirror_service = rule.get('mirror_service', None)
        if mirror_service:
            src_addresses = [rule['src']] if isinstance(rule['src'], dict) else rule['src']
            dst_addresses = [rule['dst']] if isinstance(rule['dst'], dict) else rule['dst']
            vn1_name = [addr["value"].get_fq_name_str()\
                        for addr in src_addresses if addr["type"] == "vn"][0]
            vn2_name = [addr["value"].get_fq_name_str()\
                        for addr in dst_addresses if addr["type"] == "vn"][0]
            mirror_si = self._create_service(
                [('left', vn1_name), ('right', vn2_name)], mirror_service, False,
                service_mode='transparent', service_type='analyzer')
        return mirror_si

    def create_network_policy_with_multiple_rules(self, rules):
        pentrys = []
        for rule in rules:
            addr1 = self.frame_rule_addresses(rule["src"])
            addr2 = self.frame_rule_addresses(rule["dst"])
            service_list = self.get_service_list(rule)
            mirror_service = self.get_mirror_service(rule)
            src_port = rule.get("src-port", PortType(-1, -1))
            dst_port = rule.get("dst-port", PortType(-1, -1))
            action_list = ActionListType()
            if mirror_service:
                mirror = MirrorActionType(analyzer_name=mirror_service)
                action_list.mirror_to=mirror
            if service_list:
                action_list.apply_service=service_list
            else:
                action_list.simple_action=rule["action"]
            prule = PolicyRuleType(direction=rule["direction"], protocol=rule["protocol"],
                               src_addresses=[addr1], dst_addresses=[addr2],
                               src_ports=[src_port], dst_ports=[dst_port],
                               action_list=action_list)
            pentrys.append(prule)

        pentry = PolicyEntriesType(pentrys)
        np = NetworkPolicy(str(uuid.uuid4()), network_policy_entries=pentry)
        self._vnc_lib.network_policy_create(np)
        return np
    # end create_network_policy_with_multiple_rules

    def delete_service(self, service):
        si = self._vnc_lib.service_instance_read(fq_name_str=service)
        st_ref = si.get_service_template_refs()
        st = self._vnc_lib.service_template_read(id=st_ref[0]['uuid'])

        if st.service_template_properties.version == 2:
            for pt_ref in si.get_port_tuples() or []:
                pt = self._vnc_lib.port_tuple_read(id=pt_ref['uuid'])
                for vmi_ref in pt.get_virtual_machine_interface_back_refs() or []:
                    self._vnc_lib.virtual_machine_interface_delete(id=vmi_ref['uuid'])
                self._vnc_lib.port_tuple_delete(id=pt_ref['uuid'])

        self._vnc_lib.service_instance_delete(id=si.uuid)
        self._vnc_lib.service_template_delete(id=st.uuid)

        if st.service_template_properties.service_virtualization_type == 'physical-device':
            sa_set_ref = st.get_service_appliance_set_refs()[0]
            sa_set = self._vnc_lib.service_appliance_set_read(id=sa_set_ref['uuid'])
            pr_list = set()
            pi_list = set()
            for sa_ref in sa_set.get_service_appliances() or []:
                sa = self._vnc_lib.service_appliance_read(id=sa_ref['uuid'])
                self._vnc_lib.service_appliance_delete(id=sa.uuid)
                for pi_ref in sa.get_physical_interface_refs() or []:
                    pi_list.add(pi_ref['uuid'])
            self._vnc_lib.service_appliance_set_delete(id=sa_set.uuid)
            for pi_id in pi_list:
                pi = self._vnc_lib.physical_interface_read(id=pi_id)
                pr_list.add(pi.parent_uuid)
                self._vnc_lib.physical_interface_delete(id=pi_id)
            for pr in pr_list:
                self._vnc_lib.physical_router_delete(id=pr)
    # delete_service

    def delete_network_policy(self, policy, auto_policy=False):
        action_list = policy.network_policy_entries.policy_rule[0].action_list
        if action_list:
            service_list = action_list.apply_service or []
            if action_list.mirror_to and action_list.mirror_to.analyzer_name:
                service_list.append(action_list.mirror_to.analyzer_name)
            for service in service_list:
                self.delete_service(service)
            # end for service
        # if action_list
        if not auto_policy:
            self._vnc_lib.network_policy_delete(id=policy.uuid)

    # end delete_network_policy
