import six
import abc

@six.add_metaclass(abc.ABCMeta)
class CassandraDriver(object):

    def __init__(self, server_list, credentials=None, pool_size=None):
        pass

    @abc.abstractmethod
    def get_cf_batch(keyspace_name, cf_name):
        """Get batch object bind to a column family used in insert/remove"""
        pass

    @abc.abstractmethod
    def get_range(self, keyspace_name, cf_name):
        """List all column family rows"""
        pass

    @abc.abstractmethod
    def multiget(self, keyspace_name, cf_name, keys, columns=None, start='',
                 finish='', timestamp=False, num_columns=None):
        """List multiple rows on a column family"""
        pass

    @abc.abstractmethod
    def get(self, keyspace_name, cf_name, key, columns=None, start='',
            finish=''):
        """Fetch one row in a column family"""
        pass

    @abc.abstractmethod
    def get_one_col(self, keyspace_name, cf_name, key, column):
        """Fetch one column of a row in a column family"""
        pass

    @abc.abstractmethod
    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None):
        """Insert columns with value in a row in a column family"""
        pass

    @abc.abstractmethod
    def remove(self, key, columns=None, keyspace_name=None, cf_name=None,
               batch=None):
        """Remove a specified row or a set of columns within the row"""
        pass

