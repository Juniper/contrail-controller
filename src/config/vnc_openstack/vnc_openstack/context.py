import gevent
import bottle
import uuid

class NeutronApiContext(object):
    def __init__(self, request=None, user_token=None):
        self.request = request
        self.user_token = user_token
        self.request_id = request.json['context'].get(
            'request_id', 'req-%s' % str(uuid.uuid4()))
    # end __init__
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
            user_token = bottle.request.headers.get('X_AUTH_TOKEN',
                'no user token for %s %s' %(bottle.request.method,
                                            bottle.request.url))
            set_context(NeutronApiContext(
                request=bottle.request, user_token=user_token))
        else:
            context_created = False

        try:
            return fn(*args, **kwargs)
        finally:
            if context_created:
                clear_context()
    # end wrapper
    return wrapper
