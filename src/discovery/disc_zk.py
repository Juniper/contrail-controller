#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
import sys
import gevent
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election

import logging
import json
import time
import disc_consts

from gevent.coros import BoundedSemaphore


class DiscoveryZkClient(object):

    def __init__(self, discServer, zk_srv_ip='127.0.0.1',
                 zk_srv_port='2181', reset_config=False):
        self._reset_config = reset_config
        self._service_id_to_type = {}
        self._ds = discServer
        self._zk_sem = BoundedSemaphore(1)
        self._election = None
        self._restarting = False

        zk_endpts = []
        for ip in zk_srv_ip.split(','):
            zk_endpts.append('%s:%s' %(ip, zk_srv_port))

        # logging
        logger = logging.getLogger('discovery-service')
        logger.setLevel(logging.WARNING)
        handler = logging.handlers.RotatingFileHandler('/var/log/contrail/discovery_zk.log', maxBytes=1024*1024, backupCount=10)
        log_format = logging.Formatter('%(asctime)s [%(name)s]: %(message)s', datefmt='%m/%d/%Y %I:%M:%S %p')
        handler.setFormatter(log_format)
        logger.addHandler(handler)

        self._zk = kazoo.client.KazooClient(
            hosts=','.join(zk_endpts),
            handler=kazoo.handlers.gevent.SequentialGeventHandler(),
            logger=logger)
        self._logger = logger

        # connect
        self.connect()

        if reset_config:
            self.delete_node("/services", recursive=True)
            self.delete_node("/clients", recursive=True)
            self.delete_node("/election", recursive=True)

        # create default paths
        self.create_node("/services")
        self.create_node("/clients")
        self.create_node("/election")

        self._debug = {
            'subscription_expires': 0,
            'oos_delete': 0,
            'db_excepts': 0,
        }
    # end __init__

    # Discovery server used for syslog, cleanup etc
    def set_ds(self, discServer):
        self._ds = discServer
    # end set_ds

    def is_restarting(self):
        return self._restarting
    # end is_restarting

    # restart
    def restart(self):
        self._zk_sem.acquire()
        self._restarting = True
        self.syslog("restart: acquired lock; state %s " % self._zk.state)
        # initiate restart if our state is suspended or lost
        if self._zk.state != "CONNECTED":
            self.syslog("restart: starting ...")
            try:
                self._zk.stop() 
                self._zk.close() 
                self._zk.start() 
                self.syslog("restart: done")
            except:
                e = sys.exc_info()[0]
                self.syslog('restart: exception %s' % str(e))
        self._restarting = False
        self._zk_sem.release()

    # start 
    def connect(self):
        while True:
            try:
                self._zk.start()
                break
            except gevent.event.Timeout as e:
                self.syslog(
                    'Failed to connect with Zookeeper -will retry in a second')
                gevent.sleep(1)
            # Zookeeper is also throwing exception due to delay in master election
            except Exception as e:
                self.syslog('%s -will retry in a second' % (str(e)))
                gevent.sleep(1)
        self.syslog('Connected to ZooKeeper!')
    # end

    def start_background_tasks(self):
        # spawn loop to expire subscriptions
        gevent.Greenlet.spawn(self.inuse_loop)

        # spawn loop to expire services
        gevent.Greenlet.spawn(self.service_oos_loop)
    # end

    def syslog(self, log_msg):
        if self._logger is None:
            return
        self._logger.info(log_msg)
    # end

    def get_debug_stats(self):
        return self._debug
    # end

    def _zk_listener(self, state):
        if state == "CONNECTED":
            self._election.cancel()
    # end

    def _zk_election_callback(self, func, *args, **kwargs):
        self._zk.remove_listener(self._zk_listener)
        func(*args, **kwargs)
    # end

    def master_election(self, path, identifier, func, *args, **kwargs):
        self._zk.add_listener(self._zk_listener)
        while True:
            self._election = self._zk.Election(path, identifier)
            self._election.run(self._zk_election_callback, func, *args, **kwargs)
    # end master_election

    def create_node(self, path, value='', makepath=True, sequence=False):
        value = str(value)
        while True:
            try:
                return self._zk.set(path, value)
            except kazoo.exceptions.NoNodeException:
                self.syslog('create %s' % (path))
                return self._zk.create(path, value, makepath=makepath, sequence=sequence)
            except (kazoo.exceptions.SessionExpiredError,
                    kazoo.exceptions.ConnectionLoss):
                self.restart()
    # end create_node

    def get_children(self, path):
        while True:
            try:
                return self._zk.get_children(path)
            except (kazoo.exceptions.SessionExpiredError,
                    kazoo.exceptions.ConnectionLoss):
                self.restart()
            except Exception:
                return []
    # end get_children

    def read_node(self, path):
        while True:
            try:
                data, stat = self._zk.get(path)
                return data,stat
            except (kazoo.exceptions.SessionExpiredError,
                    kazoo.exceptions.ConnectionLoss):
                self.restart()
            except kazoo.exceptions.NoNodeException:
                self.syslog('exc read: node %s does not exist' % path)
                return (None, None)
    # end read_node

    def delete_node(self, path, recursive=False):
        while True:
            try:
                return self._zk.delete(path, recursive=recursive)
            except (kazoo.exceptions.SessionExpiredError,
                    kazoo.exceptions.ConnectionLoss):
                self.restart()
            except kazoo.exceptions.NoNodeException:
                self.syslog('exc delete: node %s does not exist' % path)
                return None
    # end delete_node

    def exists_node(self, path):
        while True:
            try:
                return self._zk.exists(path)
            except (kazoo.exceptions.SessionExpiredError,
                    kazoo.exceptions.ConnectionLoss):
                self.restart()
    # end exists_node

    def service_entries(self):
        service_types = self.get_children('/services')
        for service_type in service_types:
            services = self.get_children('/services/%s' % (service_type))
            for service_id in services:
                data, stat = self.read_node(
                    '/services/%s/%s' % (service_type, service_id))
                entry = json.loads(data)
                yield(entry)

    def subscriber_entries(self):
        service_types = self.get_children('/clients')
        for service_type in service_types:
            subscribers = self.get_children('/clients/%s' % (service_type))
            for client_id in subscribers:
                cl_entry = self.lookup_client(service_type, client_id)
                if cl_entry:
                    yield((client_id, service_type))
    # end

    def update_service(self, service_type, service_id, data):
        path = '/services/%s/%s' % (service_type, service_id)
        self.create_node(path, value=json.dumps(data), makepath=True)
    # end

    def insert_service(self, service_type, service_id, data):

        # ensure election path for service type exists
        path = '/election/%s' % (service_type)
        self.create_node(path)

        # preclude duplicate service entry
        sid_set = set()

        # prevent background task from deleting node under our nose
        seq_list = self.get_children(path)
        # data for election node is service ID
        for sequence in seq_list:
            sid, stat = self.read_node(
                '/election/%s/%s' % (service_type, sequence))
            if sid is not None:
                sid_set.add(sid)
        if not service_id in sid_set:
            path = '/election/%s/node-' % (service_type)
            pp = self.create_node(
                path, service_id, makepath=True, sequence=True)
            pat = path + "(?P<id>.*$)"
            mch = re.match(pat, pp)
            seq = mch.group('id')
            data['sequence'] = seq
            self.syslog('ST %s, SID %s not found! Added with sequence %s' %
                        (service_type, service_id, seq))
    # end insert_service

    # forget service and subscribers
    def delete_service(self, service_type, service_id, recursive = False):
        #if self.lookup_subscribers(service_type, service_id):
        #    return

        path = '/services/%s/%s' %(service_type, service_id)
        self.delete_node(path, recursive = recursive)

        # delete service node if all services gone
        path = '/services/%s' %(service_type)
        if self.get_children(path):
            return
        self.delete_node(path)
     #end delete_service

    def lookup_service(self, service_type, service_id=None):
        if not self.exists_node('/services/%s' % (service_type)):
            return None
        if service_id:
            data = None
            path = '/services/%s/%s' % (service_type, service_id)
            datastr, stat = self.read_node(path)
            if datastr:
                data = json.loads(datastr)
                clients = self.get_children(path)
                data['in_use'] = len(clients)
            return data
        else:
            r = []
            services = self.get_children('/services/%s' % (service_type))
            for service_id in services:
                entry = self.lookup_service(service_type, service_id)
                r.append(entry)
            return r
    # end lookup_service

    def query_service(self, service_type):
        path = '/election/%s' % (service_type)
        if not self.exists_node(path):
            return None
        seq_list = self.get_children(path)
        seq_list = sorted(seq_list)

        r = []
        for sequence in seq_list:
            service_id, stat = self.read_node(
                '/election/%s/%s' % (service_type, sequence))
            entry = self.lookup_service(service_type, service_id)
            r.append(entry)
        return r
    # end

    # TODO use include_data available in new versions of kazoo
    # tree structure /services/<service-type>/<service-id>
    def get_all_services(self):
        r = []
        service_types = self.get_children('/services')
        for service_type in service_types:
            services = self.lookup_service(service_type)
            r.extend(services)
        return r
    # end

    def insert_client(self, service_type, service_id, client_id, blob, ttl):
        data = {'ttl': ttl, 'blob': blob}

        path = '/services/%s/%s/%s' % (service_type, service_id, client_id)
        self.create_node(path, value=json.dumps(data))

        path = '/clients/%s/%s/%s' % (service_type, client_id, service_id)
        self.create_node(path, value=json.dumps(data), makepath=True)
    # end insert_client

    def lookup_subscribers(self, service_type, service_id):
        path = '/services/%s/%s' % (service_type, service_id)
        if not self.exists_node(path):
            return None
        clients = self.get_children(path)
        return clients
    # end lookup_subscribers

    def lookup_client(self, service_type, client_id):
        try:
            datastr, stat = self.read_node(
                '/clients/%s/%s' % (service_type, client_id))
            data = json.loads(datastr) if datastr else None
        except ValueError:
            self.syslog('raise ValueError st=%s, cid=%s' %(service_type, client_id))
            data = None
        return data
    # end lookup_client

    def insert_client_data(self, service_type, client_id, cldata):
        path = '/clients/%s/%s' % (service_type, client_id)
        self.create_node(path, value=json.dumps(cldata), makepath=True)
    # end insert_client_data

    def lookup_subscription(self, service_type, client_id=None,
                            service_id=None, include_meta=False):
        if not self.exists_node('/clients/%s' % (service_type)):
            return None
        if client_id and service_id:
            try:
                datastr, stat = self.read_node(
                    '/clients/%s/%s/%s'
                    % (service_type, client_id, service_id))
                data = json.loads(datastr)
                blob = data['blob']
                if include_meta:
                    return (blob, stat, data['ttl'])
                else:
                    return blob
            except kazoo.exceptions.NoNodeException:
                return None
        elif client_id:
            # our version of Kazoo doesn't support include_data :-(
            try:
                services = self.get_children(
                    '/clients/%s/%s' % (service_type, client_id))
                r = []
                for service_id in services:
                    datastr, stat = self.read_node(
                        '/clients/%s/%s/%s'
                        % (service_type, client_id, service_id))
                    if datastr:
                        data = json.loads(datastr)
                        blob = data['blob']
                        r.append((service_id, blob, stat))
                # sort services in the order of assignment to this client
                # (based on modification time)
                rr = sorted(r, key=lambda entry: entry[2].last_modified)
                return [(service_id, blob) for service_id, blob, stat in rr]
            except kazoo.exceptions.NoNodeException:
                return None
        else:
            clients = self.get_children('/clients/%s' % (service_type))
            return clients
    # end lookup_subscription

    # delete client subscription. Cleanup path if possible
    def delete_subscription(self, service_type, client_id, service_id):
        path = '/clients/%s/%s/%s' % (service_type, client_id, service_id)
        self.delete_node(path)

        path = '/services/%s/%s/%s' % (service_type, service_id, client_id)
        self.delete_node(path)

        # delete client node if all subscriptions gone
        path = '/clients/%s/%s' % (service_type, client_id)
        if self.get_children(path):
            return
        self.delete_node(path)

        # purge in-memory cache - ideally we are not supposed to know about
        # this
        self._ds.delete_sub_data(client_id, service_type)

        # delete service node if all clients gone
        path = '/clients/%s' % (service_type)
        if self.get_children(path):
            return
        self.delete_node(path)
    # end

    # TODO use include_data available in new versions of kazoo
    # tree structure /clients/<service-type>/<client-id>/<service-id>
    # return tuple (service_type, client_id, service_id)
    def get_all_clients(self):
        r = []
        service_types = self.get_children('/clients')
        for service_type in service_types:
            clients = self.get_children('/clients/%s' % (service_type))
            for client_id in clients:
                services = self.get_children(
                    '/clients/%s/%s' % (service_type, client_id))
                rr = []
                for service_id in services:
                    (datastr, stat, ttl) = self.lookup_subscription(
                        service_type, client_id, service_id, include_meta=True)
                    rr.append(
                        (service_type, client_id, service_id,
                         stat.last_modified, ttl))
                rr = sorted(rr, key=lambda entry: entry[3])
                r.extend(rr)
        return r
    # end get_all_clients

    # reset in-use count of clients for each service
    def inuse_loop(self):
        while True:
            service_types = self.get_children('/clients')
            for service_type in service_types:
                clients = self.get_children('/clients/%s' % (service_type))
                for client_id in clients:
                    services = self.get_children(
                        '/clients/%s/%s' % (service_type, client_id))
                    for service_id in services:
                        path = '/clients/%s/%s/%s' % (
                            service_type, client_id, service_id)
                        datastr, stat = self.read_node(path)
                        data = json.loads(datastr)
                        now = time.time()
                        exp_t = stat.last_modified + data['ttl'] +\
                            disc_consts.TTL_EXPIRY_DELTA
                        if now > exp_t:
                            self.delete_subscription(
                                service_type, client_id, service_id)
                            self.syslog(
                                'Expiring st:%s sid:%s cid:%s'
                                % (service_type, service_id, client_id))
                            self._debug['subscription_expires'] += 1
            gevent.sleep(10)

    def service_oos_loop(self):
        if self._ds._args.hc_interval <= 0:
            return

        while True:
            for entry in self.service_entries():
                if not self._ds.service_expired(entry, include_down=False):
                    continue
                service_type = entry['service_type']
                service_id   = entry['service_id']
                path = '/election/%s/node-%s' % (
                    service_type, entry['sequence'])
                if not self.exists_node(path):
                    continue
                self.syslog('Deleting sequence node %s for service %s:%s' %
                        (path, service_type, service_id))
                self.delete_node(path)
                entry['sequence'] = -1
                self.update_service(service_type, service_id, entry)
                self._debug['oos_delete'] += 1
            gevent.sleep(self._ds._args.hc_interval)
    # end
