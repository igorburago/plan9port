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
	/* for generating syms in mkfile only: */
	#include <bio.h>
	#include "edit.h"

void	keyboardthread(void*);
void	mousethread(void*);
void	newwindowthread(void*);
void	plumbproc(void*);
void	shutdownthread(void*);
void	waitthread(void*);
void	xfidallocthread(void*);

void	acmeerrorinit(void);
void	readfile(Column*, char*);
int	shutdown(void*, char*);
int	timefmt(Fmt*);

Reffont		*reffonts[2];
Reffont		**fontcache;
int		nfontcache;
char		wdir[512] = ".";
int		mainpid;
char		*mtpt;
int		snarffd = -1;
int		swapscrollbuttons = FALSE;
Linesnapscroll	mousescroll;

enum { NSnarf = 1000 };	/* less than 1024, I/O buffer size */
Rune	snarfrune[NSnarf+1];

char	*fontnames[2] =
{
	"/lib/font/bit/lucsans/euro.8.font",
	"/lib/font/bit/lucm/unicode.9.font"
};

Command	*command;

void
derror(Display *d, char *errorstr)
{
	USED(d);
	error(errorstr);
}

void
usage(void)
{
	fprint(2, "usage: acme -a -c ncol -f mainfontname -F altfontname -l loadfile -W winsize\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	enum{
		Maxncol		= 256,
		Maxtabstop	= 4096
	};
	int ncol, i;
	char *loadfile, *p;
	ulong ul;
	Display *d;
	Column *c;

	rfork(RFENVG|RFNAMEG);

	ncol = -1;
	loadfile = nil;

	ARGBEGIN{
	case 'D':
		{
			extern int _threaddebuglevel;
			_threaddebuglevel = ~0;
		}
		break;
	case 'a':
		globalautoindent = TRUE;
		break;
	case 'b':
		bartflag = TRUE;
		break;
	case 'c':
		p = EARGF(usage());
		ul = strtoul(p, nil, 10);
		if(ul==0 || ul==-1)
			usage();
		if(ul <= Maxncol)
			ncol = ul;
		else
			ncol = Maxncol;
		break;
	case 'f':
		fontnames[0] = EARGF(usage());
		break;
	case 'F':
		fontnames[1] = EARGF(usage());
		break;
	case 'l':
		loadfile = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 'r':
		swapscrollbuttons = TRUE;
		break;
	case 'W':
		winsize = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	fontnames[0] = estrdup(fontnames[0]);
	fontnames[1] = estrdup(fontnames[1]);

	quotefmtinstall();
	fmtinstall('t', timefmt);

	cputype = getenv("cputype");
	objtype = getenv("objtype");
	home = getenv("HOME");
	acmeshell = getenv("acmeshell");
	if(acmeshell!=nil && *acmeshell=='\0'){
		free(acmeshell);
		acmeshell = nil;
	}

	p = getenv("tabstop");
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
	if(maxtab == 0)
		maxtab = 4;

	if(loadfile != nil)
		rowloadfonts(loadfile);
	if(*fontnames[0] != '\0')
		putenv("font", fontnames[0]);

	//snarffd = open("/dev/snarf", OREAD|OCEXEC);

	//if(cputype){
	//	sprint(buf, "/acme/bin/%s", cputype);
	//	bind(buf, "/bin", MBEFORE);
	//}
	//bind("/acme/bin", "/bin", MBEFORE);

	mainpid = getpid();
	getwd(wdir, sizeof wdir);

	//if(geninitdraw(nil, derror, fontnames[0], "acme", nil, Refnone) < 0)
	//	error("can't open display");
	if(initdraw(derror, fontnames[0], "acme") < 0)
		error("can't open display");

	d = display;
	font = d->defaultfont;

	reffont.f = font;
	reffonts[0] = &reffont;
	incref(&reffont.ref);	/* one to hold up 'font' variable */
	incref(&reffont.ref);	/* one to hold up reffonts[0] */
	fontcache = emalloc(sizeof(Reffont*));
	nfontcache = 1;
	fontcache[0] = &reffont;

	iconinit();
	timerinit();
	rxinit();

	cwait = threadwaitchan();
	ccommand = chancreate(sizeof(Command**), 0);
	ckill = chancreate(sizeof(Rune*), 0);
	cxfidalloc = chancreate(sizeof(Xfid*), 0);
	cxfidfree = chancreate(sizeof(Xfid*), 0);
	cnewwindow = chancreate(sizeof(Channel*), 0);
	cerr = chancreate(sizeof(char*), 0);
	cedit = chancreate(sizeof(int), 0);
	cexit = chancreate(sizeof(int), 0);
	cwarn = chancreate(sizeof(void*), 1);
	if(cwait==nil || ccommand==nil || ckill==nil
	|| cxfidalloc==nil || cxfidfree==nil || cnewwindow==nil
	|| cerr==nil || cedit==nil || cexit==nil || cwarn==nil)
		error("can't create initial channels");
	chansetname(ccommand, "ccommand");
	chansetname(ckill, "ckill");
	chansetname(cxfidalloc, "cxfidalloc");
	chansetname(cxfidfree, "cxfidfree");
	chansetname(cnewwindow, "cnewwindow");
	chansetname(cerr, "cerr");
	chansetname(cedit, "cedit");
	chansetname(cexit, "cexit");
	chansetname(cwarn, "cwarn");

	mousectl = initmouse(nil, screen);
	if(mousectl == nil)
		error("can't initialize mouse");
	mouse = &mousectl->m;
	keyboardctl = initkeyboard(nil);
	if(keyboardctl == nil)
		error("can't initialize keyboard");
	startplumbing();

	//plumbeditfd = plumbopen("edit", OREAD|OCEXEC);
	//if(plumbeditfd < 0)
	//	fprint(2, "acme: can't initialize plumber: %r\n");
	//else{
	//	cplumb = chancreate(sizeof(Plumbmsg*), 0);
	//	threadcreate(plumbproc, nil, STACK);
	//}
	//plumbsendfd = plumbopen("send", OWRITE|OCEXEC);

	fsysinit();

	#define WPERCOL 8
	disk = diskinit();
	if(loadfile==nil || !rowload(&row, loadfile, TRUE)){
		rowinit(&row, screen->clipr);
		if(ncol < 0){
			if(argc == 0)
				ncol = 2;
			else{
				ncol = (argc+(WPERCOL-1))/WPERCOL;
				if(ncol < 2)
					ncol = 2;
			}
		}
		if(ncol == 0)
			ncol = 2;
		for(i=0; i<ncol; i++){
			c = rowadd(&row, nil, -1);
			if(c==nil && i==0)
				error("initializing columns");
		}
		c = row.col[row.ncol-1];
		if(argc == 0)
			readfile(c, wdir);
		else
			for(i=0; i<argc; i++){
				p = utfrrune(argv[i], '/');
				if((p!=nil && strcmp(p, "/guide")==0) || i/WPERCOL>=row.ncol)
					readfile(c, argv[i]);
				else
					readfile(row.col[i/WPERCOL], argv[i]);
			}
	}
	flushimage(display, 1);

	acmeerrorinit();
	threadcreate(keyboardthread, nil, STACK);
	threadcreate(mousethread, nil, STACK);
	threadcreate(waitthread, nil, STACK);
	threadcreate(xfidallocthread, nil, STACK);
	threadcreate(newwindowthread, nil, STACK);
	//threadcreate(shutdownthread, nil, STACK);
	threadnotify(shutdown, 1);

	recvul(cexit);
	killprocs();
	threadexitsall(nil);
}

void
readfile(Column *c, char *s)
{
	Window *w;
	Rune rb[256];
	int nr;
	Runestr rs;

	w = coladd(c, nil, nil, -1);
	if(s[0] != '/')
		runesnprint(rb, sizeof rb, "%s/%s", wdir, s);
	else
		runesnprint(rb, sizeof rb, "%s", s);
	nr = runestrlen(rb);
	rs = cleanrname(runestr(rb, nr));
	winsetname(w, rs.r, rs.nr);
	textload(&w->body, 0, s, 1);
	w->body.file->mod = FALSE;
	w->dirty = FALSE;
	winsettag(w);
	winresize(w, w->r, FALSE, TRUE);
	textscrdraw(&w->body);
	textsetselect(&w->tag, w->tag.file->b.nc, w->tag.file->b.nc);
	xfidlog(w, "new");
}

char	*ignotes[] =
{
	"sys: write on closed pipe",
	"sys: ttin",
	"sys: ttou",
	"sys: tstp",
	nil
};

char	*oknotes[] =
{
	"delete",
	"hangup",
	"kill",
	"exit",
	nil
};

int	dumping;

int
shutdown(void *arg, char *msg)
{
	int i;

	USED(arg);

	for(i=0; ignotes[i]; i++)
		if(strncmp(ignotes[i], msg, strlen(ignotes[i])) == 0)
			return 1;

	killprocs();
	if(!dumping && strcmp(msg, "kill")!=0 && strcmp(msg, "exit")!=0 && getpid()==mainpid){
		dumping = TRUE;
		rowdump(&row, nil);
	}
	for(i=0; oknotes[i]; i++)
		if(strncmp(oknotes[i], msg, strlen(oknotes[i])) == 0)
			threadexitsall(msg);
	print("acme: %s\n", msg);
	return 0;
}

/*
void
shutdownthread(void *arg)
{
	char *msg;
	Channel *c;

	USED(arg);
	threadsetname("shutdown");

	c = threadnotechan();
	while((msg = recvp(c)) != nil)
		shutdown(nil, msg);
}
*/

void
killprocs(void)
{
	Command *c;

	fsysclose();
	//if(display != nil)
	//	flushimage(display, 1);

	for(c=command; c!=nil; c=c->next)
		postnote(PNGROUP, c->pid, "hangup");
}

static int	errorfd;
int		erroutfd;

void
acmeerrorproc(void *arg)
{
	char *buf;
	int n;

	USED(arg);
	threadsetname("acmeerrorproc");

	buf = emalloc(8192+1);
	while((n=read(errorfd, buf, 8192)) >= 0){
		buf[n] = '\0';
		sendp(cerr, estrdup(buf));
	}
	free(buf);
}

void
acmeerrorinit(void)
{
	int pfd[2];

	if(pipe(pfd) < 0)
		error("can't create pipe");
#if 0
	sprint(acmeerrorfile, "/srv/acme.%s.%d", getuser(), mainpid);
	fd = create(acmeerrorfile, OWRITE, 0666);
	if(fd < 0){
		remove(acmeerrorfile);
		fd = create(acmeerrorfile, OWRITE, 0666);
		if(fd < 0)
			error("can't create acmeerror file");
	}
	sprint(buf, "%d", pfd[0]);
	write(fd, buf, strlen(buf));
	close(fd);
	/* reopen pfd[1] close on exec */
	sprint(buf, "/fd/%d", pfd[1]);
	errorfd = open(buf, OREAD|OCEXEC);
#endif
	fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
	fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
	erroutfd = pfd[0];
	errorfd = pfd[1];
	if(errorfd < 0)
		error("can't re-open acmeerror file");
	proccreate(acmeerrorproc, nil, STACK);
}

/*
void
plumbproc(void *arg)
{
	Plumbmsg *m;

	USED(arg);
	threadsetname("plumbproc");

	for(;;){
		m = threadplumbrecv(plumbeditfd);
		if(m == nil)
			threadexits(nil);
		sendp(cplumb, m);
	}
}
*/

static int
navigationkey(Rune r)
{
	return r==Kleft || r==Kright || r==KctrlA || r==KctrlE ||
		r==Kup || r==Kdown || r==Kpgup || r==Kpgdown || r==Khome || r==Kend;
}

void
keyboardthread(void *arg)
{
	Rune r;
	Timer *timer;
	Text *t;
	enum { KTimer, KKey, NKALT };
	static Alt alts[NKALT+1];

	USED(arg);
	threadsetname("keyboardthread");

	typetext = nil;
	timer = nil;

	alts[KTimer].c = nil;
	alts[KTimer].v = nil;
	alts[KTimer].op = CHANNOP;
	alts[KKey].c = keyboardctl->c;
	alts[KKey].v = &r;
	alts[KKey].op = CHANRCV;
	alts[NKALT].op = CHANEND;

	for(;;){
		switch(alt(alts)){
		case KTimer:
			timerstop(timer);
			t = typetext;
			if(t!=nil && t->what==Tag){
				winlock(t->w, 'K');
				wincommit(t->w, t);
				winunlock(t->w);
				flushimage(display, 1);
			}
			alts[KTimer].c = nil;
			alts[KTimer].op = CHANNOP;
			break;
		case KKey:
			do{
				typetext = rowtype(&row, r, mouse->xy);
				t = typetext;
				/* Do not change activecol if just moving around. */
				if(t!=nil && t->col!=nil && !navigationkey(r))
					activecol = t->col;
				if(t!=nil && t->w!=nil)
					t->w->body.file->curtext = &t->w->body;
				if(timer != nil)
					timercancel(timer);
				if(t!=nil && t->what==Tag){
					timer = timerstart(500);
					alts[KTimer].c = timer->c;
					alts[KTimer].op = CHANRCV;
				}else{
					timer = nil;
					alts[KTimer].c = nil;
					alts[KTimer].op = CHANNOP;
				}
			}while(nbrecv(keyboardctl->c, &r) > 0);
			flushimage(display, 1);
			break;
		}
	}
}

static Window*
winof(Text *t)
{
	if(t == nil)
		return nil;
	return t->w;
}

void
mousethread(void *arg)
{
	Text *t, *argt;
	uint q0, q1;
	Window *w, *mw;
	Plumbmsg *pm;
	int samewin;
	Mouse m;
	int but, lines;
	char *act;
	enum { MResize, MMouse, MPlumb, MWarnings, NMALT };
	static Alt alts[NMALT+1];

	USED(arg);
	threadsetname("mousethread");

	seltext = nil;
	argtext = nil;
	mousetext = nil;
	barttext = nil;
	memset(&mousescroll, 0, sizeof(mousescroll));

	alts[MResize].c = mousectl->resizec;
	alts[MResize].v = nil;
	alts[MResize].op = CHANRCV;
	alts[MMouse].c = mousectl->c;
	alts[MMouse].v = &mousectl->m;
	alts[MMouse].op = CHANRCV;
	alts[MPlumb].c = cplumb;
	alts[MPlumb].v = &pm;
	alts[MPlumb].op = CHANRCV;
	alts[MWarnings].c = cwarn;
	alts[MWarnings].v = nil;
	alts[MWarnings].op = CHANRCV;
	if(cplumb == nil)
		alts[MPlumb].op = CHANNOP;
	alts[NMALT].op = CHANEND;

	for(;;){
		qlock(&row.lk);
		flushwarnings();
		qunlock(&row.lk);
		flushimage(display, 1);
		switch(alt(alts)){
		case MResize:
			if(getwindow(display, Refnone) < 0)
				error("attach to window");
			draw(screen, screen->r, display->black, nil, ZP);
			iconinit();
			rowresize(&row, screen->clipr);
			break;
		case MPlumb:
			if(strcmp(pm->type, "text") == 0){
				act = plumblookup(pm->attr, "action");
				if(act==nil || strcmp(act, "showfile")==0)
					plumblook(pm);
				else if(strcmp(act, "showdata")==0)
					plumbshow(pm);
			}
			plumbfree(pm);
			break;
		case MWarnings:
			break;
		case MMouse:
			/*
			 * Make a copy so decisions are consistent; mousectl changes
			 * underfoot. Can't just receive into m because this introduces
			 * another race; see /sys/src/libdraw/mouse.c.
			 */
			m = mousectl->m;
			qlock(&row.lk);
			t = rowwhich(&row, m.xy);
			w = winof(t);

			if(t != mousetext){
				mw = winof(mousetext);
				if(w != nil){
					samewin = (mw!=nil && mw->id==w->id);
					if(!samewin)
						xfidlog(w, "focus");
					/*
					 * In case mouse comes from another window or the other part
					 * (tag or body) of the same window during a scroll motion,
					 * do not carry over pending scroll, but do allow the motion
					 * to go on in the new quarters if it is not inertial.
					 */
					if(mousescroll.inmotion && (!samewin || t->what!=mousetext->what)){
						mousescroll.pendingdist = 0;
						mousescroll.motionhaltup = mousescroll.inertial;
						mousescroll.motionhaltdown = mousescroll.inertial;
					}
				}
				if(mw != nil){
					winlock(mw, 'M');
					mousetext->eq0 = ~0;
					wincommit(mw, mousetext);
					winunlock(mw);
				}
			}
			mousetext = t;
			if(t==nil || m.buttons==0)
				goto Continue;
			barttext = t;

			/* Scroll buttons, wheels, trackpad gestures, etc. */
			if(m.buttons & Mscrollsmask){
				lines = mouselinesnapscroll(&mousescroll, &m, t->fr.font->height, t->fr.maxlines);
				if(w!=nil && lines!=0){
					winlock(w, 'M');
					t->eq0 = ~0;
					winscroll(w, t, lines);
					winunlock(w);
				}
				goto Continue;
			}

			/* Clicks and drags on scroll bars and layout boxes. */
			if(ptinrect(m.xy, t->scrollr)){
				if(m.buttons == Mbutton1)
					but = 1;
				else if(m.buttons == Mbutton2)
					but = 2;
				else if(m.buttons == Mbutton3)
					but = 3;
				else
					goto Continue;
				switch(t->what){
				case Body:
					if(swapscrollbuttons){
						if(but == 1)
							but = 3;
						else if(but == 3)
							but = 1;
					}
					winlock(w, 'M');
					t->eq0 = ~0;
					textscrclick(t, but);
					winunlock(w);
					break;
				case Tag:
					/* When outside of the layout box, just interact with text. */
					if(m.xy.y >= t->scrollr.min.y+t->fr.font->height)
						goto Textclicks;
					coldragwin(t->col, t->w, but);
					barttext = &t->w->body;
					activecol = t->col;
					break;
				case Columntag:
					rowdragcol(&row, t->col, but);
					activecol = t->col;
					break;
				}
				goto Continue;
			}

		Textclicks:
			/* All other clicks, chords, and drags. */
			if(w != nil)
				winlock(w, 'M');
			t->eq0 = ~0;
			if(w != nil)
				wincommit(w, t);
			else
				textcommit(t, TRUE);
			if(m.buttons & Mbutton1){
				textselect(t);
				if(w != nil)
					winsettag(w);
				argtext = t;
				seltext = t;
				if(t->col != nil)
					activecol = t->col;	/* button 1 only */
				if(w!=nil && t==&w->body)
					activewin = w;
			}else if(m.buttons & Mbutton2){
				if(textselect2(t, &q0, &q1, &argt))
					execute(t, q0, q1, FALSE, argt);
			}else if(m.buttons & Mbutton3){
				if(textselect3(t, &q0, &q1))
					look3(t, q0, q1, FALSE);
			}
			if(w != nil)
				winunlock(w);

		Continue:
			qunlock(&row.lk);
			break;
		}
	}
}

/*
 * There is a race between process exiting and our finding out it was ever created.
 * This structure keeps a list of processes that have exited we haven't heard of.
 */
typedef struct Pid Pid;
struct Pid
{
	int	pid;
	char	msg[ERRMAX];
	Pid	*next;
};

void
waitthread(void *arg)
{
	Waitmsg *w;
	Command *c, *lc;
	uint pid;
	int found, ncmd;
	Rune *cmd;
	char *err;
	Text *t;
	Pid *pids, *p, *lastp;
	enum { WErr, WKill, WWait, WCmd, NWALT };
	Alt alts[NWALT+1];

	USED(arg);
	threadsetname("waitthread");

	command = nil;
	pids = nil;

	alts[WErr].c = cerr;
	alts[WErr].v = &err;
	alts[WErr].op = CHANRCV;
	alts[WKill].c = ckill;
	alts[WKill].v = &cmd;
	alts[WKill].op = CHANRCV;
	alts[WWait].c = cwait;
	alts[WWait].v = &w;
	alts[WWait].op = CHANRCV;
	alts[WCmd].c = ccommand;
	alts[WCmd].v = &c;
	alts[WCmd].op = CHANRCV;
	alts[NWALT].op = CHANEND;

	for(;;){
		switch(alt(alts)){
		case WErr:
			qlock(&row.lk);
			warning(nil, "%s", err);
			free(err);
			flushimage(display, 1);
			qunlock(&row.lk);
			break;
		case WKill:
			found = FALSE;
			ncmd = runestrlen(cmd);
			for(c=command; c!=nil; c=c->next){
				/* -1 for blank */
				if(runeeq(c->name, c->nname-1, cmd, ncmd)){
					if(postnote(PNGROUP, c->pid, "kill") < 0)
						warning(nil, "kill %S: %r\n", cmd);
					found = TRUE;
				}
			}
			if(!found)
				warning(nil, "Kill: no process %S\n", cmd);
			free(cmd);
			break;
		case WWait:
			pid = w->pid;
			lc = nil;
			for(c=command; c; c=c->next){
				if(c->pid == pid){
					if(lc)
						lc->next = c->next;
					else
						command = c->next;
					break;
				}
				lc = c;
			}
			qlock(&row.lk);
			t = &row.tag;
			textcommit(t, TRUE);
			if(c == nil){
				/* helper processes use this exit status */
				if(strncmp(w->msg, "libthread", 9) != 0){
					p = emalloc(sizeof(Pid));
					p->pid = pid;
					strncpy(p->msg, w->msg, sizeof(p->msg));
					p->next = pids;
					pids = p;
				}
			}else{
				if(search(t, c->name, c->nname)){
					textdelete(t, t->q0, t->q1, TRUE);
					textsetselect(t, 0, 0);
				}
				if(w->msg[0])
					warning(c->md, "%.*S: exit %s\n", c->nname-1, c->name, w->msg);
				flushimage(display, 1);
			}
			qunlock(&row.lk);
			free(w);
		Freecmd:
			if(c){
				if(c->iseditcmd)
					sendul(cedit, 0);
				free(c->text);
				free(c->name);
				fsysdelid(c->md);
				free(c);
			}
			break;
		case WCmd:
			/* has this command already exited? */
			lastp = nil;
			for(p=pids; p!=nil; p=p->next){
				if(p->pid == c->pid){
					if(p->msg[0])
						warning(c->md, "%s\n", p->msg);
					if(lastp == nil)
						pids = p->next;
					else
						lastp->next = p->next;
					free(p);
					goto Freecmd;
				}
				lastp = p;
			}
			c->next = command;
			command = c;
			qlock(&row.lk);
			t = &row.tag;
			textcommit(t, TRUE);
			textinsert(t, 0, c->name, c->nname, TRUE);
			textsetselect(t, 0, 0);
			flushimage(display, 1);
			qunlock(&row.lk);
			break;
		}
	}
}

void
xfidallocthread(void *arg)
{
	Xfid *xfree, *x;
	enum { Alloc, Free, N };
	static Alt alts[N+1];

	USED(arg);
	threadsetname("xfidallocthread");

	alts[Alloc].c = cxfidalloc;
	alts[Alloc].v = nil;
	alts[Alloc].op = CHANRCV;
	alts[Free].c = cxfidfree;
	alts[Free].v = &x;
	alts[Free].op = CHANRCV;
	alts[N].op = CHANEND;

	xfree = nil;
	for(;;){
		switch(alt(alts)){
		case Alloc:
			x = xfree;
			if(x)
				xfree = x->next;
			else{
				x = emalloc(sizeof(Xfid));
				x->c = chancreate(sizeof(void(*)(Xfid*)), 0);
				chansetname(x->c, "xc%p", x->c);
				x->arg = x;
				threadcreate(xfidctl, x->arg, STACK);
			}
			sendp(cxfidalloc, x);
			break;
		case Free:
			x->next = xfree;
			xfree = x;
			break;
		}
	}
}

/* this thread, in the main proc, allows fsysproc to get a window made without doing graphics */
void
newwindowthread(void *arg)
{
	Window *w;

	USED(arg);
	threadsetname("newwindowthread");

	for(;;){
		/* only fsysproc is talking to us, so synchronization is trivial */
		recvp(cnewwindow);
		w = makenewwindow(nil);
		winsettag(w);
		xfidlog(w, "new");
		sendp(cnewwindow, w);
	}
}

Reffont*
rfget(int fix, int save, int setfont, char *name)
{
	Reffont *r;
	Font *f;
	int i;

	r = nil;
	if(name == nil){
		name = fontnames[fix];
		r = reffonts[fix];
	}
	if(r == nil){
		for(i=0; i<nfontcache; i++)
			if(strcmp(name, fontcache[i]->f->name) == 0){
				r = fontcache[i];
				goto Found;
			}
		f = openfont(display, name);
		if(f == nil){
			warning(nil, "can't open font file %s: %r\n", name);
			return nil;
		}
		r = emalloc(sizeof(Reffont));
		r->f = f;
		fontcache = erealloc(fontcache, (nfontcache+1)*sizeof(Reffont*));
		fontcache[nfontcache++] = r;
	}
Found:
	if(save){
		incref(&r->ref);
		if(reffonts[fix])
			rfclose(reffonts[fix]);
		reffonts[fix] = r;
		if(name != fontnames[fix]){
			free(fontnames[fix]);
			fontnames[fix] = estrdup(name);
		}
	}
	if(setfont){
		reffont.f = r->f;
		incref(&r->ref);
		rfclose(reffonts[0]);
		font = r->f;
		reffonts[0] = r;
		incref(&r->ref);
		iconinit();
	}
	incref(&r->ref);
	return r;
}

void
rfclose(Reffont *r)
{
	int i;

	if(decref(&r->ref) == 0){
		for(i=0; i<nfontcache; i++)
			if(r == fontcache[i])
				break;
		if(i >= nfontcache)
			warning(nil, "internal error: can't find font in cache\n");
		else{
			nfontcache--;
			memmove(fontcache+i, fontcache+i+1, (nfontcache-i)*sizeof(Reffont*));
		}
		freefont(r->f);
		free(r);
	}
}

Cursor boxcursor = {
	{-7, -7},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F,
	 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x00, 0x00}
};

Cursor2 boxcursor2 = {
	{-15, -15},
	{0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

void
iconinit(void)
{
	Rectangle r, br;
	Image *bc;

	if(tagcols[BACK] == nil){
		tagcols[BACK] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x413627FF);
		tagcols[TEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xFAD4B0FF);
		tagcols[HIGH] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x6D5D4CFF);
		tagcols[HTEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xFEDEAEFF);
		tagcols[BORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x675644FF);

		textcols[BACK] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x231A0DFF);
		textcols[TEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xFFC892FF);
		textcols[HIGH] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x434B3CFF);
		textcols[HTEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xFFCA99FF);
		textcols[BORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x413627FF);
	}

	r = Rect(0, 0, Scrollwid, font->height);
	if(button!=nil && eqrect(r, button->r))
		return;

	if(button != nil){
		freeimage(button);
		freeimage(modbutton);
		freeimage(colbutton);
	}

	br = insetrect(r, ButtonBorder);
	bc = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xCD9B5EFF);

	button = allocimage(display, r, screen->chan, 0, DNofill);
	draw(button, r, tagcols[BACK], nil, r.min);
	border(button, br, ButtonBorder, bc, ZP);

	modbutton = allocimage(display, r, screen->chan, 0, DNofill);
	draw(modbutton, r, tagcols[BACK], nil, r.min);
	draw(modbutton, insetrect(br, ButtonBorder/2), bc, nil, r.min);

	colbutton = allocimage(display, r, screen->chan, 0, DNofill);
	draw(colbutton, r, tagcols[BACK], nil, r.min);
	border(colbutton, br, ButtonBorder, bc, ZP);

	freeimage(bc);

	but2col = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x99283DFF);
	but3col = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x657337FF);
}

void
acmeputsnarf(void)
{
	/*
	 * Rio truncates larges snarf buffers, so this limit
	 * prevents using the service if the string is huge.
	 */
	enum { Maxsnarf = 100*1024 };
	Fmt f;
	int i, n;
	char *s;

	if(snarfbuf.nc<1 || Maxsnarf<snarfbuf.nc)
		return;

	fmtstrinit(&f);
	for(i=0; i<snarfbuf.nc; i+=n){
		n = snarfbuf.nc-i;
		if(n >= NSnarf)
			n = NSnarf;
		bufread(&snarfbuf, i, snarfrune, n);
		if(fmtprint(&f, "%.*S", n, snarfrune) < 0)
			break;
	}
	s = fmtstrflush(&f);
	if(s!=nil && s[0]!='\0')
		putsnarf(s);
	free(s);
}

void
acmegetsnarf(void)
{
	char *s;
	Rune *r;
	int len, nb, nr, nulls;

	s = getsnarf();
	if(s==nil || s[0]=='\0'){
		free(s);
		return;
	}

	len = strlen(s);
	r = runemalloc(len+1);
	cvttorunes(s, len, r, &nb, &nr, &nulls);
	bufreset(&snarfbuf);
	bufinsert(&snarfbuf, 0, r, nr);
	free(r);
	free(s);
}

int
ismtpt(char *file)
{
	int n;

	if(mtpt == nil)
		return FALSE;
	/* This is not foolproof, but it will stop a lot of them. */
	n = strlen(mtpt);
	return strncmp(file, mtpt, n)==0 &&
		((n>0 && mtpt[n-1]=='/') || file[n]=='/' || file[n]=='\0');
}

int
timefmt(Fmt *f)
{
	Tm *tm;

	tm = localtime(va_arg(f->args, ulong));
	return fmtprint(f, "%04d/%02d/%02d %02d:%02d:%02d",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
}
