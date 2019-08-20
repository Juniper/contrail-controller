import pickle
import datetime
import json
import sys
import requests
import os
import lxml
from bs4 import BeautifulSoup
import xml.etree.ElementTree as ET
indent = 0
secondList = []
thirdList = []

if "-o" in sys.argv:
    fname = sys.argv[(sys.argv.index("-o")+1)]
    fileName = open(fname,"w")
else:
    fname = "defaultOutput.txt"
    fileName = open(fname,"w")
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

def printHelp(): 
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
                                            


def printRecur(root):
    global fileName
    str=' '*indent + '%s: %s' % (root.tag.title(), root.attrib.get('name', root.text))
    #print str
    fileName.write(str+"\n")
    global indent

    indent += 8
    for elem in root.getchildren():
        printRecur(elem)
    indent -= 8

def printAndWrite(urlT):
    pageT = requests.get(urlT).text
    soupT = BeautifulSoup(pageT,"xml")
    soupT = convertTime(soupT)
    xb = open("dumpBuf.xml","w")
    xb.write(str(soupT))
    xb.close()
    treeBuf = ET.ElementTree(file='dumpBuf.xml')
    printRecur(treeBuf.getroot())




def check(soupObj):
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
def getTime(epT):
    epT=float(epT)/1000000.0
    epT=str(datetime.datetime.fromtimestamp(epT).strftime('%Y-%m-%d %H:%M:%S.%f'))
    return epT


url = 'http://{0}:{1}'.format(sys.argv[1],sys.argv[2])
mainDictionary={}
if not "--load" in sys.argv:
    print("came here")
    page = requests.get(url).text
    soup = BeautifulSoup(page,"xml")
    i=1
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
                #print(urlSec)
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
if "-num" in sys.argv:
    numberList = sys.argv[(sys.argv.index("-num")+1):]
    if len(numberList) == 2 :
        first = int(sys.argv[sys.argv.index("-num")+1])
        second = int(sys.argv[sys.argv.index("-num")+2])
    
        if len(mainDictionary[first]) == 1 :
            urlT = url + "/%s"%(mainDictionary[first][0])
    
        else:
            if len(mainDictionary[first][0][second]) == 1:
                urlT = url + "/Snh_%s"%(mainDictionary[first][0][second][0])
            else:
                urlT = url + "/Snh_%s"%(mainDictionary[first][0][second][1])
    elif len(numberList) == 3:
        first = int(sys.argv[sys.argv.index("-num")+1])
        second = int(sys.argv[sys.argv.index("-num")+2])
        third = int(sys.argv[sys.argv.index("-num")+3])
        urlT = url + "/Snh_%s?x=%s"%(mainDictionary[first][0][second][0][third][1],mainDictionary[first][0][second][0][third][0])    

    printAndWrite(urlT)
    fileName.close()
    os.system("less %s"%(fname))
elif "-name" in sys.argv:
    
    name = sys.argv[sys.argv.index("-name")+1]
    if name in secondList:
       print("Here")
       urlT = url +"/Snh_%s"%(name)
       printAndWrite(urlT)
    else:
        #print(thirdList)
        for tu in thirdList:
            if name in tu:
                fileName.write("%s / %s \n"%(tu[1],tu[0]))
                urlT = url +"/Snh_%s?x=%s"%(tu[1],tu[0])
                printAndWrite(urlT)
                print("\n\n")
    fileName.close()
    os.system("less %s"%(fname))
elif "-i" in sys.argv:
    for md in mainDictionary.keys():
        if len(mainDictionary[md]) == 1:
            print("%s : %s  (No further contents to show) "%(md,mainDictionary[md][0]))
        else:
            print("%s : %s "%(md,mainDictionary[md][1]))
    fc = int(raw_input("Enter your choice : "))
    if len(mainDictionary[fc]) == 1:
        print("Nothing to show...Exiting")
        exit(0)
    else:
        for sd in mainDictionary[fc][0].keys():
            if len(mainDictionary[fc][0][sd]) == 1:
                value = mainDictionary[fc][0][sd][0]
            else:
                value = mainDictionary[fc][0][sd][1]
            print("%s : %s "%(sd,value))
    sc = int(raw_input("Enter your choice : "))
    if len(mainDictionary[fc][0][sc]) == 1:
        urlT = url +"/Snh_%s"%(mainDictionary[fc][0][sc][0])
        printAndWrite(urlT)
        os.system("less %s"%(fname))
    else:
        #urlT = url +"/Snh_%s"%(mainDictionary[fc][0][sc][1])
        #printAndWrite(urlT)
        
        for td in mainDictionary[fc][0][sc][0].keys():
            print("%s : %s / %s "%(td,mainDictionary[fc][0][sc][0][td][1],mainDictionary[fc][0][sc][0][td][0]))
        tc = int(raw_input("Enter your choice : "))
        urlT = url +"/Snh_%s?x=%s"%(mainDictionary[fc][0][sc][0][tc][1],mainDictionary[fc][0][sc][0][tc][0])
       
        printAndWrite(urlT)
        fileName.close()
        os.system("less %s"%(fname))
elif "--dump" in sys.argv:
    for md in mainDictionary.keys():
        if len(mainDictionary[md]) == 1:
            fileName.write("%s   . Nothing to show \n" %(mainDictionary[md][0]))
        else:
            secondDict = mainDictionary[md][0]
            for sk in secondDict.keys():
                if len(secondDict[sk]) == 1:
                    fileName.write("%s  /   %s \n"%(mainDictionary[md][1],secondDict[sk][0]))
                    urlT = url +"/Snh_%s"%(secondDict[sk][0])
                    printAndWrite(urlT)
                    fileName.write("\t\t\t\t-------------------------------********************************************___________________________________________________\n")
                else:
                    urlT = url +"/Snh_%s"%(secondDict[sk][1])
                    fileName.write("%s / %s \n" %(mainDictionary[md][1],secondDict[sk][1]))
                    printAndWrite(urlT)
                    fileName.write("\t\t\t\t\t\t******************************SUB-CONTENTS******************************\n")
                    thirdDict = secondDict[sk][0]
                    for tk in thirdDict.keys():
                        urlT = url+"/Snh_%s?x=%s"%(thirdDict[tk][1],thirdDict[tk][0])
                        fileName.write(" %s --> %s / %s \n"%(secondDict[sk][1],thirdDict[tk][1],thirdDict[tk][0]))
                        printAndWrite(urlT)
                        fileName.write("\t\t\t\t\t\t\t********************************************\n\n")
                fileName.write("\t\t\t\t-------------------------------********************************************___________________________________________________\n")
elif "--sort" in sys.argv:
    urlTemp = url+"/Snh_SandeshTraceRequest?x="
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
        fileName.write("\n"+getTime(ke))
        #print getTime(ke)
        for items in bufferDict[ke]:

            fileName.write(""+items+"\n")
        #fileSort.write("\n")
#printHelp()
