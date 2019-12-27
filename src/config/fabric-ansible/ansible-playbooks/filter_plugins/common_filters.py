from builtins import object
from builtins import range
import crypt
import random

from job_manager.job_utils import JobVncApi


class FilterModule(object):
    CHARS = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

    def filters(self):
        return {
            'encrypt': self.crypt_password,
            'shell_escape': self.shell_escape,
            'decrypt_device_password': self.decrypt_device_password
        }

    @classmethod
    def _generate_salt(cls, length):
        chars = []
        for _ in range(length):
            chars.append(random.choice(cls.CHARS))
        return "".join(chars)

    @classmethod
    def crypt_password(cls, strg):
        return crypt.crypt(strg, "$6$%s" % cls._generate_salt(8))

    @classmethod
    def shell_escape(cls, strg):
        return strg.replace("\\", "\\\\").replace("$", "\\$")

    @classmethod
    def decrypt_device_password(cls, encrypted_password, secret_key):
        return JobVncApi.decrypt_password(
                    encrypted_password=encrypted_password,
                    pwd_key=secret_key)
