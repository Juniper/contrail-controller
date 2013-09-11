#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
import gevent
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent

import json
import time
import disc_consts 

from gevent.coros import BoundedSemaphore

class DiscoveryZkClient(object):
    def __init__(self, discServer, zk_srv_ip='127.0.0.1', zk_srv_port='2181', reset_config=False):
        self._reset_config = reset_config
        self._service_id_to_type = {}
        self._ds = discServer
        self._zk_sem = BoundedSemaphore(1)

        self._zk = kazoo.client.KazooClient(hosts = '%s:%s' % (zk_srv_ip, zk_srv_port),
            timeout = 120, handler = kazoo.handlers.gevent.SequentialGeventHandler())

        # connect
        while True:
            try:
                self._zk.start()
                break
            except gevent.event.Timeout as e:
                self.syslog('Failed to connect with Zookeeper - will retry in a second')
                gevent.sleep(1)
        self.syslog('Connected to ZooKeeper!')

        if reset_config:
            self._zk.delete("/services", recursive = True)
            self._zk.delete("/clients", recursive = True)
            self._zk.delete("/publishers", recursive = True)
            self._zk.delete("/election", recursive = True)

        # create default paths
        self.create_node("/services")
        self.create_node("/clients")
        self.create_node("/publishers")
        self.create_node("/election")

        self._debug = {
            'subscription_expires': 0, 
            'oos_delete': 0,
            'db_excepts': 0,
            }
    #end __init__

    def start_background_tasks(self):
        # spawn loop to expire subscriptions
        gevent.Greenlet.spawn(self.inuse_loop)

        # spawn loop to expire publishers
        gevent.Greenlet.spawn(self.service_oos_loop)
    #end

    def syslog(self, log_msg):
        self._ds.syslog(log_msg)
    #end

    def get_debug_stats(self):
        return self._debug
    #end
    
    def create_node(self, path, value = '', makepath = False):
        try:
            self._zk.set(path, value)
        except kazoo.exceptions.NoNodeException:
            self._zk.create(path, value, makepath = makepath)
            self.syslog('create %s' % (path))
    #end create_node

    def service_entries(self):
        service_types = self._zk.get_children('/services')
        for service_type in service_types:
            services = self._zk.get_children('/services/%s' %(service_type))
            for service_id in services:
                data, stat = self._zk.get('/services/%s/%s' %(service_type, service_id))
                entry = json.loads(data)
                yield(entry)

    def subscriber_entries(self):
        service_types = self._zk.get_children('/clients')
        for service_type in service_types:
            subscribers = self._zk.get_children('/clients/%s' %(service_type))
            for client_id in subscribers:
                data, stat = self._zk.get('/clients/%s/%s' %(service_type, client_id))
                cl_entry = json.loads(data)
                yield((client_id, service_type))
    #end

    def update_service(self, service_type, service_id, data):
        path = '/services/%s/%s' %(service_type, service_id)
        self.create_node(path, value = json.dumps(data), makepath = True)

        path = '/publishers/%s' % (service_id)
        self.create_node(path, value = json.dumps(data))
    #end

    def insert_service(self, service_type, service_id, data):

        # ensure election path for service type exists
        path = '/election/%s' %(service_type)
        self.create_node(path)

        # preclude duplicate service entry
        sid_set = set()

        # prevent background task from deleting node under our nose
        self._zk_sem.acquire()
        seq_list = self._zk.get_children(path)
        for sequence in seq_list:
            sid, stat = self._zk.get('/election/%s/%s' %(service_type, sequence))
            sid_set.add(sid)
        self._zk_sem.release()
        if not service_id in sid_set:
            path = '/election/%s/node-' %(service_type)
            pp = self._zk.create(path, service_id, makepath = True, sequence = True)
            pat = path + "(?P<id>.*$)"
            mch = re.match(pat, pp)
            seq = mch.group('id')
            data['sequence'] = seq
            self.syslog('ST %s, SID %s not found! Added with sequence %s' %(service_type, service_id, seq))

        self.update_service(service_type, service_id, data)
    #end insert_service

    def delete_service(self, service_type, service_id):
        path = '/services/%s/%s' %(service_type, service_id)
    #end delete_service

    def lookup_service(self, service_type, service_id = None):
        if not self._zk.exists('/services/%s' %(service_type)):
            return None
        if service_id:
            try:
                data, stat = self._zk.get('/services/%s/%s' %(service_type, service_id))
                #self.syslog('[pub] %s:%s ver: %s, data:%s' % (service_type, service_id, stat.version, data.decode('utf-8')))
                return json.loads(data)
            except kazoo.exceptions.NoNodeException:
                return None
        else:
            r = []
            services = self._zk.get_children('/services/%s' %(service_type))
            #self.syslog('[pub] %s has %d publishers' % (service_type, len(services)))
            for service_id in services:
                entry = self.lookup_service(service_type, service_id)
                r.append(entry)
            return r
    #end lookup_service

    def query_service(self, service_type):
        path = '/election/%s' %(service_type)
        if not self._zk.exists(path):
            return None
        seq_list = self._zk.get_children(path)
        seq_list = sorted(seq_list)

        r = []
        for sequence in seq_list:
            service_id, stat = self._zk.get('/election/%s/%s' %(service_type, sequence))
            entry = self.lookup_service(service_type, service_id)
            r.append(entry)
        return r
    #end

    # TODO use include_data available in new versions of kazoo
    # tree structure /services/<service-type>/<service-id>
    def get_all_services(self):
        r = []
        service_types = self._zk.get_children('/services')
        for service_type in service_types:
            services = self.lookup_service(service_type)
            r.extend(services)
        return r
    #end

    def insert_client(self, service_type, service_id, client_id, blob, ttl):
        data = {'ttl': ttl, 'blob': blob}

        path = '/services/%s/%s/%s' % (service_type, service_id, client_id)
        self.create_node(path, value = json.dumps(data))

        path = '/clients/%s/%s/%s' % (service_type, client_id, service_id)
        self.create_node(path, value = json.dumps(data), makepath = True)
    #end insert_client

    def lookup_subscribers(self, service_type, service_id):
        path = '/services/%s/%s' % (service_type, service_id)
        if not self._zk.exists(path):
            return None
        clients = self._zk.get_children(path)
        return clients
    #end lookup_subscribers

    def lookup_client(self, service_type, client_id):
        try:
            datastr, stat = self._zk.get('/clients/%s/%s' %(service_type, client_id))
            data = json.loads(datastr);
            return data
        except kazoo.exceptions.NoNodeException:
            return None
    #end lookup_client

    def insert_client_data(self, service_type, client_id, cldata):
        path = '/clients/%s/%s' % (service_type, client_id)
        self.create_node(path, value = json.dumps(cldata), makepath = True)
    #end insert_client_data

    def lookup_subscription(self, service_type, client_id = None, service_id = None, include_meta = False):
        if not self._zk.exists('/clients/%s' %(service_type)):
            return None
        if client_id and service_id:
            try:
                datastr, stat = self._zk.get('/clients/%s/%s/%s' %(service_type, client_id, service_id))
                """
                self.syslog(' [sub] %s:%s => %s ver: %s, data:%s' \
                    % (service_type, client_id, service_id, stat.version, datastr.decode('utf-8')))
                """
                data = json.loads(datastr); blob = data['blob']
                if include_meta:
                    return (blob, stat, data['ttl'])
                else:
                    return blob
            except kazoo.exceptions.NoNodeException:
                return None
        elif client_id:
            # our version of Kazoo doesn't support include_data :-(
            try:
                services = self._zk.get_children('/clients/%s/%s' %(service_type, client_id))
                #self.syslog(' [sub] %s:%s subscribes to %d services' % (service_type, client_id, len(services)))
                r = []
                for service_id in services:
                    datastr, stat = self._zk.get('/clients/%s/%s/%s' %(service_type, client_id, service_id))
                    data = json.loads(datastr); blob = data['blob']
                    r.append((service_id, blob, stat))
                # sort services in the order of assignment to this client (based on modification time)
                rr = sorted(r, key = lambda entry: entry[2].last_modified)
                return [(service_id, blob) for service_id, blob, stat in rr]
            except kazoo.exceptions.NoNodeException:
                return None
        else:
            clients = self._zk.get_children('/clients/%s' %(service_type))
            #self.syslog('[sub] %s has %d subscribers' % (service_type, len(clients)))
            return clients
    #end lookup_subscription

    # delete client subscription. Cleanup path if possible
    def delete_subscription(self, service_type, client_id, service_id):
        path = '/clients/%s/%s/%s' %(service_type, client_id, service_id)
        self._zk.delete(path)

        # delete client node if all subscriptions gone
        path = '/clients/%s/%s' %(service_type, client_id)
        if self._zk.get_children(path):
            return
        self._zk.delete(path)

        # purge in-memory cache - ideally we are not supposed to know about this
        self._ds.delete_sub_data(client_id, service_type)

        # delete service node if all clients gone
        path = '/clients/%s' %(service_type)
        if self._zk.get_children(path):
            return
        self._zk.delete(path)

        path = '/services/%s/%s/%s' % (service_type, service_id, client_id)
        self._zk.delete(path)
    #end

    # TODO use include_data available in new versions of kazoo
    # tree structure /clients/<service-type>/<client-id>/<service-id>
    # return tuple (service_type, client_id, service_id)
    def get_all_clients(self):
        r = []
        service_types = self._zk.get_children('/clients')
        for service_type in service_types:
            clients = self._zk.get_children('/clients/%s' %(service_type))
            for client_id in clients:
                services = self._zk.get_children('/clients/%s/%s' %(service_type, client_id))
                rr = []
                for service_id in services:
                    (datastr, stat, ttl) = self.lookup_subscription(service_type, client_id, service_id, include_meta = True)
                    rr.append((service_type, client_id, service_id, stat.last_modified, ttl))
                rr = sorted(rr, key = lambda entry: entry[3])
                r.extend(rr)
        return r
    #end get_all_clients
    
    # reset in-use count of clients for each service
    def inuse_loop(self):
        while True:
            service_types = self._zk.get_children('/clients')
            for service_type in service_types:
                clients = self._zk.get_children('/clients/%s' %(service_type))
                for client_id in clients:
                    services = self._zk.get_children('/clients/%s/%s' %(service_type, client_id))
                    for service_id in services:
                        path = '/clients/%s/%s/%s' %(service_type, client_id, service_id)
                        datastr, stat = self._zk.get(path)
                        data = json.loads(datastr)
                        now = time.time()
                        if now > (stat.last_modified + data['ttl'] + disc_consts.TTL_EXPIRY_DELTA):
                            self.delete_subscription(service_type, client_id, service_id)
                            svc_info = self.lookup_service(service_type, service_id)       
                            self.syslog('Expiring st:%s sid:%s cid:%s inuse:%d blob:%s' \
                                %(service_type, service_id, client_id, svc_info['in_use'], data['blob']))
                            svc_info['in_use'] -= 1
                            self.update_service(service_type, service_id, svc_info)
                            self._debug['subscription_expires'] += 1
            gevent.sleep(10)

    def service_oos_loop(self):
        while True:
            service_ids = self._zk.get_children('/publishers')
            for service_id in service_ids:
                data, stat = self._zk.get('/publishers/%s' %(service_id))
                entry = json.loads(data)
                if not self._ds.service_expired(entry, include_down = False):
                    continue
                service_type = entry['service_type']
                path = '/election/%s/node-%s' %(service_type, entry['sequence'])
                if not self._zk.exists(path):
                    continue
                self.syslog('Deleting sequence node %s for service %s:%s' %(path, service_type, service_id))
                self._zk_sem.acquire()
                self._zk.delete(path)
                self._zk_sem.release()
                self._debug['oos_delete'] += 1
            gevent.sleep(self._ds._args.hc_interval)
    #end
