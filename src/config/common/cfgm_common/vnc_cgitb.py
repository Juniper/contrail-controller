"""Traceback formatting with masked password for vnc config daemons.

To enable this module, do:

    import vnc_cgitb; vnc_cgitb.enable()

at the top of your script.  The optional arguments to enable() are:

    display     - if true, tracebacks are displayed in the web browser
    logdir      - if set, tracebacks are written to files in this directory
    context     - number of lines of source code to show for each stack frame
    format      - 'text' or 'html' controls the output format

By default, tracebacks are displayed but not saved, the context is 5 lines
and the output format is 'html' (for backwards compatibility with the
original use of this module)

If you have caught an exception and want vnc_cgitb to display it
after masking passwords, call vn_cgitb.handler().  The optional argument to
handler() is a 3-item tuple (etype, evalue, etb) just like the value of
sys.exc_info().  The default handler displays output as text.

Alternatively, if you have caught an exception and want to collect the
formatted/masked traceback as string and use it ,
>>> string_buf = StringIO()
>>> vnc_cgitb.Hook(file=string_buf, format="text").handle(sys.exc_info())
>>> formatted_tb = string_buf.get_value()

"""
from __future__ import unicode_literals
from future import standard_library
standard_library.install_aliases()
import re
import sys
import cgitb
from six import StringIO


# License: Apache-2.0
# https://github.com/openstack/deb-oslo.utils

# Masking of password from openstack/common/log.py
_SANITIZE_KEYS = ['adminPass', 'admin_pass', 'password', 'admin_password']

# NOTE(ldbragst): Let's build a list of regex objects using the list of
# _SANITIZE_KEYS we already have. This way, we only have to add the new key
# to the list of _SANITIZE_KEYS and we can generate regular expressions
# for XML and JSON automatically.
_SANITIZE_PATTERNS = []
_FORMAT_PATTERNS = [r'(%(key)s\s*[=]\s*[\"\']).*?([\"\'])',
                    r'(<%(key)s>).*?(</%(key)s>)',
                    r'([\"\']%(key)s[\"\']\s*:\s*[\"\']).*?([\"\'])',
                    r'([\'"].*?%(key)s[\'"]\s*:\s*u?[\'"]).*?([\'"])']

for key in _SANITIZE_KEYS:
    for pattern in _FORMAT_PATTERNS:
        reg_ex = re.compile(pattern % {'key': key}, re.DOTALL)
        _SANITIZE_PATTERNS.append(reg_ex)


def mask_password(message, secret="***"):
    """Replace password with 'secret' in message.
    :param message: The string which includes security information.
    :param secret: value with which to replace passwords.
    :returns: The unicode value of message with the password fields masked.

    For example:

    >>> mask_password("'adminPass' : 'aaaaa'")
    "'adminPass' : '***'"
    >>> mask_password("'admin_pass' : 'aaaaa'")
    "'admin_pass' : '***'"
    >>> mask_password('"password" : "aaaaa"')
    '"password" : "***"'
    >>> mask_password("'original_password' : 'aaaaa'")
    "'original_password' : '***'"
    >>> mask_password("u'original_password' :   u'aaaaa'")
    "u'original_password' :   u'***'"
    """
    if not any(key in message for key in _SANITIZE_KEYS):
        return message

    secret = r'\g<1>' + secret + r'\g<2>'
    for pattern in _SANITIZE_PATTERNS:
        message = re.sub(pattern, secret, message)
    return message


class Hook(cgitb.Hook):
    """A hook to replace sys.excepthook that masks password from tracebacks.
    Derived from the standard cgitb Hook class.
    """

    def handle(self, info=None):
        # Format the traceback and store it in StringIO buffer
        local_buf = StringIO()
        kwargs = {'display': self.display,
                  'logdir': self.logdir,
                  'context': self.context,
                  'file': local_buf,
                  'format': self.format,
                  }
        cgitb.Hook(**kwargs).handle(info)
        # Retrive the formatted traceback from StringIO buffer,
        # mask the passwords and write to the IO
        doc = local_buf.getvalue()
        local_buf.close()
        self.file.write(mask_password(doc))
        try:
            self.file.flush()
        except:
            # Ignore errors during flush.
            pass


handler = Hook(format='text').handle


def enable(display=1, logdir=None, context=5, format="html"):
    """Install an exception handler that formats tracebacks as HTML.

    The optional argument 'display' can be set to 0 to suppress sending the
    traceback to the browser, and 'logdir' can be set to a directory to cause
    tracebacks to be written to files there."""
    sys.excepthook = Hook(display=display, logdir=logdir,
                          context=context, format=format)
