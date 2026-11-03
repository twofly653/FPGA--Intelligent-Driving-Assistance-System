# Default R3 ELF f96acef27206d58148a902f277d709aabaad9700b890d421d06848d33daabee1
# Bounded passive RAM/MMIO reads; application keeps running. 30 observations, 250ms apart.
proc u32 {a} {return [expr {[mrd -value $a] & 0xffffffff}]}
if {[catch {
connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
puts "START_DEFAULT_R3_CARD_20_30CM"
for {set i 0} {$i < 30} {incr i} {
set status [expr {[u32 0x0047CE0C] & 255}]
set mm [expr {[u32 0x0047CDFC] & 65535}]
set signal [u32 0x0047CE00]
set ambient [u32 0x0047CE04]
set io [u32 0x0047CD80]
puts "SAMPLE $i $status $mm $signal $ambient $io"
after 250
}
puts "CLOCK_VALID [u32 0x0047CC40]"
puts "CLOCK_WORDS [mrd -value 0x0047CC9C 4]"
puts "CAMERA_CONTROL [format 0x%08X [u32 0x40000078]]"
puts "CAMERA_IDENTITY_MODE [format 0x%08X [u32 0x4000005C]]"
configparams force-mem-access 0
disconnect
} err]} {puts stderr $err;catch {configparams force-mem-access 0};catch {disconnect};exit 1}
exit 0
