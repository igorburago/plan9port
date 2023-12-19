#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

#define poolalloc(a, b)	malloc(b)
#define poolfree(a, b)	free(b)

void
memimagemove(void *from, void *to)
{
	Memdata *md;

	md = *(Memdata**)to;
	if(md->alloc != from){
		print("compacted data not right: #%p\n", md->alloc);
		abort();
	}
	md->alloc = to;
	md->bdata = (uchar*)md->alloc->words;	/* if allocmemimage changes, this must change too */
}

Memimage*
allocmemimaged(Rectangle r, u32int chan, Memdata *md)
{
	int depth;
	Memimage *i;

	if(Dx(r)<=0 || Dy(r)<=0){
		werrstr("bad rectangle %R", r);
		return nil;
	}
	depth = chantodepth(chan);
	if(depth == 0){
		werrstr("bad channel descriptor %.8lux", (ulong)chan);
		return nil;
	}
	i = mallocz(sizeof(Memimage), 1);
	if(i == nil)
		return nil;

	i->width = wordsperline(r, depth);
	i->zero = sizeof(u32int) * i->width * r.min.y;
	if(r.min.x >= 0)
		i->zero += (r.min.x * depth) / 8;
	else
		i->zero -= (-r.min.x*depth + 7) / 8;
	i->zero = -i->zero;

	i->r = r;
	i->clipr = r;
	i->flags = 0;
	i->layer = nil;
	i->cmap = memdefcmap;
	if(memsetchan(i, chan) < 0){
		free(i);
		return nil;
	}

	md->ref++;
	i->data = md;
	i->userdata = nil;
	return i;
}

Memimage*
_allocmemimage(Rectangle r, u32int chan)
{
	int depth;
	Memdata *md;
	ulong datasize;
	Memimage *i;

	if(Dx(r)<=0 || Dy(r)<=0){
		werrstr("bad rectangle %R", r);
		return nil;
	}
	depth = chantodepth(chan);
	if(depth == 0){
		werrstr("bad channel descriptor %.8lux", (ulong)chan);
		return nil;
	}

	md = mallocz(sizeof(Memdata), 1);
	if(md == nil)
		return nil;
	md->allocd = 1;
	md->ref = 0;

	datasize = sizeof(u32int) * wordsperline(r, depth) * Dy(r);
	md->alloc = poolalloc(imagmem, offsetof(Memalloc, words)+datasize);
	if(md->alloc == nil){
		free(md);
		return nil;
	}
	md->alloc->pc = getcallerpc(&r);
	md->alloc->md = md;
	md->bdata = (uchar*)md->alloc->words;	/* if this changes, memimagemove must change too */

	i = allocmemimaged(r, chan, md);
	if(i == nil){
		poolfree(imagmem, md->alloc);
		free(md);
		return nil;
	}
	return i;
}

void
_freememimage(Memimage *i)
{
	if(i == nil)
		return;
	if(--i->data->ref==0 && i->data->allocd){
		if(i->data->alloc != nil)
			poolfree(imagmem, i->data->alloc);
		free(i->data);
	}
	free(i);
}

/*
 * Wordaddr is deprecated.
 */
u32int*
wordaddr(Memimage *i, Point p)
{
	return (u32int*)((ulong)byteaddr(i, p) & ~(sizeof(u32int)-1));
}

uchar*
byteaddr(Memimage *i, Point p)
{
	uchar *a;
	int np;

	/* Careful to sign-extend negative p.y for 64 bits. */
	a = i->data->bdata + i->zero + (int)(sizeof(u32int) * i->width * p.y);

	if(i->depth < 8){
		/* We need to always round down, but C rounds toward zero. */
		np = 8 / i->depth;
		if(p.x < 0)
			return a + (p.x-np+1)/np;
		else
			return a + p.x/np;
	}else
		return a + p.x*(i->depth/8);
}

int
memsetchan(Memimage *i, u32int chan)
{
	int depth, t, j, k, bytes;
	u32int cc;

	depth = chantodepth(chan);
	if(depth == 0){
		werrstr("bad channel descriptor %.8lux", (ulong)chan);
		return -1;
	}

	i->depth = depth;
	i->chan = chan;
	i->flags &= ~(Fgrey|Falpha|Fcmap|Fbytes);
	bytes = 1;
	for(cc=chan, j=0, k=0; cc!=0; j+=NBITS(cc), cc>>=8, k++){
		t = TYPE(cc);	/* 0<=t<NChan, as guaranteed by chantodepth()!=0 above */
		if(t == CGrey)
			i->flags |= Fgrey;
		if(t == CAlpha)
			i->flags |= Falpha;
		if(t==CMap && i->cmap==nil){
			i->cmap = memdefcmap;
			i->flags |= Fcmap;
		}

		i->shift[t] = j;
		i->mask[t] = (1<<NBITS(cc))-1;
		i->nbits[t] = NBITS(cc);
		if(NBITS(cc) != 8)
			bytes = 0;
	}
	i->nchan = k;
	if(bytes)
		i->flags |= Fbytes;
	return 0;
}
