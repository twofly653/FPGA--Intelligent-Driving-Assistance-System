# Read-only sampling of REG_MIC_STAT (0x40000044) driven by the reworked
# pdm_level.v.  Low byte = db, [23:8] = amp (peak |duty-50%| deviation, 0..64).
# No reset, no halt, PS keeps running.  Needs the hw description loaded so the
# PL AXI aperture is reachable in this fresh xsdb session.
set xsa {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101/vehicle_top.xsa}
set n 0
set count 25
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    loadhw -hw $xsa -mem-ranges [list {0x40000000 0xbfffffff}] -regs
    configparams force-mem-access 1
    while {$n < $count} {
        set v   [mrd -value 0x40000044]
        set db  [expr {$v & 0xFF}]
        set amp [expr {($v >> 8) & 0xFFFF}]
        puts "MIC t=$n db=$db amp=$amp"
        incr n
        after 120
    }
    configparams force-mem-access 0
    disconnect
} err]} {
    puts "READ_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
