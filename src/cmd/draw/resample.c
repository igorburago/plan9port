#include <u.h>
#include <libc.h>
#include <limits.h>
#include <draw.h>
#include <memdraw.h>

static vlong
llsign(vlong x)
{
	return (x > 0) - (x < 0);
}

static vlong
muldivround(vlong x, vlong a, vlong b)
{
	x *= a;
	return (x + llsign(x)*llsign(b)*b/2) / b;
}

static int
memchanconvert(Memimage **img, u32int chan)
{
	Memimage *old, *new;

	old = *img;
	if(old->chan == chan)
		return 0;
	new = allocmemimage(old->r, chan);
	if(new == nil)
		return -1;
	memimagedraw(new, new->r, old, old->r.min, nil, ZP, S);
	freememimage(old);
	*img = new;
	return 0;
}

static void
usage(void)
{
	fprint(2, "usage: resample [-f filter] [-x xsize[%]] [-y ysize[%]] [imagefile]\n");
	exits("usage");
}

static int
parseint(char *s, int *pv, char *suffix, int *hassuffix)
{
	char *t;
	long v;
	int n;

	if(s == nil)
		return -1;
	v = strtol(s, &t, 10);
	if(t==s || v==LONG_MIN || v==LONG_MAX || v<INT_MIN || v>INT_MAX)
		return -1;

	if(suffix != nil){
		n = strlen(suffix);
		if(strncmp(t, suffix, n) == 0){
			*hassuffix = 1;
			t += n;
		}else
			*hassuffix = 0;
	}

	while(*t==' ' || *t=='\t')
		t++;
	if(*t != 0)
		return -1;

	*pv = v;
	return 0;
}

static u32int
fullbytechanfor(Memimage *img)
{
	if(img->depth==8*img->nchan && img->chan!=CMAP8)
		return img->chan;
	if(img->depth < 8)
		return GREY8;
	if(img->depth < 24)
		return RGB24;
	return RGBA32;
}

static char*
chanlabel(u32int chan)
{
	switch(chan){
#define X(C)	case C: return #C;
	X(GREY1)
	X(GREY2)
	X(GREY4)
	X(GREY8)
	X(CMAP8)
	X(RGB15)
	X(RGB16)
	X(RGB24)
	X(BGR24)
	X(RGBA32)
	X(ARGB32)
	X(ABGR32)
	X(XRGB32)
	X(XBGR32)
#undef X
	default:	return "unknown";
	}
}

void
main(int argc, char *argv[])
{
	struct{
		char		*name;
		Memfilter	*filter;
	} filtertab[] = {
		/* «Edit /\*\//+;+/};/- <9 sed -n 's@^(extern[ 	]+)?Memfilter[ 	]*\*[ 	]*(mem([A-Za-z_0-9]+)filter)[^A-Za-z_0-9].*@		"\3",	\2,@p' ../../libmemdraw/resample.c» */
		"box",		memboxfilter,
		"triangle",	memtrianglefilter,
		"quadspline",	memquadsplinefilter,
		"quadblend",	memquadblendfilter,
		"quadinterp",	memquadinterpfilter,
		"hermite",	memhermitefilter,
		"bspline",	membsplinefilter,
		"catrom",	memcatromfilter,
		"mitchell",	memmitchellfilter,
		"lanczos",	memlanczosfilter,
		"hamming",	memhammingfilter,
		"blackman",	memblackmanfilter,
		"harris",	memharrisfilter,
		"flattop",	memflattopfilter,
		"kaiser",	memkaiserfilter,
		"mks2013",	memmks2013filter,
		"mks2021",	memmks2021filter,
	};
	Memfilter *filter;
	int i, xsize, ysize, xpercent, ypercent;
	char *arg, *file;
	int fd;
	Memimage *img, *new;

	memimageinit();

	filter = nil;
	xsize = ysize = 0;
	xpercent = ypercent = 0;

	ARGBEGIN{
	case 'a':	/* compatibility; equivalent to just -x or -y */
		if(xsize!=0 || ysize!=0)
			usage();
		if(parseint(ARGF(), &xsize, "%", &xpercent)<0 || xsize<=0)
			usage();
		ysize = xsize;
		ypercent = xpercent;
		break;
	case 'f':
		if(filter != nil)
			usage();
		arg = ARGF();
		for(i=0; i<nelem(filtertab); i++)
			if(strcmp(arg, filtertab[i].name) == 0){
				filter = filtertab[i].filter;
				break;
			}
		if(filter == nil)
			usage();
		break;
	case 'x':
		if(xsize != 0)
			usage();
		if(parseint(ARGF(), &xsize, "%", &xpercent)<0 || xsize<=0)
			usage();
		break;
	case 'y':
		if(ysize != 0)
			usage();
		if(parseint(ARGF(), &ysize, "%", &ypercent)<0 || ysize<=0)
			usage();
		break;
	default:
		usage();
	}ARGEND

	if(xsize==0 && ysize==0)
		usage();
	if(filter == nil)
		filter = memmks2013filter;

	if(argc > 1)
		usage();
	if(argc == 1){
		file = argv[0];
		fd = open(file, OREAD);
		if(fd < 0)
			sysfatal("failed to open file %s: %r", file);
	}else{
		file = "<stdin>";
		fd = 0;
	}

	img = readmemimage(fd);
	if(img == nil)
		sysfatal("failed to read image from %s: %r", file);
	if(xpercent)
		xsize = (xsize*Dx(img->r) + 50) / 100;
	if(ypercent)
		ysize = (ysize*Dy(img->r) + 50) / 100;

#if 0

	fprint(2, "%s -> %s\n", chanlabel(img->chan), chanlabel(fullbytechanfor(img)));

	new = resamplememimage(img, Rect(0, 0, xsize, ysize), fullbytechanfor(img), filter);
	if(new == nil)
		sysfatal("failed to resample image: %r");

#else

	#if 1
		if(memchanconvert(&img, RGBA32) < 0)
			sysfatal("failed to convert input image: %r");
	#endif

	Rectangle newr = Rect(0, 0, xsize, ysize);
	if(Dx(newr) == 0)
		newr.max.x = newr.min.x + muldivround(Dx(img->r), Dy(newr), Dy(img->r));
	else if(Dy(newr) == 0)
		newr.max.y = newr.min.y + muldivround(Dy(img->r), Dx(newr), Dx(img->r));

	new = allocmemimage(newr, fullbytechanfor(img));
	if(new == nil)
		sysfatal("failed to resample image: %r");

	fprint(2, "%s -> %s\n", chanlabel(img->chan), chanlabel(fullbytechanfor(img)));
	fprint(2, "%R -> %R\n", img->r, new->r);

	enum { NREP = 25 };
	for (int rep=0; rep<NREP; rep++){
		int rc = memresample(new, new->r, img, img->r, filter);
		if(rc < 0)
			sysfatal("failed to resample image: %r");
		if(rc > 0)
			fprint(2, "nonzero return code: %d\n", rc);
	}

#endif

	if(writememimage(1, new) < 0)
		sysfatal("failed to write image: %r");

	exits(nil);
}
