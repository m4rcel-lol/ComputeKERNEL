#ifndef CK_MOUSE_H
#define CK_MOUSE_H

#include <ck/types.h>

#define MOUSE_BUTTON_LEFT   0x1
#define MOUSE_BUTTON_RIGHT  0x2
#define MOUSE_BUTTON_MIDDLE 0x4

struct mouse_event {
    s8 dx;
    s8 dy;
    u8 buttons;
};

void mouse_init(void);
void mouse_irq_handler(void);
int mouse_getevent(struct mouse_event *ev);
int mouse_is_available(void);
void mouse_get_position(int *x, int *y, u8 *buttons);

#endif /* CK_MOUSE_H */
