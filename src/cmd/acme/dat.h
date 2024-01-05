enum
{
	Qdir,
	Qacme,
	Qcons,
	Qconsctl,
	Qdraw,
	Qeditout,
	Qindex,
	Qlabel,
	Qlog,
	Qnew,

	QWaddr,
	QWbody,
	QWctl,
	QWdata,
	QWeditout,
	QWerrors,
	QWevent,
	QWrdsel,
	QWwrsel,
	QWtag,
	QWxdata,
	QMAX
};

enum
{
	Blockincr	= 256,
	Maxblock	= 32*1024,
	NRange		= 10,
	Infinity	= 0x7FFFFFFF	/* huge value for regexp address */
};

#define Buffer AcmeBuffer
typedef struct Block	Block;
typedef struct Buffer	Buffer;
typedef struct Column	Column;
typedef struct Command	Command;
typedef struct Dirlist	Dirlist;
typedef struct Dirtab	Dirtab;
typedef struct Disk	Disk;
typedef struct Elog	Elog;
typedef struct Expand	Expand;
typedef struct Fid	Fid;
typedef struct File	File;
typedef struct Mntdir	Mntdir;
typedef struct Range	Range;
typedef struct Rangeset	Rangeset;
typedef struct Reffont	Reffont;
typedef struct Row	Row;
typedef struct Runestr	Runestr;
typedef struct Text	Text;
typedef struct Timer	Timer;
typedef struct Window	Window;
typedef struct Xfid	Xfid;

struct Runestr
{
	Rune	*r;
	int	nr;
};

struct Range
{
	int	q0;
	int	q1;
};

struct Block
{
	vlong		addr;	/* disk address in bytes */
	union{
		uint	n;		/* number of used runes in block */
		Block	*next;	/* pointer to next in free list */
	} u;
};

struct Disk
{
	int		fd;
	vlong		addr;	/* length of temp file */
	Block	*free[Maxblock/Blockincr+1];
};

Disk*	diskinit(void);
Block*	disknewblock(Disk*, uint);
void	diskread(Disk*, Block*, Rune*, uint);
void	diskrelease(Disk*, Block*);
void	diskwrite(Disk*, Block**, Rune*, uint);

struct Buffer
{
	uint	nc;
	Rune	*c;			/* cache */
	uint	cnc;			/* bytes in cache */
	uint	cmax;		/* size of allocated cache */
	uint	cq;			/* position of cache */
	int		cdirty;	/* cache needs to be written */
	uint	cbi;			/* index of cache Block */
	Block	**bl;		/* array of blocks */
	uint	nbl;			/* number of blocks */
};

void	bufclose(Buffer*);
void	bufdelete(Buffer*, uint, uint);
void	bufinsert(Buffer*, uint, Rune*, uint);
uint	bufload(Buffer*, uint, int, int*, DigestState*);
void	bufread(Buffer*, uint, Rune*, uint);
void	bufreset(Buffer*);

struct Elog
{
	short	type;		/* Delete, Insert, Filename */
	uint		q0;		/* location of change (unused in f) */
	uint		nd;		/* number of deleted characters */
	uint		nr;		/* # runes in string or file name */
	Rune		*r;
};

void	elogapply(File*);
void	elogclose(File*);
void	elogdelete(File*, int, int);
void	eloginsert(File*, int, Rune*, int);
void	elogreplace(File*, int, int, Rune*, int);
void	elogterm(File*);

struct File
{
	Buffer	b;			/* the data */
	Buffer	delta;	/* transcript of changes */
	Buffer	epsilon;	/* inversion of delta for redo */
	Buffer	*elogbuf;	/* log of pending editor changes */
	Elog		elog;		/* current pending change */
	Rune		*name;	/* name of associated file */
	int		nname;	/* size of name */
	uvlong	qidpath;	/* of file when read */
	ulong	mtime;	/* of file when read */
	int		dev;		/* of file when read */
	uchar	sha1[20];	/* of file when read */
	int		unread;	/* file has not been read from disk */
	int		editclean;	/* mark clean after edit command */

	int		seq;		/* if seq==0, File acts like Buffer */
	int		mod;
	Text		*curtext;	/* most recently used associated text */
	Text		**text;	/* list of associated texts */
	int		ntext;
	int		dumpid;	/* used in dumping zeroxed windows */
};

File*	fileaddtext(File*, Text*);
void	fileclose(File*);
void	filedelete(File*, uint, uint);
void	filedeltext(File*, Text*);
void	fileinsert(File*, uint, Rune*, uint);
uint	fileload(File*, uint, int, int*, DigestState*);
void	filemark(File*);
uint	fileredoseq(File*);
void	filereset(File*);
void	filesetname(File*, Rune*, int);
void	fileundelete(File*, Buffer*, uint, uint);
void	fileundo(File*, int, uint*, uint*);
void	fileuninsert(File*, Buffer*, uint, uint);
void	fileunsetname(File*, Buffer*);

enum	/* Text.what */
{
	Columntag,
	Rowtag,
	Tag,
	Body
};

struct Text
{
	File		*file;
	Frame	fr;
	Reffont	*reffont;
	uint	org;
	uint	q0;
	uint	q1;
	int	what;
	int	tabstop;
	Window	*w;
	Rectangle scrollr;
	Rectangle lastsr;
	Rectangle all;
	Row		*row;
	Column	*col;

	uint	iq1;	/* last input position */
	uint	eq0;	/* start of typing for ESC */
	uint	cq0;	/* cache position */
	int		ncache;	/* storage for insert */
	int		ncachealloc;
	Rune	*cache;
	int	nofill;
	int	needundo;
};

uint	textbacknl(Text*, uint, uint);
uint	textbsinsert(Text*, uint, Rune*, uint, int, int*);
int	textbswidth(Text*, Rune);
int	textclickhtmlmatch(Text*, uint*, uint*);
int	textclickmatch(Text*, int, int, int, uint*);
void	textclose(Text*);
void	textcolumnate(Text*, Dirlist**, int);
void	textcommit(Text*, int);
void	textconstrain(Text*, uint, uint, uint*, uint*);
void	textdelete(Text*, uint, uint, int);
void	textdoubleclick(Text*, uint*, uint*);
void	textfill(Text*);
uint	textforwardnl(Text*, uint, uint);
void	textframescroll(Text*, int);
void	textinit(Text*, File*, Rectangle, Reffont*, Image**);
void	textinsert(Text*, uint, Rune*, uint, int);
int	textload(Text*, uint, char*, int);
Rune	textreadc(Text*, uint);
void	textredraw(Text*, Rectangle, Font*, Image*, int);
void	textreset(Text*);
int	textresize(Text*, Rectangle, int);
void	textscrclick(Text*, int);
void	textscrdraw(Text*);
void	textscrollnl(Text*, int);
void	textselect(Text*);
int	textselect2(Text*, uint*, uint*, Text**);
int	textselect23(Text*, uint*, uint*, Image*, int);
int	textselect3(Text*, uint*, uint*);
void	textsetorigin(Text*, uint);
void	textsetselect(Text*, uint, uint);
void	textshow(Text*, uint, uint, int);
void	texttype(Text*, Rune);

struct Window
{
	QLock	lk;
	Ref	ref;
	Text		tag;
	Text		body;
	Rectangle	r;
	uchar	isdir;
	uchar	isscratch;
	uchar	filemenu;
	uchar	dirty;
	uchar	autoindent;
	uchar	showdel;
	int		id;
	Range	addr;
	Range	limit;
	uchar	nopen[QMAX];
	uchar	nomark;
	Range	wrselrange;
	int		rdselfd;
	Column	*col;
	Xfid		*eventx;
	char		*events;
	int		nevents;
	int		owner;
	int		maxlines;
	Dirlist	**dlp;
	int		ndl;
	int		putseq;
	int		nincl;
	Rune		**incl;
	Reffont	*reffont;
	QLock	ctllock;
	uint		ctlfid;
	char		*dumpstr;
	char		*dumpdir;
	int		dumpid;
	int		utflastqid;
	int		utflastboff;
	int		utflastq;
	int		tagsafe;		/* taglines is correct */
	int		tagexpand;
	int		taglines;
	Rectangle	tagtop;
	QLock	editoutlk;
};

void	winaddincl(Window*, Rune*, int);
int	winclean(Window*, int);
void	wincleartag(Window*);
void	winclose(Window*);
void	wincommit(Window*, Text*);
char*	winctlprint(Window*, char*, int);
void	windelete(Window*);
void	windirfree(Window*);
void	winevent(Window*, char*, ...);
void	wininit(Window*, Window*, Rectangle);
void	winlock(Window*, int);
void	winlock1(Window*, int);
void	winmousebut(Window*);
int	winresize(Window*, Rectangle, int, int);
void	winscroll(Window*, Text*, int);
void	winsetname(Window*, Rune*, int);
void	winsettag(Window*);
void	winsettag1(Window*);
void	wintype(Window*, Text*, Rune);
void	winundo(Window*, int);
void	winunlock(Window*);

struct Column
{
	Rectangle r;
	Text	tag;
	Row		*row;
	Window	**w;
	int		nw;
	int		safe;
};

Window*	coladd(Column*, Window*, Window*, int);
int	colclean(Column*);
void	colclose(Column*, Window*, int);
void	colcloseall(Column*);
void	coldragwin(Column*, Window*, int);
void	colgrow(Column*, Window*, int);
void	colinit(Column*, Rectangle);
void	colmousebut(Column*);
void	colresize(Column*, Rectangle);
void	colsort(Column*);
Text*	colwhich(Column*, Point);

struct Row
{
	QLock	lk;
	Rectangle r;
	Text	tag;
	Column	**col;
	int		ncol;
};

Column*	rowadd(Row*, Column *c, int);
int	rowclean(Row*);
void	rowclose(Row*, Column*, int);
void	rowdragcol(Row*, Column*, int);
void	rowdump(Row*, char*);
void	rowinit(Row*, Rectangle);
int	rowload(Row*, char*, int);
void	rowloadfonts(char*);
void	rowresize(Row*, Rectangle);
Text*	rowtype(Row*, Rune, Point);
Text*	rowwhich(Row*, Point);
Column*	rowwhichcol(Row*, Point);

struct Timer
{
	int		dt;
	int		cancel;
	Channel	*c;	/* chan(int) */
	Timer	*next;
};

struct Command
{
	int		pid;
	Rune		*name;
	int		nname;
	char		*text;
	char		**av;
	int		iseditcmd;
	Mntdir	*md;
	Command	*next;
};

struct Dirtab
{
	char	*name;
	uchar	type;
	uint	qid;
	uint	perm;
};

struct Mntdir
{
	int		id;
	int		ref;
	Rune		*dir;
	int		ndir;
	Mntdir	*next;
	int		nincl;
	Rune		**incl;
};

struct Fid
{
	int		fid;
	int		busy;
	int		open;
	Qid		qid;
	Window	*w;
	Dirtab	*dir;
	Fid		*next;
	Mntdir	*mntdir;
	int		nrpart;
	uchar	rpart[UTFmax];
	vlong	logoff;	// for putlog
};

struct Xfid
{
	void		*arg;	/* args to xfidinit */
	Fcall	fcall;
	Xfid	*next;
	Channel	*c;		/* chan(void(*)(Xfid*)) */
	Fid	*f;
	uchar	*buf;
	int	flushed;
};

void	xfidclose(Xfid*);
void	xfidctl(void *);
void	xfidctlwrite(Xfid*, Window*);
void	xfideventread(Xfid*, Window*);
void	xfideventwrite(Xfid*, Window*);
void	xfidflush(Xfid*);
void	xfidindexread(Xfid*);
void	xfidlog(Window*, char*);
void	xfidlogflush(Xfid*);
void	xfidlogopen(Xfid*);
void	xfidlogread(Xfid*);
void	xfidopen(Xfid*);
void	xfidread(Xfid*);
int	xfidruneread(Xfid*, Text*, uint, uint);
void	xfidutfread(Xfid*, Text*, uint, int);
void	xfidwrite(Xfid*);

struct Reffont
{
	Ref	ref;
	Font	*f;
};

void		rfclose(Reffont*);
Reffont*	rfget(int, int, int, char*);

struct Rangeset
{
	Range	r[NRange];
};

struct Dirlist
{
	Rune	*r;
	int		nr;
	int		wid;
};

struct Expand
{
	uint	q0;
	uint	q1;
	Rune	*name;
	int	nname;
	char	*bname;
	int	jump;
	union{
		Text	*at;
		Rune	*ar;
	} u;
	int	(*agetc)(void*, uint);
	int	a0;
	int	a1;
};

enum
{
	/* fbufalloc() guarantees room off end of BUFSIZE */
	BUFSIZE = Maxblock+IOHDRSZ,	/* size from fbufalloc() */
	RBUFSIZE = BUFSIZE/sizeof(Rune),
	EVENTSIZE = 256,
};

#define Scrollwid scalesize(display, 12)
#define Scrollgap scalesize(display, 4)
#define Margin scalesize(display, 4)
#define Border scalesize(display, 2)
#define ButtonBorder scalesize(display, 2)

#define	QID(w,q)	((w<<8)|(q))
#define	WIN(q)	((((ulong)(q).path)>>8) & 0xFFFFFF)
#define	FILE(q)	((q).path & 0xFF)

#undef FALSE
#undef TRUE

enum
{
	FALSE,
	TRUE,
	XXX
};

enum
{
	Empty	= 0,
	Null		= '-',
	Delete	= 'd',
	Insert	= 'i',
	Replace	= 'r',
	Filename	= 'f'
};

enum	/* editing */
{
	Inactive	= 0,
	Inserting,
	Collecting
};

uint		globalincref;
uint		seq;
uint		maxtab;	/* size of a tab, in units of the '0' character */

Display		*display;
Image		*screen;
Font			*font;
Mouse		*mouse;
Mousectl		*mousectl;
Keyboardctl	*keyboardctl;
Reffont		reffont;
Image		*modbutton;
Image		*colbutton;
Image		*button;
Image		*but2col;
Image		*but3col;
Cursor		boxcursor;
Cursor2		boxcursor2;
Row			row;
int			timerpid;
Disk			*disk;
Text			*seltext;
Text			*argtext;
Text			*mousetext;	/* global because Text.close needs to clear it */
Text			*typetext;		/* global because Text.close needs to clear it */
Text			*barttext;		/* shared between mousetask and keyboardthread */
int			bartflag;
int			swapscrollbuttons;
Window		*activewin;
Column		*activecol;
Buffer		snarfbuf;
Rectangle		nullrect;
int			fsyspid;
char			*cputype;
char			*objtype;
char			*home;
char			*acmeshell;
char			*fontnames[2];
Image		*tagcols[NCOL];
Image		*textcols[NCOL];
extern char		wdir[]; /* must use extern because no dimension given */
int			editing;
int			erroutfd;
int			messagesize;		/* negotiated in 9P version setup */
int			globalautoindent;
int			dodollarsigns;
char*		mtpt;

enum
{
	Kscrolloneup		= KF|0x20,
	Kscrollonedown	= KF|0x21
};

Channel	*cplumb;		/* chan(Plumbmsg*) */
Channel	*cwait;		/* chan(Waitmsg) */
Channel	*ccommand;	/* chan(Command*) */
Channel	*ckill;		/* chan(Rune*) */
Channel	*cxfidalloc;	/* chan(Xfid*) */
Channel	*cxfidfree;	/* chan(Xfid*) */
Channel	*cnewwindow;	/* chan(Channel*) */
Channel	*mouseexit0;	/* chan(int) */
Channel	*mouseexit1;	/* chan(int) */
Channel	*cexit;		/* chan(int) */
Channel	*cerr;		/* chan(char*) */
Channel	*cedit;		/* chan(int) */
Channel	*cwarn;		/* chan(void*)[1] (really chan(unit)[1]) */

QLock	editoutlk;

#define	STACK	65536
