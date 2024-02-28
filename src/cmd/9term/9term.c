#include <u.h>
#include <signal.h>
#include <libc.h>
#include <ctype.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include <frame.h>
#include <plumb.h>
#include <complete.h>
#define Extern
#include "dat.h"
#include "fns.h"
#include "term.h"

void	derror(Display*, char*);
void	hangupnote(void*, char*);
void	keyboardthread(void*);
void	mousethread(void*);
void	rcinputproc(void*);
void	rcoutputproc(void*);
void	resizethread(void*);
void	servedevtext(void);

const char	*termprog = "9term";
int		use9wm;
int		mainpid;
int		mousepid;
int		plumbfd;
int		rcpid;
int		rcfd;
int		sfd;
Window		*w0;
char		*fontname;

int	errorshouldabort = 0;
int	cooked;

void
usage(void)
{
	fprint(2, "usage: 9term [-s] [-f font] [-W winsize] [cmd ...]\n");
	threadexitsall("usage");
}

int
threadmaybackground(void)
{
	return 1;
}

void
threadmain(int argc, char *argv[])
{
	enum { Maxtabstop = 4096 };
	char *p;
	ulong ul;

	rfork(RFNOTEG);
	font = nil;
	_wantfocuschanges = 1;
	mainpid = getpid();
	messagesize = 8192;

	ARGBEGIN{
	default:
		usage();
	case 'l':
		loginshell = TRUE;
		break;
	case 'f':
		fontname = EARGF(usage());
		break;
	case 's':
		scrolling = TRUE;
		break;
	case 'c':
		cooked = TRUE;
		break;
	case 'w':	/* started from rio or 9wm */
		use9wm = TRUE;
		break;
	case 'W':
		winsize = EARGF(usage());
		break;
	}ARGEND

	if(fontname!=nil && *fontname!='\0')
		putenv("font", fontname);

	p = getenv("tabstop");
	if(p == nil)
		p = getenv("TABSTOP");
	if(p != nil){
		ul = strtoul(p, nil, 10);
		if(ul!=0 && ul!=-1){
			if(ul <= Maxtabstop)
				maxtab = ul;
			else
				maxtab = Maxtabstop;
		}
		free(p);
	}
	if(maxtab <= 0)
		maxtab = 4;

	if(initdraw(derror, fontname, "9term") < 0)
		error("can't open display");

	notify(hangupnote);
	noteenable("sys: child");

	mousectl = initmouse(nil, screen);
	if(mousectl == nil)
		error("can't find mouse");
	mouse = &mousectl->m;
	keyboardctl = initkeyboard(nil);
	if(keyboardctl == nil)
		error("can't find keyboard");

	timerinit();
	servedevtext();
	rcpid = rcstart(argc, argv, &rcfd, &sfd);
	w0 = new(screen, FALSE, scrolling, rcpid, ".");

	threadcreate(resizethread, w0, STACK);
	threadcreate(mousethread, w0, STACK);
	threadcreate(keyboardthread, w0, STACK);

	proccreate(rcoutputproc, w0, STACK);
	proccreate(rcinputproc, w0, STACK);
}

void
derror(Display *d, char *errorstr)
{
	USED(d);
	error(errorstr);
}

void
hangupnote(void *arg, char *msg)
{
	char buf[128];
	int n;

	USED(arg);

	if(getpid() != mainpid)
		noted(NDFLT);
	if(strcmp(msg, "hangup") == 0){
		postnote(PNPROC, rcpid, "hangup");
		noted(NDFLT);
	}
	if(strstr(msg, "child") != nil){
		n = awaitnohang(buf, sizeof buf-1);
		if(n > 0){
			buf[n] = 0;
			if(atoi(buf) == rcpid)
				threadexitsall(nil);
		}
		noted(NCONT);
	}
	noted(NDFLT);
}

void
resizethread(void *arg)
{
	Window *w;
	Point cs, fs;

	w = arg;
	threadsetname("resizethread");

	for(;;){
		wresize(w, screen, FALSE);
		flushimage(display, 1);

		cs = stringsize(display->defaultfont, "0");
		if(cs.x!=0 && cs.y!=0){
			fs = subpt(w->f.r.max, w->f.r.min);
			updatewinsize(fs.y/cs.y, fs.x/cs.x, fs.y, fs.x);
		}

		if(recv(mousectl->resizec, nil) != 1)
			break;
		if(getwindow(display, Refnone) < 0)
			error("can't reattach to window");
	}
}

void
mousethread(void *arg)
{
	Window *w;
	Mouse m;

	w = arg;
	threadsetname("mousethread");

	while(readmouse(mousectl) >= 0){
		if((mouse->buttons&(Mbutton1|Mscrollsmask))
		|| ptinrect(mouse->xy, w->scrollr))
			do{
				m = *mouse;
				if(m.buttons != 0)
					wsetcursor(w, FALSE);
				send(w->mc.c, &m);	/* send to window */
			}while(m.buttons!=0 && readmouse(mousectl)>=0);
		else if(mouse->buttons & Mbutton2)
			button2menu(w);
		else
			bouncemouse(mouse);
	}
}

void
keyboardthread(void *arg)
{
	enum { Nbuf = 20 };
	Window *w;
	Rune buf[2][Nbuf+1], *rp;
	int b, i;

	w = arg;
	threadsetname("keyboardthread");

	for(b=0; ; b=1-b){
		rp = buf[b];
		recv(keyboardctl->c, &rp[0]);
		for(i=1; i<Nbuf; i++)
			if(nbrecv(keyboardctl->c, &rp[i]) <= 0)
				break;
		rp[i] = '\0';
		sendp(w->ck, rp);
	}
}

void
wborder(Window *w, int type)
{
}

Window*
wpointto(Point pt)
{
	return w0;
}

Window*
new(Image *i, int hideit, int scrollit, int pid, char *dir)
{
	Window *w;
	Channel *cm, *ck, *cctl;

	if(i == nil)
		return nil;
	cm = chancreate(sizeof(Mouse), 0);
	ck = chancreate(sizeof(Rune*), 0);
	cctl = chancreate(sizeof(Wctlmesg), 4);
	if(cm==nil || ck==nil || cctl==nil)
		error("new: channel alloc failed");
	w = wmk(i, cm, ck, cctl, scrollit, dir);
	window = erealloc(window, ++nwindow*sizeof(Window*));
	window[nwindow-1] = w;
	if(hideit){
		hidden[nhidden++] = w;
		w->screenr = ZR;
	}
	threadcreate(winctl, w, STACK);
	if(!hideit)
		wcurrent(w);
	flushimage(display, 1);
	wsetpid(w, pid, 1);
	wsetname(w);
	return w;
}

/*
 * Button 2 menu.  Extra entry for always cook
 */
enum
{
	Cut,
	Paste,
	Snarf,
	Plumb,
	Look,
	Send,
	Scroll,
	Cook
};
char	*menu2str[] =
{
	"cut",
	"paste",
	"snarf",
	"plumb",
	"look",
	"send",
	"scroll",
	"cook",
	nil
};
Menu menu2 =
{
	menu2str
};

void
button2menu(Window *w)
{
	static Rune Lnl[] = { '\n', 0 };

	if(w->deleted)
		return;
	incref(&w->ref);
	if(w->scrolling)
		menu2str[Scroll] = "noscroll";
	else
		menu2str[Scroll] = "scroll";
	if(cooked)
		menu2str[Cook] = "nocook";
	else
		menu2str[Cook] = "cook";

	switch(menuhit(2, mousectl, &menu2, wscreen)){
	case Cut:
		wsnarf(w);
		wcut(w);
		wscrdraw(w);
		break;

	case Snarf:
		wsnarf(w);
		break;

	case Paste:
		riogetsnarf();
		wpaste(w);
		wscrdraw(w);
		break;

	case Plumb:
		wplumb(w);
		break;

	case Look:
		wlook(w);
		break;

	case Send:
		riogetsnarf();
		wsnarf(w);
		if(nsnarf == 0)
			break;
		if(w->rawing){
			waddraw(w, snarf, nsnarf);
			if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!=Keof)
				waddraw(w, Lnl, 1);
		}else{
			winsert(w, snarf, nsnarf, w->nr);
			if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!=Keof)
				winsert(w, Lnl, 1, w->nr);
		}
		wsetselect(w, w->nr, w->nr);
		wshow(w, w->nr);
		break;

	case Scroll:
		if(w->scrolling ^= 1)
			wshow(w, w->nr);
		break;
	case Cook:
		cooked ^= 1;
		break;
	}
	wclose(w);
	wsendctlmesg(w, Wakeup, ZR, nil);
	flushimage(display, 1);
}

int
rawon(void)
{
	return !cooked && !isecho(sfd);
}

/*
 * I/O with child rc.
 */

static int	label(Window*, Rune*, int);

void
rcoutputproc(void *arg)
{
	static char data[9000];
	Window *w;
	int cnt, n, nb, nr;
	Conswritemesg cwm;
	Rune *r;
	Stringpair pair;

	w = arg;
	threadsetname("rcoutputproc");

	cnt = 0;
	for(;;){
		/* XXX Let typing have a go -- maybe there's a rubout waiting. */
		n = read(rcfd, data+cnt, sizeof data-cnt);
		if(n <= 0){
			if(n < 0)
				fprint(2, "9term: rc read error: %r\n");
			threadexitsall("eof on rc output");
		}
		n = echocancel(data+cnt, n);
		if(n == 0)
			continue;
		cnt += n;
		r = runemalloc(cnt);
		cvttorunes(data, cnt-UTFmax, r, &nb, &nr, nil);
		/* approach end of buffer */
		while(fullrune(data+nb, cnt-nb)){
			nb += chartorune(&r[nr], data+nb);
			if(r[nr] != '\0')
				nr++;
		}
		if(nb < cnt)
			memmove(data, data+nb, cnt-nb);
		cnt -= nb;

		nr = label(w, r, nr);
		if(nr == 0)
			continue;

		recv(w->conswrite, &cwm);
		pair.s = r;
		pair.ns = nr;
		send(cwm.cw, &pair);
	}
}

void
winterrupt(Window *w)
{
	char rubout[1];

	USED(w);
	rubout[0] = getintr(sfd);
	write(rcfd, rubout, 1);
}

int
intrc(void)
{
	return getintr(sfd);
}

/*
 * Process in-band messages about window title changes.
 * The messages are of the form:
 *
 *	\033];xxx\007
 *
 * where xxx is the new directory.  This format was chosen
 * because it changes the label on xterm windows.
 */
static int
label(Window *w, Rune *r, int nr)
{
	static Rune Lhold[] = { '*', '9', 't', 'e', 'r', 'm', '-', 'h', 'o', 'l', 'd', '+', 0 };
	int sl, el;
	char *dir, *p;

	el = nr-1;
	while(el>=0 && r[el]!='\007')
		el--;
	sl = el;
	while(sl>=3 && !(r[sl-3]=='\033' && r[sl-2]==']' && r[sl-1]==';'))
		sl--;
	if(sl < 3)
		return nr;

	if(el-sl==nelem(Lhold)-1 && runestrncmp(r+sl, Lhold, el-sl)==0){
		w->holding = TRUE;
		wrepaint(w);
		flushimage(display, 1);
	}else if((dir = smprint("%.*S", el-sl, r+sl)) != nil){
		drawsetlabel(dir);
		/* remove trailing /-sysname if present */
		p = strrchr(dir, '/');
		if(p!=nil && p[1]=='-'){
			if(p == dir)
				p++;
			*p = '\0';
		}
		free(w->dir);
		w->dir = dir;
	}

	runemove(r+sl-3, r+el+1, nr-el-1);
	return nr-(el+1-(sl-3));
}

void
rcinputproc(void *arg)
{
	static char data[9000];
	Window *w;
	Consreadmesg crm;
	Channel *c1, *c2;
	Stringpair pair;

	w = arg;
	threadsetname("rcinputproc");

	for(;;){
		recv(w->consread, &crm);
		c1 = crm.c1;
		c2 = crm.c2;

		pair.s = data;
		pair.ns = sizeof data;
		send(c1, &pair);
		recv(c2, &pair);

		if(isecho(sfd))
			echoed(pair.s, pair.ns);
		if(write(rcfd, pair.s, pair.ns) < 0)
			threadexitsall(nil);
	}
}

/*
 * Snarf buffer - rio uses runes internally
 */
void
rioputsnarf(void)
{
	char *s;

	s = smprint("%.*S", nsnarf, snarf);
	if(s == nil)
		return;
	putsnarf(s);
	free(s);
}

void
riogetsnarf(void)
{
	char *s;
	int n, nb, nulls;

	s = getsnarf();
	if(s == nil)
		return;
	n = strlen(s)+1;
	free(snarf);
	snarf = runemalloc(n);
	cvttorunes(s, n, snarf, &nb, &nsnarf, &nulls);
	free(s);
}

/*
 * Clumsy hack to make " and "" work.
 * Then again, what's not a clumsy hack here in Unix land?
 */

char adir[100];
char thesocket[100];
int afd;

void listenproc(void*);
void textproc(void*);

void
removethesocket(void)
{
	if(thesocket[0] == '\0')
		return;
	if(remove(thesocket) < 0)
		fprint(2, "remove %s: %r\n", thesocket);
}

void
servedevtext(void)
{
	char buf[100];

	snprint(buf, sizeof buf, "unix!/tmp/9term-text.%d", getpid());

	if((afd = announce(buf, adir)) < 0){
		putenv("text9term", "");
		return;
	}

	putenv("text9term", buf);
	proccreate(listenproc, nil, STACK);
	strcpy(thesocket, buf+5);
	atexit(removethesocket);
}

void
listenproc(void *arg)
{
	int fd;
	char dir[100];

	USED(arg);
	threadsetname("listen %s", thesocket);

	for(;;){
		fd = listen(adir, dir);
		if(fd < 0){
			close(afd);
			return;
		}
		proccreate(textproc, (void*)(uintptr)fd, STACK);
	}
}

void
textproc(void *arg)
{
	int fd, i, x, n, end;
	char buf[4096], *p, *ep;
	Rune r;

	fd = (uintptr)arg;
	threadsetname("textproc");

	p = buf;
	ep = buf+sizeof buf;
	if(w0 == nil)
		goto Return;
	end = w0->org+w0->nr;	/* avoid possible output loop */
	for(i=w0->org; ; i++){
		if(i>=end || ep-p<UTFmax){
			for(x=0; x<p-buf; x+=n)
				if((n = write(fd, buf+x, p-buf-x)) <= 0)
					goto Return;
			if(i >= end)
				goto Return;
			p = buf;
		}
		if(i < w0->org)
			i = w0->org;
		r = w0->r[i-w0->org];
		if(r < Runeself)
			*p++ = r;
		else
			p += runetochar(p, &r);
	}
Return:
	close(fd);
}
