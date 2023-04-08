#ifndef _MOUSE_H_
#define _MOUSE_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Menu	Menu;
typedef struct Mousectl	Mousectl;

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

/*
 * Mousectl.resizec is buffered in case the client is waiting for
 * a mouse action before handling resize.
 */
struct Mousectl
{
	Mouse		m;
	struct Channel	*c;		/* chan(Mouse) */
	struct Channel	*resizec;	/* chan(int)[2] */
	Display		*display;	/* associated display */
};

struct Menu
{
	char	**item;
	char	*(*gen)(int);
	int	lasthit;
};

/*
 * Mouse
 */
extern Mousectl*	initmouse(char*, Image*);
extern void		moveto(Mousectl*, Point);
extern int		readmouse(Mousectl*);
extern void		closemouse(Mousectl*);
struct Cursor;
struct Cursor2;
extern void		setcursor(Mousectl*, struct Cursor*);
extern void		setcursor2(Mousectl*, struct Cursor*, struct Cursor2*);
extern void		drawgetrect(Rectangle, int);
extern Rectangle	getrect(int, Mousectl*);
extern int		menuhit(int, Mousectl*, Menu*, Screen*);

extern void		bouncemouse(Mouse*);
extern int		_windowhasfocus;	/* XXX do better */
extern int		_wantfocuschanges;

#if defined(__cplusplus)
}
#endif
#endif
