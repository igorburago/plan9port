/*
 * Window system protocol server.
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include <drawfcall.h>
#include "devdraw.h"

static void runmsg(Client*, Wsysmsg*);
static void replymsg(Client*, Wsysmsg*);
static void matchkbd(Client*);
static void matchmouse(Client*);
static void serveproc(void*);
static void listenproc(void*);
Client *client0;

int trace = 0;
static char *srvname;
static int afd;
static char adir[40];

static void
usage(void)
{
	fprint(2, "usage: devdraw (don't run directly)\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *p;

	ARGBEGIN{
	case 'D':		/* for good ps -a listings */
		break;
	case 'f':		/* fall through for backward compatibility */
	case 'g':
	case 'b':
		break;
	case 's':
		/* TODO: Update usage, man page. */
		srvname = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	memimageinit();
	fmtinstall('H', encodefmt);
	if((p = getenv("DEVDRAWTRACE")) != nil)
		trace = atoi(p);

	if(srvname == nil){
		client0 = mallocz(sizeof(Client), 1);
		if(client0 == nil)
			sysfatal("initdraw: allocating client0: out of memory");
		client0->displaydpi = 100;
		client0->rfd = 3;
		client0->wfd = 4;

		/*
		 * Move the protocol off stdin/stdout so that
		 * any inadvertent prints don't screw things up.
		 */
		dup(0,3);
		dup(1,4);
		close(0);
		close(1);
		open("/dev/null", OREAD);
		open("/dev/null", OWRITE);
	}

	fmtinstall('W', drawfcallfmt);
	gfx_main();
}

void
gfx_started(void)
{
	char *ns, *addr;

	if(srvname == nil){
		/* Legacy mode: serving single client on pipes. */
		proccreate(serveproc, client0, 0);
		return;
	}

	/* Server mode. */
	if((ns = getns()) == nil)
		sysfatal("out of memory");

	addr = smprint("unix!%s/%s", ns, srvname);
	free(ns);
	if(addr == nil)
		sysfatal("out of memory");

	if((afd = announce(addr, adir)) < 0)
		sysfatal("announce %s: %r", addr);

	proccreate(listenproc, nil, 0);
}

static void
listenproc(void *v)
{
	Client *c;
	int fd;
	char dir[40];

	USED(v);

	for(;;){
		fd = listen(adir, dir);
		if(fd < 0)
			sysfatal("listen: %r");
		c = mallocz(sizeof(Client), 1);
		if(c == nil)
			sysfatal("listenproc: allocating client: out of memory");
		c->displaydpi = 100;
		c->rfd = fd;
		c->wfd = fd;
		proccreate(serveproc, c, 0);
	}
}

static void
serveproc(void *v)
{
	Client *c;
	uchar lenbuf[4], *buf;
	int nbuf, n;
	Wsysmsg m;

	c = v;
	buf = nil;
	nbuf = 0;
	while((n = read(c->rfd, lenbuf, 4)) == 4){
		n = WMSGLEN4(lenbuf);
		if(n > nbuf){
			free(buf);
			buf = malloc(n);
			if(buf == nil)
				sysfatal("out of memory");
			nbuf = n;
		}
		memmove(buf, lenbuf, 4);
		if(readn(c->rfd, buf+4, n-4) != n-4){
			fprint(2, "serveproc: eof during message\n");
			break;
		}

		/* pick off messages one by one */
		if(convM2W(buf, n, &m) <= 0){
			fprint(2, "serveproc: cannot convert message\n");
			break;
		}
		if(trace) fprint(2, "%lld [%d] <- %W\n", nsec()/1000000, threadid(), &m);
		runmsg(c, &m);
	}

	if(c == client0){
		rpc_shutdown();
		threadexitsall(nil);
	}
}

static void
replyerror(Client *c, Wsysmsg *m)
{
	char err[256];

	rerrstr(err, sizeof err);
	m->type = Rerror;
	m->error = err;
	replymsg(c, m);
}

/*
 * Handle a single wsysmsg.
 * Might queue for later (kbd, mouse read)
 */
static void
runmsg(Client *c, Wsysmsg *m)
{
	static uchar buf[65536];
	int n;
	Memimage *i;

	switch(m->type){
	case Tctxt:
		c->wsysid = strdup(m->id);
		replymsg(c, m);
		break;

	case Tinit:
		i = rpc_attach(c, m->label, m->winsize);
		if(i == nil){
			replyerror(c, m);
			break;
		}
		draw_initdisplaymemimage(c, i);
		replymsg(c, m);
		break;

	case Trdmouse:
		qlock(&c->eventlk);
		if((c->mousetags.wi+1)%nelem(c->mousetags.t) == c->mousetags.ri){
			qunlock(&c->eventlk);
			werrstr("too many queued mouse reads");
			replyerror(c, m);
			break;
		}
		c->mousetags.t[c->mousetags.wi++] = m->tag;
		if(c->mousetags.wi == nelem(c->mousetags.t))
			c->mousetags.wi = 0;
		c->mouse.stall = 0;
		matchmouse(c);
		qunlock(&c->eventlk);
		break;

	case Trdkbd:
	case Trdkbd4:
		qlock(&c->eventlk);
		if((c->kbdtags.wi+1)%nelem(c->kbdtags.t) == c->kbdtags.ri){
			qunlock(&c->eventlk);
			werrstr("too many queued keyboard reads");
			replyerror(c, m);
			break;
		}
		c->kbdtags.t[c->kbdtags.wi++] = (m->tag<<1) | (m->type==Trdkbd4);
		if(c->kbdtags.wi == nelem(c->kbdtags.t))
			c->kbdtags.wi = 0;
		c->kbd.stall = 0;
		matchkbd(c);
		qunlock(&c->eventlk);
		break;

	case Tmoveto:
		c->impl->rpc_setmouse(c, m->mouse.xy);
		replymsg(c, m);
		break;

	case Tcursor:
		if(m->arrowcursor)
			c->impl->rpc_setcursor(c, nil, nil);
		else{
			scalecursor(&m->cursor2, &m->cursor);
			c->impl->rpc_setcursor(c, &m->cursor, &m->cursor2);
		}
		replymsg(c, m);
		break;

	case Tcursor2:
		if(m->arrowcursor)
			c->impl->rpc_setcursor(c, nil, nil);
		else
			c->impl->rpc_setcursor(c, &m->cursor, &m->cursor2);
		replymsg(c, m);
		break;

	case Tbouncemouse:
		c->impl->rpc_bouncemouse(c, m->mouse);
		replymsg(c, m);
		break;

	case Tlabel:
		c->impl->rpc_setlabel(c, m->label);
		replymsg(c, m);
		break;

	case Trdsnarf:
		m->snarf = rpc_getsnarf();
		replymsg(c, m);
		free(m->snarf);
		break;

	case Twrsnarf:
		rpc_putsnarf(m->snarf);
		replymsg(c, m);
		break;

	case Trddraw:
		n = m->count;
		if(n > sizeof buf)
			n = sizeof buf;
		n = draw_dataread(c, buf, n);
		if(n < 0)
			replyerror(c, m);
		else{
			m->count = n;
			m->data = buf;
			replymsg(c, m);
		}
		break;

	case Twrdraw:
		if(draw_datawrite(c, m->data, m->count) < 0)
			replyerror(c, m);
		else
			replymsg(c, m);
		break;

	case Ttop:
		c->impl->rpc_topwin(c);
		replymsg(c, m);
		break;

	case Tresize:
		c->impl->rpc_resizewindow(c, m->rect);
		replymsg(c, m);
		break;
	}
}

/*
 * Reply to m.
 */
static void
replymsg(Client *c, Wsysmsg *m)
{
	int n;

	/* T -> R msg */
	if(m->type%2 == 0)
		m->type++;

	if(trace) fprint(2, "%lld [%d] -> %W\n", nsec()/1000000, threadid(), m);
	/* copy to output buffer */
	n = sizeW2M(m);

	qlock(&c->wfdlk);
	if(n > c->nmbuf){
		free(c->mbuf);
		c->mbuf = malloc(n);
		if(c->mbuf == nil)
			sysfatal("out of memory");
		c->nmbuf = n;
	}
	convW2M(m, c->mbuf, n);
	if(write(c->wfd, c->mbuf, n) != n)
		fprint(2, "client write: %r\n");
	qunlock(&c->wfdlk);
}

/*
 * Match queued kbd reads with queued kbd characters.
 */
static void
matchkbd(Client *c)
{
	int tag;
	Wsysmsg m;

	if(c->kbd.stall)
		return;
	while(c->kbd.ri!=c->kbd.wi && c->kbdtags.ri!=c->kbdtags.wi){
		tag = c->kbdtags.t[c->kbdtags.ri++];
		m.type = Rrdkbd;
		if(tag&1)
			m.type = Rrdkbd4;
		m.tag = tag>>1;
		if(c->kbdtags.ri == nelem(c->kbdtags.t))
			c->kbdtags.ri = 0;
		m.rune = c->kbd.r[c->kbd.ri++];
		if(c->kbd.ri == nelem(c->kbd.r))
			c->kbd.ri = 0;
		replymsg(c, &m);
	}
}

/*
 * Match queued mouse reads with queued mouse events.
 * Must be called with c->eventlk held.
 */
static void
matchmouse(Client *c)
{
	Wsysmsg m;

	if(canqlock(&c->eventlk))
		sysfatal("matchmouse: event lock must be held");

	while(c->mouse.ri!=c->mouse.wi && c->mousetags.ri!=c->mousetags.wi){
		m.type = Rrdmouse;
		m.tag = c->mousetags.t[c->mousetags.ri++];
		if(c->mousetags.ri == nelem(c->mousetags.t))
			c->mousetags.ri = 0;
		m.mouse = c->mouse.m[c->mouse.ri];
		m.resized = c->mouse.resized;
		c->mouse.resized = 0;
		c->mouse.ri++;
		if(c->mouse.ri == nelem(c->mouse.m))
			c->mouse.ri = 0;
		replymsg(c, &m);
	}
}

/*
 * Add a mouse event to the mouse buffer.
 * Must be called with c->eventlk held.
 */
static void
putmouse(Client *c, int x, int y, int b, int scroll, uint ms)
{
	Rectangle mr;
	Mousebuf *mbuf;
	Mouse *m;

	if(canqlock(&c->eventlk))
		sysfatal("putmouse: event lock must be held");

	mr = c->mouserect;
	if(x < mr.min.x)
		x = mr.min.x;
	if(x >= mr.max.x)
		x = mr.max.x-1;
	if(y < mr.min.y)
		y = mr.min.y;
	if(y >= mr.max.y)
		y = mr.max.y-1;

	/*
	 * If reader has stopped reading, don't bother.
	 * If reader is completely caught up, definitely queue.
	 * Otherwise, queue only button change events.
	 */
	mbuf = &c->mouse;
	if(mbuf->stall || (mbuf->wi!=mbuf->ri && mbuf->last.buttons==b))
		return;

	m = &mbuf->last;
	m->xy.x = x;
	m->xy.y = y;
	m->buttons = b;
	m->scroll = scroll;
	m->msec = ms;

	mbuf->m[mbuf->wi] = *m;
	if(++mbuf->wi == nelem(mbuf->m))
		mbuf->wi = 0;
	if(mbuf->wi == mbuf->ri){
		mbuf->stall = 1;
		mbuf->ri = 0;
		mbuf->wi = 1;
		mbuf->m[0] = *m;
	}
	matchmouse(c);
}

/*
 * Repeat the last mouse event for resize.
 */
void
gfx_mouseresized(Client *c)
{
	int i;
	Mouse *m;

	qlock(&c->eventlk);
	i = c->mouse.ri;
	if(i == 0)
		i = nelem(c->mouse.m);
	m = &c->mouse.m[i-1];
	c->mouse.resized = 1;
	putmouse(c, m->xy.x, m->xy.y, m->buttons, m->scroll, m->msec);
	qunlock(&c->eventlk);
}

/*
 * Enqueue a new mouse event.
 */
void
gfx_mousetrack(Client *c, int x, int y, int b, int scroll, uint ms)
{
	qlock(&c->eventlk);
	putmouse(c, x, y, b, scroll, ms);
	qunlock(&c->eventlk);
}

/*
 * Add a key code to the keyboard buffer.
 * Must be called with c->eventlk held.
 */
static void
putkey(Client *c, int key)
{
	if(canqlock(&c->eventlk))
		sysfatal("putkey: event lock must be held");

	c->kbd.r[c->kbd.wi++] = key;
	if(c->kbd.wi == nelem(c->kbd.r))
		c->kbd.wi = 0;
	if(c->kbd.ri == c->kbd.wi)
		c->kbd.stall = 1;
	matchkbd(c);
}

/*
 * Stop any pending compose sequence (due to a mouse click).
 * Called from the graphics thread with no locks held.
 */
void
gfx_abortcompose(Client *c)
{
	qlock(&c->eventlk);
	if(c->kbd.alting){
		c->kbd.alting = 0;
		c->kbd.nk = 0;
	}
	qunlock(&c->eventlk);
}

/*
 * Record a single-rune keystroke.
 * Called from the graphics thread with no locks held.
 */
void
gfx_keystroke(Client *c, int ch)
{
	int i;

	qlock(&c->eventlk);
	if(ch == Kalt){
		c->kbd.alting = !c->kbd.alting;
		c->kbd.nk = 0;
		qunlock(&c->eventlk);
		return;
	}
	if(ch == Kcmd+'r'){
		if(c->forcedpi)
			c->forcedpi = 0;
		else if(c->displaydpi >= 200)
			c->forcedpi = 100;
		else
			c->forcedpi = 225;
		qunlock(&c->eventlk);
		c->impl->rpc_resizeimg(c);
		return;
	}
	if(!c->kbd.alting){
		putkey(c, ch);
		qunlock(&c->eventlk);
		return;
	}
	if(c->kbd.nk >= nelem(c->kbd.k))	/* should not happen */
		c->kbd.nk = 0;
	c->kbd.k[c->kbd.nk++] = ch;
	ch = latin1(c->kbd.k, c->kbd.nk);
	if(ch > 0){
		c->kbd.alting = 0;
		putkey(c, ch);
		c->kbd.nk = 0;
		qunlock(&c->eventlk);
		return;
	}
	if(ch == -1){
		c->kbd.alting = 0;
		for(i=0; i<c->kbd.nk; i++)
			putkey(c, c->kbd.k[i]);
		c->kbd.nk = 0;
		qunlock(&c->eventlk);
		return;
	}
	/* need more input */
	qunlock(&c->eventlk);
	return;
}
