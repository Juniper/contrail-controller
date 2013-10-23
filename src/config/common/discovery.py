#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import re
import gevent
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election


class DiscoveryService(object):

    def __init__(self, server, port, logger=None):
        self._zk_client = \
            kazoo.client.KazooClient(
                "%s:%s" % (server, port),
                handler=kazoo.handlers.gevent.SequentialGeventHandler(),
                logger=logger)
        self._zk_client.start()
    # end __init__

    def master_election(self, path, identifier, func, *args, **kwargs):
        election = self._zk_client.Election(path, identifier)
        election.run(func, *args, **kwargs)
    # end master_election
    
    def create_node(self, path):
        try:
            self._zk_client.create(path, '')
        except kazoo.exceptions.NodeExistsException:
            pass
    # end create_node

    def alloc_from(self, path, max_id):
        # Create a sequence node at path and return its integer value
        next_seqnum = self._zk_client.create(path, '', sequence=True,
                                             makepath=True)
        pat = path + "(?P<id>.*$)"
        mch = re.match(pat, next_seqnum)
        return int(mch.group('id')) % max_id
    # end alloc_from

    def alloc_from_str(self, path, value=''):
        try:
            # Create a sequence node at path and return its integer value
            next_seqnum = self._zk_client.create(path, str(value),
                                                 sequence=True, makepath=True)
            pat = path + "(?P<id>.*$)"
            mch = re.match(pat, next_seqnum)
            return mch.group('id')
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            return self.alloc_from_str(path, value)
    # end alloc_from_str

    def delete(self, path):
        try:
            self._zk_client.delete(path)
        except (kazoo.exceptions.SessionExpiredError,
                kazoo.exceptions.ConnectionLoss):
            self._zk_client.restart()
            self.delete(path)

    # end delete

    def read(self, path):
        try:
            value = self._zk_client.get(path)
            return value[0]
        except Exception:
            return None
    # end read
# end class DiscoveryService
