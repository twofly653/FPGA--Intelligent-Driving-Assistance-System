# 30 s read-only watch of the VL53L0X result registers (no reset, no halt).
set base 0x00600384
set n 0
set seen ""
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    while {$n < 20} {
        set valid   [mrd -value [expr {$base+0x4c}]]
        set invalid [mrd -value [expr {$base+0x58}]]
        set status  [mrd -value [expr {$base+0x14}]]
        set raw     [mrd -value [expr {$base+0x18}]]
        set sig     [mrd -value [expr {$base+0x24}]]
        set amb     [mrd -value [expr {$base+0x28}]]
        set lret    [mrd -value [expr {$base+0x04}]]
        set stw     [mrd -value 0x00600360]
        puts "S$n ret=$lret status=$status raw=$raw signal=$sig ambient=$amb ctx=valid:$valid invalid:$invalid state=$stw"
        incr n
        after 1000
    }
    disconnect
} err]} {
    puts "READ_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
