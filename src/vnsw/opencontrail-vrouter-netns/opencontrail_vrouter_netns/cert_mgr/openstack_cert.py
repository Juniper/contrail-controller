from builtins import object
import logging
import exceptions

class OSCert(object):

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
                msg = "Error in getting Private-Key-PassPhrase from Container"
                logging.exception('')
                logging.error(e.__class__)
                logging.error(e.__doc__)
                logging.error(e.message)
                return False, None
        return True, None
