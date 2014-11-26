import sys
sys.path.append("../common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/config/schema-transformer/')

from vnc_api.vnc_api import *
import uuid

class STTestCase(test_common.TestCase):
    def setUp(self):
        super(STTestCase, self).setUp()
        self._svc_mon_greenlet = gevent.spawn(test_common.launch_svc_monitor,
            self._api_server_ip, self._api_server_port)
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)

    def tearDown(self):
        self._svc_mon_greenlet.kill()
        self._st_greenlet.kill()
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

    def create_network_policy_with_multiple_rules(self, rules):
        pentrys = []
        for rule in rules:
            src_addr = rule["src"]
            if src_addr["type"] == "vn":
                vn = src_addr["value"]
                addr1 = AddressType(virtual_network=vn.get_fq_name_str())
            else:
                cidr = src_addr["value"].split('/')
                pfx = cidr[0]
                pfx_len = int(cidr[1])
                addr1 = AddressType(subnet=SubnetType(pfx, pfx_len))

            dst_addr = rule["dst"]
            if dst_addr["type"] == "vn":
                vn = dst_addr["value"]
                addr2 = AddressType(virtual_network=vn.get_fq_name_str())
            else:
                cidr = dst_addr["value"].split('/')
                pfx = cidr[0]
                pfx_len = int(cidr[1])
                addr2 = AddressType(subnet=SubnetType(pfx, pfx_len))
            #src_port = rule["src-port"]
            src_port = PortType(-1, 0)
            #dst_port = rule["dst-port"]
            dst_port = PortType(-1, 0)
            action = rule["action"]
            action_list = ActionListType(simple_action=action)
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

    def delete_network_policy(self, policy, auto_policy=False):
        action_list = policy.network_policy_entries.policy_rule[0].action_list
        if action_list:
            for service in action_list.apply_service or []:
                si = self._vnc_lib.service_instance_read(fq_name_str=service)
                st_ref = si.get_service_template_refs()
                st = self._vnc_lib.service_template_read(id=st_ref[0]['uuid'])
                self._vnc_lib.service_instance_delete(id=si.uuid)
                self._vnc_lib.service_template_delete(id=st.uuid)
            # end for service
        # if action_list
        if not auto_policy:
            self._vnc_lib.network_policy_delete(id=policy.uuid)
    # end delete_network_policy(policy)
