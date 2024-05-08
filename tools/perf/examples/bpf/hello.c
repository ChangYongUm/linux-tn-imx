#include <stdio.h>

int syscall_enter(openat)(void *args)
{
	puts("Hello, world UCY\n");
	return 0;
}

license(GPL);
