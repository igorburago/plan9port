#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

typedef struct Frscrollzones	Frscrollzones;

struct Frscrollzones
{
	/* Set once at the start of selection: */
	int	startline;	/* display line where selection started */

	/* Updated once per Frame.r.{min,max}.y change: */
	int	frminy;		/* current Frame.r.min.y */
	int	frmaxy;		/* current Frame.r.max.y */
	int	topedge;	/* y<=topedge is the top scroll zone */
	int	botedge;	/* y>=botedge is the bottom scroll zone */
	ushort	topeasems;	/* velocity ease-in time for the top zone */
	ushort	boteasems;	/* same for the bottom zone */

	/* Updated on every mouse event: */
	ushort	wentup;		/* mouse has moved at least one line up during selection */
	ushort	wentdown;	/* same for down */
	ushort	intop;		/* mouse was inside the top zone last time */
	ushort	inbot;		/* same for the bottom zone */
	uint	hittopms;	/* Mouse.msec of the last time mouse entered the top zone */
	uint	hitbotms;	/* same for the bottom zone */
};

static int
max(int a, int b)
{
	return a>b ? a : b;
}

static int
easein(int v, uint elapsed, uint period)
{
	return elapsed<period ? v*elapsed/period : v;
}

static void
frscrollinit(Frame *f, Frscrollzones *z, int y)
{
	memset(z, 0, sizeof(*z));
	z->startline = (y - f->r.min.y) / f->font->height;
}

/*
 * We ensure that mouse has a scroll-triggering zone of at least
 * Minzonelines' worth of travel on both top and bottom sides of the
 * frame. When it is too close to a screen edge, we make up for it
 * by reserving the lacking space inside the frame so that scrolling
 * on that side starts a few lines before mouse is about to leave
 * the frame, guaranteeing the user has a minimum of Minzonelines
 * gradations of scroll speed control.
 *
 * When a selection begins inside the in-frame part of a scroll zone,
 * we do not kick off scrolling until the mouse goes over at least one
 * line in the corresponding direction. During selection, every time
 * the mouse hits a scroll zone that intrudes into the frame (even if
 * the cursor is in the out-of-frame part of it at that moment), we
 * ease into the usual drag-dictated speed to help the user adjust.
 *
 * We cannot preset the scroll zones at the start of frselectscroll()
 * because frame dimensions are allowed to change in the application-
 * provided scroll function (e.g., in acme(1), a collapsed multiline
 * tag expands when the selection is dragged below the top line).
 */
static int
frscrollvel(Frame *f, Frscrollzones *z, int y, uint msec)
{
	enum{
		Minzonelines		= 4,	/* both in-frame and out-of-frame ones */
		Easemsecperline		= 200	/* per in-frame zone line */
	};
	int lh, above, below, top, bot, cap, sum, sweep;
	int wasintop, wasinbot;

	lh = f->font->height;
	if(z->frminy!=f->r.min.y || z->frmaxy!=f->r.max.y){
		z->frminy = f->r.min.y;
		z->frmaxy = f->r.max.y;

		above = (z->frminy - f->b->r.min.y) / lh;
		below = (f->b->r.max.y - z->frmaxy) / lh;
		top = max(Minzonelines-above, 0);
		bot = max(Minzonelines-below, 0);
		/* Ensure scroll zones do not cover most of the lines in the frame. */
		sum = top + bot;
		cap = f->maxlines/2;
		if(sum > cap){
			bot = (bot*cap + sum/2) / sum;
			top = cap - bot;
		}

		z->topedge = z->frminy-1 + top*lh;
		z->botedge = z->frmaxy - bot*lh;
		z->topeasems = top * Easemsecperline;
		z->boteasems = bot * Easemsecperline;
	}

	wasintop = z->intop;
	wasinbot = z->inbot;
	z->wentup = (z->wentup || y<z->frminy+lh*z->startline);
	z->wentdown = (z->wentdown || y>=z->frminy+lh*(z->startline+1));
	z->intop = (z->wentup && y<=z->topedge);
	z->inbot = (z->wentdown && y>=z->botedge);
	if(!wasintop && z->intop)
		z->hittopms = msec;
	if(!wasinbot && z->inbot)
		z->hitbotms = msec;

	if(z->intop){
		sweep = (z->topedge - y) / lh;
		return -1 - easein(sweep, msec-z->hittopms, z->topeasems);
	}
	if(z->inbot){
		sweep = (y - z->botedge) / lh;
		return +1 + easein(sweep, msec-z->hitbotms, z->boteasems);
	}
	return 0;
}

static int
region(ulong p0, ulong p1)
{
	return (p0 > p1) - (p0 < p1);
}

/*
 * A note on the necessity of the untick00sel flag in frselectscroll():
 *
 * We have no way of knowing the origin of the frame's viewport and
 * the true extent of the selection outside of it, so by default we
 * accord with frdrawsel() assuming that a p0==p1 selection should
 * always be ticked. However, when the grand selection extends above
 * the frame and p0==p1==0, it is not correct to tick. This typically
 * occurs when the user scroll-selects down until the beginning of the
 * selection is out of frame, and then goes on to scroll-select up.
 *
 * Normally, an application can correct this behavior of frdrawsel()
 * by calling frtick() itself, but here, between calls to (*scroll)(),
 * there is no way for it to do so before the image is flushed and the
 * next mouse event is polled. Furthermore, if the mouse stays inside
 * the frame for a while, the application will not even be notified
 * of the changes in selection since (*scroll)() is not going to be
 * called, and p0==p1==0 might come about in the meantime.
 *
 * Hence the untick00sel flag as a means for the scroll function to
 * indicate whether the default tick should be kept when p0==p1==0
 * happens while the viewport is at the current scroll offset.
 */
void
frselectscroll(Frame *f, Mousectl *mc, Frscrollfn *scroll, void *state)
{
	int b, reg, scrollvel, prevvel, untick00sel;
	Point mp, pt0, pt1, qt;
	ulong p0, p1, q;
	Frscrollzones z;
	uint msec;

	b = mc->m.buttons;	/* when called, button 1 is down */
	mp = mc->m.xy;
	msec = mc->m.msec;
	if(scroll != nil)
		frscrollinit(f, &z, mp.y);

	frdrawsel(f, frptofchar(f, f->p0), f->p0, f->p1, 0);
	p0 = p1 = frcharofpt(f, mp);
	f->p0 = p0;
	f->p1 = p1;
	pt0 = frptofchar(f, p0);
	pt1 = frptofchar(f, p1);
	frdrawsel(f, pt0, p0, p1, 1);

	f->selecting = 1;
	f->modified = 0;
	untick00sel = 0;
	scrollvel = 0;
	reg = 0;
	do{
		if(scroll != nil){
			prevvel = scrollvel;
			scrollvel = frscrollvel(f, &z, mp.y, msec);
			if(scrollvel < 0){
				(*scroll)(f, state, scrollvel, prevvel>=0, &untick00sel);
				p0 = f->p1;
				p1 = f->p0;
			}else if(scrollvel > 0){
				(*scroll)(f, state, scrollvel, prevvel<=0, &untick00sel);
				p0 = f->p0;
				p1 = f->p1;
			}
			if(scrollvel != 0){
				if(reg != region(p1, p0))
					q = p0, p0 = p1, p1 = q;	/* undo the swap that will happen below */
				pt0 = frptofchar(f, p0);
				pt1 = frptofchar(f, p1);
				reg = region(p1, p0);
			}
		}
		q = frcharofpt(f, mp);
		if(p1 != q){
			if(reg != region(q, p0)){	/* crossed starting point; reset */
				if(reg > 0)
					frdrawsel(f, pt0, p0, p1, 0);
				else if(reg < 0)
					frdrawsel(f, pt1, p1, p0, 0);
				p1 = p0;
				pt1 = pt0;
				reg = region(q, p0);
				if(reg == 0)
					frdrawsel(f, pt0, p0, p1, 1);
			}
			qt = frptofchar(f, q);
			if(reg > 0){
				if(q > p1)
					frdrawsel(f, pt1, p1, q, 1);
				else if(q < p1)
					frdrawsel(f, qt, q, p1, 0);
			}else if(reg < 0){
				if(q > p1)
					frdrawsel(f, pt1, p1, q, 0);
				else if(q < p1)
					frdrawsel(f, qt, q, p1, 1);
			}
			p1 = q;
			pt1 = qt;
		}
		if(p0==0 && p1==0 && untick00sel)	/* see the note above */
			frtick(f, pt0, 0);
		f->selecting = 1;
		f->modified = 0;
		if(p0 < p1){
			f->p0 = p0;
			f->p1 = p1;
		}else{
			f->p0 = p1;
			f->p1 = p0;
		}
		if(scrollvel != 0){
			(*scroll)(f, state, 0, 0, &untick00sel);
			flushimage(f->display, 1);
		}else{
			if(f->display != mc->display)	/* readmouse() flushes the display, as well */
				flushimage(f->display, 1);
			readmouse(mc);
		}
		mp = mc->m.xy;
		msec = mc->m.msec;
	}while(mc->m.buttons == b);
	f->selecting = 0;
}

void
frselect(Frame *f, Mousectl *mc)
{
	frselectscroll(f, mc, nil, nil);
}

void
frselectpaint(Frame *f, Point p0, Point p1, Image *col)
{
	int n;
	Point q0, q1;

	q0 = p0;
	q1 = p1;
	q0.y += f->font->height;
	q1.y += f->font->height;
	n = (p1.y-p0.y)/f->font->height;
	if(f->b == nil)
		drawerror(f->display, "frselectpaint b==0");
	if(p0.y == f->r.max.y)
		return;
	if(n == 0)
		draw(f->b, Rpt(p0, q1), col, nil, ZP);
	else{
		if(p0.x >= f->r.max.x)
			p0.x = f->r.max.x-1;
		draw(f->b, Rect(p0.x, p0.y, f->r.max.x, q0.y), col, nil, ZP);
		if(n > 1)
			draw(f->b, Rect(f->r.min.x, q0.y, f->r.max.x, p1.y), col, nil, ZP);
		draw(f->b, Rect(f->r.min.x, p1.y, q1.x, q1.y), col, nil, ZP);
	}
}
