# Probe: does the newly generated bitstream actually return the OB02 magic?
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
catch {stop}
configparams force-mem-access 1
puts "BEFORE=[format 0x%08X [mrd -value 0x400000f4]]"
targets -set -nocase -filter {name == "APU"}
rst -system
after 1000
targets -set -nocase -filter {name == "APU"}
set rv [catch {fpga -file {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101/vehicle_top.bit}} eo]
puts "FPGA_RC=$rv"
if {$rv} {puts "FPGA_ERR=$eo"; exit 1}
after 500
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
puts "AFTER=[format 0x%08X [mrd -value 0x400000f4]]"
puts "RAWDATA=[format 0x%08X [mrd -value 0x40000000]]"
disconnect
exit 0
