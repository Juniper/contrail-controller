import datetime
import pprint
import sys
import requests
import xmltodict, json
import os
import lxml
import urllib
from urllib import *
from bs4 import BeautifulSoup
import xml.etree.ElementTree as ET
contentDictionary = {}
secondDict = {}
indent = 0
def getTime(epT):
    epT=float(epT)/1000000.0
    epT=str(datetime.datetime.fromtimestamp(epT).strftime('%Y-%m-%d %H:%M:%S.%f'))
    return epT

def convertTime(soupObj):
    #print(soupObj)
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
def check(soupObj):
    listOfLinks=[]
    a=soupObj.find_all(link=True)
    for t in a:
        if str(t.text) != "":
            listOfLinks.append((str(t.text),str(t["link"])))
    if len(listOfLinks) != 0:
        return listOfLinks
    else:
        return []

def printRecur(root,filePointer=None):
    """Recursively prints the tree."""
    #fi=open("interospect.txt", "w")
    str=' '*indent + '%s: %s' % (root.tag.title(), root.attrib.get('name', root.text))
    #print str
    filePointer.write(str+"\n")
    global indent
    indent += 8
    for elem in root.getchildren():
        printRecur(elem,filePointer)
    indent -= 8

if "--sort" in sys.argv:
    urlTemp = 'http://{0}:{1}/Snh_SandeshTraceBufferListRequest?x='.format(sys.argv[1],sys.argv[2])
    pageTemp = requests.get(urlTemp).text
    soupTemp = BeautifulSoup(pageTemp,"xml")
    listOfBuf = check(soupTemp)
    urlTemp = 'http://{0}:{1}/Snh_SandeshTraceRequest?x='.format(sys.argv[1],sys.argv[2])
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
    
    fileSort = open("SortedBuffers.txt","w")
    
    for ke in sorted(bufferDict):
        fileSort.write("\n"+getTime(ke))
        #print getTime(ke)
        for items in bufferDict[ke]:
        
            fileSort.write(""+items+"\n")
        #fileSort.write("\n")

               
        
    exit(0)




if "--dump" in sys.argv:
    fileDump=""
    if os.path.exists("InterospectDump.txt"):
        fileDump = raw_input("File InterospectDump.txt already exists . Please enter a new name or press enter to rewrite the existing file:")
    if fileDump == "":
        fileDump = open("InterospectDump.txt","w")
    else:
        fileDump = open(fileDump+".txt","w")
    
    
    contentDictionary = {}
    secondDict = {}
    if len(sys.argv) != 2:
        url = 'http://{0}:{1}'.format(sys.argv[1],sys.argv[2])
    else:
        url = 'http://{0}:{1}'.format('127.0.0.1',sys.argv[1])
    #print(url)
    page = requests.get(url).text
    soup = BeautifulSoup(page,"xml")
                
    i=1            
    for a in soup.find_all('a', href=True,text=True):
        contentDictionary[i] = a['href']
        i += 1
    for j in range(len(contentDictionary.keys())):
        urlfirst =url+ "/%s"%(contentDictionary[j+1])
        #print(urlfirst)
        
        page = requests.get(urlfirst).text
        soup = BeautifulSoup(page,"xml")
        z =contentDictionary[j+1]
        z =''.join(z.split())[:-4]
        #print(z)
        #print(z)
        tag_list=[tag.name for tag in soup.find("%s"%(z)).find_all(recursive=False)]
        k = 1
        for t in tag_list:
            secondDict[k] =t
            
            urlsec = url+"/Snh_%s"%(secondDict[k])
            #print(urlsec)
            page = requests.get(urlsec).text
            soup = BeautifulSoup(page,"xml")      
            xd=open("dump.xml","w")
            xd.write(str(soup))
            xd.close()
            tree = ET.ElementTree(file='dump.xml')
            fileDump.write("%s / %s \n"%(contentDictionary[j+1],secondDict[k]))
            #bufList=check(soup)
            printRecur(tree.getroot(),fileDump)
            fileDump.write("--------------------------------------------------------------------**************************************------------------------------------------------------------\n")
            bufList=check(soup)
            if len(bufList) !=  0:
                
                for i in range(len(bufList)):
                    
                    urlTemp = url +"/Snh_%s?x=%s"%(bufList[i][1],bufList[i][0])
                    pageTemp = requests.get(urlTemp).text
                    soupTemp = BeautifulSoup(pageTemp,"xml")
                    soupTemp = convertTime(soupTemp)
                    fileDump.write("%s / %s \n"%(bufList[i][1],bufList[i][0]))
                    xb = open("dumpBuf.xml","w")
                    xb.write(str(soupTemp))
                    xb.close()
                    treeBuf = ET.ElementTree(file='dumpBuf.xml')
                    printRecur(treeBuf.getroot(),fileDump)
            
    exit(0)

    
def checkIfLink(soupObj,url):
    listOfLinks=[]
    a=soupObj.find_all(link=True)
    for t in a:
        if str(t.text) != "":
            listOfLinks.append((str(t.text),str(t["link"])))
    if len(listOfLinks) != 0:
        ld={}
        for i in range(len(listOfLinks)):
            ld[i+1]=listOfLinks[i]
        print("Choose one of the below options")
        for i in range(len(listOfLinks)):
            print("%d: %s"%((i+1),ld[i+1][0]))
        choice=int(raw_input("Enter your choice : "))
        splitDict = url.split("/Snh_")
        url = splitDict[0]+"/Snh_%s?x=%s"%(ld[choice][1],ld[choice][0])
        #print(url)
        
        page = requests.get(url).text
        soup = BeautifulSoup(page,"xml")
        #if len(soup.find_all('element')) != 0:
        #    for ele in soup.find_all('element'):
        #        ti=(float(ele.text.split(" ")[0])/1000000.0)
        #        tim=str(datetime.datetime.fromtimestamp(ti).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3])
        #        ele.string = "{ "+tim+"} "+ ("".join(ele.text.split(" ")[1:]))
        soup = convertTime(soup)
        fw=open("data1.xml","w")
        fw.write(str(soup))
        fw.close()
        tree = ET.ElementTree(file='data1.xml')
        #root = tree.getroot()
        fi=open("interospect1.txt", "w")
        printRecur(tree.getroot(),fi)
        fi.close()
        os.system("less interospect1.txt")
        
IntrospectPortMap = {
            "vrouter-agent" : 8085,
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

def getInitialContents(url,choice = 0,ch=None):

        if choice == 0:
                page = requests.get(url).text
                soup = BeautifulSoup(page,"xml")
                #print(soup)
                i = 1
                for a in soup.find_all('a', href=True,text=True):
                        contentDictionary[i] = a['href']
                        value="%d.   %s"%(i,contentDictionary[i])
                        print (value.strip(".xml"))
                        i += 1

        elif choice == 1:
                page = requests.get(url).text
                soup = BeautifulSoup(page,"xml")
                z =''.join(ch.split())[:-4]
                #print(z)
                tag_list=[tag.name for tag in soup.find("%s"%(z)).find_all(recursive=False)]
                j = 1
                for t in tag_list:
                        secondDict[j] =t
                        value="%d.   %s"%(j,secondDict[j])
                        print (value.strip(".xml"))
                        j += 1
        elif choice == 2:
                page = requests.get(url).text
                soup = BeautifulSoup(page,"xml")

                #z =''.join(ch.split())[:-4]
                #soup.find("%s"%(z)).find_all(recursive=False,text=True)
                #page1 = urllib.urlopen(url)
                #soup1=BeautifulSoup(page1,'html.parser')
                #print(soup1)
                #tag_list=[tag.name for tag in soup.find('trace_buf_name').find_all(recursive=False)]
                #for bt in tag_list:
                #    print(bt)
                #tag_list=[tag.name for tag in soup.find('a').find_all(recursive=False)]
                #print(tag_list)
                #print(soup)
                #href_tags = soup.find_all('a',href=True)
                #print("Tags ",href_tags)
                #for a in soup.find_all('a', href=True):
                #    print "Found the URL:", a['href']
                #a=json.loads(json.dumps(xmltodict.parse(str(soup))))
                # print(a)
                fw=open("data.xml","w")
                fw.write(str(soup))
                fw.close()
                tree = ET.ElementTree(file='data.xml')
                #root = tree.getroot()
                fi=open("interospect.txt", "w")
                printRecur(tree.getroot(),fi)
                fi.close()
                os.system("less interospect.txt")
                checkIfLink(soup,url)
loopFlag = 10
while True:
    global contentDictionary
    global secondDict
    contentDictionary={}
    secondDict = {}
    loopFlag = 10
    if len(sys.argv) != 2:
        url = 'http://{0}:{1}'.format(sys.argv[1],sys.argv[2])
    else:
        url = 'http://{0}:{1}'.format('127.0.0.1',sys.argv[1])
    getInitialContents(url)
    #choiceNumber = int(raw_input("Enter the choice : "))
    while True:
        while True:
            errFlag=10
            try:
                choiceNumber = int(raw_input("Enter the choice : "))
            except ValueError:
                print("That's not a valid number !Re-enter your choice ")
                errFlag = -10
            if errFlag >=0:
                break
        errFlag1 = 10
        try:
            choice = contentDictionary[choiceNumber]
        except KeyError:
            print("That's an invalid choice ! Re-enter your choice ")
            errFlag1 = -10
        if errFlag1 >=0:
            break
    getInitialContents(url+"/%s"%(choice),1,choice)
    #secchoiceNumber = int(input("Enter the choice(Enter 0 to go back) : "))
    while True:
        errFlag=10
        try:
            secchoiceNumber = int(raw_input("Enter the choice(Enter 0 to go back) : "))
        except ValueError:
            print("That's not a valid number !Re-enter your choice ")
            errFlag = -10
        if errFlag >=0:
            break
    if secchoiceNumber != 0:
       
        while True:
            errFlag1 = 10
            try:
                secchoice = secondDict[secchoiceNumber]
            except KeyError:
                print("That's an invalid choice ! Re-enter your choice ")
                errFlag1 = -10
                
                secchoiceNumber = int(raw_input("Re-enter the choice(Enter 0 to go back) : "))
                if secchoiceNumber in secondDict.keys():
                    errFlag1 =10
                    secchoice = secondDict[secchoiceNumber]
                    
            if errFlag1 >=0:
                break
        getInitialContents(url+"/Snh_%s"%(secchoice),2,secchoice)
        while True:
            errFlag=10
            try:
                cont= int(raw_input("Press 0 to exit or any other number to repeat: "))
            except ValueError:
                print("That's not a valid number !Re-enter your choice ")
                errFlag = -10
            if errFlag >=0:
                break
        #cont= int(raw_input("Press 0 to exit or any other number to repeat: "))
        if cont != 0:
           loopFlag = -10
        
    else:
        loopFlag = -10
    
    if loopFlag >=0:
       break
    
    #tree = ET.ElementTree(file='data.xml')
    #printRecur(tree.getroot())
    #os.system("less interospect.txt")
