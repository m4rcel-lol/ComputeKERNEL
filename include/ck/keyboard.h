#ifndef CK_KEYBOARD_H
#define CK_KEYBOARD_H

int keyboard_getchar(void);
void keyboard_init(void);

int keyboard_get_layout_count(void);
const char *keyboard_get_layout_code(int index);
const char *keyboard_get_layout_description(int index);
int keyboard_get_sublayout_count(const char *layout);
const char *keyboard_get_sublayout_name(const char *layout, int index);
const char *keyboard_current_layout(void);
const char *keyboard_current_sublayout(void);
int keyboard_set_layout(const char *layout, const char *sublayout);

#endif /* CK_KEYBOARD_H */
