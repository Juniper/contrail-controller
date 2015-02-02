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

class PartitionHandler(gevent.Greenlet):
    def __init__(self, brokers, partition, group, topic, logger, limit):
        gevent.Greenlet.__init__(self)
        self._brokers = brokers
        self._partition = partition
        self._group = group
        self._topic = topic
        self._logger = logger
        self._limit = limit
        self._partdb = {}
        self._partoffset = None

    def msg_handler(self, om):
        self._partoffset = om.offset
        self._partdb[om.message.key] = om.message.value 
        self._logger.info("%d Reading %s" % (self._partition, str(om)))
        return True

    def _run(self):
	pcount = 0
        while True:
            try:
                self._logger.info("New KafkaClient %d" % self._partition)
                kafka = KafkaClient(self._brokers ,str(os.getpid()))
                try:
                    consumer = SimpleConsumer(kafka, self._group, self._topic, buffer_size = 4096*4, max_buffer_size=4096*32)
                except:
                    self._logger.info("%d consumer failure for %s" % (self._partition , self._topic))
                    raise gevent.GreenletExit

                self._logger.info("Starting %d" % self._partition)

                # Find the offset of the last message that has been queued
                consumer.seek(-1,2)
                try:
                    mi = consumer.get_message(timeout=0.1)
                    consumer.commit()
                except common.OffsetOutOfRangeError:
                    mi = None
                #import pdb; pdb.set_trace()
                self._logger.info("Last Queued for %d is %s" % (self._partition,str(mi)))
                # Now start reading from the beginning
                # consumer.seek(0,0)
                consumer.seek(-1,2)

                if mi != None:
                    count = 0
                    self._logger.info("Syncing %d" % self._partition)
                    loff = mi.offset
                    coff = 0
                    while True:
                        try:
                            mm = consumer.get_message(timeout=None)
                            count +=1
                            if not self.msg_handler(mm):
                                self._logger.info("%d could not process %s" % (self._partition, str(mm)))
                                raise gevent.GreenletExit
                            coff = mm.offset
                            self._logger.info("Syncing offset %d" % coff)
                            if coff == loff:
                                break
                        except Exception as ex:
                            self._logger.info("Sync Error %s" % str(ex))
                            break
                    if coff != loff:
                        self._logger.info("Sync Failed for %d count %d" % (self._partition, count))
                        continue
                    else:
                        self._logger.info("Sync Completed for %d count %d" % (self._partition, count))
                else:
                    consumer.seek(0,2)
                    
                if self._limit:
                    raise gevent.GreenletExit

                while True:
                    try:
                        mm = consumer.get_message(timeout=None)
                        if mm is None:
                            continue
                        pcount += 1
		        if not self.msg_handler(mm):
                            self._logger.info("%d could not handle %s" % (self._partition, str(mm)))
                            raise gevent.GreenletExit
                    except TypeError:
                        gevent.sleep(0.1)
                    except common.FailedPayloadsError as ex:
                        self._logger.info("Payload Error: %s" %  str(ex.args))
                        gevent.sleep(0.1)
            except gevent.GreenletExit:
                break
            except Exception as ex:
                template = "An exception of type {0} occured. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)
                self._logger.info("%s" % messag)
                gevent.sleep(1)
        self._logger.info("Stopping %d pcount %d" % (self._partition, pcount))
        return self._partoffset, self._partdb

class UveStreamProc(PartitionHandler):
    
    # Arguments:
    #
    #  brokers   : broker list for kafka bootstrap
    #  partition : partition number
    #  uve_topic : topic to subscribe to
    #  logger    : logging object to use  
    #  uvedb     : initial UVE DB (map of collector info,
    #                  leading to map of generator info,
    #                  which leads to set of UVE Keys
    #  callback  : Callback function for reporting the set of the UVEs
    #              that may have changed for a given notification
    def __init__(self, brokers, partition, uve_topic, logger, uvedb, callback):
        super(UveStreamProc, self).__init__(brokers, partition, "workers", uve_topic, logger, False)
        self._uvedb = {}
        self._uvedb = uvedb
        self._callback = callback
        uves  = set()
        for kcoll,coll in self._uvedb.iteritems():
            for kgen,gen in coll.iteritems():
                uves.update(gen)
        self._logger.info("Existing UVE keys %s" % str(uves))
        self._callback(uves)

    def contents(self):
        return self._uvedb

    def msg_handler(self, om):
        self._partoffset = om.offset
        chg = set()
        try:
            uv = json.loads(om.message.value)
            self._partdb[om.message.key] = uv
            self._logger.info("%d Reading UVE %s" % (self._partition, str(om)))
            gen = uv["gen"]
            coll = uv["coll"]


            if (uv["message"] == "UVEUpdate"):
                if not self._uvedb.has_key(coll):
                    self._uvedb[coll] = {}
                if not self._uvedb[coll].has_key(gen):
                    self._uvedb[coll][gen] = set()
                self._uvedb[coll][gen].add(uv["key"])
                chg.add(uv["key"])
            else:
                # when a generator is delelted, we need to 
                # notify for *ALL* its UVEs
                if self._uvedb.has_key(coll):
                    if self._uvedb[coll].has_key(gen):
                        chg = self._uvedb[coll][gen]
                        del self._uvedb[coll][gen]
                
                # TODO : For the collector's generator, notify all
                #        UVEs of all generators of the collector
        except Exception as ex:
            template = "An exception of type {0} in uve proc . Arguments:\n{1!r}"
            messag = template.format(type(ex).__name__, ex.args)
            self._logger.info("%s" % messag)
            return False
        else:
            self._callback(chg)
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

