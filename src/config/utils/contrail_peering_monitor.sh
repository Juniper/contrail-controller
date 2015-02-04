#!/usr/bin/env bash

# Script to monitor peering between Contrail components
#
#author: Édouard Thuleau edouard.thuleau@cloudwatt.com
#license: new BSD

#set -e

usage(){
    program_name=$(basename $0)
    echo "Usage: ${program_name}"
    echo -e "All parameter are defined by environment variable:"
    echo -e "\tDISCOVERY_API_HOST\tAPI host name or IP. Default 'localhost'"
    echo -e "\tREFRESH_TIME\t\ttime between refresh. Default 1 second"
    echo -e "\tCURL_TIMEOUT\t\ttime curl commands wait for response. Default 1 second"
    echo -e "\tTEST\t\t\tuse a test bed suite to validate UI. Default 'no'"
    echo -e "\tTEST_NB_VROUTER\t\tnumber of vrouters emulated for the test. Default 10"
    echo
    echo "EXAMPLES:"
    echo -e "\tTEST=y TEST_NB_VROUTER=100 ${program_name}"
    echo
    exit ${1-2}
}

case "$1" in
    --help|-h|help)
        usage 0
        ;;
esac

TEST=${TEST:-"no"}
TEST_NB_VROUTER=${TEST_NB_VROUTER:-10}

command -v nc >/dev/null 2>&1 && NC=$(which nc) || { echo >&2 "I require 'nc' binary but it could not be found. Aborting."; exit 1; }
command -v nslookup >/dev/null 2>&1 && NSLOOKUP=$(which nslookup) || { echo >&2 "I require 'nslookup' binary but it could not be found. Aborting."; exit 1; }
# Use curl to fetch URLs because it is too slow with xmlstarlet
command -v curl >/dev/null 2>&1 && CURL=$(which curl) || { echo >&2 "I require 'curl' binary but it could not be found. Aborting."; exit 1; }
command -v xmlstarlet >/dev/null 2>&1 && XML=$(which xmlstarlet) || { echo >&2 "I require 'xmlstarlet' binary but it could not be found. Aborting."; exit 1; }
command -v jq >/dev/null 2>&1 && JQ=$(which jq) || { echo >&2 "I require 'jq' binary (http://stedolan.github.io/jq/) but it could not be found. Aborting."; exit 1; }

REFRESH_TIME=${REFRESH_TIME:-1}
CURL_TIMEOUT=${CURL_TIMEOUT:-1}

CONFIG_WIN_LINE_SIZE=8
CONTROL_WIN_LINE_SIZE=6
ERROR_WIN_LINE_SIZE=4

IPV4_REGEX=^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$

DISCOVERY_INTROSPECT_PORT="5997"
CONTROL_INTROSPECT_PORT="8083"
API_INTROSPECT_PORT="8084"
VROUTER_INTROSPECT_PORT="8085"
SCHEMA_INTROSPECT_PORT="8087"
SVC_INTROSPECT_PORT="8088"

DISCOVERY_API_HOST=${DISCOVERY_API_HOST:-"localhost"}
DISCOVERY_API_PORT="5998"

OK_MSG="OK"
ERROR_MSG="Error"

declare -A config_nodes
declare -A control_config_active_peer
declare -A control_config_backup_peer
declare -A vrouter_control_active_peer
declare -A vrouter_control_backup_peer

get_contrail_peering(){
    config_nodes=()
    control_config_active_peer=()
    control_config_backup_peer=()
    vrouter_control_active_peer=()
    vrouter_control_backup_peer=()

    # Test bed
    if [[ "1 yes Yes YES true True TRUE" =~ "${TEST}" ]]; then
        config_nodes=(
            ["config-0"]="${API_INTROSPECT_PORT} ${DISCOVERY_INTROSPECT_PORT}"
            ["config-1"]=${ERROR_MSG}
            ["config-2"]="${API_INTROSPECT_PORT} ${DISCOVERY_INTROSPECT_PORT} ${SVC_INTROSPECT_PORT}"
            ["config-3"]="${API_INTROSPECT_PORT} ${DISCOVERY_INTROSPECT_PORT} ${SCHEMA_INTROSPECT_PORT}"
        )

        control_config_active_peer=(
            ["control-0"]="config-0"
            ["control-1"]="config-3"
            ["control-2"]=${ERROR_MSG}
            ["control-3"]="config-0"
        )
        control_config_backup_peer=(
            ["control-0"]="config-2"
            ["control-1"]="config-0"
            ["control-2"]="config-1"
            ["control-3"]="config-3"
        )

        controls=("${!control_config_active_peer[@]}")
        controls+=("${ERROR_MSG}")
        vrouter_control_active_peer=()
        for i in $(seq -w 0 ${TEST_NB_VROUTER}); do
            vrouter_control_active_peer+=( ["vrouter-$i"]="${controls[RANDOM%${#controls[*]}]}" )
        done
        vrouter_control_backup_peer=()
        for i in $(seq -w 0 ${TEST_NB_VROUTER}); do
            vrouter_control_backup_peer+=( ["vrouter-$i"]="${controls[RANDOM%${#controls[*]}]}" )
        done
        return
    fi

    config_node_list=($(${CURL} -m${CURL_TIMEOUT} http://${DISCOVERY_API_HOST}:${DISCOVERY_API_PORT}/clients.json 2>/dev/null | ${JQ} '.services[] | if .client_type == "ApiServer" then .client_id else empty end' | sort | uniq))
    config_node_list=(${config_node_list[@]//:ApiServer/})
    config_node_list=(${config_node_list[@]//\"/})
    control_node_list=($(${CURL} -m${CURL_TIMEOUT} http://${DISCOVERY_API_HOST}:${DISCOVERY_API_PORT}/clients.json 2>/dev/null  | ${JQ} '.services[] | if .client_type == "ControlNode" then .client_id else empty end' | sort | uniq))
    control_node_list=(${control_node_list[@]//:ControlNode/})
    control_node_list=(${control_node_list[@]//\"/})
    compute_node_list=($(${CURL} -m${CURL_TIMEOUT} http://${DISCOVERY_API_HOST}:${DISCOVERY_API_PORT}/clients.json 2>/dev/null  | ${JQ} '.services[] | if .client_type == "VRouterAgent" then .client_id else empty end' | sort | uniq))
    compute_node_list=(${compute_node_list[@]//:VRouterAgent/})
    compute_node_list=(${compute_node_list[@]//\"/})

    for node in ${config_node_list[@]}; do
        tmp_list=()
        ${NC} -z ${node} ${API_INTROSPECT_PORT} && tmp_list+=(${API_INTROSPECT_PORT})
        ${NC} -z ${node} ${DISCOVERY_INTROSPECT_PORT} && tmp_list+=(${DISCOVERY_INTROSPECT_PORT})
        ${NC} -z ${node} ${SCHEMA_INTROSPECT_PORT} && tmp_list+=(${SCHEMA_INTROSPECT_PORT})
        ${NC} -z ${node} ${SVC_INTROSPECT_PORT} && tmp_list+=(${SVC_INTROSPECT_PORT})
        [ ${#tmp_list[@]} -eq 0 ] && config_nodes[${node%%.*}]=${ERROR_MSG} || config_nodes[${node%%.*}]="${tmp_list[*]}"
        #unset -v tmp_list
    done

    for node in ${control_node_list[@]}; do
        active=${ERROR_MSG}
        backup=${ERROR_MSG}
        if xml=$(${CURL} -m${CURL_TIMEOUT} http://${node}:${CONTROL_INTROSPECT_PORT}/Snh_IFMapPeerServerInfoReq 2>/dev/null); then
            # get active IF-MAP peer
            ip=$(echo ${xml} | ${XML} sel -t -v "/IFMapPeerServerInfoResp/server_conn_info/IFMapPeerServerConnInfo/host")
            if [[ ${ip} =~ ${IPV4_REGEX} ]]; then
                hostname=$(${NSLOOKUP} ${ip} | grep name | awk '{print $4}')
                [ -n "${hostname%%.*}" ] && active=${hostname%%.*} || active=${ip}
            fi
            # get backup IF-MAP peer
            ip=$(echo ${xml} | ${XML} sel -t -m "IFMapPeerServerInfoResp/ds_peer_info/IFMapDSPeerInfo/ds_peer_list/list/IFMapDSPeerInfoEntry" -i "in_use='false'" -v "host")
            if [[ ${ip} =~ ${IPV4_REGEX} ]]; then
                hostname=$(${NSLOOKUP} ${ip} | grep name | awk '{print $4}')
                [ -n "${hostname%%.*}" ] && backup=${hostname%%.*} || backup=${ip}
            fi
        fi
        [ "${config_nodes["${active}"]}" == "${ERROR_MSG}" ] && active=${ERROR_MSG}
        [ "${config_nodes["${active}"]}" == "${ERROR_MSG}" ] && backup=${ERROR_MSG}
        control_config_active_peer[${node%%.*}]=${active}
        control_config_backup_peer[${node%%.*}]=${backup}
    done

    for node in ${compute_node_list[@]}; do
        active=${ERROR_MSG}
        backup=${ERROR_MSG}
        if xml=$(${CURL} -m${CURL_TIMEOUT} http://${node}:${VROUTER_INTROSPECT_PORT}/Snh_AgentXmppConnectionStatusReq 2>/dev/null); then
            # get active XMPP server for the cfg control
            ip=$(echo ${xml} | ${XML} sel -t -m "//AgentXmppConnectionStatus/peer/list/AgentXmppData" -i "cfg_controller='Yes'" -i "state='Established'" -v "controller_ip")
            if [[ ${ip} =~ ${IPV4_REGEX} ]]; then
                hostname=$(${NSLOOKUP} ${ip} | grep name | awk '{print $4}')
                [ -n "${hostname%%.*}" ] && active=${hostname%%.*} || active=${ip}
            fi
            # get backup XMPP server for the cfg control
            ip=$(echo ${xml} | ${XML} sel -t -m "//AgentXmppConnectionStatus/peer/list/AgentXmppData" -i "cfg_controller='No'" -i "state='Established'" -v "controller_ip")
            if [[ ${ip} =~ ${IPV4_REGEX} ]]; then
                hostname=$(${NSLOOKUP} ${ip} | grep name | awk '{print $4}')
                [ -n "${hostname%%.*}" ] && backup=${hostname%%.*} || backup=${ip}
            fi
        fi
        [ "${control_config_active_peer["${active}"]}" == "${ERROR_MSG}" ] && active=${ERROR_MSG}
        vrouter_control_active_peer[${node%%.*}]=${active}
        [ "${control_config_active_peer["${active}"]}" == "${ERROR_MSG}" ] && backup=${ERROR_MSG}
        vrouter_control_backup_peer[${node%%.*}]=${backup}
    done
}

main(){
    get_contrail_peering

    count1=0
    config_list=()
    for config in "${!config_nodes[@]}"; do
        [ "${config_nodes["${config}"]}" == "${ERROR_MSG}" ] && continue
        config_list[${count1}]=${config}
        ((count1++))
    done
    if [ ${#config_list[@]} == 0 ]; then
        window "NO CONFIG NODE FOUND" "red" "100%"
        append "???"
        endwin
        return
    fi
    error_nodes=()
    for config in "${!config_nodes[@]}"; do
        if [ "${config_nodes["${config}"]}" == "${ERROR_MSG}" ]; then
            error_nodes+=(${config})
        fi
    done
    for control in "${!control_config_active_peer[@]}"; do
        if [ "${control_config_active_peer["${control}"]}" == "${ERROR_MSG}" ]; then
            error_nodes+=(${control})
        fi
    done
    for vrouter in "${!vrouter_control_active_peer[@]}"; do
        if [ "${vrouter_control_active_peer["${vrouter}"]}" == "${ERROR_MSG}" ]; then
            error_nodes+=(${vrouter})
        fi
    done
    if [ ! ${#error_nodes[@]} -eq 0 ]; then
        col_width1=$(( 100 / $(( ${#config_list[@]} + 1 )) ))
    else
        col_width1=$(( 100 / $(( ${#config_list[@]} )) ))
    fi
    available_lines=$(tput lines)
    count0=0
    for config in $(printf '%s\n' "${config_list[@]}"|sort); do
        window "${config}" "blue" "${col_width1}%"
        [[ "${config_nodes["${config}"]}" =~ "${API_INTROSPECT_PORT}" ]] && append "API/IF-MAP: OK" "green"|| append "API/IF-MAP: error" "red"
        [[ "${config_nodes["${config}"]}" =~ "${DISCOVERY_INTROSPECT_PORT}" ]] && append "Discovery: OK" "green"|| append "Discovery: error" "red"
        [[ "${config_nodes["${config}"]}" =~ "${SCHEMA_INTROSPECT_PORT}" ]] && append "Schema: OK" "green" || append "Schema: backup"
        [[ "${config_nodes["${config}"]}" =~ "${SVC_INTROSPECT_PORT}" ]] && append "SVC: OK" "green" || append "SVC: backup"
        endwin
        count1=0
        control_list=()
        for control in "${!control_config_active_peer[@]}"; do
            if [ "${config}" == "${control_config_active_peer["${control}"]}" ]; then
                control_list[${count1}]=${control}
               ((count1++))
            fi
        done
        if [ ${#control_list[@]} == 0 ]; then
            col_right $(( 100 % ${#config_list[@]} ))
            move_up
            continue
        fi
        col_width2=$(( ${col_width1} / ${#control_list[@]} ))
        count1=0
        for control in $(printf '%s\n' "${control_list[@]}"|sort); do
            [ "${control_config_backup_peer["${control}"]}" != "${ERROR_MSG}" ] && color="green" || color="red"
            window "${control}" "${color}" "${col_width2}%"
            [ "${control_config_backup_peer["${control}"]}" != "${ERROR_MSG}" ] && append "backup: ${control_config_backup_peer["${control}"]}" || append "backup: NONE" "red"
            addsep
            count2=0
            vrouter_list=()
            for vrouter in "${!vrouter_control_active_peer[@]}"; do
                if [ "${control}" == "${vrouter_control_active_peer["${vrouter}"]}" ]; then
                    vrouter_list[${count2}]=${vrouter}
                    ((count2++))
                fi
            done
            valid_vrouter_list=()
            non_valid_vrouter_list=()
            for vrouter in "${vrouter_list[@]}"; do
                [ "${vrouter_control_backup_peer["${vrouter}"]}" != "${ERROR_MSG}" ] && valid_vrouter_list+=(${vrouter}) || non_valid_vrouter_list+=(${vrouter})
            done
            count2=0
            for vrouter in $(printf '%s\n' "${non_valid_vrouter_list[@]}"|sort); do
                if [ $(( ${available_lines} - ${CONFIG_WIN_LINE_SIZE} - ${CONTROL_WIN_LINE_SIZE} - ${count2} - 1 )) -eq 0 ]; then
                    append "..."
                    break
                fi
                append "${vrouter} (no backup)" "red"
                ((count2++))
            done
            if [ $(( ${available_lines} - ${CONFIG_WIN_LINE_SIZE} - ${CONTROL_WIN_LINE_SIZE} - ${count2} - 1 )) -gt 0 ]; then
                for vrouter in $(printf '%s\n' "${valid_vrouter_list[@]}"|sort); do
                if [ $(( ${available_lines} - ${CONFIG_WIN_LINE_SIZE} - ${CONTROL_WIN_LINE_SIZE} - ${count2} - 1 )) -eq 0 ]; then
                        append "..."
                        break
                    fi
                    append "${vrouter} (${vrouter_control_backup_peer["${vrouter}"]})" "green"
                    ((count2++))
                done
            fi
            endwin
            ((count1++))
            if [ ${count1} -lt ${#control_list[@]} ]; then
                col_right $(( ${col_width1} % ${#control_list[@]} ))
                #move_up
                move_up_to_pos ${CONFIG_WIN_LINE_SIZE}
            fi
        done

        if [ ! ${#error_nodes[@]} -eq 0 -o ${count0} -lt ${#config_list[@]} ]; then
            col_right 
            move_up
        fi
        ((count0++))

    done
    if [ ! ${#error_nodes[@]} -eq 0 ]; then
        window "Nodes in error" "red" "${col_width1}%"
        count1=0
        for node in "${error_nodes[@]}"; do
            if [ $(( ${available_lines} - ${ERROR_WIN_LINE_SIZE} - ${count1} - 1 )) -eq 0 ]; then
                append "..."
                break
            fi
            append ${node}
            ((count1++))
        done
        endwin
    fi
    #unset -v config_list error_nodes vrouter_list valid_vrouter_list non_valid_vrouter_list config_nodes control_config_active_peer control_config_backup_peer vrouter_control_active_peer vrouter_control_backup_peer
}

#simple curses library to create windows on terminal
#
#author: Patrice Ferlet metal3d@copix.org
#license: new BSD
#
#create_buffer patch by Laurent Bachelier

create_buffer(){
  # Try to use SHM, then $TMPDIR, then /tmp
  if [ -d "/dev/shm" ]; then
    BUFFER_DIR="/dev/shm"
  elif [ -z $TMPDIR ]; then
    BUFFER_DIR=$TMPDIR
  else
    BUFFER_DIR="/tmp"
  fi

  [[ "$1" != "" ]] &&  buffername=$1 || buffername="bashsimplecurses"

  # Try to use mktemp before using the unsafe method
  if [ -x `which mktemp` ]; then
    #mktemp --tmpdir=${BUFFER_DIR} ${buffername}.XXXXXXXXXX
    mktemp ${BUFFER_DIR}/${buffername}.XXXXXXXXXX
  else
    echo "${BUFFER_DIR}/bashsimplecurses."$RANDOM
  fi
}

#Usefull variables
LASTCOLS=0
BUFFER=`create_buffer`
POSX=0
POSY=0
LASTWINPOS=0

#call on SIGINT and SIGKILL
#it removes buffer before to stop
on_kill(){
    echo "Exiting"
    rm -rf $BUFFER
    exit 0
}
trap on_kill SIGINT SIGTERM


#initialize terminal
term_init(){
    POSX=0
    POSY=0
    tput clear >> $BUFFER
}


#change line
_nl(){
    POSY=$((POSY+1))
    tput cup $POSY $POSX >> $BUFFER
    #echo 
}


move_up(){
    set_position $POSX 0
}

move_up_to_pos(){
    set_position $POSX $1
}

col_right(){
    left=$((LASTCOLS+POSX))
    [ -z $1 ] && inc=0 || inc=$1
    left=$(( ${left} + $inc ))
    set_position $left $LASTWINPOS
}

#put display coordinates
set_position(){
    POSX=$1
    POSY=$2
}

#initialize chars to use
_TL="\033(0l\033(B"
_TR="\033(0k\033(B"
_BL="\033(0m\033(B"
_BR="\033(0j\033(B"
_SEPL="\033(0t\033(B"
_SEPR="\033(0u\033(B"
_VLINE="\033(0x\033(B"
_HLINE="\033(0q\033(B"
init_chars(){    
    if [[ "$ASCIIMODE" != "" ]]; then
        if [[ "$ASCIIMODE" == "ascii" ]]; then
            _TL="+"
            _TR="+"
            _BL="+"
            _BR="+"
            _SEPL="+"
            _SEPR="+"
            _VLINE="|"
            _HLINE="-"
        fi
    fi
}

#Append a windo on POSX,POSY
window(){
    LASTWINPOS=$POSY
    title=$1
    color=$2
    tput cup $POSY $POSX 
    cols=$(tput cols)
    cols=$((cols))
    if [[ "$3" != "" ]]; then
        cols=$3
        if [ $(echo $3 | grep "%") ];then
            cols=$(tput cols)
            cols=$((cols))
            w=$(echo $3 | sed 's/%//')
            cols=$((w*cols/100))
        fi
    fi
    len=$(echo "$1" | echo $(($(wc -c)-1)))
    left=$(((cols/2) - (len/2) -1))

    #draw up line
    clean_line
    echo -ne $_TL
    for i in `seq 3 $cols`; do echo -ne $_HLINE; done
    echo -ne $_TR
    #next line, draw title
    _nl

    tput sc
    clean_line
    echo -ne $_VLINE
    tput cuf $left
    #set title color
    case $color in
        green) green ;;
        red) red ;;
        blue) blue ;;
        grey|*) grey ;;
    esac
    
    
    echo $title
    tput rc
    tput cuf $((cols-1))
    echo -ne $_VLINE
    echo -n -e "\e[00m"
    _nl
    #then draw bottom line for title
    addsep
    
    LASTCOLS=$cols

}

#append a separator, new line
addsep (){
    clean_line
    echo -ne $_SEPL
    for i in `seq 3 $cols`; do echo -ne $_HLINE; done
    echo -ne $_SEPR
    _nl
}


#clean the current line
clean_line(){
    tput sc
    #tput el
    tput rc
    
}
green() {
   echo -n -e "\E[01;32m"
}
red() {
   echo -n -e "\E[01;31m"
}
blue() {
   echo -n -e "\E[01;34m"
}
grey() {
   echo -n -e "\E[01;37m"
}

#add text on current window
append_file(){
    [[ "$1" != "" ]] && align="left" || align=$1
    while read l;do
        l=`echo $l | sed 's/____SPACES____/ /g'`
        _append "$l" $align
    done < "$1"
}
append(){
    text=$(echo -e $1 | fold -w $((LASTCOLS-2)) | head -n 1)
    rbuffer=`create_buffer bashsimplecursesfilebuffer`
    echo  -e "$text" > $rbuffer
    while read a; do
        _append "$a" $2
    done < $rbuffer
    rm -f $rbuffer
}
_append(){
    color=$2
    clean_line
    tput sc
    echo -ne $_VLINE
    len=$(echo "$1" | wc -c )
    len=$((len-1))
    left=$((LASTCOLS/2 - len/2 -1))
    
    [[ "$2" == "left" ]] && left=0

    tput cuf $left
    #set title color
    case $color in
        green) green ;;
        red) red ;;
        blue) blue ;;
        grey|*) grey ;;
    esac
    echo -e "$1"
    tput rc
    tput cuf $((LASTCOLS-1))
    echo -ne $_VLINE
    _nl
}

#add separated values on current window
append_tabbed(){
    [[ $2 == "" ]] && echo "append_tabbed: Second argument needed" >&2 && exit 1
    [[ "$3" != "" ]] && delim=$3 || delim=":"
    clean_line
    tput sc
    echo -ne $_VLINE
    len=$(echo "$1" | wc -c )
    len=$((len-1))
    left=$((LASTCOLS/$2)) 
    for i in `seq 0 $(($2))`; do
        tput rc
        tput cuf $((left*i+1))
        echo "`echo $1 | cut -f$((i+1)) -d"$delim"`" 
    done
    tput rc
    tput cuf $((LASTCOLS-1))
    echo -ne $_VLINE
    _nl
}

#append a command output
append_command(){
    buff=`create_buffer command`
    echo -e "`$1`" | sed 's/ /____SPACES____/g' > $buff 2>&1
    append_file $buff "left"
    rm -f $buff
}

#close the window display
endwin(){
    clean_line
    echo -ne $_BL
    for i in `seq 3 $LASTCOLS`; do echo -ne $_HLINE; done
    echo -ne $_BR
    _nl
}

#refresh display
refresh (){
    cat $BUFFER
    echo "" > $BUFFER
}



#main loop called
main_loop (){
    term_init
    init_chars
    [[ "$1" == "" ]] && time=1 || time=$1
    while [[ 1 ]];do
        tput cup 0 0 >> $BUFFER
        tput il $(tput lines) >>$BUFFER
        main >> $BUFFER 
        tput cup $(tput lines) $(tput cols) >> $BUFFER 
        refresh
        sleep $time
        POSX=0
        POSY=0
    done
}

main_loop ${REFRESH_TIME}
