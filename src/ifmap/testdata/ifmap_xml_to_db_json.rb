#!/build/anantha/thirdparty/ruby/bin/ruby -W0

require 'pp'
require 'rails'

def init_globals
    @debug = false
    @db = Hash.new
    @events = [ ]
    @seen = Hash.new(false)
    @only_initial_sync = true
end

def get_uuid (u)
    c = sprintf("%032x",
                (u["uuid_mslong"].to_i << 64) | u["uuid_lslong"].to_i).split(//)
    c.insert(8, "-").insert(13, "-").insert(18, "-").insert(23, "-").join
end

def read_xml_to_json (file_name)
    ifd = file_name.nil? ? STDIN : File.open(file_name, "r")
    xml = ifd.read() # File.read("server_parser_test6.xml")
    ifd.close

    # Add seq-numbers to resultItems to note down the order..
    seq = 0
    x = xml.split(/(<)/).collect { |token|
        token =~ /^resultItem(>.*)/ ? "resultItem _seq=\"#{seq+=1}\"#{$1}":token
    }
    xml = x.join
    puts xml if @debug
    json = JSON.pretty_generate(Hash.from_xml(xml))
    return JSON.parse(json)
end

def read_items (json)
# if jo["Envelope"]["Body"]["response"]["pollResult"]["updateResult"].nil?
# jo["Envelope"]["Body"]["response"]["pollResult"]["searchResult"]["resultItem"]
    t = json["Envelope"]["Body"]["response"]["pollResult"]
    records = [ ]
    t.each { |type, tmp|
        r = [ ]
        f = tmp.kind_of?(Array) ? tmp : [tmp["resultItem"]]
        f.each { |i|
            if (i.class == Array and i[0].key? "_seq") or
               (i.class == Hash and i.key? "_seq")
                r.push i
                next
            end
            r += i["resultItem"].kind_of?(Array) ? i["resultItem"] :
                                                   [i["resultItem"]]
        }
        r.flatten!
        r.each { |i| i["_oper"] = type }
        records += r
    }

    records.sort! { |k1, k2| k1["_seq"] <=> k2["_seq"] }
    pp records if @debug
    init_fake_db(records)
    return records
end

def init_fake_db (records)
    records.each { |record|
        next if !record["metadata"].key? "id_perms" or
                !record["metadata"]["id_perms"].key? "uuid"
        uuid = get_uuid(record["metadata"]["id_perms"]["uuid"])
        next if @db.key? uuid
        next if record["identity"]["name"] !~ /contrail:(.*?):(.*$)/
        fq_name = $2; type = $1.gsub("-", "_")
        @db[uuid] = {
            "uuid" => uuid, "fq_name" => fq_name.split(/:/).to_json,
            "type" => "\"#{type}\""
        }
    }
    pp @db if @debug
end

def from_name (fq, type)
    @db.each { |k, v|
        return v if v["fq_name"] == fq.split(/:/).to_json and \
                    v["type"] == "\"#{type}\""
    }
    return nil
end

def oper_convert (oper)
    return "CREATE" if oper == "createResult"
    return "UPDATE" if oper == "updateResult"
    return "DELETE" if oper == "deleteResult"
    return "createResult"
end

def print_db (oper, uuid, fq_name, type)
    if oper == "updateResult"
        oper = "createResult" if !@seen[uuid]
        @seen[uuid] = true
    end

    fqs = JSON.parse(fq_name).join(":")
    type = JSON.parse(type) if type.start_with? "\""
    event = {
        "oper" => oper_convert(oper), "fq_name" => fq_name, "type" => type,
        "uuid" => "#{@events.size + (@only_initial_sync ? 1 : 0)}:#{uuid}",
        "imid" => "contrail:#{type}:#{fqs}",
    }
    db_copy = @db.deep_dup
    db_copy.each { |k, v| v.delete "uuid" }
    @events.push({"operation" => "rabbit_enqueue", "message" => event.to_json,
                  "db" => db_copy })
end

def extract_fq_name_and_type (name)
    return $2, $1.gsub("-", "_") if name =~ /contrail:(.*?):(.*$)/
end

def parse_links (record)
    fq1, t1 = extract_fq_name_and_type(record["identity"][0]["name"])
    fq2, t2 = extract_fq_name_and_type(record["identity"][1]["name"])

#   return if record["identity"][0]["name"] !~ /contrail:(.*?):(.*$)/
#   fq1 = $2; t1 = $1.gsub("-", "_")
#   return if record["identity"][1]["name"] !~ /contrail:(.*?):(.*$)/
#   fq2 = $2; t2 = $1.gsub("-", "_")

    # Add/Remove ref and back-ref
    r1 = from_name(fq1, t1)
    r2 = from_name(fq2, t2)

    k1 = "ref:" + t2 + ":" + r2["uuid"] if !r2.nil?
#   k2 = "backref:" + t1 + ":" + r1["uuid"] if !r1.nil?
    if record["_oper"] == "updateResult" or record["_oper"] == "searchResult"
        if !r1.nil?
            a = { }
            if record.include? "metadata"
                v = record["metadata"].values.first
                if !v.nil?
                    record["metadata"].values.first.each { |k, v|
                        if !k.start_with?("xmlns:") and !k.start_with?("ifmap_")
                            v = [v] if k == "ipam_subnets" and !v.kind_of? Array
                            a[k] = v
                        end
                    }
                end
            end
            r1[k1] = ({ "attr" => a }).to_json
        end
#       r2[k2] = ({ "attr" => {} }) if !r2.nil?
    else
        r1.delete k1 if !r1.nil?
#       r2.delete k2 if !r2.nil?;
    end

    # Treat link deletes also as updates (to db)
    print_db("updateResult", r1["uuid"], r1["fq_name"], r1["type"]) if !r1.nil?
end

def parse_nodes (record)
    return if !record.key? "identity" or !record["identity"].key? "name"
    return if record["identity"]["name"] !~ /contrail:(.*?):(.*$)/
    fq_name = $2; type = $1.gsub("-", "_")
    pp record["metadata"] if record["metadata"]["id_perms"].nil?
    return if record["metadata"]["id_perms"]["uuid"].nil?
    uuid = get_uuid(record["metadata"]["id_perms"]["uuid"])
    obj = @db[uuid] ||
        ({"uuid" => uuid, "fq_name" => fq_name.split(/:/).to_json,
          "type" => "\"#{type}\""})
    record["metadata"].each { |k, v|
        if v.kind_of? Hash and v.key? "mac_address" and \
            !v["mac_address"].kind_of? Array
            v["mac_address"] = [v["mac_address"]]
        end
        if v.kind_of? Hash and v.key? "policy_rule" and \
            !v["policy_rule"].kind_of? Array
            if v["policy_rule"].kind_of? Hash
                new_v = { }
                v["policy_rule"].each { |k2, v2|
                    if !v2.kind_of? Array then
                        new_v[k2] = [v2] \
                            if k2 == "src_addresses" or k2 == "dst_addresses"
                    else
                        new_v[k2] = v2
                    end
                }
                v["policy_rule"] = new_v
            end
            v["policy_rule"] = [v["policy_rule"]]
        end
        obj["prop:" + k] = v
    }

    # Remove ifmap stuff from id-perms
    if obj.key? "prop:id_perms"
        n = Hash.new
        obj["prop:id_perms"].each { |k, v| n[k] = v if k !~ /^ifmap_|xmlns:/ }
        obj["prop:id_perms"] = n
    end

    # Convert prop to json string
    n = Hash.new
    # uuid = obj["uuid"]
    obj.each {|k, v|
        if v.class == String
            next if k.nil?
            if k == "fq_name" or k == "uuid" or k.start_with?("ref:")
                n[k] = v
            else
                n[k] = !v.start_with?("\"") ? "\"#{v}\"" : v
            end
        else
            n[k] = "#{v.to_json}"
        end
    }
    @db[uuid] = n
    print_db(record["_oper"], obj["uuid"], obj["fq_name"], obj["type"])
end

def delete_nodes (record)
    uuid = get_uuid(record["metadata"]["id_perms"]["uuid"])
    fq_name, type = extract_fq_name_and_type(record["identity"]["name"])
    fq_name = fq_name.split(":").to_json
    @db.delete uuid
    print_db(record["_oper"], uuid, fq_name, type)
end

def process (records)
    records.each { |record|
        if record["identity"].kind_of?(Array) and record["identity"].size == 2
            parse_links(record)
        elsif record["_oper"] == "deleteResult"
            delete_nodes(record)
        else
            parse_nodes(record)
        end
    }
end

def extract_test_to_xml_list (cc_file)
    fd = File.open(cc_file, "r")
    xml_files = [ ]
    fd.read.split(/\n/).each { |line|
        if line =~ /^TEST_F\(/
            xml_files.push([ ])
            next
        end
        if line =~ /FileRead\("(.*)"\)/
            xml_files.last.push $1
        end
    }
    fd.close
    return xml_files
end

def process_files (files)
    return if files.empty?
    init_globals
    pp files if @debug
    files.each { |file_name|
        @events.push({"operation" => "pause"}) if !@events.empty?
        json = read_xml_to_json(file_name)
        records = read_items(json)
        init_fake_db(records)
        process(records) # Process updates
    }

    # Add db-sync event at the beginning.
    if @only_initial_sync and @events.size() > 1
        db = @events.last["db"].deep_dup
        @events.unshift({ "operation" => "db_sync", "OBJ_FQ_NAME_TABLE"  => { }, "db" => db })
        db.each { |k, v|
            type = JSON.parse(v["type"]).to_s
            val = JSON.parse(v["fq_name"]).join(":") + ":" + k
            @events[0]["OBJ_FQ_NAME_TABLE"][type] ||= { }
            @events[0]["OBJ_FQ_NAME_TABLE"][type][val] = "null"
        }
    end

    json_file = File.dirname(files[0])+"/"+File.basename(files[0],".*")+".json"
    @events = [ @events.first ] if @only_initial_sync
    puts JSON.pretty_generate(@events) if @debug
    File.open(json_file, "w") { |fp| fp.puts JSON.pretty_generate(@events) }
    puts "Produced #{json_file}" if @debug
end

def main
    # for cc file, extract xml files from the ut cc file.
    xml_files = [ ]
    ARGV.each { |file_name|
        if file_name.end_with? ".cc" then
            extract_test_to_xml_list(file_name).each { |fl| process_files(fl) }
        else
            xml_files << file_name
        end
    }
    process_files(xml_files)
end

main
