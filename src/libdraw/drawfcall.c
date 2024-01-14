#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <cursor.h>
#include <drawfcall.h>

#define PUT1(p, x)	((p)[0] = (uchar)(x), 1)
#define GET1(p, x)	((x) = (p)[0], 1)

#define PUT2(p, x) ( \
	(p)[0] = ((u16int)(x)>>8) & 0xFF, \
	(p)[1] = (u16int)(x) & 0xFF, \
	2)
#define GET2(p, x) ( \
	(x) = ((u16int)(p)[0]<<8) | (u16int)(p)[1], \
	2)

#define PUT4(p, x) ( \
	(p)[0] = ((u32int)(x)>>24) & 0xFF, \
	(p)[1] = ((u32int)(x)>>16) & 0xFF, \
	(p)[2] = ((u32int)(x)>>8) & 0xFF, \
	(p)[3] = (u32int)(x) & 0xFF, \
	4)
#define GET4(p, x) ( \
	(x) = ((u32int)(p)[0]<<24) | ((u32int)(p)[1]<<16) | \
		((u32int)(p)[2]<<8) | (u32int)(p)[3], \
	4)

#define PUTN(p, buf, n)	(memmove((p), (buf), (n)), (n))
#define GETN(p, buf, n)	(memmove((buf), (p), (n)), (n))

#define PUTSTR(p, s)	(putstring((p), (s)))
#define GETSTR(p, s)	(getstring((p), &(s)))

static int
_stringsize(char *s)
{
	if(s == nil)
		s = "";
	return 4+strlen(s);
}

static int
putstring(uchar *p, char *s)
{
	int n;

	if(s == nil)
		s = "";
	n = strlen(s);
	PUT4(p, n);
	memmove(p+4, s, n);
	return n+4;
}

static int
getstring(uchar *p, char **s)
{
	int n;

	GET4(p, n);
	memmove(p, p+4, n);
	*s = (char*)p;
	p[n] = 0;
	return n+4;
}

uint
sizeW2M(Wsysmsg *m)
{
	switch(m->type){
	default:
		return 0;
	case Trdmouse:
	case Rbouncemouse:
	case Rmoveto:
	case Rcursor:
	case Rcursor2:
	case Trdkbd:
	case Trdkbd4:
	case Rlabel:
	case Rctxt:
	case Rinit:
	case Trdsnarf:
	case Rwrsnarf:
	case Ttop:
	case Rtop:
	case Rresize:
		return 4+1+1;
	case Rrdmouse:
		return 4+1+1+4+4+2+4+4+1;
	case Tbouncemouse:
		return 4+1+1+4+4+2+4;
	case Tmoveto:
		return 4+1+1+4+4;
	case Tcursor:
		return 4+1+1+4+4+2*16+2*16+1;
	case Tcursor2:
		return 4+1+1+4+4+2*16+2*16+4+4+4*32+4*32+1;
	case Rerror:
		return 4+1+1+_stringsize(m->error);
	case Rrdkbd:
		return 4+1+1+2;
	case Rrdkbd4:
		return 4+1+1+4;
	case Tlabel:
		return 4+1+1+_stringsize(m->label);
	case Tctxt:
		return 4+1+1
			+_stringsize(m->id);
	case Tinit:
		return 4+1+1
			+_stringsize(m->winsize)
			+_stringsize(m->label);
	case Rrdsnarf:
	case Twrsnarf:
		return 4+1+1+_stringsize(m->snarf);
	case Rrddraw:
	case Twrdraw:
		return 4+1+1+4+m->count;
	case Trddraw:
	case Rwrdraw:
		return 4+1+1+4;
	case Tresize:
		return 4+1+1+4*4;
	}
}

uint
convW2M(Wsysmsg *m, uchar *buf, uint nbuf)
{
	uchar *p;
	int n;

	n = sizeW2M(m);
	if(n<6 || n>nbuf)
		return 0;
	p = buf;
	p += PUT4(p, n);
	p += PUT1(p, m->tag);
	p += PUT1(p, m->type);

	switch(m->type){
	default:
		return 0;
	case Trdmouse:
	case Rbouncemouse:
	case Rmoveto:
	case Rcursor:
	case Rcursor2:
	case Trdkbd:
	case Trdkbd4:
	case Rlabel:
	case Rctxt:
	case Rinit:
	case Trdsnarf:
	case Rwrsnarf:
	case Ttop:
	case Rtop:
	case Rresize:
		break;
	case Rerror:
		p += PUTSTR(p, m->error);
		break;
	case Rrdmouse:
		p += PUT4(p, m->mouse.xy.x);
		p += PUT4(p, m->mouse.xy.y);
		p += PUT2(p, m->mouse.buttons);
		p += PUT4(p, m->mouse.scroll);
		p += PUT4(p, m->mouse.msec);
		p += PUT1(p, m->resized);
		break;
	case Tbouncemouse:
		p += PUT4(p, m->mouse.xy.x);
		p += PUT4(p, m->mouse.xy.y);
		p += PUT2(p, m->mouse.buttons);
		p += PUT4(p, m->mouse.scroll);
		break;
	case Tmoveto:
		p += PUT4(p, m->mouse.xy.x);
		p += PUT4(p, m->mouse.xy.y);
		break;
	case Tcursor:
		p += PUT4(p, m->cursor.offset.x);
		p += PUT4(p, m->cursor.offset.y);
		p += PUTN(p, m->cursor.clr, sizeof(m->cursor.clr));
		p += PUTN(p, m->cursor.set, sizeof(m->cursor.set));
		p += PUT1(p, m->arrowcursor);
		break;
	case Tcursor2:
		p += PUT4(p, m->cursor.offset.x);
		p += PUT4(p, m->cursor.offset.y);
		p += PUTN(p, m->cursor.clr, sizeof(m->cursor.clr));
		p += PUTN(p, m->cursor.set, sizeof(m->cursor.set));
		p += PUT4(p, m->cursor2.offset.x);
		p += PUT4(p, m->cursor2.offset.y);
		p += PUTN(p, m->cursor2.clr, sizeof(m->cursor2.clr));
		p += PUTN(p, m->cursor2.set, sizeof(m->cursor2.set));
		p += PUT1(p, m->arrowcursor);
		break;
	case Rrdkbd:
		p += PUT2(p, m->rune);
		break;
	case Rrdkbd4:
		p += PUT4(p, m->rune);
		break;
	case Tlabel:
		p += PUTSTR(p, m->label);
		break;
	case Tctxt:
		p += PUTSTR(p, m->id);
		break;
	case Tinit:
		p += PUTSTR(p, m->winsize);
		p += PUTSTR(p, m->label);
		break;
	case Rrdsnarf:
	case Twrsnarf:
		p += PUTSTR(p, m->snarf);
		break;
	case Rrddraw:
	case Twrdraw:
		p += PUT4(p, m->count);
		p += PUTN(p, m->data, m->count);
		break;
	case Trddraw:
	case Rwrdraw:
		p += PUT4(p, m->count);
		break;
	case Tresize:
		p += PUT4(p, m->rect.min.x);
		p += PUT4(p, m->rect.min.y);
		p += PUT4(p, m->rect.max.x);
		p += PUT4(p, m->rect.max.y);
		break;
	}
	if(p-buf != n)
		sysfatal("convW2M: message size mismatch");
	return n;
}

uint
convM2W(uchar *buf, uint nbuf, Wsysmsg *m)
{
	uchar *p;
	int n;

	if(nbuf < 6)
		return 0;
	p = buf;
	p += GET4(p, n);
	if(n<6 || n>nbuf)
		return 0;
	p += GET1(p, m->tag);
	p += GET1(p, m->type);
	switch(m->type){
	default:
		return 0;
	case Trdmouse:
	case Rbouncemouse:
	case Rmoveto:
	case Rcursor:
	case Rcursor2:
	case Trdkbd:
	case Trdkbd4:
	case Rlabel:
	case Rctxt:
	case Rinit:
	case Trdsnarf:
	case Rwrsnarf:
	case Ttop:
	case Rtop:
	case Rresize:
		break;
	case Rerror:
		p += GETSTR(p, m->error);
		break;
	case Rrdmouse:
		p += GET4(p, m->mouse.xy.x);
		p += GET4(p, m->mouse.xy.y);
		p += GET2(p, m->mouse.buttons);
		p += GET4(p, m->mouse.scroll);
		p += GET4(p, m->mouse.msec);
		p += GET1(p, m->resized);
		break;
	case Tbouncemouse:
		p += GET4(p, m->mouse.xy.x);
		p += GET4(p, m->mouse.xy.y);
		p += GET2(p, m->mouse.buttons);
		p += GET4(p, m->mouse.scroll);
		break;
	case Tmoveto:
		p += GET4(p, m->mouse.xy.x);
		p += GET4(p, m->mouse.xy.y);
		break;
	case Tcursor:
		p += GET4(p, m->cursor.offset.x);
		p += GET4(p, m->cursor.offset.y);
		p += GETN(p, m->cursor.clr, sizeof(m->cursor.clr));
		p += GETN(p, m->cursor.set, sizeof(m->cursor.set));
		p += GET1(p, m->arrowcursor);
		break;
	case Tcursor2:
		p += GET4(p, m->cursor.offset.x);
		p += GET4(p, m->cursor.offset.y);
		p += GETN(p, m->cursor.clr, sizeof(m->cursor.clr));
		p += GETN(p, m->cursor.set, sizeof(m->cursor.set));
		p += GET4(p, m->cursor2.offset.x);
		p += GET4(p, m->cursor2.offset.y);
		p += GETN(p, m->cursor2.clr, sizeof(m->cursor2.clr));
		p += GETN(p, m->cursor2.set, sizeof(m->cursor2.set));
		p += GET1(p, m->arrowcursor);
		break;
	case Rrdkbd:
		p += GET2(p, m->rune);
		break;
	case Rrdkbd4:
		p += GET4(p, m->rune);
		break;
	case Tlabel:
		p += GETSTR(p, m->label);
		break;
	case Tctxt:
		p += GETSTR(p, m->id);
		break;
	case Tinit:
		p += GETSTR(p, m->winsize);
		p += GETSTR(p, m->label);
		break;
	case Rrdsnarf:
	case Twrsnarf:
		p += GETSTR(p, m->snarf);
		break;
	case Rrddraw:
	case Twrdraw:
		p += GET4(p, m->count);
		m->data = p;
		p += m->count;
		break;
	case Trddraw:
	case Rwrdraw:
		p += GET4(p, m->count);
		break;
	case Tresize:
		p += GET4(p, m->rect.min.x);
		p += GET4(p, m->rect.min.y);
		p += GET4(p, m->rect.max.x);
		p += GET4(p, m->rect.max.y);
		break;
	}
	if(p-buf != n)
		sysfatal("convM2W: message size mismatch");
	return n;
}

int
readwsysmsg(int fd, uchar *buf, uint nbuf)
{
	int n;

	if(nbuf < 6)
		return -1;
	if(readn(fd, buf, 4) != 4)
		return -1;
	GET4(buf, n);
	if(n<6 || n>nbuf)
		return -1;
	if(readn(fd, buf+4, n-4) != n-4)
		return -1;
	return n;
}

int
drawfcallfmt(Fmt *fmt)
{
	Wsysmsg *m;

	m = va_arg(fmt->args, Wsysmsg*);
	fmtprint(fmt, "tag=%d ", m->tag);
	switch(m->type){
	default:
		return fmtprint(fmt, "unknown msg %d", m->type);
	case Rerror:
		return fmtprint(fmt, "Rerror error='%s'", m->error);
	case Trdmouse:
		return fmtprint(fmt, "Trdmouse");
	case Rrdmouse:
		return fmtprint(fmt, "Rrdmouse x=%d y=%d buttons=%d scroll=%d msec=%d resized=%d",
			m->mouse.xy.x, m->mouse.xy.y, m->mouse.buttons, m->mouse.scroll,
			m->mouse.msec, m->resized);
	case Tbouncemouse:
		return fmtprint(fmt, "Tbouncemouse x=%d y=%d buttons=%d scroll=%d",
			m->mouse.xy.x, m->mouse.xy.y, m->mouse.buttons, m->mouse.scroll);
	case Rbouncemouse:
		return fmtprint(fmt, "Rbouncemouse");
	case Tmoveto:
		return fmtprint(fmt, "Tmoveto x=%d y=%d", m->mouse.xy.x, m->mouse.xy.y);
	case Rmoveto:
		return fmtprint(fmt, "Rmoveto");
	case Tcursor:
		return fmtprint(fmt, "Tcursor arrow=%d", m->arrowcursor);
	case Tcursor2:
		return fmtprint(fmt, "Tcursor2 arrow=%d", m->arrowcursor);
	case Rcursor:
		return fmtprint(fmt, "Rcursor");
	case Rcursor2:
		return fmtprint(fmt, "Rcursor2");
	case Trdkbd:
		return fmtprint(fmt, "Trdkbd");
	case Rrdkbd:
		return fmtprint(fmt, "Rrdkbd rune=%C", m->rune);
	case Trdkbd4:
		return fmtprint(fmt, "Trdkbd4");
	case Rrdkbd4:
		return fmtprint(fmt, "Rrdkbd4 rune=%C", m->rune);
	case Tlabel:
		return fmtprint(fmt, "Tlabel label='%s'", m->label);
	case Rlabel:
		return fmtprint(fmt, "Rlabel");
	case Tctxt:
		return fmtprint(fmt, "Tctxt id='%s'", m->id);
	case Rctxt:
		return fmtprint(fmt, "Rctxt");
	case Tinit:
		return fmtprint(fmt, "Tinit label='%s' winsize='%s'", m->label, m->winsize);
	case Rinit:
		return fmtprint(fmt, "Rinit");
	case Trdsnarf:
		return fmtprint(fmt, "Trdsnarf");
	case Rrdsnarf:
		return fmtprint(fmt, "Rrdsnarf snarf='%s'", m->snarf);
	case Twrsnarf:
		return fmtprint(fmt, "Twrsnarf snarf='%s'", m->snarf);
	case Rwrsnarf:
		return fmtprint(fmt, "Rwrsnarf");
	case Trddraw:
		return fmtprint(fmt, "Trddraw %d", m->count);
	case Rrddraw:
		return fmtprint(fmt, "Rrddraw %d %.*H", m->count, m->count, m->data);
	case Twrdraw:
		return fmtprint(fmt, "Twrdraw %d %.*H", m->count, m->count, m->data);
	case Rwrdraw:
		return fmtprint(fmt, "Rwrdraw %d", m->count);
	case Ttop:
		return fmtprint(fmt, "Ttop");
	case Rtop:
		return fmtprint(fmt, "Rtop");
	case Tresize:
		return fmtprint(fmt, "Tresize %R", m->rect);
	case Rresize:
		return fmtprint(fmt, "Rresize");
	}
}
