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
            self.id(), self._api_server_ip, self._api_server_port)
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port)

    def tearDown(self):
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

            # end for service
        # if action_list
        if not auto_policy:
            self._vnc_lib.network_policy_delete(id=policy.uuid)
    # end delete_network_policy
