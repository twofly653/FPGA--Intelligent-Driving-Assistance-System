proc read32 {a} {return [expr {[mrd -value $a]+0}]}
if {[catch {
 connect -url tcp:127.0.0.1:3122
 targets -set -nocase -filter {name == "APU"}
 configparams force-mem-access 1
 set original [read32 0x40000078]
 for {set mode 0} {$mode<4} {incr mode} {
  set ctrl [expr {0x810e | ($mode<<9)}]
  mwr 0x40000078 $ctrl
  after 250
  set diag [read32 0x4000005c]
  if {(($diag>>8)&3)!=$mode} {error "Mode commit failed $mode diag=$diag"}
  set first [read32 0x40000064];set begin [clock milliseconds]
  after 1500
  set last [read32 0x40000064];set duration [expr {[clock milliseconds]-$begin}]
  set accepted [expr {($last-$first)&255}]
  set ddr [read32 0x4000008c]
  puts [format "OPTIMIZED_MODE mode=%d frames=%d duration_ms=%d fps=%.3f ddr=0x%08X" $mode $accepted $duration [expr {1000.0*$accepted/$duration}] $ddr]
  if {($ddr&0xf0)!=0} {error "DDR fault in mode $mode"}
  if {$accepted<18} {error "Source rate too low in mode $mode"}
 }
 mwr 0x40000078 $original
 puts "OPTIMIZED_MODES_PASS"
 disconnect
} err opts]} {catch {mwr 0x40000078 0x800e}; puts stderr $err;catch {disconnect};exit 1}
exit 0
