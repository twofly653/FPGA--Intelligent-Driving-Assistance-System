#ifndef SPA06_H
#define SPA06_H

int spa06_init(void);
int spa06_read_hpa(int *hpa);
/* No wait — returns -1 if pressure sample not ready yet. */
int spa06_try_read_hpa(int *hpa);

#endif
