#include "thread_name.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

void conscious_thread_name_set(const char *name)
{
    char buf[16];
    size_t len;

    if (name == NULL || name[0] == '\0') {
        return;
    }

    len = strlen(name);
    if (len < sizeof(buf)) {
        pthread_setname_np(pthread_self(), name);
        return;
    }

    /* Should not happen for cons main / grid / "cons TAG id". */
    snprintf(buf, sizeof(buf), "%s", name);
    pthread_setname_np(pthread_self(), buf);
}
