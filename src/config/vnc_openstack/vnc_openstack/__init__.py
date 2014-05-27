#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import json
import uuid
import gevent
import requests
import cgitb
import copy
import bottle
import logging
import logging.handlers
import Queue
from keystoneclient.v2_0 import client as keystone
import cfgm_common
try:
    from cfgm_common import vnc_plugin_base
except ImportError:
    from common import vnc_plugin_base
from vnc_api import vnc_api
from vnc_api.gen.resource_xsd import *

from vnc_api.gen.resource_xsd import *
from vnc_api.gen.resource_common import *

Q_CREATE = 'create'
Q_DELETE = 'delete'
Q_MAX_ITEMS = 100


class OpenstackDriver(vnc_plugin_base.Resync):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port

        self._config_sections = conf_sections
        self._auth_host = conf_sections.get('KEYSTONE', 'auth_host')
        self._auth_port = conf_sections.get('KEYSTONE', 'auth_port')
        self._auth_user = conf_sections.get('KEYSTONE', 'admin_user')
        self._auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
        self._admin_token = conf_sections.get('KEYSTONE', 'admin_token')
        self._auth_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')
        auth_proto = conf_sections.get('KEYSTONE', 'auth_protocol')
        auth_url = "%s://%s:%s/v2.0" % (auth_proto, self._auth_host, self._auth_port)
        self._auth_url = auth_url
        self._resync_interval_secs = 2
        self._resync_number_workers = 10 #TODO(sahid) needs to be configured by conf.
        self._kc = None

        # logging
        self._log_file_name = '/var/log/contrail/vnc_openstack.err'
        self._tmp_file_name = '/var/log/contrail/vnc_openstack.tmp'
        self._vnc_os_logger = logging.getLogger('MyLogger')
        self._vnc_os_logger.setLevel(logging.ERROR)
        # Add the log message handler to the logger
        handler = logging.handlers.RotatingFileHandler(self._log_file_name,
                                                       maxBytes=1024,
                                                       backupCount=5)

        self._vnc_os_logger.addHandler(handler)
        self.q = Queue.Queue(maxsize=Q_MAX_ITEMS)
    #end __init__

    def __call__(self):
        pass
    #end __call__

    def _get_keystone_conn(self):
        if not self._kc:
            if self._admin_token:
                self._kc = keystone.Client(token=self._admin_token,
                                           endpoint=self._auth_url)
            else:
                self._kc = keystone.Client(username=self._auth_user,
                                           password=self._auth_passwd,
                                           tenant_name=self._auth_tenant,
                                           auth_url=self._auth_url)

    def _log_exception(self):
        cgitb.Hook(
            format="text",
            file=open(self._tmp_file_name,
                      'w')).handle(sys.exc_info())
        fhandle = open(self._tmp_file_name)
        self._vnc_os_logger.error("%s" % fhandle.read())
    #end _log_exception

    def _resync_projects_forever(self):
        try:
            # get connection to api-server REST interface
            while True:
                try:
                    self._vnc_lib = vnc_api.VncApi(
                        api_server_host=self._vnc_api_ip,
                        api_server_port=self._vnc_api_port,
                        username=self._auth_user,
                        password=self._auth_passwd,
                        tenant_name=self._auth_tenant)
                    break
                except requests.ConnectionError:
                    gevent.sleep(1)

            old_projects = self._vnc_lib.projects_list()['projects']
            old_project_ids = set([proj['uuid'] for proj in old_projects])
        except Exception as e:
            self._log_exception()

        while True:
            try:
                # Get tenants/projects from Keystone and audit with api-server
                self._get_keystone_conn()

                # compare new and old set,
                # optimize for common case where nothing has changed,
                # so track the project-ids in a set add '-',
                # keystone gives uuid without...
                try:
                    new_project_ids = set(
                        [str(uuid.UUID(proj.id))
                            for proj in self._kc.tenants.list()])
                except Exception as e:
                    self._kc = None
                    continue

                if old_project_ids == new_project_ids:
                    # no change, go back to poll
                    gevent.sleep(self._resync_interval_secs)
                    continue

                for old_proj_id in old_project_ids - new_project_ids:
                    self.q.put((Q_DELETE, old_proj_id))
                for new_proj_id in new_project_ids - old_project_ids:
                    self.q.put((Q_CREATE, new_proj_id))

                self.q.join()
                gevent.sleep(0)

                # we are in sync
                old_project_ids = new_project_ids

            except Exception as e:
                self._kc = None
                self._log_exception()
                gevent.sleep(2)
        #end while True

    #end _resync_projects_forever

    def resync_projects(self):
        # add asynchronously
        gevent.spawn(self._resync_projects_forever)
        for x in range(self._resync_number_workers):
            gevent.spawn(self._resync_worker())
    #end resync_projects

    def _resync_worker(self):
        while True:
            # THINKING(sahid): Add a timeout to kill thread if not used.
            kind, proj_id = self.q.get()
            try:
                if kind == Q_DELETE:
                    self._delete_project(proj_id)
                elif kind == Q_CREATE:
                    self._create_project(proj_id)
                else:
                    raise KeyError("An invalid kind was specified: %s", kind)
            except (ValueError, KeyError):
                # For an unpack error or and invalid kind.
                self.log_exception()

    def _delete_project(self, old_proj_id):
        try:
            self._vnc_lib.project_delete(id=old_proj_id)
        except Exception as e:
            self._log_exception()

    def _create_project(self, new_proj_id):
        try:
            try:
                self._vnc_lib.project_read(id=new_proj_id)
                # project exists, no-op for now,
                # sync any attr changes in future
            except vnc_api.NoIdError:
                new_proj = \
                           self._kc.tenants.get(new_proj_id.replace('-', ''))
                p_obj = vnc_api.Project(new_proj.name)
                p_obj.uuid = new_proj_id
                self._vnc_lib.project_create(p_obj)
        except Exception:
            self.log_exception()


#end class OpenstackResync


class ResourceApiDriver(vnc_plugin_base.ResourceApi):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port
        self._config_sections = conf_sections
        self._auth_host = conf_sections.get('KEYSTONE', 'auth_host')
        self._auth_user = conf_sections.get('KEYSTONE', 'admin_user')
        self._auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
        self._auth_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')
        self._vnc_lib = None

    def _get_api_connection(self):
        if self._vnc_lib:
            return

        # get connection to api-server REST interface
        while True:
            try:
                self._vnc_lib = vnc_api.VncApi(
                    api_server_host=self._vnc_api_ip,
                    api_server_port=self._vnc_api_port,
                    username=self._auth_user,
                    password=self._auth_passwd,
                    tenant_name=self._auth_tenant)
                break
            except requests.ConnectionError:
                gevent.sleep(1)

    def __call__(self):
        pass
    #end __call__

    def _create_default_security_group(self, proj_dict):
        proj_obj = vnc_api.Project.from_dict(**proj_dict)
        sgr_uuid = str(uuid.uuid4())
        ingress_rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                      protocol='any',
                                      src_addresses=[
                                          AddressType(
                                              security_group=proj_obj.get_fq_name_str() + ':' + 'default')],
                                      src_ports=[PortType(0, 65535)],
                                      dst_addresses=[
                                          AddressType(security_group='local')],
                                      dst_ports=[PortType(0, 65535)])
        sg_rules = PolicyEntriesType([ingress_rule])

        sgr_uuid = str(uuid.uuid4())
        egress_rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                     protocol='any',
                                     src_addresses=[
                                         AddressType(security_group='local')],
                                     src_ports=[PortType(0, 65535)],
                                     dst_addresses=[
                                         AddressType(
                                             subnet=SubnetType('0.0.0.0', 0))],
                                     dst_ports=[PortType(0, 65535)])
        sg_rules.add_policy_rule(egress_rule)

        # create security group
        sg_obj = vnc_api.SecurityGroup(name='default', parent_obj=proj_obj,
                                       security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
    # end _create_default_security_group

    def post_project_create(self, proj_dict):
        self._get_api_connection()
        self._create_default_security_group(proj_dict)
    # end post_create_project

    def pre_project_delete(self, proj_uuid):
        self._get_api_connection()
        proj_obj = self._vnc_lib.project_read(id=proj_uuid)
        sec_groups = proj_obj.get_security_groups()
        for group in sec_groups:
            if group['to'][2] == 'default':
                self._vnc_lib.security_group_delete(id=group['uuid'])
    # end pre_project_delete

