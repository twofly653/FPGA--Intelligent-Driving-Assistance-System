# Generated from the current ARM ELF; regenerate after every build.
# ELF SHA256: 30b71d618a0ef1f0ad7f2a2a90700d8b70864a6bf2819537c09bad112e14d820
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
    puts "TOF_RUNNING [expr {[read_u32 0x0048FA50] & 0xFFFFFFFF}]"
    puts "TOF_IO_ERROR [expr {[read_u32 0x0048FA30] & 0xFFFFFFFF}]"
    puts "CLOCK_VALID [expr {[read_u32 0x0048F8F0] & 0xFFFFFFFF}]"
    puts "UI_PAGE [expr {[read_u32 0x0048FA2C] & 0xFF}]"
    set init_value [read_u32 0x0048FA34]
    puts "TOF_INIT_STAGE $init_value"
    set init_value [read_u32 0x0048FA38]
    if {$init_value >= 2147483648} {set init_value [expr {$init_value - 4294967296}]}
    puts "TOF_INIT_RESULT $init_value"
    set init_value [read_u32 0x0048FA3C]
    if {$init_value >= 2147483648} {set init_value [expr {$init_value - 4294967296}]}
    puts "TOF_INIT_API_STATUS $init_value"
    set init_value [read_u32 0x0048FA40]
    puts "TOF_INIT_IO_ERROR $init_value"
    set init_value [read_u32 0x0048FA44]
    puts "TOF_INIT_RESET_ENABLED $init_value"
    set init_value [read_u32 0x0048FA48]
    puts "TOF_INIT_RESET_IO_SEEN $init_value"
    set init_value [read_u32 0x0048FA4C]
    puts "TOF_INIT_RESET_READ_NACKS $init_value"
    puts "COLLISION_STATE_RAW [mrd 0x0035C440 3]"
    puts "HOME_CLOCK [read_ascii 0x0048F94C 16]"
    set range_status [expr {[read_u32 0x0048FABC] & 0xFF}]
    puts "TOF_RANGE_STATUS $range_status"
    set range_mm [expr {[read_u32 0x0048FAAC] & 0xFFFF}]
    puts "TOF_RANGE_MM $range_mm"
    set signal_16_16 [expr {[read_u32 0x0048FAB0] & 0xFFFFFFFF}]
    puts "TOF_SIGNAL_16_16 $signal_16_16"
    set ambient_16_16 [expr {[read_u32 0x0048FAB4] & 0xFFFFFFFF}]
    puts "TOF_AMBIENT_16_16 $ambient_16_16"
    puts "TOF_SIGNAL_MCPS [expr {$signal_16_16 / 65536.0}]"
    puts "TOF_AMBIENT_MCPS [expr {$ambient_16_16 / 65536.0}]"
    puts "CAMERA_CONTROL [format 0x%08X [read_u32 0x40000078]]"
    puts "CAMERA_IDENTITY_MODE [format 0x%08X [read_u32 0x4000005C]]"
    puts "LCD_TX_STATUS [format 0x%08X [read_u32 0x40000050]]"
    puts "SMALL_FONT_PIXELS_METADATA 0x001547A0 BYTES 7860"
    puts "SMALL_FONT_WIDTH_METADATA 0x00156654 BYTES 95"
    puts "SMALL_FONT_OFFSET_METADATA 0x001566B4 BYTES 380"
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
