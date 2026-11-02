if {[catch {
 connect -url tcp:127.0.0.1:3122
 targets -set -filter {name =~ "APU"}
 configparams force-mem-access 1
 mrd -bin -file {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/dashboard/photos/.work/119db1b1f11344a787e9354ab699666c/photo.bin} 0x00400340 27292
 configparams force-mem-access 0
 disconnect
} message]} { puts stderr $message; catch {disconnect}; exit 1 }
puts "PHOTO_DAP_READ_OK"
exit 0
