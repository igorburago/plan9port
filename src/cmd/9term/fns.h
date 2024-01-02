#undef isalnum
#define isalnum runeisalnum

#define	runemalloc(n)		malloc((n)*sizeof(Rune))
#define	runerealloc(a, n)	realloc(a, (n)*sizeof(Rune))
#define	runemove(a, b, n)	memmove(a, b, (n)*sizeof(Rune))

void*	emalloc(uint);
void*	erealloc(void*, uint);
char*	estrdup(char*);

void	button2menu(Window*);
void	cvttorunes(char*, int, Rune*, int*, int*, int*);
void	error(char*);
void	iconinit(void);
int	intrc(void);
int	isalnum(Rune);
int	max(int, int);
int	min(int, int);
Window*	new(Image*, int, int, int, char*);
int	rawon(void);
void	riogetsnarf(void);
void	rioputsnarf(void);
void	riosetcursor(Cursor*, int);
char*	runetobyte(Rune*, int, int*);
Rune*	strrune(Rune*, Rune);
void	timercancel(Timer*);
void	timerinit(void);
Timer*	timerstart(int);
void	timerstop(Timer*);
int	waitformouse(Mousectl*, int);
void	winterrupt(Window*);
