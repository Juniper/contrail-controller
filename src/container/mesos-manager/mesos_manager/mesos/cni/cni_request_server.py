#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
MESOS CNI Server
"""

# Standard library import
from builtins import object
import bottle
import json
from cfgm_common.rest import LinkObject
import json

class MESOSCniDataObject(object):

    def __init__(self, data=None):
        self._data = data
        self._conf = {}

    def parse_cni_data(self):
        data = self._data
        self._conf['cid'] = data['cid']
        self._conf['cmd'] = data['cmd']
	json_data = json.loads(data['args'])
        lbl_dict = {}
        lbl_dict['node-name'] = json_data['contrail']['cluster-name']
        lbl_dict['node-ip'] = json_data['contrail']['vrouter-ip']
        #lbl_dict['app_subnets'] = json_data['app_subnets']
        net_info = json_data['args']['org.apache.mesos']['network_info']
        if net_info and 'labels' in net_info:
            cni_labels = net_info['labels']
            if cni_labels:
                for item in cni_labels['labels']:
                    lbl_dict[item['key']] = item['value']
        self._conf['labels'] = lbl_dict
        return self._conf

class MesosCniServer(object):

    def __init__(self, args=None, logger=None, queue=None):
        self._args = args
        self._queue = queue
        self.logger = logger

        self._homepage_links = []
        self._cni_data = {}

        self._base_url = "http://%s:%s" % (self._args.mesos_cni_server,
                                           self._args.mesos_cni_port)
        self._pipe_start_app = None
        bottle.route('/', 'GET', self.homepage_http_get)

        # Add CNI information
        bottle.route('/add_cni_info',  'POST', self.add_cni_info)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url, '/add_cni_info', 'Add CNI information'))

        # Del CNI information
        bottle.route('/del_cni_info',  'POST', self.del_cni_info)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url, '/del_cni_info', 'Del CNI information'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

    def process_cni_data(self, container_id, data):
        self.logger.info("Server: Got CNI data for Container Id: %s."
                         % (container_id))
        cni_data_obj = MESOSCniDataObject(data)
        cni_conf = cni_data_obj.parse_cni_data()
        self._queue.put(cni_conf)
        pass

    def create_cni_data(self, container_id, data):
        if container_id not in self._cni_data:
            self._cni_data[container_id] = {}
            self._cni_data[container_id] = data
            self.process_cni_data(container_id, self._cni_data[container_id])
        return self._cni_data[container_id]
    # end

    def get_cni_data(self, container_id, service_type):
        if container_id in self._cni_data:
            return self._cni_data[container_id]
        return None
    # end

    def get_ip_addr(self):
        return self._args.mesos_cni_server
    # end get_ip_addr

    def get_port(self):
        return self._args.mesos_cni_port
    # end get_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

    def extract_arguments(self, json_req):
        args_list = []
        args_list.append('app_subnets')
        stdin_args = vars(self._args)
        for args_item in args_list:
            if args_item in stdin_args:
                json_req[args_item] = stdin_args[args_item]

    def add_cni_info(self):
        json_req = {}
        ctype = bottle.request.headers['content-type']
        try:
            if 'application/json' in ctype:
                data = bottle.request.json
            elif 'application/xml' in ctype:
                data = xmltodict.parse(bottle.request.body.read())
        except Exception:
            self.logger.info('Unable to parse publish request')
            self.logger.info(bottle.request.body.buf)
            bottle.abort(415, 'Unable to parse publish request')
        for key, value in list(data.items()):
            json_req[key] = value
        #add argument to json list
        self.extract_arguments(json_req)
        cid = json_req['cid']
        self.create_cni_data(cid, json_req)

        return json_req
    # end add_cni_info

    def del_cni_info(self):
        json_req = {}
        ctype = bottle.request.headers['content-type']
        try:
            if 'application/json' in ctype:
                data = bottle.request.json
            elif 'application/xml' in ctype:
                data = xmltodict.parse(bottle.request.body.read())
        except Exception:
            self.logger.info('Unable to parse publish request')
            self.logger.info(bottle.request.body.buf)
            bottle.abort(415, 'Unable to parse publish request')
        for key, value in list(data.items()):
            json_req[key] = value
        #add argument to json list
        self.extract_arguments(json_req)
        container_id = json_req['cid']
        if container_id in self._cni_data:
            del self._cni_data[container_id]
        self.process_cni_data(container_id, json_req)
        return json_req
    # end del_cni_info

    def homepage_http_get(self):
        json_links = []
        url = bottle.request.url[:-1]
        for link in self._homepage_links:
            json_links.append({'link': link.to_dict(with_url=url)})

        json_body = \
            {"href": self._base_url,
             "links": json_links
             }

        return json_body
    # end homepage_http_get

    def start_server(self):
        self.logger.info("Starting mesos cni server @ %s:%s."
                         % (self.get_ip_addr(), self.get_port()))

        pipe_start_app = self.get_pipe_start_app()

        try:
            bottle.run(app=pipe_start_app, host=self.get_ip_addr(),
                       port=self.get_port(), server='gevent')
        except Exception:
            self.logger.info("Error in starting  mesos-manager server.")
    # start_server

# end class MesosCniServer
