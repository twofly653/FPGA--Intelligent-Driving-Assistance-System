# Time-series read of the VL53L0X collision diagnostics. Read-only.
set base 0x00600380
set n 0
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    while {$n < 8} {
        set valid   [mrd -value [expr {$base+0x4c}]]
        set invalid [mrd -value [expr {$base+0x58}]]
        set framec  [mrd -value [expr {$base+0x48}]]
        set status  [mrd -value [expr {$base+0x14}]]
        set raw     [mrd -value [expr {$base+0x18}]]
        set sig     [mrd -value [expr {$base+0x24}]]
        set amb     [mrd -value [expr {$base+0x28}]]
        set lret    [mrd -value [expr {$base+0x04}]]
        set dist    [mrd -value 0x00600364]
        set stw     [mrd -value 0x00600360]
        puts "T$n ret=$lret status=$status raw=$raw signal=$sig ambient=$amb frames=$framec valid=$valid invalid=$invalid level_valid=$stw dist=$dist"
        incr n
        after 700
    }
    disconnect
} err]} {
    puts "READ_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
