#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

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
	ulong p0, p1, q;
	Point mp, pt0, pt1, qt;
	int b, reg, scrollvel, prevvel, untick00sel;

	b = mc->m.buttons;	/* when called, button 1 is down */
	mp = mc->m.xy;

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
			if(mp.y < f->r.min.y){
				scrollvel = -1 - (f->r.min.y-mp.y)/(int)f->font->height;
				(*scroll)(f, state, scrollvel, prevvel>=0, &untick00sel);
				p0 = f->p1;
				p1 = f->p0;
			}else if(mp.y > f->r.max.y){
				scrollvel = +1 + (mp.y-f->r.max.y)/(int)f->font->height;
				(*scroll)(f, state, scrollvel, prevvel<=0, &untick00sel);
				p0 = f->p0;
				p1 = f->p1;
			}else
				scrollvel = 0;
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
		if(scrollvel != 0)
			(*scroll)(f, state, 0, 0, &untick00sel);
		flushimage(f->display, 1);
		if(scrollvel == 0)
			readmouse(mc);
		mp = mc->m.xy;
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
