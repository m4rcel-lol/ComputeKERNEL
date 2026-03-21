#ifndef CK_SSH_H
#define CK_SSH_H

void sshd_init(void);
int sshd_is_available(void);
int sshd_is_enabled(void);
int sshd_set_enabled(int enabled);

#endif /* CK_SSH_H */
