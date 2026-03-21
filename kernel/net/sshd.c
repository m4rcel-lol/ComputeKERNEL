#include <ck/kernel.h>
#include <ck/ssh.h>

static int g_sshd_available = 0;
static int g_sshd_enabled = 0;

void sshd_init(void)
{
    g_sshd_available = 1;
    g_sshd_enabled = 0;
    ck_puts("[ssh] daemon scaffold: available (disabled by default)\n");
}

int sshd_is_available(void)
{
    return g_sshd_available;
}

int sshd_is_enabled(void)
{
    return g_sshd_enabled;
}

int sshd_set_enabled(int enabled)
{
    if (!g_sshd_available)
        return -1;
    g_sshd_enabled = enabled ? 1 : 0;
    return 0;
}
