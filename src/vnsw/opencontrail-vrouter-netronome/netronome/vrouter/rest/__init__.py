# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import absolute_import

import json
import tornado.web
import werkzeug

from tornado.web import HTTPError

from netronome.vrouter import tc

__all__ = [
    'HTTPSuccessTc', 'HTTPRedirectTc', 'ContentTypeTc', 'parse_content_type'
]

# these methods don't have a request body, it makes no sense to require a
# content-type on the request
_METHODS_WITHOUT_REQUEST_BODY = ('GET', 'HEAD', 'DELETE')


class HTTPSuccessTc(tc.Tc):
    def check(self, value):
        return isinstance(value, int) and 200 <= value and value <= 299

    def get_message(self, value):
        return 'status code {} is not an HTTP success'.format(value)


class HTTPRedirectTc(tc.Tc):
    def check(self, value):
        return isinstance(value, int) and 300 <= value and value <= 399

    def get_message(self, value):
        return 'status code {} is not an HTTP redirect'.format(value)


def parse_content_type(headers):
    """Parse a Content-Type header."""
    return werkzeug.parse_options_header(headers.get('Content-Type'))


class ContentTypeTc(tc.Tc):
    def __init__(self, expected_content_type, **kwds):
        super(tc.Tc, self).__init__(**kwds)
        self.expected_content_type = expected_content_type

    def check(self, value):
        content_type = parse_content_type(value)
        return content_type[0] == self.expected_content_type

    def get_message(self, value):
        content_type = parse_content_type(value)
        return 'expected Content-Type "{}", got "{}"'.format(
            self.expected_content_type, content_type[0]
        )


class JsonRequestHandler(tornado.web.RequestHandler):
    """Handler for application/json request body."""
    def initialize(self, **kwds):
        super(JsonRequestHandler, self).initialize(**kwds)
        self.json_args = None

    def prepare(self, **kwds):
        super(JsonRequestHandler, self).prepare(**kwds)
        assert self.json_args is None

        if self.request.method in _METHODS_WITHOUT_REQUEST_BODY:
            return

        tc = ContentTypeTc('application/json')
        if not tc.check(self.request.headers):
            raise HTTPError(415)

        try:
            self.json_args = json.loads(self.request.body)
        except ValueError:
            raise HTTPError(400)
