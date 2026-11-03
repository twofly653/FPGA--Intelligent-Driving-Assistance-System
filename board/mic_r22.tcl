# r22 sound-level verification: read the firmware's own displayed value
# (g_db_live) together with the raw PL peak (REG_MIC_STAT.amp), so we can see
# both the new curve and the new 300 ms hold / 62 ms release ballistics.
set xsa {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101/vehicle_top.xsa}
set DB_LIVE 0x0075eb24
if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    loadhw -hw $xsa -mem-ranges [list {0x40000000 0xbfffffff}] -regs
    configparams force-mem-access 1
    puts "ARMED_IN_15S"
    after 15000
    puts "CAPTURE_START"
    set max 0
    set min 999
    set n 0
    while {$n < 400} {
        set v [mrd -value 0x40000044]
        set amp [expr {($v >> 8) & 0xFFFF}]
        set live [expr {[mrd -value $DB_LIVE] & 0xFF}]
        if {$live > $max} { set max $live }
        if {$live < $min} { set min $live }
        if {$n % 3 == 0} {
            puts "MIC t=[expr {$n / 10}].[expr {$n % 10}]s amp=$amp live=$live max=$max min=$min"
        }
        incr n
        after 100
    }
    puts "SUMMARY live_max=$max live_min=$min"
    configparams force-mem-access 0
    disconnect
} err]} {
    puts "CAPTURE_FAILED $err"
    catch {disconnect}
    exit 1
}
exit 0
