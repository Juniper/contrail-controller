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
import ConfigParser
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


def get_keystone_opts(conf_sections):
    auth_user = conf_sections.get('KEYSTONE', 'admin_user')
    auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
    admin_token = conf_sections.get('KEYSTONE', 'admin_token')
    admin_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')
    try:
        keystone_sync_on_demand = conf_sections.getboolean('KEYSTONE',
                                               'keystone_sync_on_demand')
    except ConfigParser.NoOptionError:
        keystone_sync_on_demand = True

    try:
        auth_url = conf_sections.get('KEYSTONE', 'auth_url')
    except ConfigParser.NoOptionError:
        # deprecated knobs - for backward compat
        auth_proto = conf_sections.get('KEYSTONE', 'auth_protocol')
        auth_host = conf_sections.get('KEYSTONE', 'auth_host')
        auth_port = conf_sections.get('KEYSTONE', 'auth_port')
        auth_url = "%s://%s:%s/v2.0" % (auth_proto, auth_host, auth_port)

    return (auth_user, auth_passwd, admin_token, admin_tenant, auth_url,
            keystone_sync_on_demand)

class OpenstackDriver(vnc_plugin_base.Resync):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port

        self._config_sections = conf_sections
        (self._auth_user,
         self._auth_passwd,
         self._admin_token,
         self._admin_tenant,
         self._auth_url,
         self._keystone_sync_on_demand) = get_keystone_opts(conf_sections)

        if 'v3' in self._auth_url.split('/')[-1]:
            self._get_keystone_conn = self._ksv3_get_conn
            self._ks_domains_list = self._ksv3_domains_list
            self._ks_domain_get = self._ksv3_domain_get
            self._ks_projects_list = self._ksv3_projects_list
            self._ks_project_get = self._ksv3_project_get
            self.sync_project_to_vnc = self._ksv3_sync_project_to_vnc
            self._add_project_to_vnc = self._ksv3_add_project_to_vnc
            self._del_project_from_vnc = self._ksv3_del_project_from_vnc
            self._vnc_default_domain_id = None
        else:
            self._get_keystone_conn = self._ksv2_get_conn
            self._ks_domains_list = None
            self._ks_domain_get = None
            self._ks_projects_list = self._ksv2_projects_list
            self._ks_project_get = self._ksv2_project_get
            self.sync_project_to_vnc = self._ksv2_sync_project_to_vnc
            self._add_project_to_vnc = self._ksv2_add_project_to_vnc
            self._del_project_from_vnc = self._ksv2_del_project_from_vnc
        
        self._resync_interval_secs = 2
        self._ks = None
        self._vnc_lib = None

        # resync failures, don't retry forever
        self._failed_domain_dels = set()
        self._failed_project_dels = set()

        # active domains/projects in contrail/vnc api server
        self._vnc_domain_ids = set()
        self._vnc_project_ids = set()

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

    def _get_vnc_conn(self):
        if self._vnc_lib:
            return

        self._vnc_lib = vnc_api.VncApi(
            api_server_host=self._vnc_api_ip,
            api_server_port=self._vnc_api_port,
            username=self._auth_user,
            password=self._auth_passwd,
            tenant_name=self._admin_tenant)
    # end _get_vnc_conn

    def _ksv2_get_conn(self):
        if not self._ks:
            if self._admin_token:
                self._ks = keystone.Client(token=self._admin_token,
                                           endpoint=self._auth_url)
            else:
                self._ks = keystone.Client(username=self._auth_user,
                                           password=self._auth_passwd,
                                           tenant_name=self._admin_tenant,
                                           auth_url=self._auth_url)
    # end _ksv2_get_conn

    def _ksv2_projects_list(self):
        return [{'id': tenant.id} for tenant in self._ks.tenants.list()]
    # end _ksv2_projects_list

    def _ksv2_project_get(self, id):
        return {'name': self._ks.tenants.get(id).name}
    # end _ksv2_project_get

    def _ksv2_sync_project_to_vnc(self, id=None):
        self._get_keystone_conn()
        self._get_vnc_conn()
        ks_project = self._ks_project_get(id=id.replace('-', ''))

        proj_obj = vnc_api.Project(ks_project['name'])
        proj_obj.uuid = id
        self._vnc_lib.project_create(proj_obj)
    # end _ksv2_sync_project_to_vnc

    def _ksv2_add_project_to_vnc(self, project_id):
        try:
            self._vnc_lib.project_read(id=project_id)
            # project exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self._ksv2_sync_project_to_vnc(project_id)
    # _ksv2_add_project_to_vnc

    def _ksv2_del_project_from_vnc(self, project_id):
        if project_id in self._failed_project_dels:
            return

        try:
            self._vnc_lib.project_delete(id=project_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_project_dels.add(project_id)
    # _ksv2_del_project_from_vnc

    def _ksv3_get_conn(self):
        if self._ks:
            return

        self._ks = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._ks.mount("http://", adapter)
        self._ks.mount("https://", adapter)
    # end _ksv3_get_conn

    def _ksv3_domains_list(self):
        resp = self._ks.get('%s/domains' %(self._auth_url),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        domains_json = resp.text
        return json.loads(domains_json)['domains']
    # end _ksv3_domains_list

    def _ksv3_domain_id_to_uuid(self, domain_id):
        if domain_id == 'default':
            return self._vnc_default_domain_id

        return str(uuid.UUID(domain_id))
    # _ksv3_domain_id_to_uuid

    def _ksv3_domain_get(self, id=None):
        resp = self._ks.get('%s/domains/%s' %(self._auth_url, id),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        domain_json = resp.text
        return json.loads(domain_json)['domain']
    # end _ksv3_domain_get

    def _ksv3_projects_list(self):
        resp = self._ks.get('%s/projects' %(self._auth_url),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        projects_json = resp.text
        return json.loads(projects_json)['projects']
    # end _ksv3_projects_list

    def _ksv3_project_get(self, id=None):
        resp = self._ks.get('%s/projects/%s' %(self._auth_url, id),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        project_json = resp.text
        return json.loads(project_json)['project']
    # end _ksv3_project_get

    def _ksv3_sync_project_to_vnc(self, id=None, name=None):
        self._get_keystone_conn()
        self._get_vnc_conn()
        if id:
            ks_project = \
                self._ks_project_get(id=id.replace('-', ''))
            project_name = ks_project['name']
            project_id = id
        elif name:
            ks_project = \
                self._ks_project_get(name=name)
            project_id = ks_project['id']
            project_name = name

        domain_uuid = self._ksv3_domain_id_to_uuid(ks_project['domain_id'])
        dom_obj = self._vnc_lib.domain_read(id=domain_uuid)
        proj_obj = vnc_api.Project(project_name, parent_obj=dom_obj)
        proj_obj.uuid = project_id
        self._vnc_lib.project_create(proj_obj)
    # end _ksv3_sync_project_to_vnc

    def _ksv3_add_project_to_vnc(self, project_id):
        try:
            self._vnc_lib.project_read(id=project_id)
            # project exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self._ksv3_sync_project_to_vnc(id=project_id)
    # _ksv3_add_project_to_vnc

    def _ksv3_del_project_from_vnc(self, project_id):
        if project_id in self._failed_project_dels:
            return

        try:
            self._vnc_lib.project_delete(id=project_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_project_dels.add(project_id)
    # _ksv3_del_project_from_vnc

    def sync_domain_to_vnc(self, domain_id):
        self._get_keystone_conn()
        self._get_vnc_conn()
        ks_domain = \
            self._ks_domain_get(domain_id.replace('-', ''))
        dom_obj = vnc_api.Domain(ks_domain['name'])
        dom_obj.uuid = domain_id
        self._vnc_lib.domain_create(dom_obj)
    # sync_domain_to_vnc

    def _add_domain_to_vnc(self, domain_id):
        try:
            self._vnc_lib.domain_read(id=domain_id)
            # domain exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self.sync_domain_to_vnc(domain_id)
    # _add_domain_to_vnc

    def _del_domain_from_vnc(self, domain_id):
        if domain_id in self._failed_domain_dels:
            return

        try:
            self._vnc_lib.domain_delete(id=domain_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_domain_dels.add(domain_id)
    # _del_domain_from_vnc

    def _resync_domains(self, domain_id):
        if not self._ks_domains_list:
            # < keystonev3, no domains
            return False

        self._get_keystone_conn()
        # compare new and old set,
        # optimize for common case where nothing has changed,
        # so track the project-ids in a set add '-',
        # keystone gives uuid without...
        try:
            # The Default domain in ks(for v2 support) has id of 'default'
            # replace with uuid of default-domain in vnc
            ks_domain_ids = set(
                [str(uuid.UUID(dom['id']))
                    for dom in self._ks_domains_list() if dom['id'] != 'default'])
            ks_domain_ids.add(self._vnc_default_domain_id)
        except Exception as e:
            self._ks = None
            return True # retry

        vnc_domain_ids = self._vnc_domain_ids
        if vnc_domain_ids == ks_domain_ids:
            # no change, go back to poll
            return False

        for vnc_domain_id in vnc_domain_ids - ks_domain_ids:
            self._del_domain_from_vnc(vnc_domain_id)

        if self._keystone_sync_on_demand:
            # if on_demand, sync just that domain id
            self.sync_domain_to_vnc(domain_id)
            pass
        else:
            for ks_domain_id in ks_domain_ids - vnc_domain_ids:
                self.sync_domain_to_vnc(ks_domain_id)

        # we are in sync
        self._vnc_domain_ids = ks_domain_ids
    # end _resync_domains

    def _resync_projects(self, project_id):
        self._get_keystone_conn()
        # compare new and old set,
        # optimize for common case where nothing has changed,
        # so track the project-ids in a set add '-',
        # keystone gives uuid without...
        try:
            ks_project_ids = set(
                [str(uuid.UUID(proj['id']))
                    for proj in self._ks_projects_list()])
        except Exception as e:
            self._ks = None
            return True # retry

        vnc_project_ids = self._vnc_project_ids
        if vnc_project_ids == ks_project_ids:
            return

        for vnc_project_id in vnc_project_ids - ks_project_ids:
            self._del_project_from_vnc(vnc_project_id)

        if self._keystone_sync_on_demand:
            # if on_demand, sync just that project
            self.sync_project_to_vnc(project_id)
        else:
            for ks_project_id in ks_project_ids - vnc_project_ids:
                self.sync_project_to_vnc(ks_project_id)

        # we are in sync
        self._vnc_project_ids = ks_project_ids
    # end _resync_projects

    def resync_domains_projects(self):
        # add asynchronously
        pass
    #end resync_domains_projects


#end class OpenstackResync


class ResourceApiDriver(vnc_plugin_base.ResourceApi):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port
        self._config_sections = conf_sections
        (self._auth_user,
         self._auth_passwd,
         self._admin_token,
         self._admin_tenant,
         self._auth_url,
         self._keystone_sync_on_demand) = get_keystone_opts(conf_sections)

        self._vnc_lib = None
        self._openstack_drv = OpenstackDriver(api_server_ip, api_server_port, conf_sections)
        # Tracks which domains/projects have been sync'd from keystone to contrail api server
        self._vnc_domains = set()
        self._vnc_projects = set()
    # end __init__

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
                    tenant_name=self._admin_tenant)

                vnc_lib = self._vnc_lib
                domain_id = vnc_lib.fq_name_to_id(
                        'domain', ['default-domain'])
                project_id = vnc_lib.fq_name_to_id(
                        'project', ['default-domain', 'default-project'])
                self._vnc_projects.add(project_id)
                self._vnc_domains.add(domain_id)
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

    def _update_default_quota(self, id):
        """ Read the default quotas from the configuration
        and update it in the project object if not already
        updated.
        """
        if 'QUOTA' not in self._config_sections.sections():
            return
        default_quota = {}
        for (k, v) in self._config_sections.items("QUOTA"):
            try:
                key = str(k).replace('-', '_')
                default_quota[key] = int(v)
            except ValueError:
                pass

        # calling vnc_lib.project_read leads to a recursive call
        # to the function pre_project_read. But since the project id is
        # added to the _vnc_projects list before calling this function,
        # there will not be any infinite recursive calls.
        proj_obj = self._vnc_lib.project_read(id=id)
        quota = proj_obj.get_quota()
        modified = False
        for k in default_quota.keys():
            get_quota = getattr(quota, 'get_' + k)
            set_quota = getattr(quota, 'set_' + k)
            if get_quota is None or set_quota is None:
                continue
            if get_quota() is None or get_quota() == -1:
                modified = True
                set_quota(default_quota[k])

        if modified:
            proj_obj.set_quota(quota)
            self._vnc_lib.project_update(proj_obj)

    def pre_domain_read(self, id):
        if id in self._vnc_domains:
            return

        try:
            self._openstack_drv._resync_domains(id)
        except vnc_api.RefsExistError as e:
            # another api server has brought syncd it
            pass
        self._vnc_domains.add(id)
    # end pre_domain_read

    def pre_project_read(self, id):
        if id in self._vnc_projects:
            return

        try:
            self._openstack_drv._resync_projects(id)
        except vnc_api.RefsExistError as e:
            # another api server has brought syncd it
            pass
        self._vnc_projects.add(id)

        # update default quota from the configuration file
        self._update_default_quota(id)
    # end pre_project_read

    def post_project_create(self, proj_dict):
        self._get_api_connection()
        self._create_default_security_group(proj_dict)
    # end post_create_project

    def pre_project_delete(self, proj_uuid):
        self._get_api_connection()
        proj_obj = self._vnc_lib.project_read(id=proj_uuid)
        sec_groups = proj_obj.get_security_groups()
        for group in sec_groups or []:
            if group['to'][2] == 'default':
                self._vnc_lib.security_group_delete(id=group['uuid'])
        self._vnc_projects.remove(proj_uuid)
    # end pre_project_delete

    def pre_virtual_network_create(self, vn_dict):
        pass
    # end pre_virtual_network_create

