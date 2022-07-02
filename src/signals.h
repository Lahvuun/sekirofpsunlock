#include <signal.h>
#include <stdbool.h>

extern volatile sig_atomic_t got_sigchld;
extern volatile sig_atomic_t got_sigterm;

bool set_sigchld_handler(void);
bool set_sigterm_handler(void);
