# Read-only sampling of REG_MIC_STAT (0x40000044).
# Low byte = db, [23:8] = amp.  No reset, no halt, PS keeps running.
set n 0
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    while {$n < 40} {
        set v   [mrd -value 0x40000044]
        set db  [expr {$v & 0xFF}]
        set amp [expr {($v >> 8) & 0xFFFF}]
        puts "MIC t=$n db=$db amp=$amp"
        incr n
        after 150
    }
    disconnect
} err]} {
    puts "READ_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
