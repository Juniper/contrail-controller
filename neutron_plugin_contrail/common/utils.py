# Copyright 2017 Juniper Networks.  All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

import requests
from six.moves.urllib.parse import urlparse

from oslo_config import cfg
from vnc_api.vnc_api import VncApi

from neutron_plugin_contrail.common import constants
from neutron_plugin_contrail.common import exceptions as c_exc

vnc_opts = [
    cfg.StrOpt('api_server_ip',
               default=constants.VNC_API_DEFAULT_HOST,
               help='IP address to connect to VNC API'),
    cfg.IntOpt('api_server_port',
               default=constants.VNC_API_DEFAULT_PORT,
               help='Port to connect to VNC API'),
    cfg.StrOpt('api_server_base_url',
               default=constants.VNC_API_DEFAULT_BASE_URL,
               help='URL path to request VNC API'),
    cfg.DictOpt('contrail_extensions',
                default={'contrail': None,
                         'service-interface': None,
                         'vf-binding': None},
                help='Enable Contrail extensions (default: %(default)s)'),
    cfg.BoolOpt('use_ssl',
                default=constants.VNC_API_DEFAULT_USE_SSL,
                help='Use SSL to connect with VNC API'),
    cfg.BoolOpt('insecure',
                default=constants.VNC_API_DEFAULT_INSECURE,
                help='Insecurely connect to VNC API'),
    cfg.StrOpt('certfile',
               help='Certificate file path to connect securely to VNC API'),
    cfg.StrOpt('keyfile',
               help='Key file path to connect securely to  VNC API'),
    cfg.StrOpt('cafile',
               help='CA file path to connect securely to VNC API'),
    cfg.StrOpt('auth_token_url',
               help='Full URL path to request Keystone tokens. This should '
                    'not be use and determined from keystone_authtoken '
                    'configuration section.'),
    cfg.IntOpt('timeout',
               default=constants.VNC_API_DEFAULT_TIMEOUT,
               help='VNC API Server request timeout in seconds'),
    cfg.IntOpt('connection_timeout',
               default=constants.VNC_API_DEFAULT_CONN_TIMEOUT,
               help='VNC API Server connection timeout in seconds'),
]

vrouter_opts = [
    cfg.StrOpt('vhostuser_sockets_dir',
               default='/var/run/vrouter',
               help='Path to dir where vhostuser socket are placed'),
]

vnc_extra_opts = [
    cfg.BoolOpt('apply_subnet_host_routes',
                default=False,
                help="Apply Neutron subnet host routes to Contrail virtual "
                     "network with a route table"),
]


class RoundRobinApiServers(object):
    def __init__(self):
        self.api_servers = cfg.CONF.APISERVER.api_server_ip.split()
        self.index = -1

    def get(self,api_servers):
        # use the next host in the list
        self.index += 1
        if self.index >= len(api_servers):
            # reuse the first host from the list
            self.index = 0
        return api_servers[self.index]

    def len(self):
        return len(self.api_servers)

def register_vnc_api_options():
    """Register Contrail Neutron core plugin configuration flags"""
    cfg.CONF.register_opts(vnc_opts, 'APISERVER')
    cfg.CONF.register_opts(vrouter_opts, 'VROUTER')


def register_vnc_api_extra_options():
    """Register extra Contrail Neutron core plugin configuration flags"""
    cfg.CONF.register_opts(vnc_extra_opts, 'APISERVER')


def vnc_api_is_authenticated(api_server_ips):
    """ Determines if the VNC API needs credentials

    :returns: True if credentials are needed, False otherwise
    """
    for api_server_ip in api_server_ips:
        url = "%s://%s:%s/aaa-mode" % (
            'https' if cfg.CONF.APISERVER.use_ssl else 'http',
            api_server_ip,
            cfg.CONF.APISERVER.api_server_port
        )
        response = requests.get(url,
                            timeout=(cfg.CONF.APISERVER.connection_timeout,
                                     cfg.CONF.APISERVER.timeout),
                            verify=cfg.CONF.APISERVER.get('cafile', False))

        if response.status_code == requests.codes.ok:
            return False
        elif response.status_code == requests.codes.unauthorized:
            return True
    response.raise_for_status()


def get_keystone_auth_info():
    try:
        admin_user = cfg.CONF.keystone_authtoken.username
    except cfg.NoSuchOptError:
        admin_user = cfg.CONF.keystone_authtoken.admin_user
    try:
        admin_password = cfg.CONF.keystone_authtoken.password
    except cfg.NoSuchOptError:
        admin_password = cfg.CONF.keystone_authtoken.admin_password
    try:
        admin_tenant_name = cfg.CONF.keystone_authtoken.project_name
    except cfg.NoSuchOptError:
        admin_tenant_name = cfg.CONF.keystone_authtoken.admin_tenant_name
    try:
        domain_name = cfg.CONF.keystone_authtoken.project_domain_name
    except cfg.NoSuchOptError:
        domain_name = 'default'

    return (admin_user, admin_password, admin_tenant_name, domain_name)


def get_vnc_api_instance(wait_for_connect=True):
    """ Instantiates a VncApi object from configured parameters

    Read all necessary configuration options from neutron and contrail core
    plugin configuration files and instantiates a VncApi object with them.
    If authentication strategy is not define, use OpenStack Keystone
    authentication by default.

    :param wait_for_connect: retry connect infinitely when http return code is
        503
    :returns: VncApi object instance
    """
    api_server_host = cfg.CONF.APISERVER.api_server_ip.split()
    api_server_port = cfg.CONF.APISERVER.api_server_port
    api_server_base_url = cfg.CONF.APISERVER.api_server_base_url
    api_server_use_ssl = cfg.CONF.APISERVER.use_ssl
    api_server_ca_file = cfg.CONF.APISERVER.cafile
    api_server_cert_file = cfg.CONF.APISERVER.certfile
    api_server_key_file = cfg.CONF.APISERVER.keyfile

    # If VNC API needs authentication, use the same authentication strategy
    # than Neutron (default to Keystone). If not, don't provide credentials.
    if vnc_api_is_authenticated(api_server_host):
        auth_strategy = cfg.CONF.auth_strategy
        if auth_strategy not in VncApi.AUTHN_SUPPORTED_STRATEGIES:
            raise c_exc.AuthStrategyNotSupported(auth_strategy=auth_strategy)
        auth_strategy = constants.KEYSTONE_AUTH
    else:
        auth_strategy = 'noauth'

    identity_uri = None
    auth_token_url = None
    auth_protocol = None
    auth_host = None
    auth_port = None
    auth_url = None
    auth_version = None
    auth_cafile = None
    auth_certfile = None
    auth_keyfile = None
    admin_user = None
    admin_password = None
    admin_tenant_name = None
    domain_name = None
    if auth_strategy == constants.KEYSTONE_AUTH:
        try:
            ks_auth_url = cfg.CONF.keystone_authtoken.auth_url
        except cfg.NoSuchOptError:
            ks_auth_url = None
        # If APISERV.auth_token_url is define prefer it to keystone_authtoken
        # section
        auth_token_url = cfg.CONF.APISERVER.auth_token_url
        if auth_token_url is not None:
            auth_token_url_parsed = urlparse(auth_token_url)
            auth_protocol = auth_token_url_parsed.scheme
            auth_host = auth_token_url_parsed.hostname
            auth_port = auth_token_url_parsed.port
            auth_url = auth_token_url_parsed.path
        elif ks_auth_url:
            # If keystone_authtoken.auth_url is defined, prefer it to
            # keystone_authtoken.identity_uri
            auth_url_parsed = urlparse(ks_auth_url)
            auth_protocol = auth_url_parsed.scheme
            auth_host = auth_url_parsed.hostname
            auth_port = auth_url_parsed.port
        else:
            # If keystone_authtoken.identity_uri is define, prefer it to
            # specific authtoken parameters
            identity_uri = cfg.CONF.keystone_authtoken.identity_uri
            if identity_uri is not None:
                identity_uri_parsed = urlparse(identity_uri)
                auth_protocol = identity_uri_parsed.scheme
                auth_host = identity_uri_parsed.hostname
                auth_port = identity_uri_parsed.port
                auth_admin_prefix = identity_uri_parsed.path
            else:
                auth_protocol = cfg.CONF.keystone_authtoken.auth_protocol
                auth_host = cfg.CONF.keystone_authtoken.auth_host
                auth_port = cfg.CONF.keystone_authtoken.auth_port
                auth_admin_prefix =\
                    cfg.CONF.keystone_authtoken.auth_admin_prefix
                identity_uri = '%s://%s:%s/%s' % (auth_protocol, auth_host,
                                                  auth_port, auth_admin_prefix)
            # If no Keystone API version is define in indentiy_uri or in
            # specific param auth_version, use version 2.0.
            if constants.KEYSTONE_V2_REGEX.search(identity_uri):
                auth_version = constants.KEYSTONE_V2_API_VERSION
                auth_token_url = '%s/tokens' % (identity_uri)
            elif constants.KEYSTONE_V3_REGEX.search(identity_uri):
                auth_version = constants.KEYSTONE_V3_API_VERSION
                auth_token_url = '%s/tokens' % (identity_uri)
            else:
                auth_version = cfg.CONF.keystone_authtoken.auth_version or \
                    constants.KEYSTONE_V2_API_VERSION
                auth_token_url = '%s/%s/tokens' % (identity_uri, auth_version)
            auth_url = '%s/%s/tokens' % (auth_admin_prefix, auth_version)
        auth_cafile = cfg.CONF.keystone_authtoken.cafile
        auth_certfile = cfg.CONF.keystone_authtoken.certfile
        auth_keyfile = cfg.CONF.keystone_authtoken.keyfile
        (admin_user,
         admin_password,
         admin_tenant_name,
         domain_name) = get_keystone_auth_info()

    return VncApi(
        api_server_host=api_server_host,
        api_server_port=api_server_port,
        api_server_url=api_server_base_url,
        api_server_use_ssl=api_server_use_ssl,
        apicafile=api_server_ca_file,
        apicertfile=api_server_cert_file,
        apikeyfile=api_server_key_file,
        auth_type=auth_strategy,
        auth_protocol=auth_protocol,
        auth_host=auth_host,
        auth_port=auth_port,
        auth_url=auth_url,
        auth_token_url=auth_token_url,
        kscafile=auth_cafile,
        kscertfile=auth_certfile,
        kskeyfile=auth_keyfile,
        username=admin_user,
        password=admin_password,
        tenant_name=admin_tenant_name,
        domain_name=domain_name,
        wait_for_connect=wait_for_connect,
        connection_timeout=cfg.CONF.APISERVER.connection_timeout,
        timeout=cfg.CONF.APISERVER.timeout,
    )
