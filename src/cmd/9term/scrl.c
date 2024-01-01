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

typedef struct Winfrselect	Winfrselect;

/*
 * Parameters controlling the frequency and magnitude of scrolling
 * updates for mouse-dragging during (a) buttons 1 and 3 scrollbar
 * scrubbing (see scrl.c:/^wscrclick/) and (b) button 1 text
 * selection (see scrl.c:/^wframescroll/).
 *
 * By moving the mouse vertically, the user controls the scrolling
 * speed in the number of display lines to skip over per second, to
 * the constant factor of 1000/Dragscrollpacemsec. For example, when
 * Dragscrollpacemsec is 250, the scrolling speed at any given moment
 * is 4*n (a) or 4*n+1 (b) lines per second, where n is the number of
 * full lines that fit between the mouse location and the top (a) or
 * the nearest (b) edge of the text frame.
 *
 * The viewport offset is updated each time a new mouse event arrives
 * or at least every Dragscrollsleepmsec, with an increment equal to
 * the current velocity (obtained from the Y-coordinate of the mouse)
 * scaled by the ratio of the time elapsed since the viewport was last
 * moved and Dragscrollpacemsec. This way, scrolling stays unaffected
 * by the changes in the mouse event arrival rate while remaining
 * responsive to the user input.
 */
enum
{
	Dragscrollpacemsec	= 250,
	Dragscrollsleepmsec	= 30
};

struct Winfrselect
{
	Window		*win;
	uint		startq;
	Dragscroll	scroll;
};

static void
dragscrollpollmouse(Dragscroll *s, Mousectl *mc, int maxwaitms)
{
	dragscrollresume(s, &mc->m, waitformouse(mc, maxwaitms));
}

static Rectangle
wscrpuckrect(Window *w)
{
	uint tot, p0, p1;
	Rectangle r, q;
	int m;

	r = w->scrollr;
	r.max.x -= wscale(w, Scrollborder);
	if(Dy(r) <= 0)
		return r;

	tot = w->nr;
	if(tot == 0)
		return r;
	p0 = w->org;
	p1 = w->org + w->f.nchars;
	if(tot > 1024*1024){
		tot >>= 10;
		p0 >>= 10;
		p1 >>= 10;
	}

	q = r;
	if(p0 > 0)
		q.min.y += Dy(r)*p0/tot;
	if(p1 < tot)
		q.max.y -= Dy(r)*(tot-p1)/tot;

	m = wscale(w, Scrollpuckmin);
	if(q.min.y+m > q.max.y){
		if(q.min.y+m <= r.max.y)
			q.max.y = q.min.y+m;
		else{
			q.max.y = r.max.y;
			q.min.y = q.max.y-m;
			if(q.min.y < r.min.y)
				q.min.y = r.min.y;
		}
	}
	return q;
}

static void
swapint(int *a, int *b)
{
	int t;

	t = *a;
	*a = *b;
	*b = t;
}

void
wscrdraw(Window *w)
{
	Rectangle old, new;

	if(w->i == nil)
		error("wscrdraw w->i == nil");
	old = rectaddpt(w->scrpuckr0, w->scrollr.min);
	new = wscrpuckrect(w);
	if(new.min.y==old.min.y && new.max.y==old.max.y)
		return;
	w->scrpuckr0 = rectsubpt(new, w->scrollr.min);

	/* Paint only the difference between the pucks to minimize flicker. */
	if(Dy(old) <= 0)	/* first draw after a resize */
		old = w->scrollr;
	else if(old.min.y<=new.min.y && new.min.y<old.max.y)
		swapint(&new.min.y, &old.max.y);
	else if(new.min.y<=old.min.y && old.min.y<new.max.y)
		swapint(&old.min.y, &new.max.y);
	/* If an interval is inverted, so is its puck/background status. */
	if(Dy(old) != 0)
		draw(w->i, canonrect(old), w->f.cols[Dy(old)>0 ? BORD : BACK], nil, ZP);
	if(Dy(new) != 0)
		draw(w->i, canonrect(new), w->f.cols[Dy(new)>0 ? BACK : BORD], nil, ZP);
}

static uint
wfrcharofline(Window *w, int frline)
{
	Point p;

	p = w->f.r.min;
	p.y += frline * w->f.font->height;
	return w->org + frcharofpt(&w->f, p);
}

static int
wclampmouse(Window *w, int x, int ymin, int ymax)
{
	Point p;

	p.x = x;
	p.y = w->mc.m.xy.y;
	if(p.y >= ymax)
		p.y = ymax-1;
	if(p.y < ymin)
		p.y = ymin;
	if(!eqpt(w->mc.m.xy, p))
		wmovemouse(w, p);
	return p.y;
}

void
wscrclick(Window *w, int but)
{
	enum { Debouncemsec = 2*Dragscrollpacemsec };
	int butmask, totshift;
	Rectangle sr;
	int sx, sh, lh, my, y0, speed, delta, minmove;
	uint tot, oldp0, p0;
	Dragscroll scroll;
	int wait, first;
	uint t0;

	butmask = 1<<(but-1);	/* when called, the button is down */
	sr = w->scrollr;
	sx = sr.min.x + Dx(sr)/2;
	switch(but){
	case 2:
		sh = Dy(sr)-1;
		tot = w->nr, totshift = 0;
		if(tot > 1024*1024)
			tot >>= totshift = 10;
		oldp0 = ~0u;
		do{
			my = wclampmouse(w, sx, sr.min.y, sr.max.y);
			p0 = ((uvlong)tot * (my - sr.min.y) / sh) << totshift;
			if(p0 != oldp0){
				oldp0 = p0;
				p0 = wbacknl(w, p0, 0);
				if(p0 != w->org){
					wsetorigin(w, p0);
					flushimage(display, 1);
				}
			}
			recv(w->mc.c, &w->mc.m);
		}while(w->mc.m.buttons & butmask);
		break;
	case 1:
	case 3:
		/*
		 * The initiating mouse down event takes full effect immediately;
		 * the following events are debounced so that the user has time
		 * to release the mouse button if no further scrolling is needed.
		 * Dragging it half a line's worth vertically cuts the wait short.
		 */
		first = TRUE;
		dragscrollreset(&scroll, &w->mc.m, -Dragscrollpacemsec);
		lh = w->f.font->height;
		minmove = lh/2;
		do{
			my = wclampmouse(w, sx, sr.min.y, sr.max.y);
			speed = (my - sr.min.y) / lh;
			delta = dragscrolldelta(&scroll, speed, Dragscrollpacemsec);
			if(delta != 0){
				if(but == 1)
					p0 = wbacknl(w, w->org, delta);
				else	/* but == 3 */
					p0 = wfrcharofline(w, delta);
				if(p0 != w->org){
					wsetorigin(w, p0);
					flushimage(display, 1);
				}
			}
			if(first){
				/*
				 * If we were to sleep for the debounce duration in one go with no
				 * events arriving, we would oversleep by the few ms it took us to
				 * process the first event, so do it in at least two installments.
				 */
				wait = Debouncemsec/2;
				t0 = scroll.mousewallms;
				y0 = my;
				do{
					dragscrollpollmouse(&scroll, &w->mc, wait);
					my = wclampmouse(w, sx, sr.min.y, sr.max.y);
					wait = Debouncemsec - (int)(scroll.mousewallms - t0);
				}while(wait>0 && (w->mc.m.buttons&butmask) && abs(my-y0)<minmove);
				/*
				 * Until the viewport starts moving, debounce is perceived to be
				 * still going on, so jump-start scrolling by the amount of time
				 * it takes a single line to scroll by at the current speed.
				 */
				speed = (my - sr.min.y) / lh;
				dragscrollreset(&scroll, &w->mc.m, speed>0 ? -Dragscrollpacemsec/speed : 0);
				first = FALSE;
			}else
				dragscrollpollmouse(&scroll, &w->mc, Dragscrollsleepmsec);
		}while(w->mc.m.buttons & butmask);
		break;
	}
	while(w->mc.m.buttons != 0)
		recv(w->mc.c, &w->mc.m);
}

static void
wframescroll(Frame *f, void *state, int velocity, int firstinstreak, int *untick00sel)
{
	Winfrselect *s;
	Window *w;
	int delta;
	uint dragq;

	s = state;
	w = s->win;	/* f==w->f */
	if(velocity == 0){
		dragscrollpollmouse(&s->scroll, &w->mc, Dragscrollsleepmsec);
		return;
	}
	if(firstinstreak)
		dragscrollreset(&s->scroll, &w->mc.m, 0);
	dragq = w->org + (velocity<0 ? f->p0 : f->p1);
	delta = dragscrolldelta(&s->scroll, velocity, Dragscrollpacemsec);
	if(delta != 0)
		wscrollnl(w, delta, FALSE);
	if(dragq < s->startq)
		wsetselect(w, dragq, s->startq);
	else
		wsetselect(w, s->startq, dragq);
	/*
	 * If the dot extends above the origin, do not tick when f->p0==f->p1==0
	 * (while scrolling up) — see ../../libframe/frselect.c:/untick00sel/.
	 */
	*untick00sel = (w->q0 < w->org);
}

void
wframeselect(Window *w, uint startq)
{
	Winfrselect s;

	memset(&s, 0, sizeof(s));
	s.win = w;
	s.startq = startq;
	frselectscroll(&w->f, &w->mc, wframescroll, &s);
}

void
wscrollnl(Window *w, int lines, int scrollpastend)
{
	uint org;
	int n;

	if(lines < 0){
		if(w->org == 0)
			return;
		org = wbacknl(w, w->org, -lines);
		wsetorigin(w, org);
	}else if(lines > 0){
		if(scrollpastend ? w->org==w->nr : !w->f.lastlinefull)
			return;
		org = wfrcharofline(w, min(lines, w->f.maxlines));
		if(lines > w->f.maxlines)
			org = wforwardnl(w, org, lines-w->f.maxlines);
		wsetorigin(w, org);
		if(!scrollpastend && !w->f.lastlinefull){
			n = w->f.maxlines - w->f.nlines;
			if(w->nr>0 && w->r[w->nr-1]=='\n')
				n--;
			if(n > 0){
				org = wbacknl(w, w->org, n);
				wsetorigin(w, org);
			}
		}
	}
}
