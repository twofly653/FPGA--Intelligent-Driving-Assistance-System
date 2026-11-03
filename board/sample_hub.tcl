# Sample the obstacle mailbox a few times to see whether it is live.
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
proc hex32 {v} { return [format 0x%08X $v] }
set base 0x40000000
for {set s 0} {$s < 6} {incr s} {
    set ctl [mrd -value [expr {$base + 0xac}]]
    set st  [mrd -value [expr {$base + 0xf0}]]
    set b0a [mrd -value [expr {$base + 0xb0}]]
    set b0b [mrd -value [expr {$base + 0xb4}]]
    set b1a [mrd -value [expr {$base + 0xb8}]]
    set n   [expr {$ctl & 0xf}]
    set age [expr {($ctl >> 16) & 0x3ff}]
    puts [format "S%d ctl=%s n=%d age=%d bit8=%d bit9=%d | st=%s seq=%d busy=%d ack=%d rej=%d | b0=%s %s b1=%s" \
        $s [hex32 $ctl] $n $age [expr {($ctl>>8)&1}] [expr {($ctl>>9)&1}] \
        [hex32 $st] [expr {($st>>16)&0xffff}] [expr {$st&1}] [expr {($st>>1)&1}] [expr {($st>>2)&1}] \
        [hex32 $b0a] [hex32 $b0b] [hex32 $b1a]]
    after 250
}
configparams force-mem-access 0
disconnect
exit 0
