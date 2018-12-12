import gevent
import bottle
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import cfgm_common


def get_auth_hdrs():
    if have_auth_context():
        return {
            'HTTP_X_DOMAIN_ID': get_auth_context().get('HTTP_X_DOMAIN_ID'),
            'HTTP_X_DOMAIN_NAME': get_auth_context().get('HTTP_X_DOMAIN_NAME'),
            'HTTP_X_PROJECT_ID': get_auth_context().get('HTTP_X_PROJECT_ID'),
            'HTTP_X_PROJECT_NAME': get_auth_context().get('HTTP_X_PROJECT_NAME'),
            'HTTP_X_USER': get_auth_context().get('HTTP_X_USER'),
            'HTTP_X_ROLE': get_auth_context().get('HTTP_X_ROLE'),
        }
    else:
        return {}


def get_auth_context():
    return gevent.getcurrent().auth_context

def set_auth_context(auth_ctx):
    gevent.getcurrent().auth_context = auth_ctx

def clear_auth_context():
    gevent.getcurrent().auth_context = None

def have_auth_context():
    return (hasattr(gevent.getcurrent(), 'auth_context') and
            gevent.getcurrent().auth_context is not None)

def use_auth_context(fn):
    def wrapper(*args, **kwargs):
        if not have_auth_context():
            auth_context_created = True
        else:
            auth_context_created = False
        try:
            return fn(*args, **kwargs)
        finally:
            if auth_context_created:
                clear_auth_context()
    # end wrapper
    return wrapper
# end use_auth_context
