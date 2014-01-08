#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import re
import gevent
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election
from bitarray import bitarray
from cfgm_common.exceptions import ResourceExhaustionError

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
    #end _get_bit_from_zk_index
    
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
    
    def alloc(self, value):
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
            self._disc_service.create_node(self._path+id_str, value)
            return idx
        except kazoo.exceptions.NodeExistsError:
            return self.alloc(value)
    # end alloc
    
    def reserve(self, idx, value=''):
        if idx > (self._start_idx + self._size):
            return None
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._disc_service.create_node(self._path+id_str, value)
            self._set_in_use(self._get_bit_from_zk_index(idx))
            return idx
        except kazoo.exceptions.NodeExistsError:
            return None
    # end reserve

    def delete(self, idx):
        try:
            id_str = "%(#)010d" % {'#': idx}
            self._disc_service.delete_node(self._path+id_str)
        except kazoo.exceptions.NoNodeError:
            pass
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < self._in_use.length():
            self._in_use[bit_idx] = 0
    #end delete
    
    def read(self, idx):
        id_str = "%(#)010d" % {'#': idx}
        return self._disc_service.read_node(self._path+id_str)
    # end read
    
    def empty(self):
        return not self._in_use.any()
    # end empty
#end class IndexAllocator

    
class DiscoveryService(object):

    def __init__(self, server_list, logger=None):
        self._zk_client = \
            kazoo.client.KazooClient(server_list,
                handler=kazoo.handlers.gevent.SequentialGeventHandler(),
                logger=logger)

        # connect
        while True:
            try:
                self._zk_client.start()
                break
            except gevent.event.Timeout as e:
                gevent.sleep(1)
            # Zookeeper is also throwing exception due to delay in master election
            except Exception as e:
                gevent.sleep(1)
    # end __init__

    def master_election(self, path, identifier, func, *args, **kwargs):
        election = self._zk_client.Election(path, identifier)
        election.run(func, *args, **kwargs)
    # end master_election
    
    def create_node(self, path, value):
        try:
            self._zk_client.create(path, str(value), makepath=True)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            return self.create_node(path, value)

    # end create_node

    def delete_node(self, path):
        try:
            self._zk_client.delete(path)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            self.delete_node(path)
    # end delete_node

    def read_node(self, path):
        try:
            value = self._zk_client.get(path)
            return value[0]
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            return self.read_node(path)
        except Exception:
            return None
    # end read_node
    
    def get_children(self, path):
        try:
            return self._zk_client.get_children(path)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            return self.get_children(path)
        except Exception:
            return []
    # end read_node

# end class DiscoveryService
