#undef isalnum
#define isalnum runeisalnum

void	freescrtemps(void);
Window *new(Image*, int, int, int, char*, char*, char**);
void	riosetcursor(Cursor*, int);
int	min(int, int);
int	max(int, int);
Rune*	strrune(Rune*, Rune);
int	isalnum(Rune);
void	timerstop(Timer*);
void	timercancel(Timer*);
Timer*	timerstart(int);
void	error(char*);
void	iconinit(void);
void	*erealloc(void*, uint);
void *emalloc(uint);
char *estrdup(char*);
void	button2menu(Window*);
void	cvttorunes(char*, int, Rune*, int*, int*, int*);
char* runetobyte(Rune*, int, int*);
void	timerinit(void);
int	rawon(void);
void	winterrupt(Window*);
int	intrc(void);

#define	runemalloc(n)		malloc((n)*sizeof(Rune))
#define	runerealloc(a, n)	realloc(a, (n)*sizeof(Rune))
#define	runemove(a, b, n)	memmove(a, b, (n)*sizeof(Rune))

void rioputsnarf(void);
void riogetsnarf(void);
