if {[catch {
 connect -url tcp:127.0.0.1:3122
 targets -set -filter {name =~ "APU"}
 configparams force-mem-access 1
 mrd -bin -file {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/dashboard/photos/.work/c32e35249b3a4b18aec2498350290e8a/photo.bin} 0x0015c420 29922
 configparams force-mem-access 0
 disconnect
} message]} { puts stderr $message; catch {disconnect}; exit 1 }
puts "PHOTO_DAP_READ_OK"
exit 0
