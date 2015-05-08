import requests
import json
import os
import ConfigParser
import logging

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
        logging.error("Failed sending request to keystone")
        return None

class Identity():
    '''Identity class to get project-scoped
       tokens for user'''
    def __init__(self, conf_file):
        config = ConfigParser.RawConfigParser()
        config.read(conf_file)
        if not self._parse_config(config):
            raise Exception()

    def _parse_config(self, config):
        try:
            self.keystone_ep = config.get('DEFAULT', 'keystone_endpoint')
            self.barbican_ep = config.get('DEFAULT', 'barbican_endpoint')
            self.domain_name = config.get('DEFAULT', 'domain_name')
            self.os_username = config.get('DEFAULT', 'username')
            self.os_password = config.get('DEFAULT', 'password')
            self.os_project_name = \
                config.get('DEFAULT', 'project_name')
            self.identity_version = \
                config.get('DEFAULT', 'keystone_version')
            self.auth_token = None

            if not self.keystone_ep:
                logging.error("Keystone Endpoint missing in the config file")
                return False

            if not self.identity_version:
                self.identity_version = 'v2.0'

            self.keystone_ep = self.keystone_ep + '/' + self.identity_version

            if self.identity_version == 'v3' and not self.domain_name:
                logging.error("Domain name missing in the config file")
                return False

            if not self.os_username:
                logging.error("Username missing in the config file")
                return False

            if not self.os_password:
                logging.error("Password missing in the config file")
                return False

            if self.identity_version == 'v2.0':
                if not self._get_v2_project_scoped_auth_token():
                    logging.error("Failed to fetch v2.0 token")
                    return False
            elif self.identity_version == 'v3' and self.os_project_name is None:
                if not self._get_domain_scoped_auth_token():
                    logging.error("Failed to fetch domain scoped token")
                    return False
            else:
                if not self._get_v3_project_scoped_auth_token():
                    logging.error("Failed to fetch project scoped token")
                    return False

            return True

        except Exception as e:
            logging.error(str(e))
            return False

    def _get_v3_project_scoped_auth_token(self):
        try:
            headers = {
                "Content-Type": "application/json",
            }
            body = {
                "auth": {
                    "identity": {
                        "methods": ["password"],
                        "password": {
                            "user": {
                                "name": self.os_username,
                                "domain": { "id": "default" },
                                "password": self.os_password
                             }
                        }
                    },
                    "scope": {
                        "project": {
                            "name": self.os_project_name,
                            "domain": { "id": "default" }
                        }
                    }
                }
            }

            url = self.keystone_ep + "/auth/tokens"
            resp = _request(url, headers, body, 'POST')
            if resp and resp.status_code in range(200, 299):
                headers = resp.headers
                if headers and 'x-subject-token' in headers:
                    self.auth_token = headers['x-subject-token']
                    return True

            return False
        except Exception as e:
            logging.error(str(e))
            return False

    def _get_v2_project_scoped_auth_token(self):
        try:
            headers = {
                "Content-Type": "application/json",
            }
            body = {
                "auth": {
                    "tenantName": self.os_project_name,
                    "passwordCredentials": {
                        "username": self.os_username,
                        "password": self.os_password
                    }
                }
            }

            url = self.keystone_ep + "/tokens"
            resp = _request(url, headers, body, 'POST')
            if resp and resp.status_code in range(200, 299):
                json_data = json.loads(resp.text)
                self.auth_token = json_data['access']['token']['id']
                return True
            return False
        except Exception as e:
            logging.error(str(e))
            return False

    def _get_domain_scoped_auth_token(self):
        # Not supported by barbican
        return False
