connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
puts "FONT_TABLE [mrd 0x0012b30c 4]"
puts "TOF_READY [mrd -value 0x0035c43c]"
puts "TOF_STATE [mrd 0x0035c440 3]"
puts "TOF_IO_AND_RUNNING [mrd 0x0047cd80 2]"
puts "CURRENT_PAGE [mrd 0x0047cd7c 1]"
mrd -size w -bin -file {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/board_validation/font_small_live.bin} 0x00153440 1965
configparams force-mem-access 0
disconnect
exit 0
