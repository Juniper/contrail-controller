#
#  backtrace.gdb
#
#  Created by Ananth Suryanarayana on 12/05/2012.
#  Copyright (c) 2012 Contrail Systems All rights reserved.
#

#
# Brief and colored thread apply backtrace all
#
def bbta
  bBackTraceAll
end

#
# Brief and colored backtrace
#
def bbt
  bBackTrace
end

#
# Brief and colored thread apply backtrace all
#
def cbta
  cBackTraceAll
end

#
# Brief and colored backtrace
#
def cbt
  cBackTrace
end

def bBackTraceAll
  shell rm -f .gdb_backtrace.txt
  set logging off
  set logging file .gdb_backtrace.txt
  set logging overwrite on
  set logging redirect off
  set logging on
  thread apply all backtrace
  set logging off

  shell echo
  shell echo ----------------- Trimed Back Trace follows -----------------------
  shell echo
  shell cat .gdb_backtrace.txt | \
      \grep -v "boost::\|tbb::\|rml::\|std::\|log4cplus::\|pugi::impl\|__dyld\|testing::\|BackTrace::\|??"
end

def bBackTrace
  shell rm -f .gdb_backtrace.txt
  set logging off
  set logging file .gdb_backtrace.txt
  set logging overwrite on
  set logging redirect off
  set logging on
  backtrace
  set logging off

  shell echo
  shell echo ----------------- Trimed Back Trace follows -----------------------
  shell echo
  shell cat .gdb_backtrace.txt | \
      \grep -v "boost::\|tbb::\|rml::\|std::\|log4cplus::\|pugi::impl\|__dyld\|testing::\|BackTrace::\|??"
end

def cBackTraceAll
  shell rm -f .gdb_backtrace.txt
  set logging off
  set logging file .gdb_backtrace.txt
  set logging overwrite on
  set logging redirect off
  set logging on
  thread apply all backtrace
  set logging off

  FilterTrace
end

def cBackTrace
  shell rm -f .gdb_backtrace.txt
  set logging off
  set logging file .gdb_backtrace.txt
  set logging overwrite on
  set logging redirect off
  set logging on
  backtrace
  set logging off

  FilterTrace
end

def FilterTrace
   shell echo '                                                               \
                                                                              \
 COLOR_RESET       = "\e[m"                                                  ;\
 COLOR_BLACK       = "\e[0;30m"                                              ;\
 COLOR_RED         = "\e[0;31m"                                              ;\
 COLOR_GREEN       = "\e[0;32m"                                              ;\
 COLOR_BROWN       = "\e[0;33m"                                              ;\
 COLOR_BLUE        = "\e[0;34m"                                              ;\
 COLOR_MAGENTA     = "\e[0;35m"                                              ;\
 COLOR_CYAN        = "\e[0;36m"                                              ;\
 COLOR_GRAY        = "\e[0;37m"                                              ;\
 COLOR_DARKGRAY    = "\e[1;30m"                                              ;\
 COLOR_DARKBLUE    = "\e[1;34m"                                              ;\
 COLOR_DARKGREEN   = "\e[1;32m"                                              ;\
 COLOR_DARKCYAN    = "\e[1;36m"                                              ;\
 COLOR_DARKRED     = "\e[1;31m"                                              ;\
 COLOR_DARKPURPLE  = "\e[1;35m"                                              ;\
 COLOR_YELLOW      = "\e[1;33m"                                              ;\
 COLOR_WHITE       = "\e[1;37m"                                              ;\
                                                                             ;\
                                                                             ;\
 puts ""                                                                     ;\
 puts ""                                                                     ;\
 puts "----------------- Trimed Back Trace follows -----------------------"  ;\
                                                                             ;\
 File.open(ARGV[0], "r") { |fp|                                               \
    func = nil                                                               ;\
    fp.readlines.each { |line|                                                \
        line.chomp!                                                          ;\
        next if line =~ /tbb::|rml::|std::|pugi::impl/                       ;\
        next if line =~ /__dyld|testing::|BackTrace::|log4cplus::/           ;\
        next if line =~ / in \?\? \(\)/                                      ;\
        next if line =~ /_pthread_start/                                     ;\
                                                                             ;\
        if line =~ /^Thread \d+/ then                                        \
            print COLOR_RED                                                  ;\
            puts line                                                        ;\
            next                                                             ;\
        end                                                                  ;\
                                                                             ;\
        tokens = line.split                                                  ;\
                                                                             ;\
        if line !~ /(#\d+)\s+.*? in (.*)\s?\((.*)\) at (.*)/ then             \
            if line =~ /(#\d+)\s+.*? in (.*?) \((.*)\)/ then                  \
                next if !func.nil? and func == $2 and func =~ /::~/          ;\
                func = $2                                                    ;\
            end                                                              ;\
            print COLOR_CYAN                                                 ;\
            puts line                                                        ;\
            next                                                             ;\
        end                                                                  ;\
                                                                             ;\
        frame = $1                                                           ;\
        func1 = $2                                                           ;\
        args  = $3                                                           ;\
        file  = $4                                                           ;\
        next if !func.nil? and func1 == func and func1 =~ /::~/              ;\
        func = func1                                                         ;\
        next if !func.nil? and func =~ /boost::/                             ;\
                                                                             ;\
        print COLOR_CYAN                                                     ;\
        printf("%-4s", frame)                                                ;\
        print COLOR_DARKPURPLE                                               ;\
        printf("%-40s", "#{func}() ")                                        ;\
                                                                             ;\
        print COLOR_BROWN                                                    ;\
        printf("(%s) ", args)                                                ;\
        print COLOR_MAGENTA                                                  ;\
        printf("at %s\n", file)                                              ;\
    }                                                                        ;\
 }                                                                            \
                                                                             ;\
 print COLOR_RESET                                                           ;\
                                                                             ;\
' | ruby - .gdb_backtrace.txt
end
