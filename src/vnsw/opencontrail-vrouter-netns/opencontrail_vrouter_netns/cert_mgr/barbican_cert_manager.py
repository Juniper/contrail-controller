import os
import ConfigParser
import logging
import exceptions

from OpenSSL import crypto

from keystoneclient import session
from keystoneclient.auth.identity import v2 as v2_client
from keystoneclient.auth.identity import v3 as v3_client

from barbicanclient import client

class BarbicanKeystoneSession(object):

    def __init__(self):
        self.admin_user = None
        self.admin_password = None
        self.project_name = None
        self.auth_url = None
        self.auth_version = '2'
        self.region = 'RegionOne'
        self.session = {}

    def parse_args(self, auth_conf=None):
        config = ConfigParser.SafeConfigParser()
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
            self.admin_user_domain = config.get('BARBICAN', 'admin_user_domain')
            self.admin_project_domain = config.get('BARBICAN', 'admin_project_domain')

        try:
            self.region = config.get('BARBICAN', 'region')
        except Exception:
            pass

    def get_session(self, auth_conf=None):
        if self.session.get(self.project_name):
            return self.session[self.project_name]

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
            kwargs['user_domain_name'] = self.admin_user_domain
            kwargs['project_domain_name'] = self.admin_project_domain

        try:
            kc = client.Password(**kwargs)
            self.session[self.project_name] = session.Session(auth=kc)
        except exceptions.Exception as e:
            logging.exception('Error creating Keystone session')
            logging.error(e.__class__)
            logging.error(e.__doc__)
            logging.error(e.message)
            return None

        return self.session[self.project_name]

class Cert:

    def __init__(self, cert_container):
        self._cert_container = cert_container

    def get_certificate(self):
        if self._cert_container.certificate:
            try:
                payload = self._cert_container.certificate.payload
                return True, payload
            except exceptions.Exception as e:
                logging.exception('')
                logging.error(e.__class__)
                logging.error(e.__doc__)
                logging.error(e.message)
                return False, None
        return True, None

    def get_intermediates(self):
        if self._cert_container.intermediates:
            try:
                payload = self._cert_container.intermediates.payload
                return True, payload
            except exceptions.Exception as e:
                logging.exception('')
                logging.error(e.__class__)
                logging.error(e.__doc__)
                logging.error(e.message)
                return False, None
        return True, None

    def get_private_key(self):
        if self._cert_container.private_key:
            try:
                payload = self._cert_container.private_key.payload
                return True, payload
            except exceptions.Exception as e:
                logging.exception('')
                logging.error(e.__class__)
                logging.error(e.__doc__)
                logging.error(e.message)
                return False, None
        return True, None

    def get_private_key_passphrase(self):
        if self._cert_container.private_key_passphrase:
            try:
                payload = self._cert_container.private_key_passphrase.payload
                return True, payload
            except exceptions.Exception as e:
                msg = "Error in getting Barbican Private-Key-PassPhrase from Container"
                logging.exception('')
                logging.error(e.__class__)
                logging.error(e.__doc__)
                logging.error(e.message)
                return False, None
        return True, None

class TLSContainer:

    def __init__(self, id=None, certificate=None, private_key=None,
                 passphrase=None, intermediates=None, primary_cn=None):
        self.id = id
        self.certificate = certificate
        self.private_key = private_key
        self.passphrase = passphrase
        self.intermediates = intermediates
        self.primary_cn = primary_cn

def build_pem(tls_cert):
    pem = ()
    if tls_cert.intermediates:
        for c in tls_cert.intermediates:
            pem = pem + (c,)
    if tls_cert.certificate:
        pem = pem + (tls_cert.certificate,)
    if tls_cert.private_key:
        pem = pem + (tls_cert.private_key,)
    pem = "\n".join(pem)
    return pem

def get_primary_cn(certificate):
    cert = crypto.load_certificate(crypto.FILETYPE_PEM, certificate)
    subject = cert.get_subject()
    issued_to = subject.CN
    issuer = cert.get_issuer()
    issued_by = issuer.CN
    return issued_to

def get_tls_certificates(barbican, url):
    try:
        container = barbican.containers.get(url)
    except exceptions.Exception as e:
        msg = "Error in getting Barbican Containers for url %s" %url
        logging.exception(msg)
        logging.error(e.__class__)
        logging.error(e.__doc__)
        logging.error(e.message)
        return None
    cert = Cert(container)
    status, certificate = cert.get_certificate()
    if (status == False):
        msg = "Error in getting Barbican Certficates from Container %s" %url
        logging.error(msg)
        return None
    primary_cn = get_primary_cn(certificate)
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
    return TLSContainer(
        primary_cn=primary_cn,
        private_key=private_key,
        certificate=certificate,
        intermediates=intermediates)

def create_pem_file(barbican, url, dest_dir):
    tls_cert = get_tls_certificates(barbican, url)
    if tls_cert is None:
        return None
    pem = build_pem(tls_cert)
    pem_file_name = dest_dir + '/'+ tls_cert.primary_cn + '.pem'
    f = open(pem_file_name, 'w')
    f.write(pem)
    f.close()
    return pem_file_name

def update_ssl_config(haproxy_config, auth_conf, dest_dir):
    barb_auth = BarbicanKeystoneSession()
    sess = barb_auth.get_session(auth_conf)
    if sess is None:
        return None
    barbican = client.Client(session=sess)
    updated_config = haproxy_config
    for line in haproxy_config.split('\n'):
        if 'ssl crt http' in line:
            try:
                url_list = filter(lambda x: x.startswith('http'), line.split(' '))
            except IndexError:
                return None
            for url in url_list or []:
                pem_file_name = create_pem_file(barbican, url, dest_dir)
                if pem_file_name is None:
                    return None
                updated_config = updated_config.replace(url, pem_file_name)

    return updated_config
