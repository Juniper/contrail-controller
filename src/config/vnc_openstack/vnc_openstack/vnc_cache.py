import time


class VncCache(object):
    TTL = 10  # seconds

    def __init__(self):
        self._storage = {}
        self._ttl = {}

    def store(self, key, data):
        self._ttl[key] = time.time() + self.TTL
        self._storage[key] = data

    def retrieve(self, key):
        """Retrieve data from cache if storage has a key.

        :param key: Class:Str Storage key
        :return: Data or None
        """
        if not self.has(key):
            return None
        return self._storage[key]

    def has(self, key):
        """Has method invalidates key in cache
        and then checks if it still exists.
        :param key: Class:Str Storage key
        :return: Boolean
        """
        self.invalidate(key)
        return key in self._storage

    def invalidate(self, key):
        """Invalidate checks if key is still valid.
        Invalid keys will be removed from storage.

        :param key: Class:Str Storage key
        """
        now = time.time()
        if key in self._ttl:
            if now > self._ttl[key]:
                self.remove(key)

        if key in self._ttl and key not in self._storage:
            self.remove(key)

        if key in self._storage and key not in self._ttl:
            self.remove(key)

    def remove(self, key):
        try:
            del self._ttl[key]
        except KeyError:
            pass
        try:
            del self._storage[key]
        except KeyError:
            pass

    def clean(self):
        self._storage = {}
        self._ttl = {}
