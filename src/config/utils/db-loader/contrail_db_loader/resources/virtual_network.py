# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import logging
from netaddr import IPNetwork, IPAddress
from six import text_type
import uuid

from .resource import Resource
from ..utils import timeit

from cfgm_common.exceptions import NoIdError


logger = logging.getLogger(__name__)


class VirtualNetwork(Resource):

    def __init__(self, db_manager, batch_size, project_amount,
                 amount_per_project):
        super(VirtualNetwork, self).__init__(db_manager, batch_size,
                                             project_amount,
                                             amount_per_project)

        # Get default global system config dict
        try:
            id = self._db_manger.fq_name_to_uuid(
                'global-system-config', ['default-global-system-config'])
        except NoIdError as e:
            logger.error("%s. Run once the VNC API server to initialize "
                         "default resources", text_type(e))
            exit(1)
        ok, obj_dicts = self._db_manger.object_read('global-system-config',
                                                    [id])
        if not ok or len(obj_dicts) != 1:
            logger.error("Cannot read the default global system config %s or "
                         "it is not unique. Reset config?")
        self._default_gsc = obj_dicts[0]

        self._rt_id_allocator = 0

    @property
    def type(self):
        return 'virtual-network'

    @property
    def total_amount(self):
        return self._amount_per_project * 3 * self._project_amount

    @timeit(return_time_elapsed=True)
    def create_resources(self):
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                for resource_idx in range(self._amount_per_project):
                    subnet = IPNetwork(self._SUBNET_CIDR)
                    fq_name = [
                        'default-domain',
                        'project-%d' % project_idx,
                        'virtual-network-%d' % resource_idx,
                    ]
                    attr = {
                        'parent_type': 'project',
                        'virtual_network_network_id': resource_idx,
                        'router_external': False,
                        'is_shared': False,
                        'network_ipam_refs': [{
                            'to': [
                                'default-domain',
                                'default-project',
                                'default-network-ipam',
                            ],
                            'attr': {
                                'ipam_subnets': [{
                                    'subnet': {
                                        'ip_prefix': str(subnet.network),
                                        'ip_prefix_len': subnet.prefixlen
                                    },
                                    'dns_server_address':
                                    str(IPAddress(subnet.first + 1)),
                                    'enable_dhcp': True,
                                    'default_gateway':
                                    str(IPAddress(subnet.first)),
                                    'dns_nameservers': [],
                                    'allocation_pools': [],
                                    'subnet_uuid': str(uuid.uuid4()),
                                    'dhcp_option_list': None,
                                    'host_routes': None,
                                    'addr_from_start': True,
                                    'subnet_name':
                                    'virtual-network-subnet-%d' % resource_idx
                                }],
                                'host_routes': None,
                            },
                        }],
                    }
                    self._create_resource('virtual-network', fq_name, attr,
                                          uuid_batch, fqname_batch)

        ris = []
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                ris.append([])
                for resource_idx in range(self._amount_per_project):
                    fq_name = [
                        'default-domain',
                        'project-%d' % project_idx,
                        'virtual-network-%d' % resource_idx,
                        'virtual-network-%d' % resource_idx,
                    ]
                    attr = {
                        'parent_type': 'virtual-network',
                        'routing_instance_has_pnf': False,
                        'routing_instance_is_default': True,
                    }
                    ris[project_idx].append(self._create_resource(
                        'routing-instance', fq_name, attr, uuid_batch,
                        fqname_batch))

        rts = []
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                rts.append([])
                for resource_idx in range(self._amount_per_project):
                    fq_name = [
                        'target',
                        str(self._default_gsc['autonomous_system']),
                        str(8100000 + self._rt_id_allocator),
                    ]
                    rts[project_idx].append(self._create_resource(
                        'route-target', fq_name))
                    self._rt_id_allocator += 1

        # Update reference ri -> rt
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch:
            for project_idx in range(self._project_amount):
                for resource_idx in range(self._amount_per_project):
                    ri = ris[project_idx][resource_idx]
                    rt = rts[project_idx][resource_idx]
                    ri.update({
                        'route_target_refs': [
                            {
                                'uuid': rt['uuid'],
                                'to': rt['fq_name'],
                                'attr': {
                                    'import_export': None
                                },
                            },
                        ],
                    })
                    self._update_resource('routing-instance', ri, uuid_batch)
