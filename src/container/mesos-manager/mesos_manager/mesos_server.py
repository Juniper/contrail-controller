#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
MESOS CNI Server
"""

# Standard library import
import bottle
import json

# Application library import
from cfgm_common.rest import LinkObject
from mesos_cni import MESOSCniDataObject
from vnc_api.vnc_api import *


class MesosServer(object):

    def __init__(self, args=None, logger=None, queue=None):
        self._args = args
        self._queue = queue
        self.logger = logger

        self._homepage_links = []
        self._cni_data = {}

        self._base_url = "http://%s:%s" % (self._args.mesos_api_server,
                                           self._args.mesos_api_port)
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

        # Get CNI information
        bottle.route('/get_cni_info', 'GET', self.get_cni_info_all)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url, '/get_cni_info', 'get all CNI information'))

        # get a specific CNI information
        bottle.route('/get_cni_info/<container_id>', 'GET',
                     self.get_cni_info_all)

        # show config
        bottle.route('/config', 'GET', self.show_config)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url, '/config', 'show cni config'))

        # show debug
        bottle.route('/stats', 'GET', self.show_stats)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url, '/stats', 'show cni debug stats'))

        # cleanup
        bottle.route('/cleanup', 'GET', self.cleanup_http_get)
        self._homepage_links.append(LinkObject('action',
                                               self._base_url,
                                               '/cleanup',
                                               'Purge deleted cni'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

    def process_cni_data(self, container_id, data):
        self.logger.info("Server: Got CNI data for Container Id: %s."
                         % (container_id))
        print data
        cni_data_obj = MESOSCniDataObject(data)
        cni_conf = cni_data_obj.parse_cni_data()
        self._queue.put(cni_conf)
        print cni_conf
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

    # Public Methods
    def get_args(self):
        return self._args
    # end get_args

    def get_ip_addr(self):
        return self._args.mesos_api_server
    # end get_ip_addr

    def get_port(self):
        return self._args.mesos_api_port
    # end get_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

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
        for key, value in data.items():
            json_req[key] = value
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
        for key, value in data.items():
            json_req[key] = value
        container_id = json_req['cid']
        if container_id in self._cni_data:
            del self._cni_data[container_id]
        self.process_cni_data(container_id, json_req)
        return json_req
    # end del_cni_info

    def get_cni_info_all(self):
        return self._cni_data
    # end get_cni_info_all

    # purge expired cni
    def cleanup_http_get(self):
        return "Cleanup"
    # end cleanup_http_get

    def show_config(self):
        rsp = ""

        rsp += '<table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '<tr><th colspan="2">Defaults CONFIGG</th></tr>'
        for k in sorted(self._args.__dict__.iterkeys()):
            v = self._args.__dict__[k]
            rsp += '<tr><td>%s</td><td>%s</td></tr>' % (k, v)
        rsp += '</table>'
        rsp += '<br>'

        return rsp
    # end show_config

    def show_stats(self):

        rsp = ""
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        for k in sorted(stats.iterkeys()):
            rsp += '    <tr>\n'
            rsp += '        <td>%s</td>\n' % (k)
            rsp += '        <td>STATSSS</td>\n'
            rsp += '    </tr>\n'
        return rsp
    # end show_stats

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
        self.logger.info("Starting mesos-manager server @ %s:%s."
                         % (self.get_ip_addr(), self.get_port()))

        pipe_start_app = self.get_pipe_start_app()

        try:
            bottle.run(app=pipe_start_app, host=self.get_ip_addr(),
                       port=self.get_port(), server='gevent')
        except Exception:
            self.cleanup()
    # start_server

# end class MesosServer
