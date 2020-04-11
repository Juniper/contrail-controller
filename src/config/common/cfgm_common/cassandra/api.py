import abc
import collections
import six

from sandesh_common.vns import constants as vns_constants


# Name to ID mapping keyspace + tables
UUID_KEYSPACE_NAME = vns_constants.API_SERVER_KEYSPACE_NAME

# TODO describe layout
OBJ_UUID_CF_NAME = 'obj_uuid_table'

# TODO describe layout
OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

# key: object type, column ($type:$id, uuid)
# where type is entity object is being shared with. Project initially
OBJ_SHARED_CF_NAME = 'obj_shared_table'

UUID_KEYSPACE = {
    UUID_KEYSPACE_NAME: {
        OBJ_UUID_CF_NAME: {
            'cf_args': {
                'autopack_names': False,
                'autopack_values': False,
            },
        },
        OBJ_FQ_NAME_CF_NAME: {
            'cf_args': {
                'autopack_values': False,
            },
        },
        OBJ_SHARED_CF_NAME: {}
    }
}

# Useragent datastore keyspace + tables (used by neutron plugin currently)
USERAGENT_KEYSPACE_NAME = vns_constants.USERAGENT_KEYSPACE_NAME
USERAGENT_KV_CF_NAME = 'useragent_keyval_table'


OptionsDefault = {
    'db_prefix': '',
    'rw_keyspaces': {},
    'ro_keyspaces': {},
    'logger': None,
    'generate_url': None,
    'reset_config': False,
    'credential': None,
    'walk': True,
    'obj_cache_entries': 0,
    'obj_cache_exclude_types': None,
    'debug_obj_cache_types': None,
    'log_response_time': None,
    'ssl_enabled': False,
    'ca_certs': None,
    'pool_size': 0,
}
OptionsType = collections.namedtuple(
    'Options', OptionsDefault.keys())


@six.add_metaclass(abc.ABCMeta)
class CassandraDriver(object):

    options = dict(OptionsDefault)

    def __init__(self, server_list, **options):
        self.options.update(options)
        self.options = OptionsType(**self.options)

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

