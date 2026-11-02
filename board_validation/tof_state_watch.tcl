# Read-only watch of the VL53L0X state, printing the distance of valid frames.
set base 0x00600384
set n 0
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    while {$n < 25} {
        set stw  [mrd -value 0x00600360]
        set dist [mrd -value 0x00600364]
        set lv   [expr {$stw & 0xff}]
        set vd   [expr {($stw >> 24) & 0xff}]
        set valid   [mrd -value [expr {$base+0x4c}]]
        set invalid [mrd -value [expr {$base+0x58}]]
        set status  [mrd -value [expr {$base+0x14}]]
        set raw     [mrd -value [expr {$base+0x18}]]
        set sig     [mrd -value [expr {$base+0x24}]]
        set amb     [mrd -value [expr {$base+0x28}]]
        puts "T$n lvl=$lv held=$vd dist=$dist | status=$status raw=$raw sig=$sig amb=$amb | valid=$valid invalid=$invalid"
        incr n
        after 400
    }
    disconnect
} err]} {
    puts "READ_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
