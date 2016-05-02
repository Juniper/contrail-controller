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
        self.auth_version = '2'
        self.admin_user = None
        self.admin_password = None
        self.auth_url = None
        self.region = 'RegionOne'
        self.project_name = 'admin'
        self.session = {}

    def parse_args(self):
        config = ConfigParser.SafeConfigParser()
        config.read('/etc/contrail/contrail-barbican-auth.conf')
        self.admin_user = config.get('DEFAULT', 'admin_user')
        self.admin_password = config.get('DEFAULT', 'admin_password')
        self.auth_url = config.get('DEFAULT', 'auth_url')
        self.auth_version = config.get('DEFAULT', 'auth_version')
        self.region = config.get('DEFAULT', 'region')

    def get_session(self):
        if self.session.get(self.project_name):
            return self.session[self.project_name]

        self.parse_args()
        kwargs = {'auth_url': self.auth_url,
                  'username': self.admin_user,
                  'password': self.admin_password}

        if self.auth_version == '2':
            client = v2_client
            kwargs['tenant_name'] = self.project_name
        elif self.auth_version == '3':
            client = v3_client
            kwargs['project_name'] = self.project_name
            kwargs['user_domain_name'] = (cfg.CONF.service_auth.
                                          admin_user_domain)
            kwargs['project_domain_name'] = (cfg.CONF.service_auth.
                                             admin_project_domain)

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
            return self._cert_container.certificate.payload

    def get_intermediates(self):
        if self._cert_container.intermediates:
            return self._cert_container.intermediates.payload

    def get_private_key(self):
        if self._cert_container.private_key:
            return self._cert_container.private_key.payload

    def get_private_key_passphrase(self):
        if self._cert_container.private_key_passphrase:
            return self._cert_container.private_key_passphrase.payload

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
    certificate = cert.get_certificate()
    primary_cn = get_primary_cn(certificate)
    private_key = cert.get_private_key()
    intermediates = cert.get_intermediates()
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

def update_ssl_conf(file):
    barb_auth = BarbicanKeystoneSession()
    sess = barb_auth.get_session()
    if sess is None:
        return None
    barbican = client.Client(session=sess)
    dest_dir = os.path.dirname(file)
    with open(file) as f:
        conf = f.read()
        updated_conf = conf
    for line in conf.split('\n'):
        if 'ssl crt http' in line:
            try:
                url_list = filter(lambda x: x.startswith('http:'), line.split(' '))
            except IndexError:
                return None
            for url in url_list or []:
                pem_file_name = create_pem_file(barbican, url, dest_dir)
                if pem_file_name is None:
                    return None
                updated_conf = updated_conf.replace(url, pem_file_name)

    with open(file, "w") as f:
        conf = f.write(updated_conf)
    return updated_conf
