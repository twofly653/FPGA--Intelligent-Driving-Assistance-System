#ifndef COMPASS_COMMANDS_H
#define COMPASS_COMMANDS_H
void compass_commands_init(void);
/* Single UART RX owner: bounded ASCII TIME, compass, OBSTACLE and PHOTO lines. */
void compass_commands_feed(unsigned char byte);
void compass_commands_poll(void);
#endif
