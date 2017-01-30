#!/usr/bin/env ruby

require 'pp'
require 'json'
require 'tempfile'

@host = ENV["CONFIG_JSON_PARSER_TEST_DB"] || "10.204.216.23"
@cass=<<EOF
cqlsh #{@host}
use config_db_uuid;
select * from obj_uuid_table WHERE key = textAsBlob('387b8af9-7cc3-4ed0-ae29-4cc72f426a20');
EOF

def get_py_cmd (table = "OBJ_UUID_TABLE")
    run_py=<<EOF
for r,c in #{table}.get_range():
    print "\\nOrderedDict([('UUID', u'" + r + "')])"
    print "\\n"
    print c
EOF
    return run_py
end

def run (cmd)
    puts cmd
    return `#{cmd}`
end

def get_cassandra_data (host = @host)
    t = Tempfile.new(["fq", ".py"])
    t.puts get_py_cmd("OBJ_FQ_NAME_TABLE")
    fq_cmd = t.path
    t.close
    tf1 = "/tmp/#{File.basename t.path}"
    run("sshpass -p c0ntrail123 scp -q #{t.path} root@#{host}:#{tf1}")

    t = Tempfile.new(["data", ".py"])
    t.puts get_py_cmd("OBJ_UUID_TABLE")
    db_cmd = t.path
    t.close
    tf2 = "/tmp/#{File.basename t.path}"
    run("sshpass -p c0ntrail123 scp -q #{t.path} root@#{host}:#{tf2}")

    c1 = "cat #{fq_cmd} | sshpass -p c0ntrail123 ssh -q root@#{host} pycassaShell -H #{@host} -k config_db_uuid -f #{tf1}| grep OrderedDict"
    puts "Retrieving OBJ_FQ_NAME_TABLE from #{@host} cassandra db"
    o1 = run(c1)

    c2 = "cat #{db_cmd} | sshpass -p c0ntrail123 ssh -q root@#{host} pycassaShell -H #{@host} -k config_db_uuid -f #{tf2}| grep OrderedDict"
    puts "Retrieving OBJ_UUID_TABLE from #{@host} cassandra db"
    o2 = run(c2)

    @fq_file = "/tmp/cassandra_db_#{@host}_fq_#{Process.pid}.txt"
    @config_file = "/tmp/cassandra_db_#{@host}_config_#{Process.pid}.txt"
    File.open(@fq_file, "w") { |fp| fp.puts o1 }
    File.open(@config_file, "w") { |fp| fp.puts o2 }
    convert_to_json
end

def get_json (file, prefix_index = "")
    fp = File.open(file, "r")
    lines = fp.readlines
    fp.close

    output = Hash.new
    uuid = nil
    lines.each { |line|
        line.gsub!("\\\\", "\\")
        next if line.chomp !~ /^OrderedDict\(\[(.*)\]\)$/
        tokens = $1.split(/[']/)
        (1..(tokens.size-1)).step(4) { |i|
            if tokens[i] == "UUID"
                uuid = tokens[i+2]
                output["#{prefix_index}#{uuid}"] = { }
            else
                output["#{prefix_index}#{uuid}"][tokens[i]] = tokens[i+2]
            end
        }
    }

    return output
end

def convert_to_json
    File.open("controller/src/ifmap/client/testdata/bulk_sync.json", "w") { |fp|
        fp.puts JSON.pretty_generate([{
            "operation"=>"db_sync",
            "OBJ_FQ_NAME_TABLE" => get_json(@fq_file),
            "db" => get_json(@config_file) # ":0"
        }])
    }
end

def main
    get_cassandra_data
    ENV["CONFIG_JSON_PARSER_TEST_INTROSPECT"] ||= "0"
    cmd = "build/debug/ifmap/client/test/config_json_parser_test --gtest_filter=ConfigJsonParserTest.BulkSync"
    print "CONFIG_JSON_PARSER_TEST_INTROSPECT="
    puts "#{ENV["CONFIG_JSON_PARSER_TEST_INTROSPECT"]} #{cmd}"
    exec(ENV, cmd)
end

main
