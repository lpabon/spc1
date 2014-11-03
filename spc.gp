#!/usr/bin/env gnuplot

set terminal png
set datafile separator ","
set ylabel "Times Accessed"
set xlabel "4K Offet"

set output "asu1_r.png"
set title "ASU 1: Data Store"
plot "asu1_r.csv" using 1:2 title "Reads"

set output "asu1_w.png"
set title "ASU 1: Data Store"
plot "asu1_w.csv" using 1:2 title "Writes"

set output "asu2_r.png"
set title "ASU 2: User Store"
plot "asu2_r.csv" using 1:2 title "Reads"

set output "asu2_w.png"
set title "ASU 2: User Store"
plot "asu2_w.csv" using 1:2 title "Writes"

# ASU3 has no reads because it is a simulated log

set output "asu3_w.png"
set title "ASU 3: Log"
plot "asu3_w.csv" using 1:2 title "Writes"
