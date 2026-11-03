# Read-only probe of the VL53L0X collision service state while the app runs.
# Never resets, halts or reprograms anything.
set addr_ready 0x0060035c
set addr_state 0x00600360
set addr_diag  0x00600380

if {[catch {
    connect -url tcp:127.0.0.1:3121
    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
    puts "CPU_STATE=[state]"
    puts "READY=[mrd -value $addr_ready]"
    puts "STATE=[mrd -value $addr_state] [mrd -value [expr {$addr_state+4}]] [mrd -value [expr {$addr_state+8}]]"
    set base $addr_diag
    set names {ready last_return last_init_result last_api_status last_io_error range_status raw_mm recovery_reason recovery_pending signal ambient last_poll_ms last_frame_ms last_valid_ms poll_age frame_age valid_age poll_count frame_count valid_count not_ready_count io_error_count invalid_count not_running_count init_failure_count recovery_count retry_in_ms}
    set i 0
    foreach n $names {
        set v [mrd -value [expr {$base + $i*4}]]
        puts "DIAG $n = $v"
        incr i
    }
    disconnect
} err opts]} {
    puts "READ_FAILED"
    puts $err
    catch {disconnect}
    exit 1
}
exit 0
