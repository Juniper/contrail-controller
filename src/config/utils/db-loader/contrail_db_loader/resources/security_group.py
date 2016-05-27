# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import logging
from netaddr import IPNetwork
from random import randint, choice
import uuid

from .resource import Resource
from ..utils import timeit


logger = logging.getLogger(__name__)


class SecurityGroup(Resource):

    _SG_ID_ALLOC_PATH = '/id/security-groups/id/'
    _SG_ID_PADDING = 100000
    _SG_ID_ALLOC_START_IDX = 8000000 + _SG_ID_PADDING

    def __init__(self, db_manager, batch_size, zk_client, project_amount,
                 amount_per_project):
        super(SecurityGroup, self).__init__(db_manager, batch_size, zk_client,
                                            project_amount, amount_per_project)
        self._sg_id_allocator = 0

    @property
    def type(self):
        return 'security-group'

    @property
    def total_amount(self):
        total_sg = (self._project_amount + self._amount_per_project *
                    self._project_amount)
        total_acl = total_sg * 2
        return total_sg + total_acl

    @timeit(return_time_elapsed=True)
    def create_resources(self, rules_per_sg):
        sgs = []
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                fq_name = [
                    'default-domain',
                    'project-%d' % project_idx,
                    'default',
                ]
                attr = {
                    'parent_type': 'project',
                    'security_group_id': self._SG_ID_ALLOC_START_IDX +
                    self._sg_id_allocator,
                    'security_group_entries': {
                        'policy_rule': [
                            self._get_rule(remote_sg=':'.join(fq_name)),
                            self._get_rule(ethertype='IPv6',
                                           remote_sg=':'.join(fq_name)),
                            self._get_rule(direction='egress',
                                           ethertype='IPv4',
                                           remote_ip='0.0.0.0/0'),
                            self._get_rule(direction='egress',
                                           ethertype='IPv6',
                                           remote_ip='::/0'),

                        ],
                    },
                }
                id_str = "%(#)010d" % {'#': self._SG_ID_PADDING +
                                       self._sg_id_allocator}
                self._zk_client.create_node(self._SG_ID_ALLOC_PATH + id_str)
                sgs.append(self._create_resource('security_group',
                                                 fq_name, attr, uuid_batch,
                                                 fqname_batch))
                self._sg_id_allocator += 1
                for resource_idx in range(self._amount_per_project):
                    fq_name = [
                        'default-domain',
                        'project-%d' % project_idx,
                        'security-group-%d' % resource_idx,
                    ]
                    policy_rule = []
                    for _ in range(rules_per_sg):
                        random_port = randint(0, 65535)
                        policy_rule.append(
                            self._get_rule(
                                protocol=choice(['udp', 'tcp']),
                                remote_ip='0.0.0.0/0',
                                dst_ports=(random_port, random_port)
                            )
                        )
                    attr = {
                        'parent_type': 'project',
                        'security_group_id': self._SG_ID_ALLOC_START_IDX +
                        self._sg_id_allocator,
                        'security_group_entries': {
                            'policy_rule': policy_rule,
                        },
                    }
                    id_str = "%(#)010d" % {'#': self._SG_ID_PADDING +
                                           self._sg_id_allocator}
                    self._zk_client.create_node(self._SG_ID_ALLOC_PATH +
                                                id_str)
                    sgs.append(self._create_resource('security_group',
                                                     fq_name, attr, uuid_batch,
                                                     fqname_batch))
                    self._sg_id_allocator += 1

        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for sg in sgs:
                ingress, egress = self._policy_rule_to_acl_rule(
                    sg['security_group_id'],
                    sg['security_group_entries']['policy_rule'])
                fq_name = sg['fq_name'] + ['ingress-access-control-list']
                attr = {
                    'parent_type': 'security-group',
                    'access_control_list_entries': {
                        'dynamic': None,
                        'acl_rule': ingress,
                    },
                }
                self._create_resource('access_control_list', fq_name, attr,
                                      uuid_batch, fqname_batch)
                fq_name = sg['fq_name'] + ['egress-access-control-list']
                attr = {
                    'parent_type': 'security-group',
                    'access_control_list_entries': {
                        'dynamic': None,
                        'acl_rule': egress,
                    },
                }
                self._create_resource('access_control_list', fq_name, attr,
                                      uuid_batch, fqname_batch)

    def _policy_rule_to_acl_rule(self, sg_id, prules):
        ingress = []
        egress = []

        for prule in prules:
            if prule['src_addresses'][0]['security_group']:
                src_sg = sg_id
            else:
                src_sg = None
            if prule['dst_addresses'][0]['security_group']:
                dst_sg = sg_id
            else:
                dst_sg = None
            arule = {
                'rule_uuid': prule['rule_uuid'],
                'match_condition': {
                    'ethertype': prule['ethertype'],
                    'src_address': {
                        'security_group': src_sg,
                        'subnet': prule['src_addresses'][0]['subnet'],
                        'virtual_network': None,
                        'subnet_list': [],
                        'network_policy': None,
                    },
                    'dst_address': {
                        'security_group': dst_sg,
                        'subnet': prule['dst_addresses'][0]['subnet'],
                        'virtual_network': None,
                        'subnet_list': [],
                        'network_policy': None,
                    },
                    'protocol': prule['protocol'],
                    'src_port': prule['src_ports'][0],
                    'dst_port': prule['dst_ports'][0],
                },
                'action_list': {
                    'gateway_name': None,
                    'log': False,
                    'alert': False,
                    'assign_routing_instance': None,
                    'mirror_to': None,
                    'simple_action': 'pass',
                    'apply_service': [],
                },
            }
            if (arule['match_condition']['src_address']['security_group'] or
                    arule['match_condition']['src_address']['subnet']):
                ingress.append(arule)
            else:
                egress.append(arule)

        return (ingress, egress)

    def _get_rule(self, direction='ingress', ethertype='IPv4', protocol='any',
                  remote_sg=None, remote_ip=None, src_ports=(0, 65535),
                  dst_ports=(0, 65535)):
        if remote_ip:
            ip = IPNetwork(remote_ip)
            remote_ip_map = {
                'ip_prefix': str(ip.ip),
                'ip_prefix_len': ip.prefixlen
            }
        else:
            remote_ip_map = None
        return {
            'rule_uuid': str(uuid.uuid4()),
            'direction': '>',
            'ethertype': ethertype,
            'protocol': protocol,
            'action_list': None,
            'application': [],
            'rule_sequence': None,
            'src_addresses': [{
                'security_group':
                remote_sg if direction == 'ingress' else 'local',
                'subnet': remote_ip_map if direction == 'ingress' else None,
                'virtual_network': None,
                'subnet_list': [],
                'network_policy': None,
            }],
            'dst_addresses': [{
                'security_group':
                remote_sg if direction == 'egress' else 'local',
                'subnet': remote_ip_map if direction == 'egress' else None,
                'virtual_network': None,
                'subnet_list': [],
                'network_policy': None,
            }],
            'src_ports': [{
                'start_port': src_ports[0],
                'end_port': src_ports[1],
            }],
            'dst_ports': [{
                'start_port': dst_ports[0],
                'end_port': dst_ports[1],
            }],
        }
