connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
for {set i 0} {$i < 4} {incr i} {
 puts "SAMPLE $i READY [mrd -value 0x0035c43c] IO [mrd -value 0x0047cd80] RUN [mrd -value 0x0047cd84]"
 puts "CACHED [mrd 0x0047cdd8 8]"
 puts "STATE [mrd 0x0035c440 3]"
 after 500
}
configparams force-mem-access 0
disconnect
exit 0
