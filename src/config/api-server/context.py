import gevent
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import cfgm_common

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

        'PRE_DBE_CREATE': 'Before DB Entry Creation',
        'DBE_CREATE': 'DB Entry Creation',
        'POST_DBE_CREATE': 'After DB Entry Creation',

        'PRE_DBE_UPDATE': 'Before DB Entry Update',
        'DBE_UPDATE': 'DB Entry Update',
        'POST_DBE_UPDATE': 'After DB Entry Update',

        'PRE_DBE_DELETE': 'Before DB Entry Delete',
        'DBE_DELETE': 'DB Entry Delete',
        'POST_DBE_DELETE': 'After DB Entry Delete',
    }

    def __init__(self, external_req=None, internal_req=None):
        self.external_req = external_req
        self.internal_req = internal_req
        self.proc_state = self.states['INIT']
        self.undo_callables_with_args = []
    # end __init__

    @property
    def request(self):
        if self.internal_req:
            return self.internal_req
        return self.external_req
    # end request

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
