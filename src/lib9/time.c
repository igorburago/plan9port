#include <u.h>
#include <sys/time.h>
#include <time.h>
#include <sys/resource.h>
#define NOPLAN9DEFINES
#include <libc.h>

long
p9times(long *t)
{
	struct rusage ru, cru;

	if(getrusage(RUSAGE_SELF, &ru)<0
	|| getrusage(RUSAGE_CHILDREN, &cru)<0)
		return -1;

	t[0] = ru.ru_utime.tv_sec*1000 + ru.ru_utime.tv_usec/1000;
	t[1] = ru.ru_stime.tv_sec*1000 + ru.ru_stime.tv_usec/1000;
	t[2] = cru.ru_utime.tv_sec*1000 + cru.ru_utime.tv_usec/1000;
	t[3] = cru.ru_stime.tv_sec*1000 + cru.ru_stime.tv_usec/1000;

	/* BUG */
	return t[0]+t[1]+t[2]+t[3];
}

double
p9cputime(void)
{
	long t[4];

	if(p9times(t) < 0)
		return -1.0;
	return ((double)t[0]+(double)t[1]+(double)t[2]+(double)t[3])/1000.0;
}

vlong
p9nsec(void)
{
	struct timeval tv;

	if(gettimeofday(&tv, nil) < 0)
		return -1;
	return (vlong)tv.tv_sec*1000*1000*1000 + (vlong)tv.tv_usec*1000;
}

long
p9time(long *tt)
{
	long t;

	t = time(nil);
	if(tt != nil)
		*tt = t;
	return t;
}
