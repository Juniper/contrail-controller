#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import fixtures

class ConrtailLink (object):
    def __init__ (self, name, _from, _to, _type, fixtr):
        self._type = _type
        self._fixture = fixtr
        self._from = _from
        self._to = _to
        self._name = name

    def fixture (self):
        return self._fixture

class ContrailFixture (fixtures.Fixture):
    def __init__(self):
        self._pdetails = {}
    
    def _get_link_dict (self):
        if not self._pdetails.has_key ('__links__'):
            self._pdetails['__links__'] = {}
        return self._pdetails['__links__']

    def _update_link_dict(self, lname):
        self._pdetails['__links__'][lname] = []

    def links (self):
        return self._get_link_dict ().keys ()

    def get_links (self, lname):
        return self._get_link_dict ().get (lname, [])

    def add_link (self, lname, link):
        if not self.get_links (lname):
            self._update_link_dict(lname)

        return self.get_links (lname).append (link)

    def get_link_fixtures (self, lname):
        return map (lambda l: l.fixture (), self.get_links (lname))
