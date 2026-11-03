# Generated from the current ARM ELF; regenerate after every build.
# ELF SHA256: e1f4d5c48443da5d2a01feada0548d4d06585db5a8d3e756bc490284b1e5c652
# Bounded live-state reads only. No halt, reset, download, or framebuffer dump.
# Font arrays are listed as metadata only and are NOT read from hardware.
proc read_u32 {address} {
    return [expr {[mrd -value $address] & 0xffffffff}]
}
proc read_ascii {address size} {
    set result ""
    for {set i 0} {$i < $size} {incr i 4} {
        set value [read_u32 [expr {$address + $i}]]
        for {set b 0} {$b < 4 && $i + $b < $size} {incr b} {
            set ch [expr {($value >> (8 * $b)) & 0xff}]
            if {$ch == 0} {return $result}
            if {$ch >= 32 && $ch <= 126} {append result [format %c $ch]} else {append result "?"}
        }
    }
    return $result
}
if {[catch {
    connect -url tcp:127.0.0.1:3122
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    configparams force-mem-access 1
    puts "COLLISION_READY [expr {[read_u32 0x0035C43C] & 0xFFFFFFFF}]"
    puts "TOF_RUNNING [expr {[read_u32 0x0047CDBC] & 0xFFFFFFFF}]"
    puts "TOF_IO_ERROR [expr {[read_u32 0x0047CD80] & 0xFFFFFFFF}]"
    puts "CLOCK_VALID [expr {[read_u32 0x0047CC40] & 0xFFFFFFFF}]"
    puts "UI_PAGE [expr {[read_u32 0x0047CD7C] & 0xFF}]"
    set init_value [read_u32 0x0047CDA0]
    puts "TOF_INIT_STAGE $init_value"
    set init_value [read_u32 0x0047CDA4]
    if {$init_value >= 2147483648} {set init_value [expr {$init_value - 4294967296}]}
    puts "TOF_INIT_RESULT $init_value"
    set init_value [read_u32 0x0047CDA8]
    if {$init_value >= 2147483648} {set init_value [expr {$init_value - 4294967296}]}
    puts "TOF_INIT_API_STATUS $init_value"
    set init_value [read_u32 0x0047CDAC]
    puts "TOF_INIT_IO_ERROR $init_value"
    set init_value [read_u32 0x0047CDB0]
    puts "TOF_INIT_RESET_ENABLED $init_value"
    set init_value [read_u32 0x0047CDB4]
    puts "TOF_INIT_RESET_IO_SEEN $init_value"
    set init_value [read_u32 0x0047CDB8]
    puts "TOF_INIT_RESET_READ_NACKS $init_value"
    set reset_value [read_u32 0x0047CD84]
    puts "TOF_RESET_STAGE $reset_value"
    set reset_value [read_u32 0x0047CD88]
    if {$reset_value >= 2147483648} {set reset_value [expr {$reset_value - 4294967296}]}
    puts "TOF_RESET_RESULT $reset_value"
    set reset_value [read_u32 0x0047CD8C]
    if {$reset_value >= 2147483648} {set reset_value [expr {$reset_value - 4294967296}]}
    puts "TOF_RESET_FIRST_API_STATUS $reset_value"
    set reset_value [read_u32 0x0047CD90]
    puts "TOF_RESET_ASSERT_READS $reset_value"
    set reset_value [read_u32 0x0047CD94]
    puts "TOF_RESET_BOOT_READS $reset_value"
    set reset_value [read_u32 0x0047CD98]
    puts "TOF_RESET_READ_NACKS $reset_value"
    set reset_value [read_u32 0x0047CD9C]
    puts "TOF_RESET_RELEASE_ATTEMPTS $reset_value"
    puts "COLLISION_STATE_RAW [mrd 0x0035C440 3]"
    puts "HOME_CLOCK [read_ascii 0x0047CC9C 16]"
    set range_status [expr {[read_u32 0x0047CE28] & 0xFF}]
    puts "TOF_RANGE_STATUS $range_status"
    set range_mm [expr {[read_u32 0x0047CE18] & 0xFFFF}]
    puts "TOF_RANGE_MM $range_mm"
    set signal_16_16 [expr {[read_u32 0x0047CE1C] & 0xFFFFFFFF}]
    puts "TOF_SIGNAL_16_16 $signal_16_16"
    set ambient_16_16 [expr {[read_u32 0x0047CE20] & 0xFFFFFFFF}]
    puts "TOF_AMBIENT_16_16 $ambient_16_16"
    puts "TOF_SIGNAL_MCPS [expr {$signal_16_16 / 65536.0}]"
    puts "TOF_AMBIENT_MCPS [expr {$ambient_16_16 / 65536.0}]"
    puts "CAMERA_CONTROL [format 0x%08X [read_u32 0x40000078]]"
    puts "CAMERA_IDENTITY_MODE [format 0x%08X [read_u32 0x4000005C]]"
    puts "LCD_TX_STATUS [format 0x%08X [read_u32 0x40000050]]"
    puts "SMALL_FONT_PIXELS_METADATA 0x00153B60 BYTES 7860"
    puts "SMALL_FONT_WIDTH_METADATA 0x00155A14 BYTES 95"
    puts "SMALL_FONT_OFFSET_METADATA 0x00155A74 BYTES 380"
    configparams force-mem-access 0
    disconnect
} err opts]} {
    puts stderr $err
    if {[dict exists $opts -errorinfo]} {puts stderr [dict get $opts -errorinfo]}
    catch {configparams force-mem-access 0}
    catch {disconnect}
    exit 1
}
exit 0
