connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
puts "RANGE_CACHED_SAMPLE [mrd 0x0047cdd8 8]"
configparams force-mem-access 0
disconnect
exit 0
