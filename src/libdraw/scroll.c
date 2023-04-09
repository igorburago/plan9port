#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>

int
mousescrollsize(int maxlines)
{
	static int lines, pcnt;
	char *mss;

	if(lines == 0 && pcnt == 0){
		mss = getenv("mousescrollsize");
		if(mss){
			if(strchr(mss, '%') != nil)
				pcnt = atof(mss);
			else
				lines = atoi(mss);
			free(mss);
		}
		if(lines == 0 && pcnt == 0)
			lines = 1;
		if(pcnt>=100)
			pcnt = 100;
	}

	if(lines)
		return lines;
	return pcnt * maxlines/100.0;
}

static int
sign(int x)
{
	return (x > 0) - (x < 0);
}

int
mouselinesnapscroll(Linesnapscroll *s, Mouse *m, int lineheight)
{
	enum { Inertiacarryovermsec = 50 };
	int delta;

	switch(m->buttons){
	case Mscrollmotionstart:
		s->inmotion = 1;
		s->inertial = 0;
		s->pendingdist = 0;
		s->motionhaltup = 0;
		s->motionhaltdown = 0;
		return 0;

	case Mscrollinertiastart:
		s->inmotion = 1;
		s->inertial = 1;
		s->pendingdist = 0;
		/*
		 * If an inertial motion starts shortly after another (typically,
		 * non-inertial) motion ends, consider the former to be a seamless
		 * continuation of the latter and do not reset scroll cancellations.
		 * This is the likely case that we expect to hold almost always, at
		 * least on macOS (where inertial motions are never standalone).
		 */
		if(m->msec-s->motionstopmsec >= Inertiacarryovermsec){
			s->motionhaltup = 0;
			s->motionhaltdown = 0;
		}
		return 0;

	case Mscrollmotionstop:
	case Mscrollinertiastop:
		s->inmotion = 0;
		s->inertial = 0;
		s->pendingdist = 0;
		s->motionstopmsec = m->msec;
		return 0;

	case Mpixelscroll:
		if(s->inmotion){
			if((s->motionhaltup && m->scroll<0)
			|| (s->motionhaltdown && m->scroll>0)){
				s->pendingdist = 0;
				return 0;
			}
			if(sign(s->pendingdist)*sign(m->scroll) < 0)
				s->pendingdist = 0;
			delta = m->scroll + s->pendingdist;
			s->pendingdist = delta % lineheight;
		}else{
			delta = m->scroll;
			s->pendingdist = 0;
			s->motionstopmsec = 0;
		}
		return delta / lineheight;

	case Mlinescroll:
		return m->scroll;

	default:
		return 0;
	}
}
