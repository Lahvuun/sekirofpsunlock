#define _XOPEN_SOURCE 500

#include "signals.h"

#include <stdio.h>
#include <signal.h>

volatile sig_atomic_t got_sigchld = 0;
volatile sig_atomic_t got_sigterm = 0;

static void handle_signals(int signum)
{
	switch (signum) {
	case SIGCHLD:
		got_sigchld += 1;
		break;
	case SIGTERM:
		got_sigterm += 1;
	}
}

bool set_sigchld_handler(void)
{
	struct sigaction act = { 0 };
	act.sa_handler = handle_signals;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		perror("sigaction() failed");
		return false;
	}

	return true;
}

bool set_sigterm_handler(void)
{
	struct sigaction act = { 0 };
	act.sa_handler = handle_signals;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGTERM, &act, NULL) == -1) {
		perror("sigaction() failed");
		return false;
	}

	return true;
}
