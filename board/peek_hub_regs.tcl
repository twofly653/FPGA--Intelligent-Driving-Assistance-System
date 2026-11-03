# Read-only peek at the PL hub status while the firmware runs.
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
configparams force-mem-access 1
proc hex32 {v} { return [format 0x%08X $v] }
set base 0x40000000
puts "MAGIC=[hex32 [mrd -value [expr {$base + 0xf4}]]]  (expect 0x4F423032)"
puts "COMMIT=[hex32 [mrd -value [expr {$base + 0xac}]]]"
puts "STATUS=[hex32 [mrd -value [expr {$base + 0xf0}]]]"
for {set k 0} {$k < 8} {incr k} {
    set a [mrd -value [expr {$base + 0xb0 + $k*8}]]
    set b [mrd -value [expr {$base + 0xb4 + $k*8}]]
    set xmin [expr {($a >> 0) & 0x3ff}]
    set ymin [expr {($a >> 16) & 0x3ff}]
    set xmax [expr {($b >> 0) & 0x3ff}]
    set ymax [expr {($b >> 16) & 0x3ff}]
    set id [expr {((($b >> 10) & 0xf) << 12) | ((($a >> 26) & 0x3f) << 6) | (($a >> 10) & 0x3f)}]
    set cls [expr {(($b >> 14) & 0x3) | ((($b >> 26) & 0x3f) << 2)}]
    puts [format "BOX%d a=%s b=%s id=%d cls=%d rect=(%d,%d)-(%d,%d)" $k [hex32 $a] [hex32 $b] $id $cls $xmin $ymin $xmax $ymax]
}
configparams force-mem-access 0
disconnect
exit 0
