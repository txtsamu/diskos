#ifndef USBHID_H
#define USBHID_H
/* Media buttons of a USB IEM/DAC in USB Audio mode (route OUT_USB). Call once per main-loop pass; cheap when idle
 * or when the route is not USB. screen_lit: 1 only when the panel is fully on (shows the volume bar).
 * Returns 1 if a button was handled (caller counts it as activity, never as a wake). */
int usbhid_poll(int screen_lit);
#endif
