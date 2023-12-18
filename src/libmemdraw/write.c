#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

#define	CHUNK	16000

#define	HSHIFT	3	/* HSHIFT==5 runs slightly faster, but hash table is 64x bigger */
#define	NHASH	(1<<(HSHIFT*NMATCH))
#define	HMASK	(NHASH-1)
#define	hupdate(h, c)	((((h)<<HSHIFT)^(c))&HMASK)
typedef struct Hlist Hlist;
struct Hlist
{
	uchar *s;
	Hlist *next, *prev;
};

int
writememimage(int fd, Memimage *i)
{
	uchar *outbuf, *outp, *eout;		/* encoded data, pointer, end */
	uchar *loutp;				/* start of encoded line */
	Hlist *hash;				/* heads of hash chains of past strings */
	Hlist *chain, *hp;			/* hash chain members, pointer */
	Hlist *cp;				/* next Hlist to fall out of window */
	int h;					/* hash value */
	uchar *line, *eline;			/* input line, end pointer */
	uchar *data, *edata;			/* input buffer, end pointer */
	long n;					/* length of input buffer */
	int nb;					/* # of bytes returned by unloadmemimage */
	int bpl;				/* input line length */
	int lpu;				/* max # of lines per unload call */
	int offs, runlen;			/* offset, length of consumed data */
	uchar dumpbuf[NDUMP];			/* dump accumulator */
	int ndump;				/* length of dump accumulator */
	int miny, dy;				/* y values while unloading input */
	int ncblock;				/* size of compressed blocks */
	Rectangle r;
	uchar *p, *q, *s, *es, *t;
	char hdr[11+5*12+1];
	char cbuf[20];

	r = i->r;
	bpl = bytesperline(r, i->depth);
	n = Dy(r)*bpl;
	data = malloc(n);
	ncblock = _compblocksize(r, i->depth);
	outbuf = malloc(ncblock);
	hash = malloc(NHASH*sizeof(Hlist));
	chain = malloc(NMEM*sizeof(Hlist));
	if(data==nil || outbuf==nil || hash==nil || chain==nil){
	Errout:
		free(data);
		free(outbuf);
		free(hash);
		free(chain);
		return -1;
	}
	lpu = CHUNK/bpl;
	if(lpu < 1)
		lpu = 1;
	for(miny=r.min.y; miny!=r.max.y; miny+=dy){
		dy = r.max.y-miny;
		if(dy > lpu)
			dy = lpu;
		nb = unloadmemimage(i, Rect(r.min.x, miny, r.max.x, miny+dy),
			data+(miny-r.min.y)*bpl, dy*bpl);
		if(nb != dy*bpl)
			goto Errout;
	}
	snprint(hdr, sizeof(hdr), "compressed\n%11s %11d %11d %11d %11d ",
		chantostr(cbuf, i->chan), r.min.x, r.min.y, r.max.x, r.max.y);
	if(write(fd, hdr, 11+5*12) != 11+5*12)
		goto Errout;
	edata = data+n;
	eout = outbuf+ncblock;
	line = data;
	r.max.y = r.min.y;
	while(line != edata){
		memset(hash, 0, NHASH*sizeof(Hlist));
		memset(chain, 0, NMEM*sizeof(Hlist));
		cp = chain;
		h = 0;
		outp = outbuf;
		for(n=0; n!=NMATCH; n++)
			h = hupdate(h, line[n]);
		loutp = outbuf;
		while(line != edata){
			ndump = 0;
			eline = line+bpl;
			for(p=line; p!=eline; ){
				if(eline-p < NRUN)
					es = eline;
				else
					es = p+NRUN;
				q = nil;
				runlen = 0;
				for(hp=hash[h].next; hp!=nil; hp=hp->next){
					s = p + runlen;
					if(s >= es)
						continue;
					t = hp->s + runlen;
					for(; s>=p; s--)
						if(*s != *t--)
							goto Matchloop;
					t += runlen+2;
					s += runlen+2;
					for(; s<es; s++)
						if(*s != *t++)
							break;
					n = s-p;
					if(n > runlen){
						runlen = n;
						q = hp->s;
						if(n == NRUN)
							break;
					}
				Matchloop:;
				}
				if(runlen < NMATCH){
					if(ndump == NDUMP){
						if(eout-outp < ndump+1)
							goto Bfull;
						*outp++ = ndump-1+128;
						memmove(outp, dumpbuf, ndump);
						outp += ndump;
						ndump = 0;
					}
					dumpbuf[ndump++] = *p;
					runlen = 1;
				}else{
					if(ndump != 0){
						if(eout-outp < ndump+1)
							goto Bfull;
						*outp++ = ndump-1+128;
						memmove(outp, dumpbuf, ndump);
						outp += ndump;
						ndump = 0;
					}
					offs = p-q-1;
					if(eout-outp < 2)
						goto Bfull;
					*outp++ = ((runlen-NMATCH)<<2) + (offs>>8);
					*outp++ = offs&255;
				}
				for(q=p+runlen; p!=q; p++){
					if(cp->prev != nil)
						cp->prev->next = nil;
					cp->next = hash[h].next;
					cp->prev = &hash[h];
					if(cp->next != nil)
						cp->next->prev = cp;
					cp->prev->next = cp;
					cp->s = p;
					if(++cp == &chain[NMEM])
						cp = chain;
					if(edata-p > NMATCH)
						h = hupdate(h, p[NMATCH]);
				}
			}
			if(ndump != 0){
				if(eout-outp < ndump+1)
					goto Bfull;
				*outp++ = ndump-1+128;
				memmove(outp, dumpbuf, ndump);
				outp += ndump;
			}
			line = eline;
			loutp = outp;
			r.max.y++;
		}
	Bfull:
		if(loutp == outbuf)
			goto Errout;
		n = loutp-outbuf;
		snprint(hdr, sizeof(hdr), "%11d %11ld ", r.max.y, n);
		write(fd, hdr, 2*12);
		write(fd, outbuf, n);
		r.min.y = r.max.y;
	}
	free(data);
	free(outbuf);
	free(hash);
	free(chain);
	return 0;
}
