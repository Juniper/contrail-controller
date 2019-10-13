from __future__ import absolute_import
from future import standard_library
standard_library.install_aliases()
from builtins import object
import os
from six.moves import configparser
import logging
import exceptions

from .tls import TLS
from .openstack_cert import OSCert

from keystoneclient import session
from keystoneclient.auth.identity import v2 as v2_client
from keystoneclient.auth.identity import v3 as v3_client

from barbicanclient import client

class BarbicanCertManager(object):

    def __init__(self, auth_conf):
        self.admin_user = None
        self.admin_password = None
        self.project_name = None
        self.auth_url = None
        self.auth_version = '2'
        self.region = 'RegionOne'
        self.get_session(auth_conf)

    def get_session(self, auth_conf=None):
        self.parse_args(auth_conf)
        kwargs = {'auth_url': self.auth_url,
                  'username': self.admin_user,
                  'password': self.admin_password}

        if self.auth_version == '2':
            client = v2_client
            kwargs['tenant_name'] = self.project_name
        elif self.auth_version == '3':
            client = v3_client
            kwargs['project_name'] = self.project_name
            kwargs['user_domain_name'] = self.user_domain_name
            kwargs['project_domain_name'] = self.project_domain_name

        try:
            kc = client.Password(**kwargs)
            self.session = session.Session(auth=kc)
        except exceptions.Exception as e:
            logging.exception('Error creating Keystone session')
            logging.error(e.__class__)
            logging.error(e.__doc__)
            logging.error(e.message)
            return None

        return self.session

    def parse_args(self, auth_conf=None):
        config = configparser.SafeConfigParser()
        if (auth_conf):
            self.auth_conf = auth_conf
        else:
            self.auth_conf = '/etc/contrail/contrail-lbaas-auth.conf'
        config.read(self.auth_conf)

        self.admin_user = config.get('BARBICAN', 'admin_user')
        self.admin_password = config.get('BARBICAN', 'admin_password')
        self.project_name = config.get('BARBICAN', 'admin_tenant_name')

        self.auth_url = config.get('BARBICAN', 'auth_url')
        tmp_auth_url = self.auth_url
        if (tmp_auth_url[-1] == '/'):
            tmp_auth_url[:-1]
        auth_version = tmp_auth_url.split('/')[-1]
        if (auth_version.lower() == 'v2.0'):
            self.auth_version = '2'
        elif (auth_version.lower() == 'v3'):
            self.auth_version = '3'
            self.user_domain_name = config.get('BARBICAN', 'user_domain_name')
            self.project_domain_name = config.get('BARBICAN', 'project_domain_name')

        try:
            self.region = config.get('BARBICAN', 'region')
        except Exception:
            pass

    def get_tls_certificates(self, barbican, url):
        try:
            container = barbican.containers.get(url)
        except exceptions.Exception as e:
            msg = "Error in getting Barbican Containers for url %s" %url
            logging.exception(msg)
            logging.error(e.__class__)
            logging.error(e.__doc__)
            logging.error(e.message)
            return None
        cert = OSCert(container)
        status, certificate = cert.get_certificate()
        if (status == False):
            msg = "Error in getting Barbican Certficates from Container %s" %url
            logging.error(msg)
            return None
        primary_cn = TLS.get_primary_cn(certificate)
        status, private_key = cert.get_private_key()
        if (status == False):
            msg = "Error in getting Barbican Private-Key from Container %s" %url
            logging.error(msg)
            return None
        status, intermediates = cert.get_intermediates()
        if (status == False):
            msg = "Error in getting Barbican Intermediates from Container %s" %url
            logging.error(msg)
            return None
        return TLS(
            primary_cn=primary_cn,
            private_key=private_key,
            certificate=certificate,
            intermediates=intermediates)

    def update_ssl_config(self, haproxy_config, dest_dir):
        if self.session is None:
            return None
        barbican = client.Client(session=self.session)
        updated_config = haproxy_config
        for line in haproxy_config.split('\n'):
            if 'ssl crt' in line:
                try:
                    items = [x for x in line.split(' ') if x.startswith('crt__')]
                except IndexError:
                    return None
                for item in items or []:
                    url = item[5:] # len('crt__')
                    tls = self.get_tls_certificates(barbican, url)
                    pem_file_name = tls.create_pem_file(dest_dir)
                    if pem_file_name is None:
                        return None
                    updated_config = updated_config.replace(item, 'crt ' + pem_file_name)

        return updated_config
