from builtins import object
import os

from OpenSSL import crypto

class TLS(object):

    def __init__(self, id=None, certificate=None, private_key=None,
                 passphrase=None, intermediates=None, primary_cn=None):
        self.id = id
        self.certificate = certificate
        self.private_key = private_key
        self.passphrase = passphrase
        self.intermediates = intermediates
        self.primary_cn = primary_cn

    def build_pem(self):
        pem = ()
        if self.intermediates:
            for c in self.intermediates:
                pem = pem + (c,)
        if self.certificate:
            pem = pem + (self.certificate,)
        if self.private_key:
            pem = pem + (self.private_key,)
        pem = "\n".join(pem)
        return pem

    @staticmethod
    def get_primary_cn(certificate):
        cert = crypto.load_certificate(crypto.FILETYPE_PEM, certificate)
        subject = cert.get_subject()
        issued_to = subject.CN
        issuer = cert.get_issuer()
        issued_by = issuer.CN
        return issued_to

    def create_pem_file(self, dest_dir):
        if self is None:
            return None
        pem = self.build_pem()
        pem_file_name = dest_dir + '/'+ self.primary_cn + '.pem'
        f = open(pem_file_name, 'w')
        f.write(pem)
        f.close()
        os.chmod(pem_file_name, 0o600)
        return pem_file_name
