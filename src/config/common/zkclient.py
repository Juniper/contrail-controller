#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import logging
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election
from bitarray import bitarray
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from gevent.coros import BoundedSemaphore

import uuid


class IndexAllocator(object):

    def __init__(self, disc_service, path, size, start_idx=0, reverse=False):
        self._disc_service = disc_service
        self._path = path
        self._start_idx = start_idx
        self._size = size
        self._in_use = bitarray('0')
        self._reverse = reverse
        for idx in self._disc_service.get_children(path):
            idx_int = self._get_bit_from_zk_index(int(idx))
            self._set_in_use(idx_int)
        # end for idx
    # end __init__

    def _get_zk_index_from_bit(self, idx):
        if self._reverse:
            return self._start_idx + self._size - idx
        else:
            return self._start_idx + idx
    # end _get_zk_index

    def _get_bit_from_zk_index(self, idx):
        if self._reverse:
            return self._size - idx + self._start_idx
        else:
            return idx - self._start_idx
    # end _get_bit_from_zk_index

    def _set_in_use(self, idx):
        # set idx bit to 1, extend the _in_use if needed
        if idx >= self._in_use.length():
            temp = bitarray(idx - self._in_use.length())
            temp.setall(0)
            temp.append('1')
            self._in_use.extend(temp)
        else:
            self._in_use[idx] = 1
    # end _set_in_use

    def alloc(self, value=None):
        if self._in_use.all():
            idx = self._in_use.length()
            if idx > self._size:
                raise ResourceExhaustionError()
            self._in_use.append(1)
        else:
            idx = self._in_use.index(0)
            self._in_use[idx] = 1

        idx = self._get_zk_index_from_bit(idx)
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._disc_service.create_node(self._path + id_str, value)
            return idx
        except ResourceExistsError:
            return self.alloc(value)
    # end alloc

    def reserve(self, idx, value=None):
        if idx > (self._start_idx + self._size):
            return None
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._disc_service.create_node(self._path + id_str, value)
            self._set_in_use(self._get_bit_from_zk_index(idx))
            return idx
        except ResourceExistsError:
            self._set_in_use(self._get_bit_from_zk_index(idx))
            return None
    # end reserve

    def delete(self, idx):
        id_str = "%(#)010d" % {'#': idx}
        self._disc_service.delete_node(self._path + id_str)
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < self._in_use.length():
            self._in_use[bit_idx] = 0
    # end delete

    def read(self, idx):
        id_str = "%(#)010d" % {'#': idx}
        id_val = self._disc_service.read_node(self._path+id_str)
        if id_val is not None:
            self._set_in_use(self._get_bit_from_zk_index(idx))
        return id_val
    # end read

    def empty(self):
        return not self._in_use.any()
    # end empty

    @classmethod
    def delete_all(cls, disc_service, path):
        disc_service.delete_node(path, recursive=True)
    # end delete_all

#end class IndexAllocator


class ZookeeperClient(object):

    def __init__(self, module, server_list):
        # logging
        logger = logging.getLogger(module)
        logger.setLevel(logging.INFO)
        try:
            handler = logging.handlers.RotatingFileHandler('/var/log/contrail/' + module + '-zk.log', maxBytes=10*1024*1024, backupCount=5)
        except IOError:
            print "Cannot open log file in /var/log/contrail/"
        else:
            log_format = logging.Formatter('%(asctime)s [%(name)s]: %(message)s', datefmt='%m/%d/%Y %I:%M:%S %p')
            handler.setFormatter(log_format)
            logger.addHandler(handler)

        self._zk_client = \
            kazoo.client.KazooClient(
                server_list,
                handler=kazoo.handlers.gevent.SequentialGeventHandler(),
                logger=logger)

        self._logger = logger
        self._election = None
        self._zk_sem = BoundedSemaphore(1)
        self.connect()
    # end __init__

    # reconnect
    def reconnect(self):
        self._zk_sem.acquire()
        self.syslog("restart: acquired lock; state %s " % self._zk.state)
        # initiate restart if our state is suspended or lost
        if self._zk_client.state != "CONNECTED":
            self.syslog("restart: starting ...")
            try:
                self._zk_client.stop() 
                self._zk_client.close() 
                self._zk_client.start() 
                self.syslog("restart: done")
            except gevent.event.Timeout as e:
                self.syslog("restart: timeout!")
            except Exception as e:
                self.syslog('restart: exception %s' % str(e))
            except Exception as str:
                self.syslog('restart: str exception %s' % str)
        self._zk_sem.release()

    # start 
    def connect(self):
        while True:
            try:
                self._zk_client.start()
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

    def syslog(self, msg):
        if not self._logger:
            return
        self._logger.info(msg)
    # end syslog

    def _zk_listener(self, state):
        if state == "CONNECTED":
            self._election.cancel()
    # end

    def _zk_election_callback(self, func, *args, **kwargs):
        self._zk_client.remove_listener(self._zk_listener)
        func(*args, **kwargs)
        # Exit if running master encounters error or exception
        exit(1)
    # end

    def master_election(self, path, identifier, func, *args, **kwargs):
        self._zk_client.add_listener(self._zk_listener)
        while True:
            self._election = self._zk_client.Election(path, identifier)
            self._election.run(self._zk_election_callback, func, *args, **kwargs)
    # end master_election

    def create_node(self, path, value=None):
        try:
            if value is None:
                value = uuid.uuid4()
            self._zk_client.create(path, str(value), makepath=True)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self.reconnect()
            return self.create_node(path, value)
        except kazoo.exceptions.NodeExistsError:
            current_value = self.read_node(path)
            if current_value == value:
                return True;
            raise ResourceExistsError(path, str(current_value))
    # end create_node

    def delete_node(self, path, recursive=False):
        try:
            self._zk_client.delete(path, recursive=recursive)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self.reconnect()
            self.delete_node(path, recursive=recursive)
        except kazoo.exceptions.NoNodeError:
            pass
    # end delete_node

    def read_node(self, path):
        try:
            value = self._zk_client.get(path)
            return value[0]
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self.reconnect()
            return self.read_node(path)
        except Exception:
            return None
    # end read_node

    def get_children(self, path):
        try:
            return self._zk_client.get_children(path)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self.reconnect()
            return self.get_children(path)
        except Exception:
            return []
    # end read_node

# end class ZookeeperClient
