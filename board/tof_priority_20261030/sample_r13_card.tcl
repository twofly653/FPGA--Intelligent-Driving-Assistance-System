connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
for {set i 0} {$i<30} {incr i} {
 set st [expr {[mrd -value 0x0049894c]&255}]
 set mm [expr {[mrd -value 0x0049893c]&65535}]
 set signal [mrd -value 0x00498940]
 set ambient [mrd -value 0x00498944]
 puts "RANGE $i $st $mm $signal $ambient"
 after 220
}
configparams force-mem-access 0
disconnect
exit 0
