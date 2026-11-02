if {[catch {
connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
puts "CLOCK_VALID [mrd -value 0x0047cc40]"
puts "HOME_TIME_A [mrd 0x0047cc9c 4]"
puts "HOME_DATE [mrd 0x0047cc7c 8]"
after 1500
puts "HOME_TIME_B [mrd 0x0047cc9c 4]"
puts "CAMERA_AND_MODE [mrd 0x40000058 9]"
puts "LCD_TX_STATUS [mrd 0x40000050]"
mrd -size w -bin -file {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/board_validation/lcd_live_rgb565.bin} 0x0038131c 34080
configparams force-mem-access 0
disconnect
} err opts]} {puts stderr $err;puts stderr [dict get $opts -errorinfo];catch {disconnect};exit 1}
exit 0
