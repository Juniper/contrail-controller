import json
import keystone_auth
import sys
import logging

class Barbican_Cert_Manager(object):
    """Class to download certs from barbican and
       populate the pem file as required by HAProxy
    """
    def __init__(self, keystone_auth_conf_file):
        self.identity = keystone_auth.Identity(keystone_auth_conf_file)
        if not self.identity:
            raise Exception()

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
            resp = keystone_auth._request(url, headers, 'GET')
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
