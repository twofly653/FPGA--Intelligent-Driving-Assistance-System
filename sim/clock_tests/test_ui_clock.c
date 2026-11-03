#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_clock.h"
#include "compass_commands.h"

static uint64_t ticks;
uint64_t ui_clock_test_ticks(void) { return ticks; }
static unsigned photo_count, calibration_count, obstacle_count, obstacle_enabled;
void obstacle_service_set_enabled(unsigned enabled) { ++obstacle_count; obstacle_enabled=enabled; }
int camera_snapshot_command(const char *line)
{
    if (!strcmp(line,"PHOTO BEGIN")) { ++photo_count; return 1; }
    return 0;
}
void compass_service_begin_calibration(void) { ++calibration_count; }
int compass_service_finish_calibration(void) { return 1; }
void compass_service_cancel_calibration(void) {}
int compass_service_align_reference(int ref) { (void)ref; return 1; }
void compass_service_flip_direction(void) {}
void compass_service_confirm_direction(void) {}
static void feed(const char *line)
{
    while (*line) compass_commands_feed((unsigned char)*line++);
}
static ui_clock_snapshot_t expect(unsigned y,unsigned m,unsigned d,
                                  unsigned h,unsigned min,unsigned sec)
{
    ui_clock_snapshot_t s;
    ui_clock_get_snapshot(&s);
    assert(s.valid && s.year==y && s.month==m && s.day==d);
    assert(s.hour==h && s.min==min && s.sec==sec);
    return s;
}
static void set(const char *line)
{
    assert(ui_clock_command(line)==1);
}
int main(void)
{
    ui_clock_snapshot_t s;
    const char *invalid[] = {
        "TIME SET 2026-02-29T10:20:30", "TIME SET 2100-02-29T10:20:30",
        "TIME SET 2026-04-31T10:20:30", "TIME SET 2026-00-01T10:20:30",
        "TIME SET 2026-13-01T10:20:30", "TIME SET 2026-10-00T10:20:30",
        "TIME SET 2026-10-32T10:20:30", "TIME SET 1999-12-31T10:20:30",
        "TIME SET 2200-01-01T10:20:30", "TIME SET 2026-10-28T24:20:30",
        "TIME SET 2026-10-28T10:60:30", "TIME SET 2026-10-28T10:20:60",
        "TIME SET 2026-10-28T10:20:30Z", "TIME SET 2026-10-28T10:20:30 ",
        "TIME SET 2026-10-28 10:20:30", "TIME SET 2026/10/28T10:20:30",
        "TIME SET +026-10-28T10:20:30", "TIME SET 2026-1a-28T10:20:30",
        "TIME SET 2026-10-28T10:20", "TIME", "TIME RESET"
    };
    unsigned i;
    ticks=UINT64_C(1200000000);
    ui_clock_reset();
    ui_clock_get_snapshot(&s);
    assert(!s.valid && !s.hour && !s.year);
    ticks+=UINT64_C(90000000);
    ui_clock_poll(); ui_clock_get_snapshot(&s);
    assert(!s.valid); /* Uptime must never pretend to be civil time. */
    ui_clock_get_snapshot(0);
    assert(!ui_clock_command(0));
    assert(!ui_clock_command("PHOTO BEGIN"));
    set("TIME SET 2026-10-28T23:59:59");
    s=expect(2026,10,28,23,59,59);
    assert(s.weekday==3 && s.sync_age_seconds==0);
    ticks+=999999; expect(2026,10,28,23,59,59);
    ticks+=1; s=expect(2026,10,29,0,0,0);
    assert(s.weekday==4 && s.sync_age_seconds==1);
    /* Keep running with no polling and no serial traffic for 3 days. */
    ticks+=UINT64_C(259201000000); s=expect(2026,11,1,0,0,1);
    assert(s.weekday==0 && s.sync_age_seconds==259202);
    set("TIME SET 2026-12-31T23:59:59");
    ticks+=1000000; expect(2027,1,1,0,0,0);
    set("TIME SET 2024-02-28T23:59:59");
    ticks+=1000000; expect(2024,2,29,0,0,0);
    ticks+=UINT64_C(86400000000); expect(2024,3,1,0,0,0);
    set("TIME SET 2100-02-28T23:59:59");
    ticks+=1000000; expect(2100,3,1,0,0,0);
    set("TIME SET 2000-02-29T12:00:00");
    expect(2000,2,29,12,0,0);
    set("TIME SET 2026-10-28T10:20:30");
    for (i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        assert(ui_clock_command(invalid[i])==1);
        expect(2026,10,28,10,20,30);
    }
    ticks+=5000000;
    expect(2026,10,28,10,20,35); /* Rejections did not reset anchor. */
    set("TIME SET 2026-10-28T10:22:00");
    s=expect(2026,10,28,10,22,0); assert(!s.sync_age_seconds);
    /* XTime is unsigned: normal wrap still preserves elapsed time. */
    ticks=UINT64_MAX-500000;
    set("TIME SET 2026-10-28T10:22:00");
    ticks+=1000000; expect(2026,10,28,10,22,1);
    /* Test real UART owner: length/ASCII safeguards and existing commands. */
    ticks=1000000;
    ui_clock_reset(); compass_commands_init();
    feed("TIME SET 2026-10-28T08:09:10\r\n");
    expect(2026,10,28,8,9,10);
    feed("PHOTO BEGIN\nCAL BEGIN\nOBSTACLE ON\nOBSTACLE OFF\n");
    assert(photo_count==1 && calibration_count==1);
    assert(obstacle_count==2 && obstacle_enabled==0);
    expect(2026,10,28,8,9,10);
    compass_commands_init(); /* UI restarts may reset parser, not clock. */
    ticks+=2000000; expect(2026,10,28,8,9,12);
    feed("TIME SET 2026-10-28T01:02:03extra-extra-extra-extra-extra-extra\n");
    expect(2026,10,28,8,9,12);
    feed("TIME SET 2026-10-28T01:02:03");
    compass_commands_feed(0xff); compass_commands_feed('\n');
    expect(2026,10,28,8,9,12);
    feed("TIME SET 2026-10-28T01:02:03\nPHOTO BEGIN\nCAL BEGIN\n");
    expect(2026,10,28,1,2,3);
    assert(photo_count==2 && calibration_count==2);
    ui_clock_reset(); ui_clock_get_snapshot(&s); assert(!s.valid);
    puts("PASS: ui_clock civil-time rollovers, leap years, validation, holdover, UART coexistence");
    return 0;
}
