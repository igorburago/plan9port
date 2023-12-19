#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

typedef struct Area		Area;
typedef struct Sample		Sample;

#define PIf	(3.14159265f)

struct Area
{
	Memimage	*img;
	Rectangle	nomr;	/* nominal: defines scale; may be outside of the image bounds */
	Rectangle	opr;	/* operational: nomr clipped to img->r and img->clipr */
	uchar		*start;	/* first byte corresponding to opr.min */
	int		scan;	/* size of the image scan line in bytes */
	int		lacking;	/* whether any pixels inside opr are devoid of content */
};

struct Sample
{
	int	i;
	float	w;
};

static float	boxfn(float, float, float[]);
static float	trianglefn(float, float, float[]);
static float	dodgsonfn(float, float, float[]);
static float	bcsplinefn(float, float, float[]);
static float	lanczosfn(float, float, float[]);
static float	sinc4cosfn(float, float, float[]);
static float	kaiserfn(float, float, float[]);
static float	mks2013fn(float, float, float[]);
static float	mks2021fn(float, float, float[]);

/* Basic filters */
static Memfilter	boxfilter	= { boxfn,	0.5f };
static Memfilter	trianglefilter	= { trianglefn,	1.0f };
/* Quadratic filters */
static Memfilter	quadinterpfilter	= { dodgsonfn,	1.5f, { 1.0f } };
static Memfilter	quadsplinefilter	= { dodgsonfn,	1.5f, { 0.5f } };
static Memfilter	quadblendfilter		= { dodgsonfn,	1.5f, { 0.8f } };
/* Cubic filters */
static Memfilter	hermitefilter	= { bcsplinefn,	2.0f,	{ 0.0f, 0.0f } };
static Memfilter	bsplinefilter	= { bcsplinefn,	2.0f,	{ 1.0f, 0.0f } };
static Memfilter	catromfilter	= { bcsplinefn,	2.0f,	{ 0.0f, 0.5f } };
static Memfilter	mitchellfilter	= { bcsplinefn,	2.0f,	{ 1/3.0f, 1/3.0f } };
/* Windowed sinc filters */
static Memfilter	lanczosfilter	= { lanczosfn,	3.0f };
static Memfilter	hammingfilter	= { sinc4cosfn,	3.0f,	{ 0.54f, 0.46f } };
static Memfilter	blackmanfilter	= { sinc4cosfn,	3.0f,	{ 0.42f, 0.5f, 0.08f } };
static Memfilter	harrisfilter	= { sinc4cosfn,	3.0f,	{ 0.35875f, 0.48829f, 0.14128f, 0.01168f } };
static Memfilter	flattopfilter	= { sinc4cosfn,	3.0f,	{ 0.21557895f, 0.41663158f, 0.277263158f, 0.083578947f, 0.006947368f } };
static Memfilter	kaiserfilter	= { kaiserfn,	3.0f,	{ 4.0f /*β*/, 0.08848053f /*1/I₀(β)*/ } };
/* Magic Kernel Sharp filters (https://johncostella.com/magic/) */
static Memfilter	mks2013filter	= { mks2013fn,	2.5f };
static Memfilter	mks2021filter	= { mks2021fn,	4.5f };

Memfilter	*memboxfilter		= &boxfilter;
Memfilter	*memtrianglefilter	= &trianglefilter;

Memfilter	*memquadsplinefilter	= &quadsplinefilter;
Memfilter	*memquadblendfilter	= &quadblendfilter;
Memfilter	*memquadinterpfilter	= &quadinterpfilter;

Memfilter	*memhermitefilter	= &hermitefilter;
Memfilter	*membsplinefilter	= &bsplinefilter;
Memfilter	*memcatromfilter	= &catromfilter;
Memfilter	*memmitchellfilter	= &mitchellfilter;

Memfilter	*memlanczosfilter	= &lanczosfilter;
Memfilter	*memhammingfilter	= &hammingfilter;
Memfilter	*memblackmanfilter	= &blackmanfilter;
Memfilter	*memharrisfilter	= &harrisfilter;
Memfilter	*memflattopfilter	= &flattopfilter;
Memfilter	*memkaiserfilter	= &kaiserfilter;

Memfilter	*memmks2013filter	= &mks2013filter;
Memfilter	*memmks2021filter	= &mks2021filter;

static const Area Nilarea;

static Rectangle
Rptdim(Point p, int w, int h)
{
	return Rect(p.x, p.y, p.x+w, p.y+h);
}

static int
min(int x, int y)
{
	return x < y ? x : y;
}

static int
max(int x, int y)
{
	return x > y ? x : y;
}

static uchar
byteclamp(float v)
{
	return v <= 0 ? 0 : min((int)(v+0.5f), 0xFF);
}

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

static vlong
muldivfloor(vlong x, vlong a, vlong b)
{
	x *= a;
	return (x - (llsign(x)*llsign(b)<0)*(b-llsign(b))) / b;
}

static vlong
muldivceil(vlong x, vlong a, vlong b)
{
	x *= a;
	return (x + (llsign(x)*llsign(b)>0)*(b-llsign(b))) / b;
}

/* sinc(x) := sin(πx)/(πx) — normalized sinc function */
static float
nsincf(float x)
{
	if(x == 0)
		return 1;
	x *= PIf;
	return sinf(x) / x;
}

/* I₀(x): modified bessel function of the first kind */
static float
besseli0f(float x)
{
	float s0, s, term, y;
	int i;

	s0 = 0;
	s = term = 1;
	for(i=1; s!=s0; i++){
		y = x/i;
		term *= y*y/4;
		s0 = s;
		s += term;
	}
	return s;
}

#define FILTERPREAMBLE \
	float t; \
	USED(params); \
	x = fabsf(x); \
	if(x > support) \
		return 0; \
	t = x / support;

static float
boxfn(float x, float support, float params[])
{
	FILTERPREAMBLE; USED(t);
	return 1;
}

static float
trianglefn(float x, float support, float params[])
{
	FILTERPREAMBLE;
	return 1 - t;
}

static float
dodgsonfn(float x, float support, float params[])
{
	float r;

	FILTERPREAMBLE;
	r = params[0];
	t *= 1.5f;
	if(t < 0.5f)
		return -2*r*t*t + 0.5f*(r+1);
	return (r*(t-2)-0.5f)*t + 0.75f*(r+1);
}

static float
bcsplinefn(float x, float support, float params[])
{
	float b, c;

	FILTERPREAMBLE;
	b = params[0];
	c = params[1];
	t *= 2;
	if(t <= 1)
		return (((12-9*b-6*c)*t + (-18+12*b+6*c))*t*t + (6-2*b))/6;
	return ((((-b-6*c)*t + (6*b+30*c))*t + (-12*b-48*c))*t + (8*b+24*c))/6;
}

static float
lanczosfn(float x, float support, float params[])
{
	FILTERPREAMBLE;
	return nsincf(x) * nsincf(t);
}

static float
sinc4cosfn(float x, float support, float params[])
{
	float w;

	FILTERPREAMBLE;
	w = params[0] + params[1]*cosf(t*PIf) + params[2]*cosf(t*2*PIf);
	if(params[3] != 0)
		w += params[3] * cosf(t*3*PIf);
	if(params[4] != 0)
		w += params[4] * cosf(t*4*PIf);
	return nsincf(x) * w;
}

static float
kaiserfn(float x, float support, float params[])
{
	float beta, oneoveri0ofbeta;

	FILTERPREAMBLE;
	beta = params[0];
	oneoveri0ofbeta = params[1];
	return nsincf(x) * (besseli0f(beta*sqrtf(1-t*t)) * oneoveri0ofbeta);
}

static float
mks2013fn(float x, float support, float params[])
{
	FILTERPREAMBLE;
	if(t <= 0.2f)
		return (17 - 175*t*t)/16;
	if(t <= 0.6f)
		return ((50*t - 55)*t + 14)/8;
	t = 1 - t;
	return -25*t*t/32;
}

static float
mks2021fn(float x, float support, float params[])
{
	FILTERPREAMBLE;
	if(t <= 1/9.0f)
		return (577 - 19359*t*t)/576;
	if(t <= 3/9.0f)
		return ((5670*t - 3411)*t + 478)/288;
	if(t <= 5/9.0f)
		return -((972*t - 1017)*t + 260)/288;
	if(t <= 7/9.0f)
		return ((162*t - 243)*t + 70)/288;
	return -((162*t - 324)*t + 162)/2304;
}

#undef FILTERPREAMBLE

static int
emptyrect(Rectangle r)
{
	return r.min.x>=r.max.x || r.min.y>=r.max.y;
}

static int
areasoverlap(Area a, Area b)
{
	return a.img!=nil && a.img==b.img && rectXrect(a.opr, b.opr);
}

static void
copypixels(Area dst, Area src)
{
	Rectangle dstclipr, srcclipr;

	if(dst.img==nil || src.img==nil)
		return;
	dstclipr = dst.img->clipr;
	srcclipr = src.img->clipr;
	dst.img->clipr = dst.opr;
	src.img->clipr = src.opr;
	memimagedraw(dst.img, dst.nomr, src.img, src.nomr.min, nil, ZP, S);
	dst.img->clipr = dstclipr;
	src.img->clipr = srcclipr;
	dst.lacking = src.lacking ||
		!rectinrect(rectsubpt(dst.opr, dst.nomr.min), rectsubpt(src.opr, src.nomr.min));
}

static Area
area(Memimage *img, Rectangle r)
{
	Area a;

	memset(&a, 0, sizeof(a));
	if(img==nil || emptyrect(img->r) || emptyrect(r))
		return a;
	a.img = img;
	a.nomr = r;
	if(!rectclip(&r, img->r) || emptyrect(img->clipr) || !rectclip(&r, img->clipr))
		return a;
	a.opr = r;
	a.start = byteaddr(img, r.min);
	a.scan = img->width * sizeof(u32int);
	return a;
}

static Rectangle
outerscaledrect(Rectangle r, Rectangle refr, int newrefw, int newrefh)
{
	r = rectsubpt(r, refr.min);
	r.min.x = muldivfloor(r.min.x, newrefw, Dx(refr));
	r.min.y = muldivfloor(r.min.y, newrefh, Dy(refr));
	r.max.x = muldivceil(r.max.x, newrefw, Dx(refr));
	r.max.y = muldivceil(r.max.y, newrefh, Dy(refr));
	return rectaddpt(r, refr.min);
}

static Area
allocscaledarea(Area base, int nomw, int nomh, u32int chan)
{
	Area a;
	Rectangle opr;

	opr = outerscaledrect(base.opr, base.nomr, nomw, nomh);
	a = area(allocmemimage(opr, chan), opr);
	if(a.img == nil)
		return a;
	a.nomr = Rptdim(base.nomr.min, nomw, nomh);
	memfillcolor(a.img, DTransparent);
	return a;
}

static Area
getoutputarea(Area in, int outnomw, int outnomh, Area dst, Memimage **aux)
{
	Area out;

	if(outnomw==Dx(dst.nomr) && outnomh==Dy(dst.nomr)
	&& in.img->chan==dst.img->chan && !areasoverlap(in, dst)){
		out = dst;
		*aux = nil;
	}else{
		out = allocscaledarea(dst, outnomw, outnomh, in.img->chan);
		if(out.img == nil)
			return out;
		*aux = out.img;
	}
	out.lacking = 1;
	return out;
}

static int
fullbytechan(Memimage *img)
{
	return img->depth==8*img->nchan && img->chan!=CMAP8;
}

static Area
ensurefullbytechan(Area in, Memimage **aux)
{
	u32int chan;
	Area out;

	*aux = nil;
	if(in.img==nil || fullbytechan(in.img))
		return in;

	if(in.img->depth < 8)
		chan = GREY8;
	else if(in.img->depth < 24)
		chan = RGB24;
	else
		chan = RGBA32;
	out = allocscaledarea(in, Dx(in.nomr), Dy(in.nomr), chan);
	if(out.img == nil)
		return out;
	*aux = out.img;

	copypixels(out, in);
	return out;
}

static int
chanbyteoffsets(Memimage *img, int *ncolors, int *coloroff, int *alphaoff)
{
	int hasalpha, hasignore, noncoloroff;

	if(!fullbytechan(img))
		return -1;
	hasalpha = ((img->flags & Falpha) != 0);
	hasignore = (img->mask[CIgnore] != 0);
	if(hasalpha && hasignore)
		return -1;
	*alphaoff = hasalpha ? img->shift[CAlpha]/8 : -1;
	noncoloroff = hasignore ? img->shift[CIgnore]/8 : *alphaoff;
	*coloroff = 0 + (noncoloroff == 0);
	*ncolors = img->nchan - (noncoloroff >= 0);
	if(*ncolors!=1 && *ncolors!=3)
		return -1;
	return 0;
}

static Sample*
samplefilter(Memfilter *f, int outmin, int outmax, float inscale,
	int inmin, int inmax, int instride, int *outlacking)
{
	float wzerotol, fscale, radius, mid, wsum, wcutoff;
	Sample *samples, *s;
	int lacking, o, beg, end, i, k, n;

	wzerotol = 1.0f/(1<<10);	/* zero threshold for normalized weights */

	fscale = fmaxf(inscale, 1);
	radius = fscale * f->support;
	if(!isfinite(radius) || radius<=0)
		return nil;
	fscale = 1/fscale;

	n = (outmax - outmin) * (1 + 2*(int)ceilf(radius));
	samples = calloc(n, sizeof(samples[0]));
	if(samples == nil){
		*outlacking = 1;
		return nil;
	}

	lacking = 0;
	for(s=samples+1, o=outmin; o<outmax; o++, s+=n+1){
		mid = (o + 0.5f)*inscale - 0.5f;
		beg = max((int)ceilf(mid-radius), inmin);
		end = min((int)ceilf(mid+radius), inmax);
		if(beg >= end){
			s[-1].i = n = 0;
			lacking = 1;
			continue;
		}
		wsum = 0;
		for(k=0, i=beg; i<end; i++, k++){
			s[k].i = (i - inmin) * instride;
			s[k].w = (*f->kernel)((i-mid)*fscale, f->support, f->params);
			wsum += s[k].w;
		}
		wcutoff = wzerotol * fabsf(wsum);
		n = 0;
		for(k=0; k<end-beg; k++){
			if(fabsf(s[k].w) < wcutoff)
				wsum -= s[k].w;
			else
				s[n++] = s[k];
		}
		/*
		 * Individual weights may be negative for some filters (e.g., sinc-based
		 * ones), but their sum should always be strictly positive. If it is zero
		 * or negative, either the filter is malformed or its window is truncated
		 * to very few samples due to an extreme shift or scale of the output
		 * coordinate range relative to the input one. In that case, we just fall
		 * back to using the nearest input as the only sample.
		 */
		if(n==0 || !isfinite(wsum) || wsum<=0){
			i = max(inmin, min((int)ceilf(mid), inmax-1));
			s[0].i = (i - inmin) * instride;
			s[0].w = wsum = 1;
			n = 1;
		}
		s[-1].i = n;
		for(k=0; k<n; k++)
			s[k].w /= wsum;
	}
	*outlacking = lacking;
	return samples;
}

static const float floatinvofbyte[0x100] =
{
#define X(h) 1.0f/(0x##h),
	0,   X(01)X(02)X(03)X(04)X(05)X(06)X(07)X(08)X(09)X(0A)X(0B)X(0C)X(0D)X(0E)X(0F)
	X(10)X(11)X(12)X(13)X(14)X(15)X(16)X(17)X(18)X(19)X(1A)X(1B)X(1C)X(1D)X(1E)X(1F)
	X(20)X(21)X(22)X(23)X(24)X(25)X(26)X(27)X(28)X(29)X(2A)X(2B)X(2C)X(2D)X(2E)X(2F)
	X(30)X(31)X(32)X(33)X(34)X(35)X(36)X(37)X(38)X(39)X(3A)X(3B)X(3C)X(3D)X(3E)X(3F)
	X(40)X(41)X(42)X(43)X(44)X(45)X(46)X(47)X(48)X(49)X(4A)X(4B)X(4C)X(4D)X(4E)X(4F)
	X(50)X(51)X(52)X(53)X(54)X(55)X(56)X(57)X(58)X(59)X(5A)X(5B)X(5C)X(5D)X(5E)X(5F)
	X(60)X(61)X(62)X(63)X(64)X(65)X(66)X(67)X(68)X(69)X(6A)X(6B)X(6C)X(6D)X(6E)X(6F)
	X(70)X(71)X(72)X(73)X(74)X(75)X(76)X(77)X(78)X(79)X(7A)X(7B)X(7C)X(7D)X(7E)X(7F)
	X(80)X(81)X(82)X(83)X(84)X(85)X(86)X(87)X(88)X(89)X(8A)X(8B)X(8C)X(8D)X(8E)X(8F)
	X(90)X(91)X(92)X(93)X(94)X(95)X(96)X(97)X(98)X(99)X(9A)X(9B)X(9C)X(9D)X(9E)X(9F)
	X(A0)X(A1)X(A2)X(A3)X(A4)X(A5)X(A6)X(A7)X(A8)X(A9)X(AA)X(AB)X(AC)X(AD)X(AE)X(AF)
	X(B0)X(B1)X(B2)X(B3)X(B4)X(B5)X(B6)X(B7)X(B8)X(B9)X(BA)X(BB)X(BC)X(BD)X(BE)X(BF)
	X(C0)X(C1)X(C2)X(C3)X(C4)X(C5)X(C6)X(C7)X(C8)X(C9)X(CA)X(CB)X(CC)X(CD)X(CE)X(CF)
	X(D0)X(D1)X(D2)X(D3)X(D4)X(D5)X(D6)X(D7)X(D8)X(D9)X(DA)X(DB)X(DC)X(DD)X(DE)X(DF)
	X(E0)X(E1)X(E2)X(E3)X(E4)X(E5)X(E6)X(E7)X(E8)X(E9)X(EA)X(EB)X(EC)X(ED)X(EE)X(EF)
	X(F0)X(F1)X(F2)X(F3)X(F4)X(F5)X(F6)X(F7)X(F8)X(F9)X(FA)X(FB)X(FC)X(FD)X(FE)X(FF)
#undef X
};

#define FOR1(i,x)	do{{enum{i=0};(x);}}while(0)
#define FOR3(i,x)	do{{enum{i=0};(x);}{enum{i=1};(x);}{enum{i=2};(x);}}while(0)

static int
resample(int ncolors, int coloroff, int alphaoff, int nruns, int outrunlen,
	uchar *outrun, int outrunstride, int outelemstride,
	uchar *inrun, int inrunstride, Sample *samples)
{
	int hasalpha;
	Sample *s;
	uchar *in, *out;
	float mixalpha, mix[3], w;
	int l, n;

	hasalpha = (alphaoff >= 0);
	inrun += coloroff;
	outrun += coloroff;
	alphaoff -= coloroff;

	#define OPAQUERESAMPLE(NCOLORS) \
		do{ \
			for(; nruns!=0; nruns--, inrun+=inrunstride, outrun+=outrunstride) \
				for(s=samples, out=outrun, l=outrunlen; l!=0; l--, out+=outelemstride){ \
					FOR##NCOLORS(c, mix[c] = 0); \
					for(n=s++->i; n!=0; n--, s++){ \
						in = inrun + s->i; \
						w = s->w; \
						FOR##NCOLORS(c, mix[c] += w * in[c]); \
					} \
					FOR##NCOLORS(c, out[c] = byteclamp(mix[c])); \
				} \
		}while(0)
	#define ALPHARESAMPLE(NCOLORS) \
		do{ \
			for(; nruns!=0; nruns--, inrun+=inrunstride, outrun+=outrunstride) \
				for(s=samples, out=outrun, l=outrunlen; l!=0; l--, out+=outelemstride){ \
					mixalpha = 0; \
					FOR##NCOLORS(c, mix[c] = 0); \
					for(n=s++->i; n!=0; n--, s++){ \
						in = inrun + s->i; \
						w = s->w * in[alphaoff]; \
						mixalpha += w; \
						FOR##NCOLORS(c, mix[c] += w * in[c]); \
					} \
					out[alphaoff] = byteclamp(mixalpha); \
					w = floatinvofbyte[out[alphaoff]]; \
					FOR##NCOLORS(c, out[c] = byteclamp(w * mix[c])); \
				} \
		}while(0)
	if(hasalpha)
		switch(ncolors){
		case 1: ALPHARESAMPLE(1); break;
		case 3: ALPHARESAMPLE(3); break;
		}
	else
		switch(ncolors){
		case 1: OPAQUERESAMPLE(1); break;
		case 3: OPAQUERESAMPLE(3); break;
		}
	#undef OPAQUERESAMPLE
	#undef ALPHARESAMPLE

	return 0;
}

static int
transversescanresample(int ncolors, int coloroff, int alphaoff, int nruns, int outrunlen,
	uchar *outrun, int outrunstride, int outelemstride,
	uchar *inrun, int inrunstride, Sample *samples)
{
	int hasalpha;
	Sample *s;
	uchar *in, *out;
	float *mixes, *mix, sw, w;
	int mixessize, n, l;

	hasalpha = (alphaoff >= 0);
	inrun += coloroff;
	outrun += coloroff;
	alphaoff -= coloroff;

	/* Accumulators for the same element index across all runs. */
	mixessize = nruns * (ncolors + hasalpha) * sizeof(*mixes);
	mixes = malloc(mixessize);
	if(mixes == nil)
		return -1;

	#define TSOPAQUERESAMPLE(NCOLORS) \
		do{ \
			enum { ncolors = (NCOLORS) }; \
			for(s=samples; outrunlen!=0; outrunlen--, outrun+=outelemstride){ \
				memset(mixes, 0, mixessize); \
				for(n=s++->i; n!=0; n--, s++){ \
					w = s->w; \
					for(in=inrun+s->i, mix=mixes, l=nruns; l!=0; l--, mix+=ncolors, in+=inrunstride) \
						FOR##NCOLORS(c, mix[c] += w * in[c]); \
				} \
				for(out=outrun, mix=mixes, l=nruns; l!=0; l--, mix+=ncolors, out+=outrunstride) \
					FOR##NCOLORS(c, out[c] = byteclamp(mix[c])); \
			} \
		}while(0)
	#define TSALPHARESAMPLE(NCOLORS) \
		do{ \
			enum { ncolors = (NCOLORS), A = ncolors }; \
			for(s=samples; outrunlen!=0; outrunlen--, outrun+=outelemstride){ \
				memset(mixes, 0, mixessize); \
				for(n=s++->i; n!=0; n--, s++){ \
					sw = s->w; \
					for(in=inrun+s->i, mix=mixes, l=nruns; l!=0; l--, mix+=ncolors+1, in+=inrunstride){ \
						w = sw * in[alphaoff]; \
						mix[A] += w; \
						FOR##NCOLORS(c, mix[c] += w * in[c]); \
					} \
				} \
				for(out=outrun, mix=mixes, l=nruns; l!=0; l--, mix+=ncolors+1, out+=outrunstride){ \
					out[alphaoff] = byteclamp(mix[A]); \
					w = floatinvofbyte[out[alphaoff]]; \
					FOR##NCOLORS(c, out[c] = byteclamp(w * mix[c])); \
				} \
			} \
		}while(0)
	if(hasalpha)
		switch(ncolors){
		case 1: TSALPHARESAMPLE(1); break;
		case 3: TSALPHARESAMPLE(3); break;
		}
	else
		switch(ncolors){
		case 1: TSOPAQUERESAMPLE(1); break;
		case 3: TSOPAQUERESAMPLE(3); break;
		}
	#undef TSOPAQUERESAMPLE
	#undef TSALPHARESAMPLE

	free(mixes);
	return 0;
}

#undef FOR1
#undef FOR3

static Area
xresamplearea(Memfilter *f, Area in, Area dst, Memimage **aux)
{
	int ncolors, coloroff, alphaoff, lackingsamples, rc;
	Area out;
	Rectangle inr0, outr0;
	int ymin, ymax;
	Sample *samples;

	*aux = nil;
	if(in.img==nil || Dx(in.nomr)==Dx(dst.nomr))
		return in;
	if(chanbyteoffsets(in.img, &ncolors, &coloroff, &alphaoff) < 0)
		return Nilarea;
	out = getoutputarea(in, Dx(dst.nomr), Dy(in.nomr), dst, aux);
	if(out.img == nil)
		return Nilarea;

	inr0 = rectsubpt(in.opr, in.nomr.min);
	outr0 = rectsubpt(out.opr, out.nomr.min);
	ymin = max(inr0.min.y, outr0.min.y);
	ymax = min(inr0.max.y, outr0.max.y);
	if(ymin >= ymax)
		return out;

	samples = samplefilter(f, outr0.min.x, outr0.max.x, (float)Dx(in.nomr)/Dx(out.nomr),
		inr0.min.x, inr0.max.x, in.img->nchan, &lackingsamples);
	if(samples == nil)
		return Nilarea;
	out.lacking = in.lacking || lackingsamples || ymin!=outr0.min.y || ymax!=outr0.max.y;

	rc = resample(ncolors, coloroff, alphaoff, ymax-ymin, Dx(outr0),
		out.start+(ymin-outr0.min.y)*out.scan, out.scan, out.img->nchan,
		in.start+(ymin-inr0.min.y)*in.scan, in.scan, samples);
	if(rc < 0)
		out = Nilarea;

	free(samples);
	return out;
}

static Area
yresamplearea(Memfilter *f, Area in, Area dst, Memimage **aux)
{
	enum { Minscanlinetogorowbyrow = 1024 };
	int ncolors, coloroff, alphaoff, lackingsamples, rc;
	Area out;
	Rectangle inr0, outr0;
	int xmin, xmax;
	Sample *samples;

	*aux = nil;
	if(in.img==nil || Dy(in.nomr)==Dy(dst.nomr))
		return in;
	if(chanbyteoffsets(in.img, &ncolors, &coloroff, &alphaoff) < 0)
		return Nilarea;
	out = getoutputarea(in, Dx(in.nomr), Dy(dst.nomr), dst, aux);
	if(out.img == nil)
		return Nilarea;

	inr0 = rectsubpt(in.opr, in.nomr.min);
	outr0 = rectsubpt(out.opr, out.nomr.min);
	xmin = max(inr0.min.x, outr0.min.x);
	xmax = min(inr0.max.x, outr0.max.x);
	if(xmin >= xmax)
		return out;

	samples = samplefilter(f, outr0.min.y, outr0.max.y, (float)Dy(in.nomr)/Dy(out.nomr),
		inr0.min.y, inr0.max.y, in.scan, &lackingsamples);
	if(samples == nil)
		return Nilarea;
	out.lacking = in.lacking || lackingsamples || xmin!=outr0.min.x || xmax!=outr0.max.x;

	/*
	 * The natural order of resampling along the Y-axis is to loop over columns
	 * first, output rows second, and input samples for a given output pixel third,
	 * thus scanning input column by column, as well. However, for long scan lines,
	 * it is faster to loop over output rows first, input rows containing samples
	 * for a given output row second, and columns third, so that samples belonging
	 * to the same input scan line are always visited together for better cache
	 * utilization. Intermediate floating-point accumulators for one output row are
	 * stored in an auxiliary buffer and then transferred to that output row once
	 * all input rows to be sampled for it are scanned. This extra work is exactly
	 * why this row-by-row processing is slightly slower when scan lines are short;
	 * hence the switch between the two resampling routines below.
	 */
	rc = (in.scan<Minscanlinetogorowbyrow ? resample : transversescanresample)(
		ncolors, coloroff, alphaoff, xmax-xmin, Dy(outr0),
		out.start+(xmin-outr0.min.x)*out.img->nchan, out.img->nchan, out.scan,
		in.start+(xmin-inr0.min.x)*in.img->nchan, in.img->nchan, samples);
	if(rc < 0)
		out = Nilarea;

	free(samples);
	return out;
}

int
memresample(Memimage *dstimg, Rectangle dstr, Memimage *srcimg, Rectangle srcr, Memfilter *f)
{
	char *err;
	Area src, dst, cur;
	Memimage *aux[3];
	int naux;

	err = nil;
	src = area(srcimg, srcr);
	dst = area(dstimg, dstr);
	if(src.img == nil){
		err = "invalid or empty source region";
		goto Error;
	}
	if(dst.img == nil){
		err = "invalid or empty destination region";
		goto Error;
	}
	if(emptyrect(dst.opr))
		return 0;
	if(emptyrect(src.opr))
		return 1;
	if(Dx(dst.nomr)==Dx(src.nomr) && Dy(dst.nomr)==Dy(src.nomr)){
		copypixels(dst, src);
		return dst.lacking;
	}

	if(f == nil)
		f = memlanczosfilter;
	memset(&aux, 0, sizeof(aux));
	naux = 0;

	/* The resampling routines assume that one channel is one byte. */
	cur = ensurefullbytechan(src, &aux[naux++]);
	if(Dx(dst.nomr) < Dx(cur.nomr)){
		cur = xresamplearea(f, cur, dst, &aux[naux++]);
		cur = yresamplearea(f, cur, dst, &aux[naux++]);
	}else{
		cur = yresamplearea(f, cur, dst, &aux[naux++]);
		cur = xresamplearea(f, cur, dst, &aux[naux++]);
	}
	if(cur.img != dst.img)
		copypixels(dst, cur);

	while(naux-- > 0)
		freememimage(aux[naux]);
	if(cur.img == nil){
		err = "unable to produce intermediate images: %r";
		goto Error;
	}
	return cur.lacking;

Error:
	werrstr("memresample(0x%p %R <- 0x%p %R): %s", dstimg, dstr, srcimg, srcr, err);
	return -1;
}

Memimage*
resamplememimage(Memimage *img, Rectangle newr, u32int newchan, Memfilter *f)
{
	Memimage *new;

	if(img==nil || emptyrect(img->r)){
		werrstr("resamplememimage: invalid image at 0x%p", img);
		return nil;
	}
	/*
	 * If one of the new dimensions is zero, set it from the other one,
	 * assuming that the aspect ratio of the image is to be maintained.
	 */
	if(newr.min.x == newr.max.x)
		newr.max.x = newr.min.x + muldivround(Dx(img->r), Dy(newr), Dy(img->r));
	else if(newr.min.y == newr.max.y)
		newr.max.y = newr.min.y + muldivround(Dy(img->r), Dx(newr), Dx(img->r));
	if(emptyrect(newr)){
		werrstr("resamplememimage: invalid target rectangle %R", newr);
		return nil;
	}

	if(Dx(newr)==Dx(img->r) && Dy(newr)==Dy(img->r) && newchan==img->chan)
		return allocmemimaged(newr, img->chan, img->data);
	new = allocmemimage(newr, newchan);
	if(new==nil || memresample(new, newr, img, img->r, f)<0){
		freememimage(new);
		return nil;
	}
	return new;
}
