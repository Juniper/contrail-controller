# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import abc
import logging
from six import add_metaclass
import uuid

from cfgm_common.exceptions import NoIdError

logger = logging.getLogger(__name__)


@add_metaclass(abc.ABCMeta)
class Resource(object):

    _PERMS2 = {
        'owner': None,
        'owner_access': 7,
        'global_access': 0,
        'share': [],
    }
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid/"
    _SUBNET_ALLOC_PATH = "/api-server/subnets/"
    _SUBNET_CIDR = '10.0.0.0/16'

    def __init__(self, db_manager, batch_size, zk_client, project_amount,
                 amount_per_project):
        self._db_manger = db_manager
        self._zk_client = zk_client
        self._batch_size = batch_size
        self._project_amount = project_amount
        self._amount_per_project = amount_per_project
        self._uuid_cf = self._db_manger.get_cf('obj_uuid_table')
        self._fqname_cf = self._db_manger.get_cf('obj_fq_name_table')

    @abc.abstractproperty
    def type(self):
        return 'Should never get here'

    @property
    def amount_per_project(self):
        return self._amount_per_project

    @property
    def total_amount(self):
        return self._amount_per_project * self._project_amount

    def _uuid_to_longs(self, id):
        msb_id = id.int >> 64
        lsb_id = id.int & ((1 << 64) - 1)
        return msb_id, lsb_id

    def _get_id_perms(self, id):
        mslong, lslong = self._uuid_to_longs(id)
        return {
            'enable': True,
            'description': None,
            'creator': None,
            'user_visible': True,
            'permissions': {
                'owner': 'cloud-admin',
                'owner_access': 7,
                'other_access': 7,
                "group": "cloud-admin-group",
                "group_access": 7,
            },
            'uuid': {
                'uuid_mslong': mslong,
                'uuid_lslong': lslong,
            },
        }

    def _create_resource(self, type, fq_name, attributs=None, uuid_batch=None,
                         fqname_batch=None):
        try:
            self._db_manger.fq_name_to_uuid(type, fq_name)
        except NoIdError:
            if not attributs or 'uuid' not in attributs.keys():
                id = uuid.uuid4()
            else:
                id = uuid.UUID(attributs['uuid'])
            dict = {
                'uuid': str(id),
                'fq_name': fq_name,
                'display_name': fq_name[-1],
                'name': fq_name[-1],
                'id_perms': self._get_id_perms(id),
                'perms2': self._PERMS2,
            }
            if attributs:
                dict.update(attributs)
            self._zk_client.create_node(self._FQ_NAME_TO_UUID_PATH +
                                        ":".join([type] + fq_name))
            ok, _ = self._db_manger.object_create(type, str(id), dict,
                                                  uuid_batch, fqname_batch)
            if not ok:
                logger.error("Cannot create %s %s", type, ':'.join(fq_name))
                exit(1)
            return dict
        logger.error("%s %s already exists", type, ':'.join(fq_name))
        exit(1)

    def _update_resource(self, type, new_dict, uuid_batch=None):
        ok, _ = self._db_manger.object_update(type, new_dict['uuid'], new_dict,
                                              uuid_batch)
        if not ok:
            logger.error("Cannot update %s %s", type,
                         ':'.join(new_dict['fq_name']))
            exit(1)
        return new_dict

    @abc.abstractmethod
    def create_resources(self):
        """Create all resources type for all projects"""
