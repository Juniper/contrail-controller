import json
import sys
import logging
import os
import requests
import abc
import six

@six.add_metaclass(abc.ABCMeta)
class CertManager(object):
    """Class to download certs from specific
       drivers mentioned in the conf_file"""
    def __init__(self):
        pass

    @staticmethod
    def _request(url, headers=None, body=None, request_type=None):
        try:
            if request_type == 'PUT':
                encoded_body = json.dumps(body)
                return requests.put(url, headers=headers, data=encoded_body)
            elif request_type == 'POST':
                encoded_body = json.dumps(body)
                return requests.post(url, headers=headers, data=encoded_body)
            else:
                return requests.get(url, headers=headers)

        except Exception as e:
            logging.exception("Failed sending request to keystone")
            return None

    @abc.abstractmethod
    def _validate_tls_secret(self, tls_container_ref):
        pass

    @abc.abstractmethod
    def _populate_tls_pem(self, tls_container_ref):
        pass

class BarbicanCertManager(CertManager):
    """Class to download certs from barbican and
       populate the pem file as required by HAProxy
    """
    def __init__(self, identity=None):
        super(Barbican_Cert_Manager, self).__init__()
        self.identity = identity

    def _get_barbican_entity(self, barbican_ep, auth_token,
                             entity_ref, metadata=True):
        if metadata:
            accept_data = 'application/json'
        else:
            accept_data = 'text/plain'

        try:
            headers = {
                "Accept": "%s" % accept_data,
                "X-Auth-Token": "%s" % auth_token
            }
            url = entity_ref
            resp = self._request(url, headers, 'GET')
            if resp.status_code in range(200, 299):
                if metadata:
                    return json.loads(resp.text)
                else:
                    return resp.text
            else:
                logging.error("%s getting barbican entity %s" % \
                                (resp.text, url))
        except Exception as e:
            logging.error("%s getting barbican entity %s" % \
                            (str(e), url))
        return None

    def _validate_tls_secret(self, tls_container_ref):
        try:
            if self.identity:
                #self.identity = keystone_auth.Identity()
                container_detail = self._get_barbican_entity(\
                                        self.identity.barbican_ep,
                                        self.identity.auth_token,
                                        entity_ref=tls_container_ref,
                                        metadata=True)

                if not container_detail:
                    return False

                # Validate that secrets are stored plain text
                for secret in container_detail['secret_refs']:
                    secret_meta_data = self._get_barbican_entity(\
                                           self.identity.barbican_ep,
                                           self.identity.auth_token,
                                           entity_ref=secret['secret_ref'],
                                           metadata=True)
                    if not secret_meta_data or secret_meta_data\
                        ['content_types']['default'] != 'text/plain':
                        logging.error("Invalid secret format: %s" % \
                            secret_meta_data['content_types']['default'])
                        return False
                return True
            else:
                return False
        except Exception as e:
            logging.error("%s while validating TLS Container" % str(e))
            return False

    def _populate_tls_pem(self, tls_container_ref):
        try:
            if self.identity:
                #self.identity = keystone_auth.Identity()
                container_detail = self._get_barbican_entity(\
                                        self.identity.barbican_ep,
                                        self.identity.auth_token,
                                        entity_ref=tls_container_ref,
                                        metadata=True)

                if not container_detail:
                    return False

                # Fetch the secrets stored in plain text
                secret_text = ''
                for secret in container_detail['secret_refs']:
                    secret_detail = self._get_barbican_entity(\
                                         self.identity.barbican_ep,
                                         self.identity.auth_token,
                                         entity_ref=secret['secret_ref'],
                                         metadata=False)
                    if secret_detail:
                        secret_text += secret_detail
                        secret_text += "\n"

                return secret_text
            else:
                return None
        except Exception as e:
            logging.error("%s while populating SSL Pem file" % str(e))
            return None


class GenericCertManager(CertManager):
    """Class to download certs from Generic Cert Manager and
       populate the pem file as required by HAProxy
    """
    def __init__(self, identity=None):
        super(Generic_Cert_Manager, self).__init__()

    def _validate_tls_secret(self, tls_container_ref):
        if tls_container_ref is None:
            return False

        # Check if the file exists
        if not os.path.isfile(tls_container_ref):
            return False

        # Check if file is readable
        if not os.access(tls_container_ref, os.R_OK):
            return False

        return True

    def _populate_tls_pem(self, tls_container_ref):
        secret_text = ''
        with open(tls_container_ref) as tls_container:
            secret_text = tls_container.read()

        return secret_text
