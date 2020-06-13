from builtins import str
from builtins import object
import gevent
import bottle
import uuid


class NeutronApiContext(object):
    def __init__(self, request=None, user_token=None):
        self.undo_callables_with_args = []
        self.request = request
        self.user_token = user_token
        if request.json and 'context' in request.json:
            self.request_id = request.json['context'].get(
                'request_id', 'req-%s' % str(uuid.uuid4()))
        else:
            self.request_id = 'req-%s' % str(uuid.uuid4())
    # end __init__

    def push_undo(self, undo_callable, *args, **kwargs):
        self.undo_callables_with_args.append(
            (undo_callable, (args, kwargs)))
    # end push_undo

    def invoke_undo(self):
        for undo_callable, (args, kwargs) in self.undo_callables_with_args:
            undo_callable(*args, **kwargs)
# end class NeutronApiContext


def get_context():
    return gevent.getcurrent().neutron_api_context


def set_context(api_ctx):
    gevent.getcurrent().neutron_api_context = api_ctx


def clear_context():
    gevent.getcurrent().neutron_api_context = None


def have_context():
    return (hasattr(gevent.getcurrent(), 'neutron_api_context') and
            gevent.getcurrent().neutron_api_context is not None)


def use_context(fn):
    def wrapper(*args, **kwargs):
        if not have_context():
            context_created = True
            user_token = bottle.request.headers.get(
                'X_AUTH_TOKEN', 'no user token for %s %s' %
                (bottle.request.method, bottle.request.url))
            set_context(NeutronApiContext(
                request=bottle.request, user_token=user_token))
        else:
            context_created = False

        try:
            return fn(*args, **kwargs)
        except Exception:
            get_context().invoke_undo()
            raise
        finally:
            if context_created:
                clear_context()
    # end wrapper
    return wrapper
