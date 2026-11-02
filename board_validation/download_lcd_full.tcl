if {[catch {
set release {D:/project/deliveries/Task3_Submission_20261026/github/PYNQ-Z2-Vehicle-Vision/current_release}
connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name == "APU"}
rst -system
after 1000
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
catch {stop}
targets -set -nocase -filter {name =~ "*Cortex-A9*#1"}
catch {stop}
targets -set -nocase -filter {name == "APU"}
fpga -file [file join $release vehicle_top.bit]
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
loadhw -hw [file join $release vehicle_top.xsa] -mem-ranges [list {0x40000000 0xbfffffff}] -regs
configparams force-mem-access 1
source [file join $release ps7_init.tcl]
ps7_init
ps7_post_config
set magic [mrd -value 0x400000f4]
if {$magic != 0x4f423031} {error "Wrong hardware OB01 magic: $magic"}
dow {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release/vehicle_assist_lcd_english.elf}
configparams force-mem-access 0
con -addr 0x00100000
puts "LCD_ENGLISH_CLOCK_FULL_DOWNLOAD_COMPLETE"
disconnect
} err opts]} {puts stderr $err;puts stderr [dict get $opts -errorinfo];catch {disconnect};exit 1}
exit 0
