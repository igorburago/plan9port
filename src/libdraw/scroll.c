#include <u.h>
#include <libc.h>
#include <limits.h>
#include <draw.h>
#include <mouse.h>

static struct{
	double	scale;
} pixelscroll;
static struct{
	double	scale;
	int	viewbased;
	int	minlines;
} linescroll;

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

static double
roundhalfeven(double x)
{
	return nearbyint(x);
}

static int
sign(int x)
{
	return (x > 0) - (x < 0);
}

static int
parselong(char *s, long *pv)
{
	char *t;
	long v;

	v = strtol(s, &t, 10);
	if(t==s || v==LONG_MIN || v==LONG_MAX)
		return -1;

	while(*t==' ' || *t=='\t')
		t++;
	if(*t != 0)
		return -1;

	*pv = v;
	return 0;
}

static int
parsedouble(char *s, double *pv, char *suffix, int *hassuffix)
{
	char *t;
	double v;
	int n;

	v = strtod(s, &t);
	if(t==s || fabs(v)==HUGE_VAL || isnan(v))
		return -1;

	if(suffix != nil){
		n = strlen(suffix);
		if(strncmp(t, suffix, n) == 0){
			*hassuffix = 1;
			t += n;
		}else
			*hassuffix = 0;
	}

	while(*t==' ' || *t=='\t')
		t++;
	if(*t != 0)
		return -1;

	*pv = v;
	return 0;
}

void
initmousescrollscaling(void)
{
	double scale;
	int viewbased;
	long minlines;
	char *var, *vhsuffix;

	var = getenv("mousepixelscrollscale");
	if(var==nil || parsedouble(var, &scale, nil, nil)<0 || scale<=0)
		scale = 1.0;
	free(var);

	pixelscroll.scale = scale;

	var = getenv("mouselinescrollscale");
	if(var != nil)
		vhsuffix = "vh";	/* as in CSS: percent of viewport's height */
	else{
		/* Backward compatibility. */
		var = getenv("mousescrollsize");
		vhsuffix = "%";
	}
	if(var==nil || parsedouble(var, &scale, vhsuffix, &viewbased)<0 || scale<=0){
		scale = 1.0;
		viewbased = 0;
	}else if(viewbased)
		scale *= 0.01;
	free(var);

	var = getenv("mouselinescrollmin");
	if(var==nil || parselong(var, &minlines)<0 || minlines<0)
		minlines = 1;
	free(var);

	linescroll.scale = scale;
	linescroll.viewbased = viewbased;
	linescroll.minlines = minlines;
}

int
mousepixelscrollscaled(int delta)
{
	return (int)roundhalfeven(delta * pixelscroll.scale);
}

int
mouselinescrollscaled(int delta, int viewlines)
{
	double s;
	int d, min;

	s = linescroll.scale;
	if(linescroll.viewbased)
		s *= viewlines;
	d = (int)roundhalfeven(delta * s);

	min = linescroll.minlines;
	if(-min<d && d<min)
		d = sign(delta) * min;
	return d;
}

int
mouselinesnapscroll(Linesnapscroll *s, Mouse *m, int lineheight, int viewlines)
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
			delta = mousepixelscrollscaled(m->scroll + s->pendingdist);
			s->pendingdist = delta % lineheight;
		}else{
			delta = mousepixelscrollscaled(m->scroll);
			s->pendingdist = 0;
			s->motionstopmsec = 0;
		}
		return delta / lineheight;

	case Mlinescroll:
		return mouselinescrollscaled(m->scroll, viewlines);

	default:
		return 0;
	}
}
