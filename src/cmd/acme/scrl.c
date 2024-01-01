#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"

typedef struct Textfrselect	Textfrselect;

/*
 * Parameters controlling the frequency and magnitude of scrolling
 * updates for mouse-dragging during (a) buttons 1 and 3 scrollbar
 * scrubbing (see scrl.c:/^textscrclick/) and (b) button 1 text
 * selection (see scrl.c:/^textframescroll/).
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

struct Textfrselect
{
	Text		*text;
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

void
scrlresize(void)
{
	freeimage(scrtmp);
	scrtmp = allocimage(display, Rect(0, 0, 32, screen->r.max.y), screen->chan, 0, DNofill);
	if(scrtmp == nil)
		error("scroll alloc");
}

void
textscrdraw(Text *t)
{
	Rectangle r, r1, r2;
	Image *b;

	if(t->w==nil || t!=&t->w->body)
		return;
	if(scrtmp == nil)
		scrlresize();
	r = t->scrollr;
	b = scrtmp;
	r1 = r;
	r1.min.x = 0;
	r1.max.x = Dx(r);
	r2 = scrpos(r1, t->org, t->org+t->fr.nchars, t->file->b.nc);
	if(!eqrect(r2, t->lastsr)){
		t->lastsr = r2;
		draw(b, r1, t->fr.cols[BORD], nil, ZP);
		draw(b, r2, t->fr.cols[BACK], nil, ZP);
		r2.min.x = r2.max.x-1;
		draw(b, r2, t->fr.cols[BORD], nil, ZP);
		draw(t->fr.b, r, b, nil, Pt(0, r1.min.y));
/*flushimage(display, 1); // BUG? */
	}
}

static uint
textfrcharofline(Text *t, int frline)
{
	Point p;

	p = t->fr.r.min;
	p.y += frline * t->fr.font->height;
	return t->org + frcharofpt(&t->fr, p);
}

static int
clampmouse(int x, int ymin, int ymax)
{
	Point p;

	p.x = x;
	p.y = mouse->xy.y;
	if(p.y >= ymax)
		p.y = ymax-1;
	if(p.y < ymin)
		p.y = ymin;
	if(!eqpt(mouse->xy, p))
		moveto(mousectl, p);
	return p.y;
}

void
textscrclick(Text *t, int but)
{
	enum { Debouncemsec = 2*Dragscrollpacemsec };
	int butmask;
	Rectangle sr;
	int sx, sh, lh, my, y0, speed, delta, minmove;
	uint tot, oldp0, p0;
	Dragscroll scroll;
	int wait, first;
	uint t0;

	butmask = 1<<(but-1);	/* when called, the button is down */
	sr = t->scrollr;
	sx = sr.min.x + Dx(sr)/2;
	switch(but){
	case 2:
		sh = Dy(sr)-1;
		tot = t->file->b.nc;
		oldp0 = ~0u;
		do{
			my = clampmouse(sx, sr.min.y, sr.max.y);
			p0 = (uvlong)tot * (my - sr.min.y) / sh;
			if(p0 != oldp0){
				oldp0 = p0;
				p0 = textbacknl(t, p0, 0);
				if(p0 != t->org){
					textsetorigin(t, p0);
					flushimage(display, 1);
				}
			}
			recv(mousectl->c, &mousectl->m);
		}while(mouse->buttons & butmask);
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
		dragscrollreset(&scroll, mouse, -Dragscrollpacemsec);
		lh = t->fr.font->height;
		minmove = lh/2;
		do{
			my = clampmouse(sx, sr.min.y, sr.max.y);
			speed = (my - sr.min.y) / lh;
			delta = dragscrolldelta(&scroll, speed, Dragscrollpacemsec);
			if(delta != 0){
				if(but == 1)
					p0 = textbacknl(t, t->org, delta);
				else	/* but == 3 */
					p0 = textfrcharofline(t, delta);
				if(p0 != t->org){
					textsetorigin(t, p0);
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
					dragscrollpollmouse(&scroll, mousectl, wait);
					my = clampmouse(sx, sr.min.y, sr.max.y);
					wait = Debouncemsec - (int)(scroll.mousewallms - t0);
				}while(wait>0 && (mouse->buttons&butmask) && abs(my-y0)<minmove);
				/*
				 * Until the viewport starts moving, debounce is perceived to be
				 * still going on, so jump-start scrolling by the amount of time
				 * it takes a single line to scroll by at the current speed.
				 */
				speed = (my - sr.min.y) / lh;
				dragscrollreset(&scroll, mouse, speed>0 ? -Dragscrollpacemsec/speed : 0);
				first = FALSE;
			}else
				dragscrollpollmouse(&scroll, mousectl, Dragscrollsleepmsec);
		}while(mouse->buttons & butmask);
		break;
	}
	while(mouse->buttons != 0)
		recv(mousectl->c, &mousectl->m);
}

static void
textframescroll(Frame *f, void *state, int velocity, int firstinstreak)
{
	Textfrselect *s;
	Text *t;
	int delta;
	uint dragq;

	s = state;
	t = s->text;	/* f==t->fr */
	if(velocity == 0){
		dragscrollpollmouse(&s->scroll, mousectl, Dragscrollsleepmsec);
		return;
	}
	if(firstinstreak)
		dragscrollreset(&s->scroll, mouse, 0);
	if(velocity>0 && t->what==Tag && !t->w->tagexpand){
		textsetselect(t, t->org+f->p0, t->org+f->p1);
		t->w->tagexpand = TRUE;
		t->w->tagsafe = FALSE;
		winresize(t->w, t->w->r, TRUE, TRUE);
		return;
	}
	dragq = t->org + (velocity<0 ? f->p0 : f->p1);
	delta = dragscrolldelta(&s->scroll, velocity, Dragscrollpacemsec);
	if(delta != 0)
		textscrollnl(t, delta, FALSE);
	if(dragq < s->startq)
		textsetselect(t, dragq, s->startq);
	else
		textsetselect(t, s->startq, dragq);
}

void
textframeselect(Text *t, uint startq)
{
	Textfrselect s;

	memset(&s, 0, sizeof(s));
	s.text = t;
	s.startq = startq;
	frselectscroll(&t->fr, mousectl, textframescroll, &s);
}

/*
 * Enforce the do-not-scroll-past-end constraint when it may be
 * violated due to content modification or a change of origin.
 * The text frame must be in a valid state for its current origin.
 */
void
textscrollupifpastend(Text *t)
{
	int n;

	if(t->fr.lastlinefull)
		return;
	n = t->fr.maxlines - t->fr.nlines;
	if(textendswithnl(t))
		n--;
	if(n > 0)
		textsetorigin(t, textbacknl(t, t->org, n));
}

void
textscrollnl(Text *t, int lines, int scrollpastend)
{
	uint org;

	if(lines < 0){
		if(t->org == 0)
			return;
		org = textbacknl(t, t->org, -lines);
		textsetorigin(t, org);
	}else if(lines > 0){
		if(scrollpastend ? t->org==t->file->b.nc : !t->fr.lastlinefull)
			return;
		org = textfrcharofline(t, min(lines, t->fr.maxlines));
		if(lines > t->fr.maxlines)
			org = textforwardnl(t, org, lines-t->fr.maxlines);
		textsetorigin(t, org);
		if(!scrollpastend)
			textscrollupifpastend(t);
	}
}
