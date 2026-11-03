# r21 symbol layout: g_ready=0x0060035c, g_state=0x00600360, g_diag=0x00600384
# (g_diag moved +4 vs r20 because stopwatch/ui gained globals).
# Read-only: no reset, no halt.
set base_diag 0x00600384
set addr_state 0x00600360
set n 0
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    puts "READY=[mrd -value 0x0060035c]"
    while {$n < 6} {
        set valid   [mrd -value [expr {$base_diag+0x4c}]]
        set invalid [mrd -value [expr {$base_diag+0x58}]]
        set framec  [mrd -value [expr {$base_diag+0x48}]]
        set status  [mrd -value [expr {$base_diag+0x14}]]
        set raw     [mrd -value [expr {$base_diag+0x18}]]
        set sig     [mrd -value [expr {$base_diag+0x24}]]
        set amb     [mrd -value [expr {$base_diag+0x28}]]
        set lret    [mrd -value [expr {$base_diag+0x04}]]
        set dist    [mrd -value [expr {$addr_state+0x4}]]
        set stw     [mrd -value $addr_state]
        puts "T$n ret=$lret status=$status raw=$raw signal=$sig ambient=$amb frames=$framec valid=$valid invalid=$invalid level=$stw dist=$dist"
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
