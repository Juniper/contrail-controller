#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Contains cleaning up the swift container of residual chucks."""

from builtins import object
from builtins import str
import logging
import re
from threading import RLock
import time

from ansible.module_utils.fabric_utils import FabricAnsibleModule
from future import standard_library
standard_library.install_aliases()
import requests
import swiftclient
import swiftclient.utils

DOCUMENTATION = '''
---

module: Swift container cleanup
author: Juniper Networks
short_description: Private module to clean up container of residual chunks
description:
    - Pass the container name and delete all the residual chunks
    that don't have a manifest file
requirements:
    -
options:
    authtoken:
        description:
            - authentication token string
        required: true
    authurl:
        description:
            - authentication url string
        required: true
    user:
        description:
            - Swift username
        type: string
        required: true
    key:
        description:
            - Swift password
        type: string
        required: true
    tenant_name:
        description:
            - Tenant name.
        type: string
        required: false
        default: 'admin'
    auth_version:
        description:
            - Keystone Auth version.
        required: false
        default: '3.0'
    temp_url_key:
        description:
            - Temp url key
        required: true
    temp_url_key_2:
        description:
            - Temp url key 2
        required: true
    connection_retry_count:
        description:
            - Connection retry count
        type: int
        required: false
        default: 5
    chosen_temp_url_key:
        description:
            - Chosen Temp url key
        required: false
        default: 'temp_url_key'
    container_name:
        description:
            - Name of the container
        required: true

'''

EXAMPLES = '''
'''

RETURN = '''
url:
  description:
    - Success message that the container has been cleaned up
     of all the residual chunks
  returned: on success always
  type: str
error_msg:
  description:
    - Its an error message that is returned if there is any exception or error.
  returned: on failure
  type: str
'''

connection_lock = RLock()


class FileSvcUtil(object):  # pragma: no cover
    def __init__(self, authtoken, authurl, user, key, tenant_name,
                 auth_version, container_name, filename, temp_url_key,
                 temp_url_key2, connection_retry_count, chosen_temp_url_key):
        """Initializer."""
        self.requests = requests
        self.authurl = authurl
        self.preauthtoken = authtoken
        self.user = user
        self.key = key
        self.auth_version = auth_version
        self.container_name = container_name
        self.filename = filename
        self.temp_url_key = temp_url_key
        self.temp_url_key_2 = temp_url_key2
        self.connection_retry_count = connection_retry_count
        self.chosen_temp_url_key = chosen_temp_url_key
        self.conn_timeout_sec = 10
        self.tenant_name = tenant_name
        self.generateToken()
        self.updateAccount()

    # Connect to the swift client
    def generateToken(self):
        retry_count = 0
        incr_sleep = 10
        while retry_count <= self.connection_retry_count:
            try:
                acquired = connection_lock.acquire()
                swiftconn = swiftclient.client.Connection(
                    authurl=self.authurl, user=self.user, key=self.key,
                    preauthtoken=self.preauthtoken,
                    tenant_name=self.tenant_name,
                    auth_version=self.auth_version,
                    timeout=self.conn_timeout_sec,
                    insecure=True)
                self.swift_conn = swiftconn
                swiftconn.get_account()
                self.storageurl = swiftconn.url
                break
            except Exception as e:
                retry_count += 1
                err_msg = e.message
                logging.error(err_msg)
                if retry_count == self.connection_retry_count:
                    raise Exception("Connection failed with swift"
                                    " file server: " + str(err_msg))
                logging.error("Connection failed with swift file server,"
                              " retrying to connect")
                incr_sleep *= 2
                time.sleep(incr_sleep)
            finally:
                if acquired:
                    connection_lock.release()

    def updateAccount(self):
        headers = {'Temp-URL-Key': self.temp_url_key}
        if self.temp_url_key_2 is not None:
            headers['Temp-URL-Key-2'] = self.temp_url_key_2
        try:
            self.swift_conn.post_account(headers)
        except Exception as err:
            logging.error(str(err))
            raise Exception(
                "Update account failed with swift file server: " + str(err))

    # Pick out residual chunks without manifest file and delete them
    def container_cleanup(self, container_name, filename):
        list_item = self.swift_conn.get_container(container_name)
        manifest_list = []
        chunk_dict = {}
        delete_list = []
        container_items = list_item[1]
        regex = re.compile(r'.*/[0-9]+')
        for item in container_items:
            if filename == "":
                f = regex.match(item['name'])
                if f is not None:
                    image_name = f.group().split("__")
                    if image_name[0] not in list(chunk_dict.keys()):
                        chunk_dict[image_name[0]] = [f.group()]
                    else:
                        chunk_dict[image_name[0]].append(f.group())
                else:
                    manifest_list.append(item['name'])
            else:
                if filename in item['name']:
                    delete_list.append(item['name'])

        if filename == "":
            for key in list(chunk_dict.keys()):
                if key in manifest_list:
                    pass
                else:
                    delete_chunk_list = chunk_dict[key]
                    for delete_item in delete_chunk_list:
                        self.swift_conn.delete_object(container_name,
                                                      delete_item)
        else:
            for item in delete_list:
                self.swift_conn.delete_object(container_name, item)

    def close(self):
        if self.swift_conn:
            self.swift_conn.close()


def main():
    module = FabricAnsibleModule(
        argument_spec=dict(
            authtoken=dict(required=True),
            authurl=dict(required=True),
            user=dict(required=True),
            key=dict(required=True),
            tenant_name=dict(required=False, default="admin"),
            auth_version=dict(required=False, default='3.0'),
            temp_url_key=dict(required=True),
            temp_url_key_2=dict(required=True),
            chosen_temp_url_key=dict(required=False, default="temp_url_key"),
            container_name=dict(required=True),
            filename=dict(required=False, default=""),
            connection_retry_count=dict(required=False, default=5,
                                        type='int')),
        supports_check_mode=False)
    m_args = module.params
    authtoken = m_args['authtoken']
    authurl = m_args['authurl']
    user = m_args['user']
    key = m_args['key']
    tenant_name = m_args['tenant_name']
    auth_version = m_args['auth_version']
    temp_url_key = m_args['temp_url_key']
    temp_url_key_2 = m_args['temp_url_key_2']
    chosen_temp_url_key = m_args['chosen_temp_url_key']
    container_name = m_args['container_name']
    filename = m_args['filename']
    connection_retry_count = m_args['connection_retry_count']

    error_msg = ''
    try:
        fileutil = FileSvcUtil(authtoken, authurl, user, key, tenant_name,
                               auth_version, container_name, filename,
                               temp_url_key, temp_url_key_2,
                               connection_retry_count, chosen_temp_url_key)

        fileutil.container_cleanup(container_name, filename)

        fileutil.close()

    except Exception as e:
        error_msg = "Exception occurred in swift_fileutil: " + str(e)

    results = {}
    results['error_msg'] = error_msg

    module.exit_json(**results)


if __name__ == '__main__':
    main()
