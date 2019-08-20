import pickle
import datetime
import json
import sys
import requests
import os
import lxml
from bs4 import BeautifulSoup
import xml.etree.ElementTree as ET
thirdList = []
class Interospect:
    __slots__ = ["url","indent","secondList","mainDictionary","fname","fileName","ip","port"]
    def __init__(self,indent,mainDictionary,secondList,fname,fileName,ip,port,url):
        self.indent = indent
        self.secondList = secondList
        self.mainDictionary = mainDictionary
        self.fname = fname
        self.fileName = fileName
        self.ip = ip
        self.port = port
        self.url = url
    def convertTime(self,soupObj):
        if len(soupObj.find_all('element')) != 0:
            for ele in soupObj.find_all('element'):
                if (not ele.text.split(" ")[0].isdigit()):
                    continue
                if (len(ele.text.split(" ")[0])) < 12:
                    continue
                ti=(float(ele.text.split(" ")[0])/1000000.0)
                tim=str(datetime.datetime.fromtimestamp(ti).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3])
                ele.string = "{ "+tim+"} "+ (" ".join(ele.text.split(" ")[1:]))
            return soupObj
        else:
            return soupObj
 
    def printHelp(self): 
        print("\t\t\t\t\t\t------HELP------\n\n")
        print("Interactive mode :\n")
        print("python cli.py --ip <IP-ADDRESS> --port <PORT-NUMBER or Name of Interospect > -o <fileName> -i \n ")
        print("Examples : \n")
        print("python cli.py --ip 10.84.13.53 --port 8085 -o interactive.txt -i \n ")
        print("python cli.py --ip 10.84.13.53 --port agent -o interactive.txt -i \n ")
        print("python cli.py --port 8085 -o interactive.txt -i \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("Dump mode :\n")
        print("python cli.py --ip <IP-ADDRESS> --port <PORT-NUMBER or Name of Interospect > -o <fileName> --dump \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("Sort Sandesh Trace Buffers :")
        print("python cli.py --ip <IP-ADDRESS> --port <PORT-NUMBER or Name of Interospect > -o <fileName> --sort \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("Number mode :\n")
        print("python cli.py --ip <IP-ADDRESS> --port <PORT-NUMBER or Name of Interospect > -o <fileName> -num <Number sequence> \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("Examples : \n")
        print("python cli.py --ip 10.84.13.53  --port 8085 -o num.txt -num 1 3 \n ")
        print("python cli.py --ip 10.84.13.53  --port 8085 -o num.txt -num 16 2 1 \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("Name :")
        print("python cli.py --ip <IP-ADDRESS> --port <PORT-NUMBER or Name of Interospect > -o <fileName> -name <Name of Component> \n ")
        print("Examples : ")
        print("python cli.py --ip 10.84.13.53  --port 8085 -o num.txt -name  ShowIFMapAgentReq \n ")
        print("python cli.py --ip 10.84.13.53  --port 8085 -o num.txt -name Acl  \n ")
        print("------------------------------------------------------------------------------------------------------------------------------------------------\n")
        print("For all the above modes ,optional arguements are :\n 1) --ip <IP-ADDRESS> (If nothing is specified , localhost will be used . ) \n 2) -o <fileName> (If nothing is specified , output will be \      in a file name IP-ADDRESS/PORT.txt )")
                                            


    def printRecur(self,root):
        deltaVal = 8
        str=' '*self.indent + '%s: %s' % (root.tag.title(), root.attrib.get('name', root.text))
        #print str
        self.fileName.write(str+"\n")
        

        self.indent += deltaVal
        for elem in root.getchildren():
            self.printRecur(elem)
        self.indent -= deltaVal

    def printAndWrite(self,urlT):
        pageT = requests.get(urlT).text
        soupT = BeautifulSoup(pageT,"xml")
        soupT = self.convertTime(soupT)
        xb = open("dumpBuf.xml","w")
        xb.write(str(soupT))
        xb.close()
        treeBuf = ET.ElementTree(file='dumpBuf.xml')
        self.printRecur(treeBuf.getroot())
    def getTime(self,epT):
        epT=float(epT)/1000000.0
        epT=str(datetime.datetime.fromtimestamp(epT).strftime('%Y-%m-%d %H:%M:%S.%f'))
        return epT



    def num(self,numList):
        global thirdList
        numberList = numList
        print(numberList)
        if len(numberList) == 2 :
            first = int(numList[0])
            second = int(numList[1])
    
            if len(self.mainDictionary[first]) == 1 :
                urlT = self.url + "/%s"%(self.mainDictionary[first][0])
    
            else:
                if len(self.mainDictionary[first][0][second]) == 1:
                    urlT = self.url + "/Snh_%s"%(self.mainDictionary[first][0][second][0])
                else:
                    urlT = self.url + "/Snh_%s"%(self.mainDictionary[first][0][second][1])
        elif len(numberList) == 3:
            first = int(numList[0])
            second = int(numList[1])
            third = int(numList[2])
            urlT = self.url + "/Snh_%s?x=%s"%(self.mainDictionary[first][0][second][0][third][1],self.mainDictionary[first][0][second][0][third][0])    

        self.printAndWrite(urlT)
        self.fileName.close()
        os.system("less %s"%(self.fname))
                
    def name(self,name):
    
        global thirdList
        if name in self.secondList:
            print("Here")
            urlT = self.url +"/Snh_%s"%(name)
            self.printAndWrite(urlT)
        else:
            #print(thirdList)
            for tu in thirdList:
                if name in tu:
                    self.fileName.write("%s / %s \n"%(tu[1],tu[0]))
                    urlT = self.url +"/Snh_%s?x=%s"%(tu[1],tu[0])
                    self.printAndWrite(urlT)
                    print("\n\n")
        self.fileName.close()
        os.system("less %s"%(self.fname))

    def interactive(self):
        for md in self.mainDictionary.keys():
            if len(self.mainDictionary[md]) == 1:
                print("%s : %s  (No further contents to show) "%(md,self.mainDictionary[md][0]))
            else:
                print("%s : %s "%(md,self.mainDictionary[md][1]))
        fc = int(raw_input("Enter your choice : "))
        if len(self.mainDictionary[fc]) == 1:
            print("Nothing to show...Exiting")
            exit(0)
        else:
            for sd in self.mainDictionary[fc][0].keys():
                if len(self.mainDictionary[fc][0][sd]) == 1:
                    value = self.mainDictionary[fc][0][sd][0]
                else:
                    value = self.mainDictionary[fc][0][sd][1]
                print("%s : %s "%(sd,value))
        sc = int(raw_input("Enter your choice : "))
        if len(self.mainDictionary[fc][0][sc]) == 1:
            urlT = self.url +"/Snh_%s"%(self.mainDictionary[fc][0][sc][0])
            self.printAndWrite(urlT)
            os.system("less %s"%(self.fname))
        else:
            #urlT = url +"/Snh_%s"%(self.mainDictionary[fc][0][sc][1])
            #printAndWrite(urlT)
        
            for td in self.mainDictionary[fc][0][sc][0].keys():
                print("%s : %s / %s "%(td,self.mainDictionary[fc][0][sc][0][td][1],self.mainDictionary[fc][0][sc][0][td][0]))
            tc = int(raw_input("Enter your choice : "))
            urlT = self.url +"/Snh_%s?x=%s"%(self.mainDictionary[fc][0][sc][0][tc][1],self.mainDictionary[fc][0][sc][0][tc][0])
       
            self.printAndWrite(urlT)
            self.fileName.close()
            os.system("less %s"%(self.fname))
    def dump(self):
        for md in self.mainDictionary.keys():
            if len(self.mainDictionary[md]) == 1:
                self.fileName.write("%s   . Nothing to show \n" %(self.mainDictionary[md][0]))
            else:
                secondDict = self.mainDictionary[md][0]
                for sk in secondDict.keys():
                    if len(secondDict[sk]) == 1:
                        self.fileName.write("%s  /   %s \n"%(self.mainDictionary[md][1],secondDict[sk][0]))
                        urlT = self.url +"/Snh_%s"%(secondDict[sk][0])
                        self.printAndWrite(urlT)
                        self.fileName.write("\t\t\t\t-------------------------------********************************************___________________________________________________\n")
                    else:
                        urlT = self.url +"/Snh_%s"%(secondDict[sk][1])
                        self.fileName.write("%s / %s \n" %(self.mainDictionary[md][1],secondDict[sk][1]))
                        self.printAndWrite(urlT)
                        self.fileName.write("\t\t\t\t\t\t******************************SUB-CONTENTS******************************\n")
                        thirdDict = secondDict[sk][0]
                        for tk in thirdDict.keys():
                            urlT = self.url+"/Snh_%s?x=%s"%(thirdDict[tk][1],thirdDict[tk][0])
                            self.fileName.write(" %s --> %s / %s \n"%(secondDict[sk][1],thirdDict[tk][1],thirdDict[tk][0]))
                            self.printAndWrite(urlT)
                            self.fileName.write("\t\t\t\t\t\t\t********************************************\n\n")
                    self.fileName.write("\t\t\t\t-------------------------------********************************************___________________________________________________\n")
    def sort(self):
        global thirdList
        urlTemp = self.url+"/Snh_SandeshTraceRequest?x="
        listOfBuf = [tp for tp in thirdList if "SandeshTraceRequest" in tp]
        bufferDict={}
        for lob in listOfBuf:
            ut =urlTemp+lob[0]
            pageSort = requests.get(ut).text
            soupSort = BeautifulSoup(pageSort,"xml")
            eleList = soupSort.find_all('element')
            if len(eleList) == 0:
                continue
            else:
                for si in eleList:
                    tempTime = si.text.split(" ")
                    if tempTime[0].isdigit() and len(tempTime[0]) >= 12:
                        if float(tempTime[0]) in bufferDict.keys():
                            bufferDict[tempTime[0]].append(" ".join(tempTime[1:]))
                        else:
                            bufferDict[tempTime[0]]=[]
                            bufferDict[tempTime[0]].append("  %s:\t"%(lob[0])+(" ".join(tempTime[1:])) )
                    #print bufferDict[tempTime[0]]
        #print(sorted(bufferDict))

        #fileSort = open("SortedBuffers.txt","w")

        for ke in sorted(bufferDict):
            self.fileName.write("\n"+self.getTime(ke))
            #print getTime(ke)
            for items in bufferDict[ke]:

                self.fileName.write(""+items+"\n")
            #fileSort.write("\n")
def check(soupObj):
    global thirdList
    listOfLinks=[]
    a=soupObj.find_all(link=True)
    for t in a:
        if str(t.text) != "":
            tup=(str(t.text),str(t["link"]))
            if tup not in listOfLinks and tup not in thirdList:
                thirdList.append(tup)
                listOfLinks.append(tup)
    if len(listOfLinks) != 0:
        return listOfLinks
    else:
        return []

def initialize(url):
    if not "--load" in sys.argv:
        print("came here")
        page = requests.get(url).text
        soup = BeautifulSoup(page,"xml")
        i=1
        mainDictionary={}
        secondList = []
        global thirdList
        for a in soup.find_all('a', href=True,text=True):
            mainDictionary[i] = []
            tempDict= {}
            urlFirst = url+ "/%s"%(a['href'])
            pageFirst = requests.get(urlFirst).text
            soupFirst = BeautifulSoup(pageFirst,"xml")
    
            z =a['href']
            z =''.join(z.split())[:-4]
            #print(z)
            tag_list=[tag.name for tag in soupFirst.find("%s"%(z)).find_all(recursive=False)]
    
            #print(tag_list)
    
            if len(tag_list) == 0:
        
                mainDictionary[i].append(a['href'])
                i = i + 1
                continue
            else:
                j = 1
                for tl in tag_list:
                    tempDict[j] = []
                    secondList.append(tl)
                    urlSec = url+"/Snh_%s"%(tl)
                    pageSec = requests.get(urlSec).text
                    soupSec = BeautifulSoup(pageSec,"xml")
                    bufList = check(soupSec)
                    if len(bufList) == 0:
                        tempDict[j].append(tl)
               
                    else:
                        tempDict1={}
                        k = 1
                        for bl in bufList:
                            tempDict1[k]=bl
                            k = k + 1
                        tempDict[j].append(tempDict1)
                        tempDict[j].append(tl)
                    #mainDictionary[i].append(tempDict[j])
                    #mainDictionary[i].append(a['href'])   
                    j = j + 1
            mainDictionary[i].append(tempDict) 
            mainDictionary[i].append(a['href'])
            i = i + 1
            #print(mainDictionary)
        with open('lookup.pickle', 'wb') as handle:
            pickle.dump(mainDictionary, handle, protocol=pickle.HIGHEST_PROTOCOL)
        with open('secondLevel.pickle', 'wb') as handle:
            pickle.dump(secondList, handle, protocol=pickle.HIGHEST_PROTOCOL)
        with open('thirdLevel.pickle', 'wb') as handle:
            pickle.dump(thirdList, handle, protocol=pickle.HIGHEST_PROTOCOL)
    else:
        if not os.path.exists("lookup.pickle"):
            print("First execute without --load option and then with --load..Exiting")
            exit(0)
        with open('lookup.pickle', 'rb') as handle:
            mainDictionary = pickle.load(handle)
        with open('secondLevel.pickle', 'rb') as handle:
            secondList = pickle.load(handle)
        with open('thirdLevel.pickle', 'rb') as handle:
            thirdList = pickle.load(handle)
    return mainDictionary,secondList
def main():
    IntrospectPortMap = {
            "agent" : 8085,
            "control" : 8083,
            "collector" : 8089,
            "query-engine" : 8091,
            "analytics-api" : 8090,
            "dns" : 8092,
            "api" : 8084,
            "api:0" : 8084,
            "schema" : 8087,
            "svc-monitor" : 8088,
            "device-manager" : 8096,
            "config-nodemgr" : 8100,
            "analytics-nodemgr" : 8104,
            "vrouter-nodemgr" : 8102,
            "control-nodemgr" : 8101,
            "database-nodemgr" : 8103,
            "storage-stats" : 8105,
            "ipmi-stats" : 8106,
            "inventory-agent" : 8107,
            "alarm-gen" : 5995,
            "alarm-gen:0" : 5995,
            "snmp-collector" : 5920,
            "topology" : 5921,
            "discovery" : 5997,
            "discovery:0" : 5997,
        }
    if "-o" in sys.argv:
        fname = sys.argv[(sys.argv.index("-o")+1)]
        fileName = open(fname,"w")
    else:
        fname = "defaultOutput.txt"
        fileName = open(fname,"w")
    if "--ip" in sys.argv:
        ip = sys.argv[(sys.argv.index("--ip")+1)]
    else:
        ip = '127.0.0.1'
    
    if sys.argv[(sys.argv.index("--port")+1)].isdigit():
        port=sys.argv[(sys.argv.index("--port")+1)]
    else:
        port = str(IntrospectPortMap[sys.argv[(sys.argv.index("--port")+1)]])

    url = "http://%s:%s"%(ip,port)

    mainDictionary,secondList = initialize(url)

    #(self,indent,mainDictionary,secondList,thirdList,fname,fileName,ip,port)

    
    cliObj = Interospect(0,mainDictionary,secondList,fname,fileName,ip,port,url)
    if "--help" in sys.argv:
        cliObj.printHelp()
        exit(0)
    if "-name" in sys.argv:
        name = sys.argv[sys.argv.index("-name")+1]
        cliObj.name(name)
    if "-num" in sys.argv:
        numberList = sys.argv[(sys.argv.index("-num")+1):]
        cliObj.num(numberList)
    if "--dump" in sys.argv:
        cliObj.dump()
    if "--sort" in sys.argv:
        cliObj.sort()
    if "-i" in sys.argv:
        cliObj.interactive()
    cliObj.fileName.close()
if __name__ == "__main__":
    main()

    
