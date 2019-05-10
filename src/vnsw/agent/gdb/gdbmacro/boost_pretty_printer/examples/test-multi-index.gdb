b done
r
p s
python import boost.printers
python v = boost.printers.parse_and_eval('s')
python boost.printers.Boost_Multi_Index.idx[long(v.address)] = 1
p s
python boost.printers.Boost_Multi_Index.idx[long(v.address)] = 2
p s
python boost.printers.Boost_Multi_Index.idx[long(v.address)] = 3
p s
python boost.printers.Boost_Multi_Index.idx[long(v.address)] = 4
p s
q
