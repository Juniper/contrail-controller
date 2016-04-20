# Copyright http://michaelnielsen.org/blog/consistent-hashing/
#

import bisect
import hashlib

class ConsistentHash:
  '''ConsistentHash(rl,nr) creates a consistent hash object for a
  cluster of resources rl, using nr replicas.

  resource_list is list of resource names. hash_tuples is a list of
  tuples (j,k,hash), where j ranges over resources, k ranges over
  replicas (0...r-1), and hash is the corresponding hash value, in
  the range [0,1).  The tuples are sorted by increasing hash value.

  Class instance method, get_resource(key), returns the name of
  the resource to which key should be mapped.
  get_resources returns the entire list of resources in the ring
  starting from resource to which key should be mapped and moving
  clockwise. This is useful for picking backups or replica
  candidates.
'''

  def __init__(self,resource_list=[], num_replicas=1):
    self.num_replicas = num_replicas
    hash_tuples = [(j,k,my_hash(str(j)+"_"+str(k))) \
                   for j in resource_list \
                   for k in range(self.num_replicas)]
    # Sort the hash tuples based on just the hash values
    hash_tuples.sort(lambda x,y: cmp(x[2],y[2]))
    self.hash_tuples = hash_tuples

  def get_index(self,key):
    '''Returns the index of the resource which key gets sent to.'''
    h = my_hash(key)
    # edge case where we cycle past hash value of 1 and back to 0.
    if h > self.hash_tuples[-1][2]: return 0
    hash_values = map(lambda x: x[2],self.hash_tuples)
    index = bisect.bisect_left(hash_values,h)
    return index

  def get_resource(self,key):
    '''Returns the name of the resource which key gets sent to.'''
    index = self.get_index(key)
    return self.hash_tuples[index][0]

  def get_resources(self, key):
    l = []
    if len(self.hash_tuples) == 0:
        return l
    index = self.get_index(key)
    h = self.hash_tuples[index-1][2] if index else self.hash_tuples[-1][2]
    while h != self.hash_tuples[index][2]:
        machine = self.hash_tuples[index][0]
        if machine not in l:
            l.append(machine)
        index = (index + 1) % len(self.hash_tuples)
    return l

def my_hash(key):
  '''my_hash(key) returns a hash in the range [0,1).'''
  return (int(hashlib.md5(key).hexdigest(),16) % 1000000)/1000000.0
