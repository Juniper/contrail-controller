#!/bin/bash
# Script to restore Cassandra db data [snapshot taken by nodetool command as follows]
# nodetool -h localhost -p 7199 snapshot 
# The snapshot is created in <data_directory_location>/<keyspace_name>/snapshots/<snapshot_name>. 

# Initialize variables
ss_dir=""
base_db_dir=""
ss_name=""
ss="snapshots"
declare -a dirs_to_be_restored=( )
me=`basename $0`

function join_path()
{
    parts=("${!1}")
    printf '/%s' "${parts[@]%/}"
}

function find_target_table_name()
{
    dest_path=$1
    keyspace_name=$2
    src_table_name=$3
    find_in_dir_parts=($dest_path $keyspace_name)
    find_in_dir=$( join_path find_in_dir_parts[@] )
    tname_without_uuid=$(echo $src_table_name | cut -d '-' -f 1)
    dest_table_name=$(ls -td -- $find_in_dir/$tname_without_uuid* | head -n 1 | rev | cut -d'/' -f1 | rev)
    printf $dest_table_name
}

function print_usage()
{
	echo "NAME"
	echo "	Script to restore Cassandra datbase from snapshot"
	echo "SYNOPSIS"
	echo "	$me [--help|-h] [--base_db_dir|-b] [--snapshot_dir|-s] [--snapshot_name|-n]"
	echo "	MUST OPTIONS: base_db_dir, snapshot_dir, snapshot_name"
	echo "DESCRIPTION"
	echo "	--base_db_dir, -b"
	echo "		Location of running Cassandra database"
	echo "	--snapshot_dir, -s"
	echo "		Snapshot location of Cassandra database"
	echo "	--snapshot_name, -n"
	echo "		Snapshot name"
	echo "	--skip_keyspaces, -k"
	echo "		Comma seperated list of keyspaces to be skipped"
	echo "EXAMPLE"
	echo "	$me -b /var/lib/cassandra/data -s /root/data.ss -n 1403068337967 -k DISCOVERY_SERVER,ContrailAnalyticsCql"
	exit
}
if [ $# -eq  0 ]
then
	print_usage
fi

while [[ $# > 0 ]]
do
key="$1"
shift

case $key in
	-h|--help)
	print_usage
	;;
	-b|--base_db_dir)
	base_db_dir="$1"
	shift
	;;
	-s|--snapshot_dir)
	ss_dir="$1"
	shift
	;;
	-n|--snapshot_name)	
	ss_name="$1"
	;;
	-k|--skip_keyspaces)
	skip_keyspaces="$1"
	;;
	--default)
	DEFAULT=YES
	shift
	;;
	*)
	# unknown option
	;;
esac
done

# Validate inputs
if [ "$base_db_dir" == "" ] || [ "$ss_dir" == "" ] || [ "$ss_name" == "" ]
then
	echo ""
	echo ">>>>>>>>>>Not all inputs provided, please check usage >>>>>>>>>>"
	echo ""
	print_usage
fi
 
ss_check="`find $ss_dir -name $ss_name` "
if [ "$ss_check" == " " ]
then
	echo "Snapshot not found, please check !!"
	exit 
else    
    echo "Snapshot available...continuing.."
fi

# Split the comma separated keyspaces to make an array
IFS=,
skip_keyspaces_list=($skip_keyspaces)

data_dirs="`find $ss_dir -name $ss`"
# Get directories with snapshots
# expected format ss_dir/keyspace_dir/column_family_dir/snapshots/*.db
# reverse output dirs & cut 3 fields of interest
for i in $data_dirs
do
	#x="`echo $i | cut -d "/" -f 4- | sed 's/snapshots$//'`"
	x="`echo $i | rev | cut -d '/' -f -3 | rev | sed 's/snapshots$//'`"
    # Get the list of snapshots path by reading each line
    snapshot_paths=()
    while read -r line
    do
        snapshot_paths+=("$line")
    done <<< "$x"
    for snapshot_path in "${snapshot_paths[@]}"
    do
        echo $snapshot_path
        # Get this keyspace name by spliting the snapshot_path
        this_keyspace=$(echo $snapshot_path | cut -d '/' -f 1)
        # Check if this keyspace is asked to be skipped
        skip=0
        for keyspace in "${skip_keyspaces_list[@]}"
        do
            if [ $keyspace = $this_keyspace ]
            then
                skip=1
                echo "Skipping keyspace $keyspace"
                break
            fi
        done
        if [ "$skip" -ne 1 ]
        then
            dirs_to_be_restored+=("$snapshot_path")
        fi
    done
done

# Print directories with snapshots without full path
echo "----------------dirs to be restored------------"
for i in ${dirs_to_be_restored[@]}
do
	echo $i
done

# Remove commit logs from current data dir
#/var/lib/cassandra/commitlog/CommitLog*.log
find $base_db_dir/../  -name "CommitLog*.log"  -delete
# Remove *.db from current data dir excluding skipped keyspaces
if [ -z "$skip_keyspaces" ]
then
    find $base_db_dir  -name "*.db"  -delete
else
    echo "Skipping deletion of keyspaces $skip_keyspaces"
    find $base_db_dir -name "*.db" | egrep -v $(echo "$skip_keyspaces" | sed -r 's/,/|/g') | xargs rm
fi

# Copy snapshots to data dir
echo "----------db files in snapshots--------------" 
for i in ${dirs_to_be_restored[@]}
do
    src_path_parts=($ss_dir $i $ss $ss_name)
    src_path=$( join_path src_path_parts[@] )
    # Find the destination
    keyspace_name=$(echo $i | cut -d '/' -f 1)
    table_name=$(echo $i | cut -d '/' -f 2)
    dest_table=$(find_target_table_name $base_db_dir $keyspace_name $table_name)
    dest_path_parts=($base_db_dir $keyspace_name $dest_table)
    dest_path=$( join_path dest_path_parts[@] )
    # Create keyspace/table directory if not exists
    #if [ ! -d "$dest_path" ]; then
    #    mkdir -p $dest_path
    #fi
    db_files=$(ls $src_path/*.db 2> /dev/null | wc -l)
    if [ $db_files -ne 0 ]
    then
        cp $src_path/*.db $dest_path
        if [ $? -ne 0 ]
        then
            echo "=====ERROR: Unable to restore $src_path/*.db to $dest_path====="
            exit 1
        fi
        echo "=======check $dest_path ==============="
        ls $dest_path
   fi
done

# Change the ownership of the cassandra data directory
chown -R cassandra:cassandra $base_db_dir
