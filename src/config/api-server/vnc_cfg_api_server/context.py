import gevent
import bottle
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import cfgm_common
from datetime import datetime


class ApiInternalRequest(object):
    def __init__(self, url, urlparts, environ, headers, json_as_dict, query):
        self.url = url
        self.urlparts = urlparts
        self.environ = environ
        self.headers = headers
        self.json = json_as_dict
        self.query = query
    # end __init__
# end class ApiInternalRequest


class ApiContext(object):
    """
    An object holding request-specific context. Holds a reference
    to external(bottle) request or internal(follow-up) request
    """
    states = {
        'INIT': 'Initializing',

        'PENDING_DBE_CREATE': ('Before create action, just after sanity and '
                               'perms checks'),
        'PRE_DBE_ALLOC': 'Before IDs allocation',
        'DBE_ALLOC': 'IDs allocation',
        'PRE_DBE_CREATE': 'Before DB Entry Creation',
        'DBE_CREATE': 'DB Entry Creation',
        'POST_DBE_CREATE': 'After DB Entry Creation',

        'PENDING_DBE_UPDATE': ('Before update action, just after sanity and '
                               'perms checks'),
        'PRE_DBE_UPDATE': 'Before DB Entry Update',
        'DBE_UPDATE': 'DB Entry Update',
        'POST_DBE_UPDATE': 'After DB Entry Update',

        'PENDING_DBE_DELETE': ('Before delete action, just after sanity and '
                               'perms checks'),
        'PRE_DBE_DELETE': 'Before DB Entry Delete',
        'DBE_DELETE': 'DB Entry Delete',
        'POST_DBE_DELETE': 'After DB Entry Delete',

        'PRE_KEYSTONE_REQ': 'Before Keystone request',
        'POST_KEYSTONE_REQ': 'After Keystone request',
    }

    def __init__(self, external_req=None, internal_req=None):
        self.external_req = external_req
        self.internal_req = internal_req
        self.proc_state = self.states['INIT']
        self.undo_callables_with_args = []
        self.proc_times = {}
        self.keystone_response_time = 0
    # end __init__

    @property
    def request(self):
        if self.internal_req:
            return self.internal_req
        return self.external_req
    # end request

    @property
    def path(self):
        return self.request.headers.environ['PATH_INFO']

    def set_proc_time(self, state):
        self.proc_times[state] = datetime.utcnow()

    def get_proc_time(self, state):
        return self.proc_times[state]

    def get_keystone_response_time(self):
        if (('PRE_KEYSTONE_REQ' in self.proc_times)
                and ('POST_KEYSTONE_REQ' in self.proc_times)):
            pre = self.proc_times['PRE_KEYSTONE_REQ']
            post = self.proc_times['POST_KEYSTONE_REQ']
            return (post - pre)
        return None

    def set_state(self, state):
        # set to enumerated or if no mapping, user-passed state-str
        self.proc_state = self.states.get(state, state)
    # end state

    def get_state(self):
        # return enumerated or if no-mapping actual state val
        return self.states.get(self.proc_state, self.proc_state)
    # end get_state

    def push_undo(self, undo_callable, *args, **kwargs):
        self.undo_callables_with_args.append(
            (undo_callable, (args, kwargs)))
    # end push_undo

    def invoke_undo(self, failure_code, failure_msg, logger):
        for undo_callable, (args, kwargs) in self.undo_callables_with_args:
            try:
                undo_callable(*args, **kwargs)
            except Exception as e:
                err_msg = cfgm_common.utils.detailed_traceback()
                logger(err_msg, level=SandeshLevel.SYS_ERR)
    # end invoke_undo
# end class ApiContext


def get_request():
    return gevent.getcurrent().api_context.request


def get_context():
    return gevent.getcurrent().api_context


def set_context(api_ctx):
    gevent.getcurrent().api_context = api_ctx


def clear_context():
    gevent.getcurrent().api_context = None


def have_context():
    return (hasattr(gevent.getcurrent(), 'api_context') and
            gevent.getcurrent().api_context is not None)


def is_internal_request():
    return isinstance(get_context().request, ApiInternalRequest)


def use_context(fn):
    def wrapper(*args, **kwargs):
        if not have_context():
            context_created = True
            set_context(ApiContext(external_req=bottle.request))
        else:
            context_created = False

        try:
            return fn(*args, **kwargs)
        finally:
            if context_created:
                clear_context()
    # end wrapper

    return wrapper
# end use_context
