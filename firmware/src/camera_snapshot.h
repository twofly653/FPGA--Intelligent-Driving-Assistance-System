#ifndef CAMERA_SNAPSHOT_H
#define CAMERA_SNAPSHOT_H
void camera_snapshot_init(void);
/* Exact ASCII commands only. Return 1 if this module recognized the command.
 * Commands enqueue work; capture is never executed by an LCD idle hook. */
int camera_snapshot_command(const char *line);
void camera_snapshot_poll(void);
#endif
