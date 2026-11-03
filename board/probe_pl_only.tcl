# Can we still program the PL? Tells us whether the FPGA is powered.
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name == "xc7z020"}
set bit {D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/firmware/release_ob02_20261101/vehicle_top.bit}
if {[catch {fpga -file $bit} e]} {
    puts "PL_PROGRAM_FAIL=$e"
} else {
    puts "PL_PROGRAM_OK"
}
disconnect
exit 0
