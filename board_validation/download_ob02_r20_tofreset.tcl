# OB02 download with the r20 sensor-reset application.
set release {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101}
set elf {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release/vehicle_assist_pcnet_r20_tofreset_tof_soft_reset_experimental.elf}
set golden {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/golden_ob02_bit}
if {[file exists [file join $golden vehicle_top.bit]]} {
    set bit [file join $golden vehicle_top.bit]
} else {
    set bit [file join $release vehicle_top.bit]
}
puts "OB02_BIT=$bit"
puts "APP_ELF=$elf"
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name == "APU"}
rst -system
after 1000
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
catch {stop}
targets -set -nocase -filter {name =~ "*Cortex-A9*#1"}
catch {stop}
targets -set -nocase -filter {name == "APU"}
fpga -file $bit
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
loadhw -hw [file join $release vehicle_top.xsa] -mem-ranges [list {0x40000000 0xbfffffff}] -regs
configparams force-mem-access 1
source [file join $release ps7_init.tcl]
ps7_init
ps7_post_config
set magic [mrd -value 0x400000f4]
if {$magic != 0x4f423032} {error "Wrong hardware OB02 magic: $magic"}
puts "OB02_MAGIC_OK=$magic"
dow $elf
configparams force-mem-access 0
con -addr 0x00100000
puts "OB02_DOWNLOAD_COMPLETE"
disconnect
} err opts]} {puts stderr $err;puts stderr [dict get $opts -errorinfo];catch {disconnect};exit 1}
exit 0
