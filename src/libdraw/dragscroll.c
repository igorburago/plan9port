#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>

static uint
msec(void)
{
	return nsec()/1000000;
}

static int
sign(int x)
{
	return (x > 0) - (x < 0);
}

static int
divround(int x, int d)
{
	return (x + sign(x)*sign(d)*d/2) / d;
}

void
dragscrollreset(Dragscroll *s, Mouse *m, int pacediffms)
{
	s->mousewallms = msec();
	s->lastmovems = s->mousems = m->msec;
	s->pacediffms = pacediffms;
}

void
dragscrollresume(Dragscroll *s, Mouse *m, int newevent)
{
	uint now, newms;

	now = msec();
	if(newevent){
		/*
		 * Manual correction of lastmovems on wrap-around is necessary for two
		 * reasons. (1) Mouse.msec may wrap around a smaller modulus than that
		 * of its storage type. (2) After a prolonged absence of events, the
		 * mousems timestamp advanced with our local clock may get a tiny bit
		 * ahead of the newly arrived Mouse.msec set from the client's clock.
		 */
		newms = m->msec;
		if(newms < s->mousems)
			s->lastmovems = newms - (s->mousems - s->lastmovems) - (now - s->mousewallms);
		s->mousems = newms;
	}else{
		/* A virtual repeat of the last event. */
		s->mousems += now - s->mousewallms;
		m->msec = s->mousems;
	}
	s->mousewallms = now;
}

int
dragscrolldelta(Dragscroll *s, int velocity, int pacems)
{
	int dur, durmulvel, delta;

	dur = (int)(s->mousems - s->lastmovems) - s->pacediffms;
	if(dur < 0)
		dur = 0;
	durmulvel = dur * velocity;
	delta = divround(durmulvel, pacems);
	if(velocity==0 || delta!=0){
		s->pacediffms = divround(delta*pacems-durmulvel, velocity);
		s->lastmovems = s->mousems;
	}
	return delta;
}
