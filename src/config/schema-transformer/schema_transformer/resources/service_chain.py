#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import InstanceTargetType
from vnc_api.gen.resource_xsd import PolicyBasedForwardingRuleType
import jsonpickle
import copy
import uuid
from cfgm_common.uve.virtual_network.ttypes import UveServiceChainData
from cfgm_common.uve.virtual_network.ttypes import UveServiceChain
from schema_transformer.sandesh.st_introspect import ttypes as sandesh


class ServiceChain(ResourceBaseST):
    _dict = {}
    obj_type = 'service_chain'

    @classmethod
    def init(cls):
        # When schema transformer restarts, read all service chains from cassandra
        for (name, columns) in cls._object_db.list_service_chain_uuid():
            chain = jsonpickle.decode(columns['value'])

            # Some service chains may not be valid any more. We may need to
            # delete such service chain objects or we have to destroy them.
            # To handle each case, we mark them with two separate flags,
            # which will be reset when appropriate calls are made.
            # Any service chains for which these flags are still set after
            # all objects are read from database, we will delete/destroy them
            chain.present_stale = True
            chain.created_stale = chain.created
            if not hasattr(chain, 'partially_created'):
                chain.partially_created = False
            if not hasattr(chain, 'si_info'):
                chain.si_info = None
            cls._dict[name] = chain
        cls.sc_ipam_obj = None
        cls._get_service_chain_ipam()
    # end init

    @classmethod
    def _get_service_chain_ipam(cls):
        if cls.sc_ipam_obj:
            return cls.sc_ipam_obj
        fq_name = ['default-domain', 'default-project', 'service-chain-flat-ipam']
        cls.sc_ipam_obj = cls._vnc_lib.network_ipam_read(fq_name=fq_name)
        return cls.sc_ipam_obj
    # end _get_service_chain_ipam

    def __init__(self, name, left_vn, right_vn, direction, sp_list, dp_list,
                 protocol, services):
        self.name = name
        self.left_vn = left_vn
        self.right_vn = right_vn
        self.direction = direction
        self.sp_list = sp_list
        self.dp_list = dp_list
        self.service_list = list(services)
        self.si_info = None

        self.protocol = protocol
        self.created = False
        self.partially_created = False

        self.present_stale = False
        self.created_stale = False

        self.error_msg = None
    # end __init__

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.sp_list != other.sp_list:
            return False
        if self.dp_list != other.dp_list:
            return False
        if self.direction != other.direction:
            return False
        if self.service_list != other.service_list:
            return False
        if self.protocol != other.protocol:
            return False
        if self.service_list != other.service_list:
            return False
        return True
    # end __eq__

    @classmethod
    def find(cls, left_vn, right_vn, direction, sp_list, dp_list, protocol,
             service_list):
        for sc in ServiceChain.values():
            if (left_vn == sc.left_vn and
                right_vn == sc.right_vn and
                sp_list == sc.sp_list and
                dp_list == sc.dp_list and
                direction == sc.direction and
                protocol == sc.protocol and
                service_list == sc.service_list):
                    return sc
        # end for sc
        return None
    # end find

    @classmethod
    def find_or_create(cls, left_vn, right_vn, direction, sp_list, dp_list,
                       protocol, service_list):
        sc = cls.find(left_vn, right_vn, direction, sp_list, dp_list, protocol,
                      service_list)
        if sc is not None:
            sc.present_stale = False
            return sc

        name = str(uuid.uuid4())
        sc = ServiceChain(name, left_vn, right_vn, direction, sp_list,
                          dp_list, protocol, service_list)
        ServiceChain._dict[name] = sc
        cls._object_db.add_service_chain_uuid(name, jsonpickle.encode(sc))
        return sc
    # end find_or_create

    def update_ipams(self, vn2_name):
        if not self.created:
            return
        if self.left_vn == vn2_name:
            vn1 = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.right_vn)
        if self.right_vn == vn2_name:
            vn1 = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.left_vn)

        vn2 = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn2_name)
        if vn1 is None or vn2 is None:
            return

        for service in self.service_list:
            service_name = vn1.get_service_name(self.name, service)
            service_ri = ResourceBaseST.get_obj_type_map().get('routing_instance').get(service_name)
            if service_ri is None:
                continue
            service_ri.add_service_info(vn2, service)
            self._vnc_lib.routing_instance_update(service_ri.obj)
        # end for service
    # end update_ipams

    def log_error(self, msg):
        self.error_msg = msg
        self._logger.error('service chain %s: %s' % (self.name, msg))
    # end log_error

    def _get_vm_pt_info(self, vm_pt, mode):
        # From a ResourceBaseST.get_obj_type_map().get('virtual_machine') or ResourceBaseST.get_obj_type_map().get('port_tuple') object, create a vm_info
        # dict to be used during service chain creation
        vm_info = {'vm_uuid': vm_pt.uuid}

        for interface_name in vm_pt.virtual_machine_interfaces:
            interface = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(interface_name)
            if not interface:
                continue
            if not (interface.is_left() or interface.is_right()):
                continue
            v4_addr = None
            v6_addr = None
            if mode != 'transparent':
                v4_addr = interface.get_any_instance_ip_address(4)
                v6_addr = interface.get_any_instance_ip_address(6)
                if v4_addr is None and v6_addr is None:
                    self.log_error("No ip address found for interface "
                                   + interface_name)
                    return None
            vmi_info = {'vmi': interface, 'v4-address': v4_addr,
                        'v6-address': v6_addr}
            vm_info[interface.service_interface_type] = vmi_info

        if 'left' not in vm_info:
            self.log_error('Left interface not found for %s' %
                           vm_pt.name)
            return None
        if ('right' not in vm_info and mode != 'in-network-nat' and
            self.direction == '<>'):
            self.log_error('Right interface not found for %s' %
                           vm_pt.name)
            return None
        return vm_info
    # end _get_vm_pt_info

    def check_create(self):
        # Check if this service chain can be created:
        # - all service instances have VMs
        # - all service instances have proper service mode
        # - all service VMs have left and if needed, right interface
        # If checks pass, return a dict containing mode and vm_list for all
        # service instances so that we don't have to find them again
        ret_dict = {}
        for service in self.service_list:
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(service)
            if si is None:
                self.log_error("Service instance %s not found " % service)
                return None
            vm_list = si.virtual_machines
            pt_list = si.port_tuples
            if not vm_list and not pt_list:
                self.log_error("No vms/pts found for service instance " + service)
                return None
            mode = si.get_service_mode()
            if mode is None:
                self.log_error("service mode not found: %s" % service)
                return None
            vm_info_list = []
            for service_vm in vm_list:
                vm_obj = ResourceBaseST.get_obj_type_map().get('virtual_machine').get(service_vm)
                if vm_obj is None:
                    self.log_error('virtual machine %s not found' % service_vm)
                    return None
                vm_info = self._get_vm_pt_info(vm_obj, mode)
                if vm_info:
                    vm_info_list.append(vm_info)
            for pt in pt_list:
                pt_obj = ResourceBaseST.get_obj_type_map().get('port_tuple').get(pt)
                if pt_obj is None:
                    self.log_error('virtual machine %s not found' % pt)
                    return None
                vm_info = self._get_vm_pt_info(pt_obj, mode)
                if vm_info:
                    vm_info_list.append(vm_info)
            # end for service_vm
            if not vm_info_list:
                return None
            virtualization_type = si.get_virtualization_type()
            ret_dict[service] = {'mode': mode, 'vm_list': vm_info_list,
                                 'virtualization_type': virtualization_type}
        return ret_dict
    # check_create

    def create(self):
        si_info = self.check_create()
        if self.created:
            if self.created_stale:
                self.uve_send()
                self.created_stale = False
            if si_info is None:
                # if previously created but no longer valid, then destroy
                self.destroy()
                return

            # If the VMIs associated with the SC has changed
            # after the SC is created, recreate the SC object.
            if self.si_info != None and si_info != self.si_info:
                self._create(si_info)
                self.si_info = si_info
                if self.partially_created:
                    self.destroy()
                    return
            return

        if si_info is None:
            return
        self._create(si_info)
        if self.partially_created:
            self.destroy()
            return
        self.si_info = si_info
        self.uve_send()
    # end create

    def uve_send(self):
        uve = UveServiceChainData(name=self.name)
        uve.source_virtual_network = self.left_vn
        uve.destination_virtual_network = self.right_vn
        if self.sp_list:
            uve.source_ports = "%d-%d" % (
                self.sp_list[0].start_port, self.sp_list[0].end_port)
        if self.dp_list:
            uve.destination_ports = "%d-%d" % (
                self.dp_list[0].start_port, self.dp_list[0].end_port)
        uve.protocol = self.protocol
        uve.direction = self.direction
        uve.services = copy.deepcopy(self.service_list)
        uve_msg = UveServiceChain(data=uve, sandesh=self._sandesh)
        uve_msg.send(sandesh=self._sandesh)
    # end uve_send

    def _create(self, si_info):
        self.partially_created = True
        vn1_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').locate(self.left_vn)
        vn2_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').locate(self.right_vn)
        if not vn1_obj or not vn2_obj:
            self.log_error("vn1_obj or vn2_obj is None")
            return

        right_si_name = self.service_list[-1]
        right_si = ResourceBaseST.get_obj_type_map().get('service_instance').get(right_si_name)
        multi_policy_enabled = (vn1_obj.multi_policy_service_chains_enabled and
                                vn2_obj.multi_policy_service_chains_enabled and
                                right_si.get_service_mode() != 'in-network-nat')
        service_ri2 = None
        if not multi_policy_enabled:
            service_ri2 = vn1_obj.get_primary_routing_instance()
            if service_ri2 is None:
                self.log_error("primary ri is None for " + self.left_vn)
                return
        first_node = True
        for service in self.service_list:
            service_name1 = vn1_obj.get_service_name(self.name, service)
            service_name2 = vn2_obj.get_service_name(self.name, service)
            has_pnf = (si_info[service]['virtualization_type'] == 'physical-device')
            ri_obj = ResourceBaseST.get_obj_type_map().get('routing_instance').create(service_name1, vn1_obj, has_pnf)
            service_ri1 = ResourceBaseST.get_obj_type_map().get('routing_instance').locate(service_name1, ri_obj)
            if service_ri1 is None:
                self.log_error("service_ri1 is None")
                return
            if service_ri2 is not None:
                service_ri2.add_connection(service_ri1)
            else:
                # add primary ri's target to service ri
                rt_obj = ResourceBaseST.get_obj_type_map().get('route_target').get(vn1_obj.get_route_target())
                service_ri1.obj.add_route_target(rt_obj.obj,
                                                 InstanceTargetType('import'))
                self._vnc_lib.routing_instance_update(service_ri1.obj)

            mode = si_info[service]['mode']
            nat_service = (mode == "in-network-nat")
            transparent = (mode not in ["in-network", "in-network-nat"])
            self._logger.info("service chain %s: creating %s chain" %
                              (self.name, mode))

            if not nat_service:
                ri_obj = ResourceBaseST.get_obj_type_map().get('routing_instance').create(service_name2, vn2_obj, has_pnf)
                service_ri2 = ResourceBaseST.get_obj_type_map().get('routing_instance').locate(service_name2, ri_obj)
                if service_ri2 is None:
                    self.log_error("service_ri2 is None")
                    return
            else:
                service_ri2 = None

            if first_node:
                first_node = False
                rt_list = set(vn1_obj.rt_list)
                if vn1_obj.allow_transit:
                    rt_list.add(vn1_obj.get_route_target())
                service_ri1.update_route_target_list(rt_add_export=rt_list)

            if transparent:
                v4_address, v6_address = vn1_obj.allocate_service_chain_ip(
                    service_name1)
                if v4_address is None and v6_address is None:
                    self.log_error('Cannot allocate service chain ip address')
                    return
                service_ri1.add_service_info(
                    vn2_obj, service, v4_address, v6_address,
                    service_chain_id=self.name)
                if service_ri2 and self.direction == "<>":
                    service_ri2.add_service_info(
                        vn1_obj, service, v4_address, v6_address,
                        service_chain_id=self.name)

            for vm_info in si_info[service]['vm_list']:
                if transparent:
                    result = self.process_transparent_service(
                        vm_info, v4_address, v6_address, service_ri1,
                        service_ri2)
                else:
                    result = self.process_in_network_service(
                        vm_info, service, vn1_obj, vn2_obj, service_ri1,
                        service_ri2, nat_service)
                if not result:
                    return
            self._vnc_lib.routing_instance_update(service_ri1.obj)
            if service_ri2:
                self._vnc_lib.routing_instance_update(service_ri2.obj)

        if service_ri2:
            rt_list = set(vn2_obj.rt_list)
            if vn2_obj.allow_transit:
                rt_list.add(vn2_obj.get_route_target())
            service_ri2.update_route_target_list(rt_add_export=rt_list)

            if not multi_policy_enabled:
                service_ri2.add_connection(
                    vn2_obj.get_primary_routing_instance())
            else:
                # add primary ri's target to service ri
                rt_obj = ResourceBaseST.get_obj_type_map().get('route_target').get(vn2_obj.get_route_target())
                service_ri2.obj.add_route_target(rt_obj.obj,
                                                 InstanceTargetType('import'))
                self._vnc_lib.routing_instance_update(service_ri2.obj)

        self.created = True
        self.partially_created = False
        self.error_msg = None
        self._object_db.add_service_chain_uuid(self.name, jsonpickle.encode(self))
    # end _create

    def add_pbf_rule(self, vmi, ri, v4_address, v6_address, vlan):
        if vmi.service_interface_type not in ["left", "right"]:
            return

        pbf = PolicyBasedForwardingRuleType(
            direction='both', vlan_tag=vlan, service_chain_address=v4_address,
            ipv6_service_chain_address=v6_address)

        if vmi.is_left():
            pbf.set_src_mac('02:00:00:00:00:01')
            pbf.set_dst_mac('02:00:00:00:00:02')
        else:
            pbf.set_src_mac('02:00:00:00:00:02')
            pbf.set_dst_mac('02:00:00:00:00:01')

        vmi.add_routing_instance(ri, pbf)
    # end add_pbf_rule

    def process_transparent_service(self, vm_info, v4_address, v6_address,
                                    service_ri1, service_ri2):
        vlan = self._object_db.allocate_service_chain_vlan(vm_info['vm_uuid'],
                                                           self.name)
        self.add_pbf_rule(vm_info['left']['vmi'], service_ri1,
                          v4_address, v6_address, vlan)
        self.add_pbf_rule(vm_info['right']['vmi'], service_ri2,
                          v4_address, v6_address, vlan)
        return True
    # end process_transparent_service

    def process_in_network_service(self, vm_info, service, vn1_obj, vn2_obj,
                                   service_ri1, service_ri2, nat_service):
        service_ri1.add_service_info(
            vn2_obj, service, vm_info['left'].get('v4-address'),
            vm_info['left'].get('v6-address'),
            vn1_obj.get_primary_routing_instance().get_fq_name_str(),
            service_chain_id=self.name)

        if self.direction == '<>' and not nat_service:
            service_ri2.add_service_info(
                vn1_obj, service, vm_info['right'].get('v4-address'),
                vm_info['right'].get('v6-address'),
                vn2_obj.get_primary_routing_instance().get_fq_name_str(),
                service_chain_id=self.name)
        return True
    # end process_in_network_service

    def destroy(self):
        if not self.created and not self.partially_created:
            return

        self.created = False
        self.partially_created = False
        self._object_db.add_service_chain_uuid(self.name,
                                               jsonpickle.encode(self))

        vn1_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.left_vn)
        vn2_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.right_vn)

        for service in self.service_list:
            if vn1_obj:
                service_name1 = vn1_obj.get_service_name(self.name, service)
                ResourceBaseST.get_obj_type_map().get('routing_instance').delete(service_name1, True)
            if vn2_obj:
                service_name2 = vn2_obj.get_service_name(self.name, service)
                ResourceBaseST.get_obj_type_map().get('routing_instance').delete(service_name2, True)
        # end for service
    # end destroy

    def delete(self):
        if self.created or self.partially_created:
            self.destroy()
        del self._dict[self.name]
        self._object_db.remove_service_chain_uuid(self.name)
    # end delete

    def build_introspect(self):
        sc = sandesh.ServiceChain(sc_name=self.name)
        sc.left_virtual_network = self.left_vn
        sc.right_virtual_network = self.right_vn
        sc.protocol = self.protocol
        port_list = ["%s-%s" % (sp.start_port, sp.end_port)
                     for sp in self.sp_list]
        sc.src_ports = ','.join(port_list)
        port_list = ["%s-%s" % (dp.start_port, dp.end_port)
                     for dp in self.dp_list]
        sc.dst_ports = ','.join(port_list)
        sc.direction = self.direction
        sc.service_list = self.service_list
        sc.created = self.created
        sc.error_msg = self.error_msg
        return sc
    # end build_introspect

    def handle_st_object_req(self):
        resp = super(ServiceChain, self).handle_st_object_req()
        resp.obj_refs = [
            sandesh.RefList('service_instance', self.service_list)
        ]
        resp.properties = [
            sandesh.PropList('left_network', self.left_vn),
            sandesh.PropList('right_network', self.right_vn),
            sandesh.PropList('protocol', self.protocol),
            sandesh.PropList('src_ports',
                             ','.join(["%s-%s" % (sp.start_port, sp.end_port)
                                       for sp in self.sp_list])),
            sandesh.PropList('dst_ports',
                             ','.join(["%s-%s" % (dp.start_port, dp.end_port)
                                       for dp in self.dp_list])),
            sandesh.PropList('created', str(self.created)),
            sandesh.PropList('error_msg', str(self.error_msg)),
        ]
        return resp
    # end handle_st_object_req
# end ServiceChain

