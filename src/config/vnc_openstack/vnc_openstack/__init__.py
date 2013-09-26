#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import json
import uuid
import gevent
import requests
import cgitb
import logging
import logging.handlers
from keystoneclient.v2_0 import client as keystone
try:
    from cfgm_common import vnc_plugin_base
except ImportError:
    from common import vnc_plugin_base
from vnc_api import vnc_api


class OpenstackDriver(vnc_plugin_base.Resync):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port

        self._config_sections = conf_sections
        self._auth_host = conf_sections.get('KEYSTONE', 'auth_host')
        self._auth_user = conf_sections.get('KEYSTONE', 'admin_user')
        self._auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
        self._auth_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')
        auth_proto = conf_sections.get('KEYSTONE', 'auth_protocol')
        auth_url = "%s://%s:35357/v2.0" % (auth_proto, self._auth_host)
        self._auth_url = auth_url
        self._resync_interval_secs = 2

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
    #end __init__

    def __call__(self):
        pass
    #end __call__

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

            # Get tenants/projects from Keystone and audit with api-server
            kc = keystone.Client(username=self._auth_user,
                                 password=self._auth_passwd,
                                 tenant_name=self._auth_tenant,
                                 auth_url=self._auth_url)

            old_projects = self._vnc_lib.projects_list()['projects']
            old_project_ids = set([proj['uuid'] for proj in old_projects])
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())

        while True:
            try:
                # compare new and old set,
                # optimize for common case where nothing has changed,
                # so track the project-ids in a set add '-',
                # keystone gives uuid without...
                new_project_ids = set(
                    [str(uuid.UUID(proj.id)) for proj in kc.tenants.list()])

                if old_project_ids == new_project_ids:
                    # no change, go back to poll
                    gevent.sleep(self._resync_interval_secs)

                for old_proj_id in old_project_ids - new_project_ids:
                    try:
                        self._vnc_lib.project_delete(id=old_proj_id)
                    except Exception as e:
                        cgitb.Hook(
                            format="text",
                            file=open(self._tmp_file_name,
                                      'w')).handle(sys.exc_info())
                        fhandle = open(self._tmp_file_name)
                        self._vnc_os_logger.error("%s" % fhandle.read())
                        pass

                for new_proj_id in new_project_ids - old_project_ids:
                    try:
                        self._vnc_lib.project_read(id=new_proj_id)
                        # project exists, no-op for now,
                        # sync any attr changes in future
                    except vnc_api.NoIdError:
                        new_proj = kc.tenants.get(new_proj_id.replace('-', ''))
                        p_obj = vnc_api.Project(new_proj.name)
                        p_obj.uuid = new_proj_id
                        self._vnc_lib.project_create(p_obj)

                    # yield, project list might be large...
                    gevent.sleep(0)
                #end for all new projects

                # we are in sync
                old_project_ids = new_project_ids

            except Exception as e:
                cgitb.Hook(
                    format="text",
                    file=open(self._tmp_file_name,
                              'w')).handle(sys.exc_info())
                fhandle = open(self._tmp_file_name)
                self._vnc_os_logger.error("%s" % fhandle.read())
        #end while True

    #end _resync_projects_forever

    def resync_projects(self):
        # add asynchronously
        gevent.spawn(self._resync_projects_forever)
    #end resync_projects

#end class OpenstackResync
