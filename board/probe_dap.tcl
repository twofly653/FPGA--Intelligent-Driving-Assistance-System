# Diagnose a stuck Zynq DAP: slow TCK, re-enumerate, then try APU select.
connect -url tcp:127.0.0.1:3121
puts "--- jtag targets ---"
puts [targets]
catch {targets -set -nocase -filter {name == "xc7z020"}} e0
puts "DAP_FPGA_SELECT=$e0"
catch {jtag frequency 1000000} e1
puts "DAP_SLOW_TCK=$e1"
after 800
puts "--- targets @1MHz ---"
puts [targets]
catch {targets -set -nocase -filter {name == "APU"}} e2
puts "DAP_APU_SELECT=$e2"
catch {targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}} e3
puts "DAP_A9_SELECT=$e3"
disconnect
exit 0
