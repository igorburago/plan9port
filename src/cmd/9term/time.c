#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

static Channel	*ctimer;	/* chan(Timer*)[100] */
static Timer	*timer;

static uint
msec(void)
{
	return nsec()/1000000;
}

void
timerstop(Timer *t)
{
	t->next = timer;
	timer = t;
}

void
timercancel(Timer *t)
{
	t->cancel = TRUE;
}

static void
timerproc(void *arg)
{
	int i, nt, na, dt, del;
	Timer **t, *x;
	uint old, new;

	USED(arg);
	threadsetname("timerproc");
	rfork(RFFDG);

	t = nil;
	na = 0;
	nt = 0;
	old = msec();
	for(;;){
		sleep(1);
		new = msec();
		if(new < old){	/* timer wrapped; go around, losing a tick */
			old = new;
			continue;
		}
		dt = new-old;
		old = new;
		for(i=0; i<nt; i++){
			x = t[i];
			x->dt -= dt;
			del = FALSE;
			if(x->cancel){
				timerstop(x);
				del = TRUE;
			}else if(x->dt <= 0){
				/* avoid possible deadlock if client is now sending on ctimer */
				if(nbsendul(x->c, 0) > 0)
					del = TRUE;
			}
			if(del){
				memmove(&t[i], &t[i+1], (nt-i-1)*sizeof t[0]);
				nt--;
				i--;
			}
		}
		if(nt == 0)
			x = recvp(ctimer);
		else
			x = nbrecvp(ctimer);
		for(; x!=nil; x=nbrecvp(ctimer)){
			if(nt == na){
				na += 10;
				t = realloc(t, na*sizeof(Timer*));
				if(t == nil)
					error("timer realloc failed");
			}
			t[nt++] = x;
			old = msec();
		}
	}
}

void
timerinit(void)
{
	ctimer = chancreate(sizeof(Timer*), 100);
	chansetname(ctimer, "ctimer");
	proccreate(timerproc, nil, STACK);
}

Timer*
timerstart(int dt)
{
	Timer *t;

	t = timer;
	if(t != nil)
		timer = timer->next;
	else{
		t = emalloc(sizeof(Timer));
		t->c = chancreate(sizeof(int), 0);
	}
	t->next = nil;
	t->dt = dt;
	t->cancel = FALSE;
	sendp(ctimer, t);
	return t;
}

int
waitformouse(Mousectl *mc, int maxwaitms)
{
	enum { WTimer, WMouse, NWALT };
	Alt alts[NWALT+1];
	Timer *timer;

	timer = timerstart(maxwaitms);
	alts[WTimer].c = timer->c;
	alts[WTimer].v = nil;
	alts[WTimer].op = CHANRCV;
	alts[WMouse].c = mc->c;
	alts[WMouse].v = &mc->m;
	alts[WMouse].op = CHANRCV;
	alts[NWALT].op = CHANEND;
	for(;;)
		switch(alt(alts)){
		case WTimer:
			timerstop(timer);
			return FALSE;
		case WMouse:
			timercancel(timer);
			return TRUE;
		}
}
