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
import os
import sys
import traceback
import tempfile

import cgitb

from utils import mask_password


class Hook(cgitb.Hook):
    """A hook to replace sys.excepthook that masks password from tracebacks.
    Derived from the standard cgitb Hook class.
    """

    def handle(self, info=None):
        info = info or sys.exc_info()
        if self.format == "html":
            self.file.write(cgitb.reset())

        formatter = (self.format == "html") and cgitb.html or cgitb.text
        plain = False
        try:
            doc = formatter(info, self.context)
        except:                         # just in case something goes wrong
            doc = ''.join(traceback.format_exception(*info))
            plain = True
        doc = mask_password(doc)

        if self.display:
            if plain:
                doc = doc.replace('&', '&amp;').replace('<', '&lt;')
                self.file.write('<pre>' + doc + '</pre>\n')
            else:
                self.file.write(doc + '\n')
        else:
            self.file.write('<p>A problem occurred in a Python script.\n')

        if self.logdir is not None:
            suffix = ['.txt', '.html'][self.format == "html"]
            (fd, path) = tempfile.mkstemp(suffix=suffix, dir=self.logdir)

            try:
                file = os.fdopen(fd, 'w')
                file.write(doc)
                file.close()
                msg = '%s contains the description of this error.' % path
            except:
                msg = 'Tried to save traceback to %s, but failed.' % path

            if self.format == 'html':
                self.file.write('<p>%s</p>\n' % msg)
            else:
                self.file.write(msg + '\n')
        try:
            self.file.flush()
        except:
            pass


handler = Hook(format='text').handle


def enable(display=1, logdir=None, context=5, format="html"):
    """Install an exception handler that formats tracebacks as HTML.

    The optional argument 'display' can be set to 0 to suppress sending the
    traceback to the browser, and 'logdir' can be set to a directory to cause
    tracebacks to be written to files there."""
    sys.excepthook = Hook(display=display, logdir=logdir,
                          context=context, format=format)
