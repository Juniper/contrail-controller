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
    def __init__(self):
        config = ConfigParser.RawConfigParser()
        config.read('/etc/contrail/contrail-vrouter-agent.conf')
        if not self._parse_config(config):
            raise Exception()

    def _parse_config(self, config):
        try:
            self.keystone_ep = config.get('keystone_authtoken', 'keystone_endpoint')
            self.barbican_ep = config.get('keystone_authtoken', 'barbican_endpoint')
            self.domain_id = config.get('keystone_authtoken', 'domain_id')
            self.os_username = config.get('keystone_authtoken', 'username')
            self.os_password = config.get('keystone_authtoken', 'password')
            self.os_project_name = config.get('keystone_authtoken', 'project_name')
            self.auth_token = None

            if not self.keystone_ep:
                logging.error("Keystone Endpoint missing in the config file")
                return False

            self.keystone_ep = self.keystone_ep + '/v3'

            if not self.domain_id:
                logging.error("Domain id missing in the config file")
                return False

            if not self.os_username:
                logging.error("Username missing in the config file")
                return False

            if not self.os_password:
                logging.error("Password missing in the config file")
                return False

            if self.os_project_name is None:
                if not self._get_domain_scoped_auth_token():
                    logging.error("Failed to fetch domain scoped token")
                    return False
            else:
                if not self._get_project_scoped_auth_token():
                    logging.error("Failed to fetch project scoped token")
                    return False

            return True

        except Exception as e:
            logging.error(str(e))
            return False

    def _get_project_scoped_auth_token(self):
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
                                "domain": { "id": self.domain_id },
                                "password": self.os_password
                             }
                        }
                    },
                    "scope": {
                        "project": {
                            "name": self.os_project_name,
                            "domain": { "id": self.domain_id }
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

    def _get_domain_scoped_auth_token(self):
        # Not supported by barbican
        return False
