#!/usr/bin/env gnuplot

set terminal png
set datafile separator ","

set output "asu1_r.png"
plot "asu1_r.csv" using 1:2 title "Reads";

set output "asu_1w.png"
plot "asu1_w.csv" using 1:2 title "Writes";

set output "asu2_r.png"
plot "asu2_r.csv" using 1:2 title "Reads";

set output "asu_2w.png"
plot "asu2_w.csv" using 1:2 title "Writes";

#set output "asu3_r.png"
#plot "asu3_r.csv" using 1:2 title "Reads";

set output "asu3_w.png"
plot "asu3_w.csv" using 1:2 title "Writes";



#set output "cache_writehitrate.png"
#plot "cache.data" using 1:3 every 5 title "Write Hit Rate"
#
#set output "cache_reads.png"
#plot "cache.data" using 1:7 every 5 title "Reads", \
#	 "cache.data" using 1:4 every 5 title "Read Hits"
#
#set output "cache_writes.png"
#plot "cache.data" using 1:8 every 5 title "Writes", \
#     "cache.data" using 1:5 every 5 title "Write Hits"
#
#set output "ios"
#plot "csv" using 2:2 title "Reads", \
#     "cache.data" using 1:6 every 5 title "Deletion Hits"
#
#set output "cache_evictions.png"
#plot "cache.data" using 1:11 every 5 title "Evictions"
#
#set output "cache_readlatency.png"
#plot "cache.data" using 1:13 every 5 title "Mean Read Latency (#usecs)"
#
#set output "cache_writelatency.png"
#plot "cache.data" using 1:14 every 5 title "Mean Write Latency (#usecs)"
#
#set output "cache_deletelatency.png"
#plot "cache.data" using 1:15 every 5 title "Mean Delete Latency (#usecs)"
