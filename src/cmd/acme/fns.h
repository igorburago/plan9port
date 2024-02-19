/*
#pragma	varargck	argpos	warning	2
#pragma	varargck	argpos	warningew	2
*/

#undef isalnum
#define isalnum acmeisalnum

#define	fbufalloc()	emalloc(BUFSIZE)
#define	fbuffree(x)	free(x)

#define	runemalloc(a)		(Rune*)emalloc((a)*sizeof(Rune))
#define	runerealloc(a, b)	(Rune*)erealloc((a), (b)*sizeof(Rune))
#define	runemove(a, b, c)	memmove((a), (b), (c)*sizeof(Rune))

void*	emalloc(uint);
void*	erealloc(void*, uint);
char*	estrdup(char*);

void	acmegetsnarf(void);
void	acmeputsnarf(void);
Range	address(uint, Text*, Range, Range, void*, uint, uint, int(*)(void*, uint),  int*, uint*);
void	allwindows(void(*)(Window*, void*), void*);
Rune*	bytetorune(char*, int*);
Runestr	cleanrname(Runestr);
void	clearmouse(void);
void	cut(Text*, Text*, Text*, int, int, Rune*, int);
void	cvttorunes(char*, int, Rune*, int*, int*, int*);
Runestr	dirname(Text*, Rune*, int);
void	editcmd(Text*, Rune*, uint);
char*	edittext(Window*, int, Rune*, int);
void	error(char*);
Window*	errorwin(Mntdir*, int);
Window*	errorwinforwin(Window*);
void	execute(Text*, uint, uint, int, Text*);
int	expand(Text*, uint, uint, Expand*);
Rune*	findbl(Rune*, int, int*);
void	flushwarnings(void);
void	fontx(Text*, Text*, Text*, int, int, Rune*, int);
void	fsysclose(void);
void	fsysdelid(Mntdir*);
void	fsysincid(Mntdir*);
void	fsysinit(void);
Mntdir*	fsysmount(Rune*, int, Rune**, int);
void	get(Text*, Text*, Text*, int, int, Rune*, int);
char*	getarg(Text*, int, int, Rune**, int*);
char*	getbytearg(Text*, int, int, char**);
void	iconinit(void);
int	isaddrc(int);
int	isalnum(Rune);
int	isfilec(Rune);
int	ismtpt(char*);
int	isregexc(int);
void	killprocs(void);
uint	loadfile(int, uint, int*, int(*)(void*, uint, Rune*, int), void*, DigestState*);
void	look3(Text*, uint, uint, int);
Window*	lookfile(Rune*, int);
Window*	lookid(int, int);
Window*	makenewwindow(Text *t);
uint	max(uint, uint);
uint	min(uint, uint);
void	movetodel(Window*);
void	new(Text*, Text*, Text*, int, int, Rune*, int);
long	nlcount(Text*, long, long, long*);
long	nlcounttopos(Text*, long, long, long);
Rune*	parsetag(Window*, int, int*);
void	paste(Text*, Text*, Text*, int, int, Rune*, int);
void	plumblook(Plumbmsg*);
void	plumbshow(Plumbmsg*);
void	put(Text*, Text*, Text*, int, int, Rune*, int);
void	putfile(File*, int, int, Rune*, int);
Range	range(int, int);
Xfid*	respond(Xfid*, Fcall*, char*);
int	restoremouse(Window*);
int	rgetc(void*, uint);
void	run(Window*, char*, Rune*, int, int, char*, char*, int);
int	runeeq(Rune*, uint, Rune*, uint);
Runestr	runestr(Rune*, uint);
char*	runetobyte(Rune*, int);
int	rxbexecute(Text*, uint, Rangeset*);
int	rxcompile(Rune*);
int	rxexecute(Text*, Rune*, uint, uint, Rangeset*);
void	rxinit(void);
int	rxnull(void);
void	savemouse(Window*);
void	scrlresize(void);
int	search(Text*, Rune*, uint);
uint	seqof(Window*, int);
void	setcurtext(Text*, int);
Rune*	skipbl(Rune*, int, int*);
void	startplumbing(void);
int	tempfile(void);
int	tgetc(void*, uint);
void	timercancel(Timer*);
void	timerinit(void);
Timer*	timerstart(int);
void	timerstop(Timer*);
void	undo(Text*, Text*, Text*, int, int, Rune*, int);
int	waitformouse(Mousectl*, int);
void	warning(Mntdir*, char*, ...);
void	warningew(Window*, Mntdir*, char*, ...);
