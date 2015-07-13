#!/usr/bin/python

from gevent import monkey
monkey.patch_all()
import logging
import gevent
from gevent.coros import BoundedSemaphore
from kafka import KafkaClient, KeyedProducer, SimpleConsumer, common
from uveserver import UVEServer
import os
import json
import copy
import traceback
import uuid
import struct
import socket

class PartitionHandler(gevent.Greenlet):
    def __init__(self, brokers, group, topic, logger, limit):
        gevent.Greenlet.__init__(self)
        self._brokers = brokers
        self._group = group
        self._topic = topic
        self._logger = logger
        self._limit = limit
        self._uvedb = {}
        self._partoffset = 0
        self._kfk = None

    def msg_handler(self, mlist):
        self._logger.info("%s Reading %s" % (self._topic, str(mlist)))
        return True

    def _run(self):
	pcount = 0
        while True:
            try:
                self._logger.error("New KafkaClient %s" % self._topic)
                self._kfk = KafkaClient(self._brokers , "kc-" + self._topic)
                try:
                    consumer = SimpleConsumer(self._kfk, self._group, self._topic, buffer_size = 4096*4, max_buffer_size=4096*32)
                    #except:
                except Exception as ex:
                    template = "Consumer Failure {0} occured. Arguments:\n{1!r}"
                    messag = template.format(type(ex).__name__, ex.args)
                    self._logger.info("%s" % messag)
                    raise RuntimeError(messag)

                self._logger.error("Starting %s" % self._topic)

                # Find the offset of the last message that has been queued
                consumer.seek(0,2)
                try:
                    mi = consumer.get_message(timeout=0.1)
                    consumer.commit()
                except common.OffsetOutOfRangeError:
                    mi = None
                #import pdb; pdb.set_trace()
                self._logger.info("Last Queued for %s is %s" % \
                                  (self._topic,str(mi)))

                # start reading from last previously processed message
                if mi != None:
                    consumer.seek(0,1)
                else:
                    consumer.seek(0,0)

                if self._limit:
                    raise gevent.GreenletExit

                while True:
                    try:
                        mlist = consumer.get_messages(10,timeout=0.5)
                        if not self.msg_handler(mlist):
                            raise gevent.GreenletExit
                        consumer.commit()
                        pcount += len(mlist) 
                    except TypeError as ex:
                        self._logger.error("Type Error: %s trace %s" % \
                                (str(ex.args), traceback.format_exc()))
                        gevent.sleep(0.1)
                    except common.FailedPayloadsError as ex:
                        self._logger.error("Payload Error: %s" %  str(ex.args))
                        gevent.sleep(0.1)
            except gevent.GreenletExit:
                break
            except AssertionError as ex:
                self._partoffset = ex
                break
            except Exception as ex:
                template = "An exception of type {0} occured. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)
                self._logger.error("%s : traceback %s" % \
                                  (messag, traceback.format_exc()))
                self.stop_partition()
                gevent.sleep(2)

        self._logger.error("Stopping %s pcount %d" % (self._topic, pcount))
        partdb = self.stop_partition()
        return self._partoffset, partdb

class UveStreamProc(PartitionHandler):
    # Arguments:
    #
    #  brokers   : broker list for kafka bootstrap
    #  partition : partition number
    #  uve_topic : Topic to consume
    #  logger    : logging object to use  
    #  callback  : Callback function for reporting the set of the UVEs
    #              that may have changed for a given notification
    #  rsc       : Callback function to check on collector status
    #              and get sync contents for new collectors
    def __init__(self, brokers, partition, uve_topic, logger, callback,
            host_ip, rsc):
        super(UveStreamProc, self).__init__(brokers, "workers",
            uve_topic, logger, False)
        self._uvedb = {}
        self._uvein = {}
        self._uveout = {}
        self._callback = callback
        self._partno = partition
        self._host_ip, = struct.unpack('>I', socket.inet_pton(
                                        socket.AF_INET, host_ip))
        self.disc_rset = set()
        self._resource_cb = rsc

    def resource_check(self, msgs):
        '''
        This function compares the known collectors with the
        list from discovery, and syncs UVE keys accordingly
        '''
        newset , coll_delete, chg_res = self._resource_cb(self._partno, self.disc_rset, msgs)
        for coll in coll_delete:
            self._logger.error("Part %d lost collector %s" % (self._partno, coll))
            self.stop_partition(coll)
        if len(chg_res):
            self.start_partition(chg_res)
        self.disc_rset = newset
        
    def stop_partition(self, kcoll=None):
        clist = []
        if not kcoll:
            clist = self._uvedb.keys()
            # If all collectors are being cleared, clear resoures too
            self.disc_rset = set()
        else:
            clist = [kcoll]
        self._logger.error("Stopping part %d collectors %s" % \
                (self._partno,clist))

        partdb = {}
        chg = {}
        for coll in clist:
            partdb[coll] = {}
            for gen in self._uvedb[coll].keys():
                partdb[coll][gen] = {}
                for tab in self._uvedb[coll][gen].keys():
                    for rkey in self._uvedb[coll][gen][tab].keys():
                        uk = tab + ":" + rkey
                        chg[uk] = None
                        partdb[coll][gen][uk] = \
                            set(self._uvedb[coll][gen][tab][rkey].keys())
                        
            del self._uvedb[coll]
        self._logger.error("Stopping part %d UVEs %s" % \
                (self._partno,str(chg.keys())))
        self._callback(self._partno, chg)
        return partdb

    def start_partition(self, cbdb):
        ''' This function loads the initial UVE database.
            for the partition
        '''
        self._logger.error("Starting part %d collectors %s" % \
                (self._partno, str(cbdb.keys())))
        uves  = {}
        for kcoll,coll in cbdb.iteritems():
            self._uvedb[kcoll] = {}
            for kgen,gen in coll.iteritems():
                self._uvedb[kcoll][kgen] = {}
                for kk in gen.keys():
                    tabl = kk.split(":",1)
                    tab = tabl[0]
                    rkey = tabl[1]
                    if not tab in self._uvedb[kcoll][kgen]:
                        self._uvedb[kcoll][kgen][tab] = {}
                    self._uvedb[kcoll][kgen][tab][rkey] = {}

                    uves[kk] = {}
                    for typ, contents in gen[kk].iteritems():
                        self._uvedb[kcoll][kgen][tab][rkey][typ] = {}
                        self._uvedb[kcoll][kgen][tab][rkey][typ]["c"] = 0
                        self._uvedb[kcoll][kgen][tab][rkey][typ]["u"] = \
                                uuid.uuid1(self._host_ip)
                        uves[kk][typ] = contents
                    
        self._logger.error("Starting part %d UVEs %s" % \
                          (self._partno, str(uves.keys())))
        self._callback(self._partno, uves)

    def contents(self):
        return self._uvedb

    def stats(self):
        ''' Return the UVEKey-Count stats collected over 
            the last time period for this partition, and 
            the incoming UVE Notifs as well.
            Also, the stats should be cleared to prepare
            for the next period of collection.
        '''
        ret_out = copy.deepcopy(self._uveout)
        ret_in  = copy.deepcopy(self._uvein)
        self._uveout = {}
        self._uvein = {}
        return ret_in, ret_out

    def msg_handler(self, mlist):
        self.resource_check(mlist)
        for mm in mlist:
            if mm is None:
                continue
            self._logger.debug("%s Reading offset %d" % \
                    (self._topic, mm.offset))
            if not self.msg_handler_single(mm):
                self._logger.info("%s could not handle %s" % \
                    (self._topic, str(mm)))
                return False
        return True

    def msg_handler_single(self, om):
        self._partoffset = om.offset
        chg = {}
        try:
            uv = json.loads(om.message.value)
            coll = uv["coll"]
            gen = uv["gen"]

            if not self._uvedb.has_key(coll):
                # This partition is not synced yet.
                # Ignore this message
                self._logger.debug("%s Ignoring UVE %s" % (self._topic, str(om)))
                return True

            if not self._uvedb[coll].has_key(gen):
                self._uvedb[coll][gen] = {}

            if (uv["message"] == "UVEUpdate"):
                tabl = uv["key"].split(":",1)
                tab = tabl[0]
                rkey = tabl[1]
                
                if tab not in self._uvedb[coll][gen]:
                    self._uvedb[coll][gen][tab] = {}

                if not rkey in self._uvedb[coll][gen][tab]:
                    self._uvedb[coll][gen][tab][rkey] = {}
         
                removed = False

                # uv["type"] and uv["value"] can be decoded as follows:

                # uv["type"] can be one of the following:
                # - None       # All Types under this UVE are deleted
                #                uv["value"] will not be present
                #                (this option is only for agg UVE updates)
                # - "<Struct>" # uv["value"] refers to this struct

                # uv["value"] can be one of the following:
                # - None      # This Type has been deleted.
                # - {}        # The Type has a value, which is 
                #               not available in this message.
                #               (this option is only for raw UVE updates)
                # - {<Value>} # The Value of the Type
                #               (this option is only for agg UVE updates)

                if uv["type"] is None:
                    # TODO: Handling of delete UVE case
                    return False
                
                if uv["value"] is None:
                    if uv["type"] in self._uvedb[coll][gen][tab][rkey]:
                        del self._uvedb[coll][gen][tab][rkey][uv["type"]]
                    if not len(self._uvedb[coll][gen][tab][rkey]):
                        del self._uvedb[coll][gen][tab][rkey]
                    removed = True

                if not removed: 
                    if uv["type"] in self._uvedb[coll][gen][tab][rkey]:
                        self._uvedb[coll][gen][tab][rkey][uv["type"]]["c"] +=1
                    else:
                        self._uvedb[coll][gen][tab][rkey][uv["type"]] = {}
                        self._uvedb[coll][gen][tab][rkey][uv["type"]]["c"] = 1
                        self._uvedb[coll][gen][tab][rkey][uv["type"]]["u"] = \
                            uuid.uuid1(self._host_ip)
                chg[uv["key"]] = { uv["type"] : uv["value"] }

                # Record stats on UVE Keys being processed
                if not self._uveout.has_key(tab):
                    self._uveout[tab] = {}
                if self._uveout[tab].has_key(uv["key"]):
                    self._uveout[tab][uv["key"]] += 1
                else:
                    self._uveout[tab][uv["key"]] = 1

                # Record stats on the input UVE Notifications
                if not self._uvein.has_key(tab):
                    self._uvein[tab] = {}
                if not self._uvein[tab].has_key(coll):
                    self._uvein[tab][coll] = {}
                if not self._uvein[tab][coll].has_key(gen):
                    self._uvein[tab][coll][gen] = {}
                if not self._uvein[tab][coll][gen].has_key(uv["type"]):
                    self._uvein[tab][coll][gen][uv["type"]] = 1
                else:
                    self._uvein[tab][coll][gen][uv["type"]] += 1

            else:
                # Record stats on UVE Keys being processed
                for tab in self._uvedb[coll][gen].keys():
                    for rkey in self._uvedb[coll][gen][tab].keys():
                        uk = tab + ":" + rkey

                        if not self._uveout.has_key(tab):
                            self._uveout[tab] = {}

                        if self._uveout[tab].has_key(uk):
                            self._uveout[tab][uk] += 1
                        else:
                            self._uveout[tab][uk] = 1
                
                        # when a generator is delelted, we need to 
                        # notify for *ALL* its UVEs
                        chg[uk] = None

                del self._uvedb[coll][gen]

        except Exception as ex:
            template = "An exception of type {0} in uve proc . Arguments:\n{1!r}"
            messag = template.format(type(ex).__name__, ex.args)
            self._logger.info("%s" % messag)
            return False
        else:
            self._callback(self._partno, chg)
        return True
           
if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO,
        format='%(asctime)s %(levelname)s %(message)s')

    workers = {}
    brokers = "localhost:9092,localhost:9093,localhost:9094"
    group = "workers"
    
    kafka = KafkaClient(brokers,str(os.getpid()))
    cons = SimpleConsumer(kafka, group, "ctrl")
    cons.provide_partition_info()
    print "Starting control"
    end_ready = False
    while end_ready == False:
	try:
	    while True:
		part, mmm = cons.get_message(timeout=None)
                mm = mmm.message
		print "Consumed ctrl " + str(mm)
                if mm.value == "start":
                    if workers.has_key(mm.key):
                        print "Dup partition %s" % mm.key
                        raise ValueError
                    else:
                        ph = UveStreamProc(brokers, int(mm.key), "uve-" + mm.key, "alarm-x" + mm.key, logging)
                        ph.start()
                        workers[int(mm.key)] = ph
                elif mm.value == "stop":
                    #import pdb; pdb.set_trace()
                    if workers.has_key(int(mm.key)):
                        ph = workers[int(mm.key)]
                        gevent.kill(ph)
                        res,db = ph.get()
                        print "Returned " + str(res)
                        print "State :"
                        for k,v in db.iteritems():
                            print "%s -> %s" % (k,str(v)) 
                        del workers[int(mm.key)]
                else:
                    end_ready = True
                    cons.commit()
		    gevent.sleep(2)
                    break
	except TypeError:
	    gevent.sleep(0.1)
	except common.FailedPayloadsError as ex:
	    print "Payload Error: " + str(ex.args)
	    gevent.sleep(0.1)
    lw=[]
    for key, value in workers.iteritems():
        gevent.kill(value)
        lw.append(value)

    gevent.joinall(lw)
    print "Ending Consumers"

