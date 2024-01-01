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
scrpos(Rectangle r, uint p0, uint p1, uint tot)
{
	Rectangle q;
	int h;

	q = r;
	h = q.max.y-q.min.y;
	if(tot == 0)
		return q;
	if(tot > 1024*1024){
		tot>>=10;
		p0>>=10;
		p1>>=10;
	}
	if(p0 > 0)
		q.min.y += h*p0/tot;
	if(p1 < tot)
		q.max.y -= h*(tot-p1)/tot;
	if(q.max.y < q.min.y+2){
		if(q.min.y+2 <= r.max.y)
			q.max.y = q.min.y+2;
		else
			q.min.y = q.max.y-2;
	}
	return q;
}

static Image *scrtmp;

static void
scrtemps(void)
{
	int h;

	if(scrtmp)
		return;
	h = BIG*Dy(screen->r);
	scrtmp = allocimage(display, Rect(0, 0, 32, h), screen->chan, 0, DWhite);
	if(scrtmp == nil)
		error("scrtemps");
}

void
freescrtemps(void)
{
	freeimage(scrtmp);
	scrtmp = nil;
}

void
wscrdraw(Window *w)
{
	Rectangle r, r1, r2;
	Image *b;

	scrtemps();
	if(w->i == nil)
		error("scrdraw");
	r = w->scrollr;
	b = scrtmp;
	r1 = r;
	r1.min.x = 0;
	r1.max.x = Dx(r);
	r2 = scrpos(r1, w->org, w->org+w->f.nchars, w->nr);
	if(!eqrect(r2, w->lastsr)){
		w->lastsr = r2;
		/* move r1, r2 to (0,0) to avoid clipping */
		r2 = rectsubpt(r2, r1.min);
		r1 = rectsubpt(r1, r1.min);
		draw(b, r1, w->f.cols[BORD], nil, ZP);
		draw(b, r2, w->f.cols[BACK], nil, ZP);
		r2.min.x = r2.max.x-1;
		draw(b, r2, w->f.cols[BORD], nil, ZP);
		draw(w->i, r, b, nil, Pt(0, r1.min.y));
	}
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
wframescroll(Frame *f, void *state, int velocity, int firstinstreak)
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
