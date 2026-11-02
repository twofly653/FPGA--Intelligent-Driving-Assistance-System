# Loudness capture: wait 20 s (so the operator can get ready), then sample
# REG_MIC_STAT every 200 ms for 80 s and report the peak-to-peak span.
# Read-only: no reset, no halt.
set xsa {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101/vehicle_top.xsa}
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    loadhw -hw $xsa -mem-ranges [list {0x40000000 0xbfffffff}] -regs
    configparams force-mem-access 1
    puts "CAPTURE_ARMED_IN_20S"
    after 20000
    puts "CAPTURE_START"
    set max 0
    set min 9999
    set n 0
    while {$n < 400} {
        set v [mrd -value 0x40000044]
        set db [expr {$v & 0xFF}]
        set amp [expr {($v >> 8) & 0xFFFF}]
        if {$amp > $max} { set max $amp }
        if {$amp < $min} { set min $amp }
        if {$n % 5 == 0} {
            puts "MIC t=[expr {$n * 200}] db=$db amp=$amp max=$max min=$min"
        }
        incr n
        after 200
    }
    puts "SUMMARY max=$max min=$min span=[expr {$max - $min}]"
    configparams force-mem-access 0
    disconnect
} err]} {
    puts "CAPTURE_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
