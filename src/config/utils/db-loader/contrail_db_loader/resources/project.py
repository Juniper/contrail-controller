# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import logging
from six import text_type
import uuid

from cfgm_common.exceptions import NoIdError
from keystoneclient import utils as kutils, exceptions as kexceptions

from .resource import Resource
from ..utils import timeit


logger = logging.getLogger(__name__)


class Project(Resource):
    def __init__(self, db_manager, batch_size, zk_client, project_amount,
                 amount_per_project):
        super(Project, self).__init__(db_manager, batch_size, zk_client,
                                      project_amount, amount_per_project)
        try:
            self._db_manger.fq_name_to_uuid('domain', ['default-domain'])
        except NoIdError as e:
            logger.error("%s. Run once the VNC API server to initialize "
                         "default resources", text_type(e))
            exit(1)

    @property
    def type(self):
        return 'project'

    @property
    def total_amount(self):
        return self._amount_per_project

    @timeit(return_time_elapsed=True)
    def create_resources(self, keystone_client=None):
        with self._uuid_cf.batch(queue_size=self._batch_size) as uuid_batch,\
                self._fqname_cf.batch(queue_size=self._batch_size) as \
                fqname_batch:
            for project_idx in range(self._project_amount):
                name = 'project-%d' % project_idx
                fq_name = [
                    'default-domain',
                    name,
                ]
                attr = {
                    'parent_type': 'domain',
                }
                if keystone_client:
                    try:
                        project = kutils.find_resource(keystone_client.tenants,
                                                       name)
                        attr['uuid'] = str(uuid.UUID(project.id))
                    except kexceptions.CommandError:
                        keystone_client.tenants.create(tenant_name=name,
                                                       description=name,
                                                       enabled=True)
                self._create_resource('project', fq_name, attr, uuid_batch,
                                      fqname_batch)
