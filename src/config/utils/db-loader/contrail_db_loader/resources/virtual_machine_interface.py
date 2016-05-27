# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import logging
from netaddr import IPNetwork, IPAddress
from random import randint
import uuid

from .resource import Resource
from ..utils import timeit


logger = logging.getLogger(__name__)


class VirtualMachineInterface(Resource):
    @property
    def type(self):
        return 'virtual-machine-interface'

    @property
    def total_amount(self):
        # For each VMI, it creates a IIP and VM
        return self._project_amount * self._amount_per_project * 3

    @timeit(return_time_elapsed=True)
    def create_resources(self, vn_per_project):
        vms = []
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                for resource_idx in range(self._amount_per_project):
                    device_id = str(uuid.uuid4())
                    fq_name = [
                        device_id,
                    ]
                    attr = {
                        'uuid': device_id
                    }
                    vms.append(
                        self._create_resource(
                            'virtual_machine',
                            fq_name,
                            attr,
                            uuid_batch,
                            fqname_batch
                        )
                    )

        vmis = []
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                for resource_idx in range(self._amount_per_project):
                    vn_idx = randint(0, vn_per_project - 1)
                    fq_name = [
                        'default-domain',
                        'project-%d' % project_idx,
                        'virtual-machine-interface-%d' % resource_idx,
                    ]
                    attr = {
                        'parent_type': 'project',
                        'virtual_machine_interface_device_owner':
                        'compute:None',
                        'virtual_machine_interface_mac_addresses': {
                            'mac_address': [
                                '02:14:a9:%02x:%02x:%02x' % (
                                    randint(0, 255),
                                    randint(0, 255),
                                    randint(0, 255),
                                ),
                            ],
                        },
                        'virtual_machine_interface_bindings': {
                            'key_value_pair': [
                                {
                                    'key': 'host_id',
                                    'value': 'ubuntu',
                                },
                                {
                                    'key': 'vif_type',
                                    'value': 'vrouter',
                                },
                                {
                                    'key': 'vnic_type',
                                    'value': 'normal',
                                },
                            ],
                        },
                        'virtual_network_refs': [{
                            'to': [
                                'default-domain',
                                'project-%d' % project_idx,
                                'virtual-network-%d' % vn_idx,
                            ],
                            'attr': None,
                        }],
                        'routing_instance_refs': [{
                            'to': [
                                'default-domain',
                                'project-%d' % project_idx,
                                'virtual-network-%d' % vn_idx,
                                'virtual-network-%d' % vn_idx,
                            ],
                            'attr': {
                                'direction': 'both',
                                'protocol': None,
                                'ipv6_service_chain_address': None,
                                'dst_mac': None,
                                'mpls_label': None,
                                'vlan_tag': None,
                                'src_mac': None,
                                'service_chain_address': None,
                            },
                        }],
                        'security_group_refs': [{
                            'to': [
                                'default-domain',
                                'project-%d' % project_idx,
                                'default',
                            ],
                            'attr': None,
                        }],
                        "virtual_machine_refs": [{
                            "to": vms[resource_idx]['fq_name'],
                            "attr": None,
                        }],
                    }
                    vmis.append(
                        self._create_resource(
                            'virtual_machine_interface',
                            fq_name,
                            attr,
                            uuid_batch,
                            fqname_batch
                        )
                    )
        ip_allocator = {}
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for vmi in vmis:
                vn_fq_name = vmi['virtual_network_refs'][0]['to']
                vn_fq_name_str = ':'.join(vn_fq_name)
                subnet = IPNetwork(self._SUBNET_CIDR)
                if vn_fq_name_str not in ip_allocator.keys():
                    ip = IPAddress(subnet.first + 3)
                    ip_allocator[vn_fq_name_str] = int(ip)
                else:
                    ip = IPAddress(ip_allocator[vn_fq_name_str] + 1)
                fq_name = [
                    str(uuid.uuid4()),
                ]
                attr = {
                    'instance_ip_address': str(ip),
                    # TODO(ethuleu): re-used the subnet uuid generated during
                    #                virtual network generation. Probably used
                    #                by the neutron plugin?
                    'subnet_uuid': str(uuid.uuid4()),
                    "instance_ip_family": "v4",
                    "virtual_network_refs": [{
                        "to": vn_fq_name,
                        "attr": None,
                    }],
                    "virtual_machine_interface_refs": [{
                        "to": vmi['fq_name'],
                        "attr": None,
                    }],
                }
                subnet_path = (self._SUBNET_ALLOC_PATH + '%s:%s/' %
                               (vn_fq_name_str, str(subnet)))
                id_str = "%(#)010d" % {'#': int(ip)}
                self._zk_client.create_node(subnet_path + id_str)
                self._create_resource(
                    'instance_ip',
                    fq_name,
                    attr,
                    uuid_batch,
                    fqname_batch
                )
