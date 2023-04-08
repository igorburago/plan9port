#ifndef _EVENT_H_
#define _EVENT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Event	Event;
typedef struct Menu	Menu;

enum
{
	Emouse		= 1,
	Ekeyboard	= 2
};

enum
{
	MAXSLAVE	= 32,
	EMAXMSG		= 128+8192	/* size of 9p header+data */
};

enum
{
	Mbutton1		= 1<<0,	/* left button */
	Mbutton2		= 1<<1,	/* middle button */
	Mbutton3		= 1<<2,	/* right button */
	Mlinescroll		= 1<<3,	/* coarse scroll in line increments */
	Mpixelscroll		= 1<<4,	/* precise scroll in pixel increments */
	Mscrollmotionstart	= 1<<5,	/* upcoming Mpixelscrolls come from one gesture */
	Mscrollmotionstop	= 1<<6,	/* further Mpixelscrolls are standalone again */
	Mscrollinertiastart	= 1<<7,	/* like a regular motion, but for inertial scrolling */
	Mscrollinertiastop	= 1<<8,	/* likewise, back to standalone scrolls */

	Mbuttonsmask	= Mbutton1 | Mbutton2 | Mbutton3,
	Mscrollsmask	= Mlinescroll | Mpixelscroll |
		Mscrollmotionstart | Mscrollmotionstop |
		Mscrollinertiastart | Mscrollinertiastop
};

struct Mouse
{
	int	buttons;	/* event bit array (see bit assignment above) */
	int	scroll;		/* signed scroll distance in pixels or lines (as per event) */
	Point	xy;
	uint	msec;
};

struct Event
{
	int	kbdc;
	Mouse	mouse;
	int	n;		/* number of characters in message */
	void	*v;		/* data unpacked by general event-handling function */
	uchar	data[EMAXMSG];	/* message from an arbitrary file descriptor */
};

struct Menu
{
	char	**item;
	char	*(*gen)(int);
	int	lasthit;
};

/*
 * Events
 */
extern void		einit(ulong);
extern ulong		estart(ulong, int, int);
extern ulong		estartfn(ulong, int, int, int (*fn)(int, Event*, uchar*, int));
extern ulong		etimer(ulong, int);
extern ulong		event(Event*);
extern ulong		eread(ulong, Event*);
extern Mouse		emouse(void);
extern int		ekbd(void);
extern int		ecanread(ulong);
extern int		ecanmouse(void);
extern int		ecankbd(void);
extern void		eresized(int);	/* supplied by user */
extern int		emenuhit(int, Mouse*, Menu*);
extern int		eatomouse(Mouse*, char*, int);
extern Rectangle	getrect(int, Mouse*);
struct Cursor;
struct Cursor2;
extern void		esetcursor(struct Cursor*);
extern void		esetcursor2(struct Cursor*, struct Cursor2*);
extern void		emoveto(Point);
extern Rectangle	egetrect(int, Mouse*);
extern void		edrawgetrect(Rectangle, int);
extern int		ereadmouse(Mouse*);
extern int		eatomouse(Mouse*, char*, int);

#if defined(__cplusplus)
}
#endif
#endif
