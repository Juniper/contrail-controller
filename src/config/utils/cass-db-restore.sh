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
	echo "EXAMPLE"
	echo "	$me -b /var/lib/cassandra/data -s /root/data.ss -n 1403068337967"	
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

data_dirs="`find $ss_dir -name $ss` "
# Get directories with snapshots
# expected format ss_dir/keyspace_dir/column_family_dir/snapshots/*.db
# reverse output dirs & cut 3 fields of interest
for i in $data_dirs
do
	#x="`echo $i | cut -d "/" -f 4- | sed 's/snapshots$//'`"
	x="`echo $i | rev | cut -d '/' -f -3 | rev | sed 's/snapshots$//'`"
	dirs_to_be_restored+=("$x")	
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
# Remove *.db from current data dir
find $base_db_dir  -name "*.db"  -delete

# Copy snapshots to data dir
echo "----------db files in snapshots--------------" 
for i in ${dirs_to_be_restored[@]}
do
	src_path=$ss_dir/$i/$ss/$ss_name/
	dest_path=$base_db_dir/$i/
	cp $src_path/*.db $dest_path
	echo "=======check $dest_path ==============="
	ls $dest_path
done
