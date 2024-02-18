#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include <libsec.h>
#include <complete.h>
#include "dat.h"
#include "fns.h"

Image	*tagcols[NCOL];
Image	*textcols[NCOL];

enum
{
	TABDIR = 3	/* width of tabs in directory windows */
};

void
textinit(Text *t, File *f, Rectangle r, Reffont *rf, Image *cols[NCOL])
{
	t->file = f;
	t->all = r;
	t->scrollr = r;
	t->scrollr.max.x = r.min.x+Scrollwid;
	t->scrpuckr0 = ZR;
	r.min.x += Scrollwid+Scrollgap;
	t->eq0 = ~0;
	t->ncache = 0;
	t->reffont = rf;
	t->tabstop = maxtab;
	memmove(t->fr.cols, cols, sizeof t->fr.cols);
	textredraw(t, r, rf->f, screen, -1);
}

void
textredraw(Text *t, Rectangle r, Font *f, Image *b, int odx)
{
	int maxt;
	Rectangle rr;

	frinit(&t->fr, r, f, b, t->fr.cols);
	rr = t->fr.r;
	rr.min.x -= Scrollwid+Scrollgap;	/* back fill to scroll bar */
	if(!t->fr.noredraw)
		draw(t->fr.b, rr, t->fr.cols[BACK], nil, ZP);
	/* use no wider than 3-space tabs in a directory */
	maxt = maxtab;
	if(t->what == Body){
		if(t->w->isdir)
			maxt = min(TABDIR, maxtab);
		else
			maxt = t->tabstop;
	}
	t->fr.maxtab = maxt*stringwidth(f, "0");
	if(t->what==Body && t->w->isdir && odx!=Dx(t->all)){
		if(t->fr.maxlines > 0){
			textreset(t);
			textcolumnate(t, t->w->dlp,  t->w->ndl);
			textshow(t, 0, 0, 1);
		}
	}else{
		textfill(t);
		textsetselect(t, t->q0, t->q1);
	}
}

int
textresize(Text *t, Rectangle r, int keepextra)
{
	int odx;

	if(Dy(r) <= 0)
		r.max.y = r.min.y;
	else if(!keepextra)
		r.max.y -= Dy(r)%t->fr.font->height;
	odx = Dx(t->all);
	t->all = r;
	t->scrollr = r;
	t->scrollr.max.x = r.min.x+Scrollwid;
	t->scrpuckr0 = ZR;
	r.min.x += Scrollwid+Scrollgap;
	frclear(&t->fr, 0);
	textredraw(t, r, t->fr.font, t->fr.b, odx);
	if(keepextra && t->fr.r.max.y < t->all.max.y && !t->fr.noredraw){
		/* draw background in bottom fringe of window */
		r.min.x -= Scrollgap;
		r.min.y = t->fr.r.max.y;
		r.max.y = t->all.max.y;
		draw(screen, r, t->fr.cols[BACK], nil, ZP);
	}
	return t->all.max.y;
}

void
textclose(Text *t)
{
	free(t->cache);
	frclear(&t->fr, 1);
	filedeltext(t->file, t);
	t->file = nil;
	rfclose(t->reffont);
	if(argtext == t)
		argtext = nil;
	if(typetext == t)
		typetext = nil;
	if(seltext == t)
		seltext = nil;
	if(mousetext == t)
		mousetext = nil;
	if(barttext == t)
		barttext = nil;
}

int
dircmp(const void *a, const void *b)
{
	Dirlist *da, *db;
	int i, n;

	da = *(Dirlist**)a;
	db = *(Dirlist**)b;
	n = min(da->nr, db->nr);
	i = memcmp(da->r, db->r, n*sizeof(Rune));
	if(i)
		return i;
	return da->nr - db->nr;
}

void
textcolumnate(Text *t, Dirlist **dlp, int ndl)
{
	int i, j, w, colw, mint, maxt, ncol, nrow;
	Dirlist *dl;
	uint q1;
	static Rune Lnl[] = { '\n', 0 };
	static Rune Ltab[] = { '\t', 0 };

	if(t->file->ntext > 1)
		return;
	mint = stringwidth(t->fr.font, "0");
	/* go for narrower tabs if set more than 3 wide */
	t->fr.maxtab = min(maxtab, TABDIR)*mint;
	maxt = t->fr.maxtab;
	colw = 0;
	for(i=0; i<ndl; i++){
		dl = dlp[i];
		w = dl->wid;
		if(maxt-w%maxt < mint || w%maxt==0)
			w += mint;
		if(w % maxt)
			w += maxt-(w%maxt);
		if(w > colw)
			colw = w;
	}
	if(colw == 0)
		ncol = 1;
	else
		ncol = max(1, Dx(t->fr.r)/colw);
	nrow = (ndl+ncol-1)/ncol;

	q1 = 0;
	for(i=0; i<nrow; i++){
		for(j=i; j<ndl; j+=nrow){
			dl = dlp[j];
			fileinsert(t->file, q1, dl->r, dl->nr);
			q1 += dl->nr;
			if(j+nrow >= ndl)
				break;
			w = dl->wid;
			if(maxt-w%maxt < mint){
				fileinsert(t->file, q1, Ltab, 1);
				q1++;
				w += mint;
			}
			do{
				fileinsert(t->file, q1, Ltab, 1);
				q1++;
				w += maxt-(w%maxt);
			}while(w < colw);
		}
		fileinsert(t->file, q1, Lnl, 1);
		q1++;
	}
}

int
textload(Text *t, uint q0, char *file, int setqid)
{
	Rune *rp;
	Dirlist *dl, **dlp;
	int fd, i, j, n, ndl, nulls;
	uint q, q1;
	Dir *d, *dbuf;
	char *tmp;
	Text *u;
	DigestState *h;

	if(t->ncache!=0 || t->file->b.nc || t->w==nil || t!=&t->w->body)
		error("text.load");
	if(t->w->isdir && t->file->nname==0){
		warning(nil, "empty directory name");
		return -1;
	}
	if(ismtpt(file)){
		warning(nil, "will not open self mount point %s\n", file);
		return -1;
	}
	fd = open(file, OREAD);
	if(fd < 0){
		warning(nil, "can't open %s: %r\n", file);
		return -1;
	}
	d = dirfstat(fd);
	if(d == nil){
		warning(nil, "can't fstat %s: %r\n", file);
		goto Rescue;
	}
	nulls = FALSE;
	h = nil;
	if(d->qid.type & QTDIR){
		/* this is checked in get() but it's possible the file changed underfoot */
		if(t->file->ntext > 1){
			warning(nil, "%s is a directory; can't read with multiple windows on it\n", file);
			goto Rescue;
		}
		t->w->isdir = TRUE;
		t->w->filemenu = FALSE;
		if(t->file->nname > 0 && t->file->name[t->file->nname-1] != '/'){
			rp = runemalloc(t->file->nname+1);
			runemove(rp, t->file->name, t->file->nname);
			rp[t->file->nname] = '/';
			winsetname(t->w, rp, t->file->nname+1);
			free(rp);
		}
		dlp = nil;
		ndl = 0;
		dbuf = nil;
		while((n=dirread(fd, &dbuf)) > 0){
			for(i=0; i<n; i++){
				dl = emalloc(sizeof(Dirlist));
				j = strlen(dbuf[i].name);
				tmp = emalloc(j+1+1);
				memmove(tmp, dbuf[i].name, j);
				if(dbuf[i].qid.type & QTDIR)
					tmp[j++] = '/';
				tmp[j] = '\0';
				dl->r = bytetorune(tmp, &dl->nr);
				dl->wid = stringwidth(t->fr.font, tmp);
				free(tmp);
				ndl++;
				dlp = realloc(dlp, ndl*sizeof(Dirlist*));
				dlp[ndl-1] = dl;
			}
			free(dbuf);
		}
		qsort(dlp, ndl, sizeof(Dirlist*), dircmp);
		t->w->dlp = dlp;
		t->w->ndl = ndl;
		textcolumnate(t, dlp, ndl);
		q1 = t->file->b.nc;
	}else{
		t->w->isdir = FALSE;
		t->w->filemenu = TRUE;
		if(q0 == 0)
			h = sha1(nil, 0, nil, nil);
		q1 = q0 + fileload(t->file, q0, fd, &nulls, h);
	}
	if(setqid){
		if(h != nil) {
			sha1(nil, 0, t->file->sha1, h);
			h = nil;
		} else {
			memset(t->file->sha1, 0, sizeof t->file->sha1);
		}
		t->file->dev = d->dev;
		t->file->mtime = d->mtime;
		t->file->qidpath = d->qid.path;
	}
	close(fd);
	rp = fbufalloc();
	for(q=q0; q<q1; q+=n){
		n = q1-q;
		if(n > RBUFSIZE)
			n = RBUFSIZE;
		bufread(&t->file->b, q, rp, n);
		if(q < t->org)
			t->org += n;
		else if(q <= t->org+t->fr.nchars)
			frinsert(&t->fr, rp, rp+n, q-t->org);
		if(t->fr.lastlinefull)
			break;
	}
	fbuffree(rp);
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		if(u != t){
			if(u->org > u->file->b.nc)	/* will be 0 because of reset(), but safety first */
				u->org = 0;
			textresize(u, u->all, TRUE);
			textbacknl(u, u->org, 0);	/* go to beginning of line */
		}
		textsetselect(u, q0, q0);
	}
	if(nulls)
		warning(nil, "%s: NUL bytes elided\n", file);
	free(d);
	return q1-q0;

    Rescue:
	close(fd);
	return -1;
}

uint
textbsinsert(Text *t, uint q0, Rune *r, uint n, int tofile, int *nrp)
{
	Rune *bp, *tp, *up;
	int i, initial;

	if(t->what == Tag){	/* can't happen but safety first: mustn't backspace over file name */
    Err:
		textinsert(t, q0, r, n, tofile);
		*nrp = n;
		return q0;
	}
	bp = r;
	for(i=0; i<n; i++)
		if(*bp++ == '\b'){
			--bp;
			initial = 0;
			tp = runemalloc(n);
			runemove(tp, r, i);
			up = tp+i;
			for(; i<n; i++){
				*up = *bp++;
				if(*up == '\b')
					if(up == tp)
						initial++;
					else
						--up;
				else
					up++;
			}
			if(initial){
				if(initial > q0)
					initial = q0;
				q0 -= initial;
				textdelete(t, q0, q0+initial, tofile);
			}
			n = up-tp;
			textinsert(t, q0, tp, n, tofile);
			free(tp);
			*nrp = n;
			return q0;
		}
	goto Err;
}

void
textinsert(Text *t, uint q0, Rune *r, uint n, int tofile)
{
	int c, i;
	Text *u;

	if(tofile && t->ncache != 0)
		error("text.insert");
	if(n == 0)
		return;
	if(tofile){
		fileinsert(t->file, q0, r, n);
		if(t->what == Body){
			t->w->dirty = TRUE;
			t->w->utflastqid = -1;
		}
		if(t->file->ntext > 1)
			for(i=0; i<t->file->ntext; i++){
				u = t->file->text[i];
				if(u != t){
					u->w->dirty = TRUE;	/* always a body */
					textinsert(u, q0, r, n, FALSE);
					textsetselect(u, u->q0, u->q1);
					textscrdraw(u);
				}
			}
	}
	if(q0 < t->iq1)
		t->iq1 += n;
	if(q0 < t->q1)
		t->q1 += n;
	if(q0 < t->q0)
		t->q0 += n;
	if(q0 < t->org)
		t->org += n;
	else if(q0 <= t->org+t->fr.nchars)
		frinsert(&t->fr, r, r+n, q0-t->org);
	if(t->w){
		c = 'i';
		if(t->what == Body)
			c = 'I';
		if(n <= EVENTSIZE)
			winevent(t->w, "%c%d %d 0 %d %.*S\n", c, q0, q0+n, n, n, r);
		else
			winevent(t->w, "%c%d %d 0 0 \n", c, q0, q0+n, n);
	}
}

void
typecommit(Text *t)
{
	if(t->w != nil)
		wincommit(t->w, t);
	else
		textcommit(t, TRUE);
}

void
textfill(Text *t)
{
	Rune *rp;
	uint n, nl, i;

	if(t->fr.lastlinefull || t->nofill)
		return;
	if(t->ncache > 0)
		typecommit(t);
	rp = fbufalloc();
	do{
		n = t->file->b.nc-(t->org+t->fr.nchars);
		if(n == 0)
			break;
		if(n > 2000)	/* educated guess at reasonable amount */
			n = 2000;
		bufread(&t->file->b, t->org+t->fr.nchars, rp, n);
		/* It's expensive to frinsert more than we need, so count newlines. */
		nl = t->fr.maxlines-t->fr.nlines+1;
		for(i=0; nl>0 && i<n; i++)
			nl -= (rp[i] == '\n');
		frinsert(&t->fr, rp, rp+i, t->fr.nchars);
	}while(!t->fr.lastlinefull);
	fbuffree(rp);
}

void
textdelete(Text *t, uint q0, uint q1, int tofile)
{
	uint n, p0, p1;
	int i, c;
	Text *u;

	if(tofile && t->ncache != 0)
		error("text.delete");
	n = q1-q0;
	if(n == 0)
		return;
	if(tofile){
		filedelete(t->file, q0, q1);
		if(t->what == Body){
			t->w->dirty = TRUE;
			t->w->utflastqid = -1;
		}
		if(t->file->ntext > 1)
			for(i=0; i<t->file->ntext; i++){
				u = t->file->text[i];
				if(u != t){
					u->w->dirty = TRUE;	/* always a body */
					textdelete(u, q0, q1, FALSE);
					textsetselect(u, u->q0, u->q1);
					textscrdraw(u);
				}
			}
	}
	if(q0 < t->iq1)
		t->iq1 -= min(n, t->iq1-q0);
	if(q0 < t->q0)
		t->q0 -= min(n, t->q0-q0);
	if(q0 < t->q1)
		t->q1 -= min(n, t->q1-q0);
	if(q1 <= t->org)
		t->org -= n;
	else if(q0 < t->org+t->fr.nchars){
		p1 = q1 - t->org;
		if(p1 > t->fr.nchars)
			p1 = t->fr.nchars;
		if(q0 < t->org){
			t->org = q0;
			p0 = 0;
		}else
			p0 = q0 - t->org;
		frdelete(&t->fr, p0, p1);
		textfill(t);
	}
	if(t->w){
		c = 'd';
		if(t->what == Body)
			c = 'D';
		winevent(t->w, "%c%d %d 0 0 \n", c, q0, q1);
	}
}

void
textconstrain(Text *t, uint q0, uint q1, uint *p0, uint *p1)
{
	*p0 = min(q0, t->file->b.nc);
	*p1 = min(q1, t->file->b.nc);
}

Rune
textreadc(Text *t, uint q)
{
	Rune r;

	if(t->cq0<=q && q<t->cq0+t->ncache)
		r = t->cache[q-t->cq0];
	else
		bufread(&t->file->b, q, &r, 1);
	return r;
}

int
textendswithnl(Text *t)
{
	Buffer *b;

	b = &t->file->b;
	return b->nc>0 && textreadc(t, b->nc-1)=='\n';
}

static uint
textbspos(Text *t, uint q0, Rune bs)
{
	int inword;
	uint q;
	Rune r;

	if(bs == KctrlH)	/* ^H: erase character */
		return q0 - (q0>0);
	inword = FALSE;
	for(q=q0; q>0; q--){
		r = textreadc(t, q-1);
		if(r == '\n'){
			/*
			 * When started from a newline, both ^W and ^U skip it
			 * before stopping, unlike ^A which stays put instead.
			 */
			if(q==q0 && bs!=KctrlA)
				q--;
			break;
		}
		if(bs == KctrlW){	/* ^W: erase word */
			if(isalnum(r))
				inword = TRUE;
			else if(inword)
				break;
		}
	}
	return q;
}

/*
 * Besides whitespace, exclude some runes that are unlikely to occur in
 * a typical path, but are likely to surround one in a shell context or
 * in general writing.
 */
static int
ispathc(Rune r)
{
	return r>' ' && utfrune("`'\"=:;<|>{}()[]&#!?,", r)==nil;
}

static Runestr
textcompletepath(Text *t)
{
	enum { Maxcontext = 1024 };
	char Dot[] = ".";
	Completion *c;
	Runestr ins, rdir;
	char *dir, *base;
	int n, nrdir, nrbase, i;
	uint q;
	Rune r;

	ins = runestr(nil, 0);
	if(t->q0<t->file->b.nc && ispathc(textreadc(t, t->q0)))
		return ins;

	n = nrdir = 0;
	for(q=t->q0; q>0; q--){
		r = textreadc(t, q-1);
		if(!ispathc(r))
			break;
		if(++n > Maxcontext)
			return ins;
		nrdir += (nrdir>0 || r=='/');
	}
	nrbase = n - nrdir;

	if(nrdir>0 && textreadc(t, q)=='/'){	/* absolute path */
		dir = emalloc(nrdir*UTFmax+1);
		n = 0;
		for(i=0; i<nrdir; i++){
			r = textreadc(t, q++);
			n += runetochar(dir+n, &r);
		}
		dir[n] = 0;
		cleanname(dir);
	}else{	/* path relative to the window directory */
		rdir = runestr(runemalloc(nrdir), nrdir);
		for(i=0; i<rdir.nr; i++)
			rdir.r[i] = textreadc(t, q++);
		rdir = dirname(t, rdir.r, rdir.nr);
		if(rdir.nr > 0)
			dir = runetobyte(rdir.r, rdir.nr);
		else	/* both nrdir==0 and the window directory is empty */
			dir = Dot;
		free(rdir.r);
	}

	base = emalloc(nrbase*UTFmax+1);
	n = 0;
	for(i=0; i<nrbase; i++){
		r = textreadc(t, q++);
		n += runetochar(base+n, &r);
	}
	base[n] = 0;

	c = complete(dir, base);
	if(c == nil)
		warning(nil, "cannot complete at %s: %r\n", dir);
	else if(c->advance)
		ins.r = runesmprint("%s%n", c->string, &ins.nr);
	else{
		/* Only if dir is root can it end with a slash after name cleaning. */
		warning(nil, "%s%s%s*%s%s\n",
			dir, dir[0]=='/' && dir[1]=='\0' ? "" : "/", base,
			c->nmatch==0 ? ": no matches" : "",
			c->nmatch==0 && c->nfile>0 ? " in:" : "");
		for(i=0; i<c->nfile; i++)
			warning(nil, " %s\n", c->filename[i]);
	}

	freecompletion(c);
	free(base);
	if(dir != Dot)
		free(dir);
	return ins;
}

void
texttype(Text *t, Rune r)
{
	uint q0, q1, nb;
	int n, i;
	Runestr rs;
	Text *u;

	if(t->what!=Body && t->what!=Tag && r=='\n')
		return;
	if(t->what == Tag)
		t->w->tagsafe = FALSE;

	rs = runestr(&r, 1);
	switch(r){
	case Kup:
		winscroll(t->w, t, -(int)max(t->fr.maxlines/3, 1));
		return;
	case Kdown:
		winscroll(t->w, t, +(int)max(t->fr.maxlines/3, 1));
		return;
	case Kpgup:
		winscroll(t->w, t, -(int)max(2*t->fr.maxlines/3, 1));
		return;
	case Kpgdown:
		winscroll(t->w, t, +(int)max(2*t->fr.maxlines/3, 1));
		return;
	case Kleft:
		typecommit(t);
		if(t->q0 > 0)
			textshow(t, t->q0-1, t->q0-1, TRUE);
		return;
	case Kright:
		typecommit(t);
		if(t->q1 < t->file->b.nc)
			textshow(t, t->q1+1, t->q1+1, TRUE);
		return;
	case Khome:
		typecommit(t);
		if(t->what==Tag && !t->w->tagexpand)
			return;
		if(t->org > t->iq1)
			q0 = t->iq1;
		else
			q0 = 0;
		if(t->what==Body && q0==0)
			textshow(t, q0, q0, FALSE);
		else{
			q0 = textbacknl(t, q0, t->fr.maxlines>1 ? 1 : 0);
			textsetorigin(t, q0);
		}
		return;
	case Kend:
		typecommit(t);
		if(t->what==Tag && !t->w->tagexpand){
			t->w->tagexpand = TRUE;
			t->w->tagsafe = FALSE;
			winresize(t->w, t->w->r, TRUE, TRUE);
		}
		if(t->org+t->fr.nchars < t->iq1)
			q0 = min(t->iq1, t->file->b.nc);	/* BUG: iq1>nc should never happen, but does! */
		else
			q0 = t->file->b.nc;
		if(t->what==Body && q0==t->file->b.nc)
			textshow(t, q0, q0, FALSE);
		else{
			q0 = textbacknl(t, q0, t->fr.maxlines>1 ? 1 : 0);
			textsetorigin(t, q0);
			if(t->what != Body)
				textscrollupifpastend(t);
		}
		return;
	case KctrlA:	/* ^A: beginning of line */
		typecommit(t);
		q0 = textbspos(t, t->q0, KctrlA);
		textshow(t, q0, q0, TRUE);
		return;
	case KctrlE:	/* ^E: end of line */
		typecommit(t);
		q0 = t->q0;
		while(q0<t->file->b.nc && textreadc(t, q0)!='\n')
			q0++;
		textshow(t, q0, q0, TRUE);
		return;
	case Kcmd+'c':	/* %C: copy */
		typecommit(t);
		cut(t, t, nil, TRUE, FALSE, nil, 0);
		return;
	case Kcmd+'z':	/* %Z: undo */
	 	typecommit(t);
		undo(t, nil, nil, TRUE, 0, nil, 0);
		return;
	case Kcmd+'Z':	/* %-shift-Z: redo */
	 	typecommit(t);
		undo(t, nil, nil, FALSE, 0, nil, 0);
		return;
	}
	if(t->what == Body){
		seq++;
		filemark(t->file);
	}
	/* cut/paste must be done after the seq++/filemark */
	switch(r){
	case Kcmd+'x':	/* %X: cut */
		typecommit(t);
		if(t->what == Body){
			seq++;
			filemark(t->file);
		}
		cut(t, t, nil, TRUE, TRUE, nil, 0);
		textshow(t, t->q0, t->q0, TRUE);
		t->iq1 = t->q0;
		return;
	case Kcmd+'v':	/* %V: paste */
		typecommit(t);
		if(t->what == Body){
			seq++;
			filemark(t->file);
		}
		paste(t, t, nil, TRUE, FALSE, nil, 0);
		textshow(t, t->q0, t->q1, TRUE);
		t->iq1 = t->q1;
		return;
	}
	if(t->q1 > t->q0){
		if(t->ncache != 0)
			error("text.type");
		cut(t, t, nil, TRUE, TRUE, nil, 0);
		t->eq0 = ~0;
	}
	textshow(t, t->q0, t->q0, TRUE);
	switch(r){
	case KctrlF:	/* ^F: complete */
	case Kins:
		typecommit(t);
		rs = textcompletepath(t);
		if(rs.r == nil)
			return;
		goto Insertrunes;
	case Kesc:
		if(t->eq0 != ~0){
			if(t->eq0 <= t->q0)
				textsetselect(t, t->eq0, t->q0);
			else
				textsetselect(t, t->q0, t->eq0);
		}
		if(t->ncache > 0)
			typecommit(t);
		t->iq1 = t->q0;
		return;
	case KctrlH:	/* ^H: erase character */
	case KctrlU:	/* ^U: erase line */
	case KctrlW:	/* ^W: erase word */
		q1 = t->q0;
		q0 = textbspos(t, q1, r);
		/* if selection is at beginning of window, avoid deleting invisible text */
		if(q0 < t->org)
			q0 = t->org;
		if(q0 >= q1)
			return;
		for(i=0; i<t->file->ntext; i++){
			u = t->file->text[i];
			u->nofill = TRUE;
			nb = q1-q0;
			n = u->ncache;
			if(n > 0){
				if(q1 != u->cq0+n)
					error("text.type backspace");
				if(n > nb)
					n = nb;
				u->ncache -= n;
				textdelete(u, q1-n, q1, FALSE);
				nb -= n;
			}
			if(u->eq0==q1 || u->eq0==~0)
				u->eq0 = q0;
			if(nb>0 && u==t)
				textdelete(u, q0, q0+nb, TRUE);
			if(u != t)
				textsetselect(u, u->q0, u->q1);
			else
				textsetselect(t, q0, q0);
			u->nofill = FALSE;
		}
		for(i=0; i<t->file->ntext; i++)
			textfill(t->file->text[i]);
		t->iq1 = t->q0;
		return;
	case '\n':
		if(t->w->autoindent){
			q1 = t->q0;
			q0 = textbspos(t, q1, KctrlA);
			if(q0 >= q1)
				goto Insertrunes;
			/*
			 * Leading whitespace on the first line of a tag is never an indentation,
			 * so do not carry it over to the second line with autoindent. (It may
			 * occur due to a relative file path starting with whitespace or, more
			 * commonly, an empty path exposing the following space separator.)
			 */
			if(t->what==Tag && q0==0)
				goto Insertrunes;
			rs.r = runemalloc(1+q1-q0);
			rs.nr = 0;
			rs.r[rs.nr++] = r;
			for(; q0<q1; q0++){
				r = textreadc(t, q0);
				if(r!=' ' && r!='\t')
					break;
				rs.r[rs.nr++] = r;
			}
		}
		goto Insertrunes;
	}
Insertrunes:
	/* otherwise ordinary character; just insert, typically in caches of all texts */
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		if(u->eq0 == ~0)
			u->eq0 = t->q0;
		if(u->ncache == 0)
			u->cq0 = t->q0;
		else if(t->q0 != u->cq0+u->ncache)
			error("text.type cq1");
		/*
		 * Change the tag before we add to ncache, so that if the window
		 * body is resized, the commit will not find anything in ncache.
		 */
		if(u->what==Body && u->ncache==0){
			u->needundo = TRUE;
			winsettag(t->w);
			u->needundo = FALSE;
		}
		textinsert(u, t->q0, rs.r, rs.nr, FALSE);
		if(u != t)
			textsetselect(u, u->q0, u->q1);
		if(u->ncache+rs.nr > u->ncachealloc){
			u->ncachealloc += 10 + rs.nr;
			u->cache = runerealloc(u->cache, u->ncachealloc);
		}
		runemove(u->cache+u->ncache, rs.r, rs.nr);
		u->ncache += rs.nr;
	}
	if(rs.r != &r)
		free(rs.r);
	textshow(t, t->q0+rs.nr, t->q0+rs.nr, TRUE);
	if(r=='\n' && t->w!=nil)
		wincommit(t->w, t);
	t->iq1 = t->q0;
}

void
textcommit(Text *t, int tofile)
{
	if(t->ncache == 0)
		return;
	if(tofile)
		fileinsert(t->file, t->cq0, t->cache, t->ncache);
	if(t->what == Body){
		t->w->dirty = TRUE;
		t->w->utflastqid = -1;
	}
	t->ncache = 0;
}

void
textselect(Text *t)
{
	enum { Doubleclickmsec = 500 };
	static Text *clicktext;
	static uint clickmsec;
	uint q0, q1, selectq;
	int b, x, y;
	enum { None, Cut, Paste } state;

	/*
	 * To have double-clicking and chording, we double-click
	 * immediately if it might make sense.
	 */
	b = mouse->buttons;
	q0 = t->q0;
	q1 = t->q1;
	selectq = t->org+frcharofpt(&t->fr, mouse->xy);
	if(clicktext==t && mouse->msec-clickmsec<Doubleclickmsec)
	if(q0==q1 && selectq==q0){
		textdoubleclick(t, &q0, &q1);
		textsetselect(t, q0, q1);
		flushimage(display, 1);
		x = mouse->xy.x;
		y = mouse->xy.y;
		/* stay here until something interesting happens */
		do
			readmouse(mousectl);
		while(mouse->buttons==b && abs(mouse->xy.x-x)<3 && abs(mouse->xy.y-y)<3);
		mouse->xy.x = x;	/* in case we're calling frselect */
		mouse->xy.y = y;
		q0 = t->q0;	/* may have changed */
		q1 = t->q1;
		selectq = q0;
	}
	if(mouse->buttons == b){
		textframeselect(t, selectq);
		/* horrible botch: while asleep, may have lost selection altogether */
		if(selectq > t->file->b.nc)
			selectq = t->org + t->fr.p0;
		if(selectq < t->org)
			q0 = selectq;
		else
			q0 = t->org + t->fr.p0;
		if(selectq > t->org+t->fr.nchars)
			q1 = selectq;
		else
			q1 = t->org+t->fr.p1;
	}
	if(q0 == q1){
		if(q0==t->q0 && clicktext==t && mouse->msec-clickmsec<Doubleclickmsec){
			textdoubleclick(t, &q0, &q1);
			clicktext = nil;
		}else{
			clicktext = t;
			clickmsec = mouse->msec;
		}
	}else
		clicktext = nil;
	textsetselect(t, q0, q1);
	flushimage(display, 1);
	state = None;	/* what we've done; undo when possible */
	while(mouse->buttons != 0){
		mouse->msec = 0;
		b = mouse->buttons;
		if((b&Mbutton1) && (b&(Mbutton2|Mbutton3))){
			if(state==None && t->what==Body){
				seq++;
				filemark(t->w->body.file);
			}
			if(b & Mbutton2){
				if(state==Paste && t->what==Body){
					winundo(t->w, TRUE);
					textsetselect(t, q0, t->q1);
					state = None;
				}else if(state != Cut){
					cut(t, t, nil, TRUE, TRUE, nil, 0);
					if(t->what == Tag)
						wintagshowdot(t->w, FALSE);
					state = Cut;
				}
			}else{
				if(state==Cut && t->what==Body){
					winundo(t->w, TRUE);
					textsetselect(t, q0, t->q1);
					state = None;
				}else if(state != Paste){
					paste(t, t, nil, TRUE, FALSE, nil, 0);
					if(t->what == Tag)
						wintagshowdot(t->w, FALSE);
					state = Paste;
				}
			}
			textscrdraw(t);
			clearmouse();
		}
		flushimage(display, 1);
		while(mouse->buttons == b)
			readmouse(mousectl);
		clicktext = nil;
	}
}

void
textshow(Text *t, uint q0, uint q1, int doselect)
{
	uint nc, qe, q;
	int nl, tsd;

	if(t->what != Body){
		if(doselect)
			textsetselect(t, q0, q1);
		return;
	}
	if(t->w!=nil && t->fr.maxlines==0)
		colgrow(t->col, t->w, 1);
	if(doselect)
		textsetselect(t, q0, q1);
	qe = t->org+t->fr.nchars;
	tsd = FALSE;	/* do we call textscrdraw? */
	nc = t->file->b.nc+t->ncache;
	if(t->org <= q0){
		if(nc==0 || q0<qe)
			tsd = TRUE;
		else if(q0==qe && qe==nc){
			if(textreadc(t, nc-1) == '\n'){
				if(t->fr.nlines < t->fr.maxlines)
					tsd = TRUE;
			}else
				tsd = TRUE;
		}
	}
	if(tsd)
		textscrdraw(t);
	else{
		if(t->w->nopen[QWevent] > 0)
			nl = 3*t->fr.maxlines/4;
		else
			nl = t->fr.maxlines/4;
		q = textbacknl(t, q0, nl);
		/* avoid going backwards if trying to go forwards - long lines! */
		if(!(q0>t->org && q<t->org))
			textsetorigin(t, q);
		while(q0 > t->org+t->fr.nchars)
			textsetorigin(t, textforwardnl(t, t->org, 1));
	}
}

static uint
textfrcharofpos(Text *t, uint q)
{
	if(q <= t->org)
		return 0;
	q -= t->org;
	if(q <= t->fr.nchars)
		return q;
	return t->fr.nchars;
}

static void
swappos(uint *p0, uint *p1)
{
	uint p;

	p = *p0;
	*p0 = *p1;
	*p1 = p;
}

static void
frdraworientedsel(Frame *f, uint p0, uint p1, int issel)
{
	if(p0 > p1){
		swappos(&p0, &p1);
		issel = !issel;
	}
	frdrawsel(f, frptofchar(f, p0), p0, p1, issel);
}

void
textsetselect(Text *t, uint q0, uint q1)
{
	uint old0, old1, new0, new1, p0, p1;

	/* t->fr.p0 and t->fr.p1 are always right; t->q0 and t->q1 may be off. */
	old0 = t->fr.p0;
	old1 = t->fr.p1;
	new0 = p0 = textfrcharofpos(t, q0);
	new1 = p1 = textfrcharofpos(t, q1);
	if(t->fr.ticked)
		frtick(&t->fr, frptofchar(&t->fr, t->fr.p0), FALSE);

	/* Find the symmetric difference of the two ranges, in place. */
	if(old0<=new0 && new0<old1)
		swappos(&new0, &old1);
	else if(new0<=old0 && old0<new1)
		swappos(&old0, &new1);
	/* Draw the difference; if a range is inverted, so is its selection status. */
	if(old0 != old1)
		frdraworientedsel(&t->fr, old0, old1, FALSE);
	if(new0 != new1)
		frdraworientedsel(&t->fr, new0, new1, TRUE);

	if(q0==q1 && q0==t->org+p0)	/* the new dot is empty and visible */
		frtick(&t->fr, frptofchar(&t->fr, p0), TRUE);
	t->fr.p0 = p0;
	t->fr.p1 = p1;
	t->q0 = q0;
	t->q1 = q1;
}

static int
region(uint p0, uint p1)
{
	return (p0 > p1) - (p0 < p1);
}

static uint
clamppos(uint p, uint p0, uint p1)
{
	if(p > p1)
		p = p1;
	if(p < p0)
		p = p0;
	return p;
}

static void
frredrawrange(Frame *f, Point pt0, uint p0, uint p1)
{
	uint s0, s1;

	/* Clip the selection range to the drawing one. */
	s0 = clamppos(f->p0, p0, p1);
	s1 = clamppos(f->p1, p0, p1);
	if(p0 < s0)
		frdrawsel0(f, pt0,
			p0, s0, f->cols[BACK], f->cols[TEXT]);
	if(s0 < s1)
		frdrawsel0(f, s0==p0 ? pt0 : frptofchar(f, s0),
			s0, s1, f->cols[HIGH], f->cols[HTEXT]);
	if(s1 < p1)
		frdrawsel0(f, s1==p0 ? pt0 : frptofchar(f, s1),
			s1, p1, f->cols[BACK], f->cols[TEXT]);
}

uint
xselect(Frame *f, Mousectl *mc, Image *col, uint *p1p)
{
	/*
	 * If the button is released in less than Mindelaymsec and the mouse
	 * moved less than Minmovepx along both axes, it is considered a null
	 * selection regardless of whether it crossed a char boundary.
	 */
	enum{
		Minmovemsec	= 2,
		Minmovepx	= 4
	};
	int b, reg;
	uint p0, p1, q;
	Point mp, pt0, pt1, qt;
	uint msec;

	b = mc->m.buttons;	/* when called, a button is down */
	mp = mc->m.xy;
	msec = mc->m.msec;

	if(f->p0 == f->p1)	/* remove tick */
		frtick(f, frptofchar(f, f->p0), 0);
	p0 = p1 = frcharofpt(f, mp);
	pt0 = frptofchar(f, p0);
	pt1 = frptofchar(f, p1);
	reg = 0;
	frtick(f, pt0, 1);
	do{
		q = frcharofpt(f, mc->m.xy);
		if(p1 != q){
			if(p0 == p1)
				frtick(f, pt0, 0);
			if(reg != region(q, p0)){	/* crossed starting point; reset */
				if(reg > 0)
					frredrawrange(f, pt0, p0, p1);
				else if(reg < 0)
					frredrawrange(f, pt1, p1, p0);
				p1 = p0;
				pt1 = pt0;
				reg = region(q, p0);
				if(reg == 0)
					frdrawsel0(f, pt0, p0, p1, col, display->white);
			}
			qt = frptofchar(f, q);
			if(reg > 0){
				if(q > p1)
					frdrawsel0(f, pt1, p1, q, col, display->white);
				else if(q < p1)
					frredrawrange(f, qt, q, p1);
			}else if(reg < 0){
				if(q > p1)
					frredrawrange(f, pt1, p1, q);
				else if(q < p1)
					frdrawsel0(f, qt, q, p1, col, display->white);
			}
			p1 = q;
			pt1 = qt;
		}
		if(p0 == p1)
			frtick(f, pt0, 1);
		flushimage(f->display, 1);
		readmouse(mc);
	}while(mc->m.buttons == b);
	if(p0!=p1 && mc->m.msec-msec<Minmovemsec
	&& abs(mp.x-mc->m.xy.x)<Minmovepx
	&& abs(mp.y-mc->m.xy.y)<Minmovepx){
		if(reg > 0)
			frredrawrange(f, pt0, p0, p1);
		else if(reg < 0)
			frredrawrange(f, pt1, p1, p0);
		p1 = p0;
		pt1 = pt0;
	}
	if(p1 < p0){
		q = p0, p0 = p1, p1 = q;
		qt = pt0, pt0 = pt1, pt1 = qt;
	}
	if(p0 == p1)
		frtick(f, pt0, 0);
	frredrawrange(f, pt0, p0, p1);
	if(f->p0 == f->p1)	/* restore tick */
		frtick(f, frptofchar(f, f->p0), 1);
	flushimage(f->display, 1);
	*p1p = p1;
	return p0;
}

int
textselect23(Text *t, uint *q0, uint *q1, Image *high, int mask)
{
	uint p0, p1;
	int buts;

	p0 = xselect(&t->fr, mousectl, high, &p1);
	buts = mousectl->m.buttons;
	if((buts & mask) == 0){
		*q0 = p0+t->org;
		*q1 = p1+t->org;
	}

	while(mousectl->m.buttons)
		readmouse(mousectl);
	return buts;
}

int
textselect2(Text *t, uint *q0, uint *q1, Text **tp)
{
	int buts;

	*tp = nil;
	buts = textselect23(t, q0, q1, but2col, 4);
	if(buts & 4)
		return 0;
	if(buts & 1){	/* pick up argument */
		*tp = argtext;
		return 1;
	}
	return 1;
}

int
textselect3(Text *t, uint *q0, uint *q1)
{
	int h;

	h = (textselect23(t, q0, q1, but3col, 1|2) == 0);
	return h;
}

static Rune left1[] =  { '{', '[', '(', '<', 0xab, 0 };
static Rune right1[] = { '}', ']', ')', '>', 0xbb, 0 };
static Rune left2[] =  { '\n', 0 };
static Rune left3[] =  { '\'', '"', '`', 0 };

static
Rune *left[] = {
	left1,
	left2,
	left3,
	nil
};
static
Rune *right[] = {
	right1,
	left2,
	left3,
	nil
};

void
textdoubleclick(Text *t, uint *q0, uint *q1)
{
	int c, i;
	Rune *r, *l, *p;
	uint q;

	if(textclickhtmlmatch(t, q0, q1))
		return;

	for(i=0; left[i]!=nil; i++){
		q = *q0;
		l = left[i];
		r = right[i];
		/* try matching character to left, looking right */
		if(q == 0)
			c = '\n';
		else
			c = textreadc(t, q-1);
		p = runestrchr(l, c);
		if(p != nil){
			if(textclickmatch(t, c, r[p-l], 1, &q))
				*q1 = q-(c!='\n');
			return;
		}
		/* try matching character to right, looking left */
		if(q == t->file->b.nc)
			c = '\n';
		else
			c = textreadc(t, q);
		p = runestrchr(r, c);
		if(p != nil){
			if(textclickmatch(t, c, l[p-r], -1, &q)){
				*q1 = *q0+(*q0<t->file->b.nc && c=='\n');
				*q0 = q;
				if(c!='\n' || q!=0 || textreadc(t, 0)=='\n')
					(*q0)++;
			}
			return;
		}
	}

	/* try filling out word to right */
	while(*q1<t->file->b.nc && isalnum(textreadc(t, *q1)))
		(*q1)++;
	/* try filling out word to left */
	while(*q0>0 && isalnum(textreadc(t, *q0-1)))
		(*q0)--;
}

int
textclickmatch(Text *t, int cl, int cr, int dir, uint *q)
{
	Rune c;
	int nest;

	nest = 1;
	for(;;){
		if(dir > 0){
			if(*q == t->file->b.nc)
				break;
			c = textreadc(t, *q);
			(*q)++;
		}else{
			if(*q == 0)
				break;
			(*q)--;
			c = textreadc(t, *q);
		}
		if(c == cr){
			if(--nest==0)
				return 1;
		}else if(c == cl)
			nest++;
	}
	return cl=='\n' && nest==1;
}

// Is the text starting at location q an html tag?
// Return 1 for <a>, -1 for </a>, 0 for no tag or <a />.
// Set *q1, if non-nil, to the location after the tag.
static int
ishtmlstart(Text *t, uint q, uint *q1)
{
	int c, c1, c2;

	if(q+2 > t->file->b.nc)
		return 0;
	if(textreadc(t, q++) != '<')
		return 0;
	c = textreadc(t, q++);
	c1 = c;
	c2 = c;
	while(c != '>') {
		if(q >= t->file->b.nc)
			return 0;
		c2 = c;
		c = textreadc(t, q++);
	}
	if(q1)
		*q1 = q;
	if(c1 == '/')	// closing tag
		return -1;
	if(c2 == '/' || c2 == '!')	// open + close tag or comment
		return 0;
	return 1;
}

// Is the text ending at location q an html tag?
// Return 1 for <a>, -1 for </a>, 0 for no tag or <a />.
// Set *q0, if non-nil, to the start of the tag.
static int
ishtmlend(Text *t, uint q, uint *q0)
{
	int c, c1, c2;

	if(q < 2)
		return 0;
	if(textreadc(t, --q) != '>')
		return 0;
	c = textreadc(t, --q);
	c1 = c;
	c2 = c;
	while(c != '<') {
		if(q == 0)
			return 0;
		c1 = c;
		c = textreadc(t, --q);
	}
	if(q0)
		*q0 = q;
	if(c1 == '/')	// closing tag
		return -1;
	if(c2 == '/' || c2 == '!')	// open + close tag or comment
		return 0;
	return 1;
}

int
textclickhtmlmatch(Text *t, uint *q0, uint *q1)
{
	int depth, n;
	uint q, nq;

	q = *q0;
	// after opening tag?  scan forward for closing tag
	if(ishtmlend(t, q, nil) == 1) {
		depth = 1;
		while(q < t->file->b.nc) {
			n = ishtmlstart(t, q, &nq);
			if(n != 0) {
				depth += n;
				if(depth == 0) {
					*q1 = q;
					return 1;
				}
				q = nq;
				continue;
			}
			q++;
		}
	}

	// before closing tag?  scan backward for opening tag
	if(ishtmlstart(t, q, nil) == -1) {
		depth = -1;
		while(q > 0) {
			n = ishtmlend(t, q, &nq);
			if(n != 0) {
				depth += n;
				if(depth == 0) {
					*q0 = q;
					return 1;
				}
				q = nq;
				continue;
			}
			q--;
		}
	}

	return 0;
}

uint
textbacknl(Text *t, uint p, uint n)
{
	Font *font;
	int mintab, maxtab, maxw;
	int w, wl, wr, seentab;
	uint p0;
	Rune r;

	font = t->fr.font;
	mintab = stringnwidth(font, " ", 1);
	maxtab = t->fr.maxtab;
	maxw = Dx(t->fr.r);

	/*
	 * Skip over n display lines' worth of runes going backwards from p.
	 * n==0 has the same meaning as n==1, except there is no need to go
	 * anywhere if p is at a (physical) line boundary.
	 */
	if(n==0 && p>0 && textreadc(t, p-1)!='\n')
		n = 1;
	for(; n>0 && p>0; n--){
		/* Loop invariant: p is at the first rune after a display line break. */
		if(textreadc(t, p-1) == '\n')
			p--;
		/*
		 * wl is the width of the (non-tab) runes to the left of the last
		 * seen tab; wr is the width of everything to the right of that tab
		 * until the end of the current display line (until, and excluding,
		 * the rune that p is at prior to entering the inner loop).
		 */
		seentab = FALSE;
		w = wl = wr = 0;
		for(p0=p; p>0; p--){
			r = textreadc(t, p-1);
			if(r == '\n')
				break;
			if(r == '\t'){
				seentab = TRUE;
				wl = 0;
				wr = w;
				w += maxtab;
			}else{
				wl += runestringnwidth(font, &r, 1);
				w = wl;
				if(seentab)
					w += wr + max(mintab, maxtab-wl%maxtab);
			}
			if(w > maxw){
				/* If even a single rune does not fit, force one anyway. */
				if(p == p0)
					p--;
				break;
			}
		}
	}
	return p;
}

uint
textforwardnl(Text *t, uint p, uint n)
{
	Font *font;
	int mintab, maxtab, maxw, w;
	uint end, p0;
	Rune r;

	font = t->fr.font;
	mintab = stringnwidth(font, " ", 1);
	maxtab = t->fr.maxtab;
	maxw = Dx(t->fr.r);
	end = t->file->b.nc;

	/*
	 * Skip over the next n display lines' worth of runes starting at p.
	 * n==0 has the same meaning as n==1, except there is no need to go
	 * anywhere if p is at a (physical) line boundary.
	 */
	if(n==0 && p>0 && textreadc(t, p-1)!='\n')
		n = 1;
	for(; n>0 && p<end; n--){
		/* Loop invariant: p is at the first rune after a display line break. */
		w = 0;
		for(p0=p; p<end; p++){
			r = textreadc(t, p);
			if(r == '\n'){
				p++;
				break;
			}
			if(r == '\t')
				w += max(mintab, maxtab-w%maxtab);
			else
				w += runestringnwidth(font, &r, 1);
			if(w > maxw){
				/* If even a single rune does not fit, force one anyway. */
				if(p == p0)
					p++;
				break;
			}
		}
	}
	return p;
}

void
textsetorigin(Text *t, uint org)
{
	int fixup;
	uint n;
	Rune *r;

	fixup = FALSE;
	if(org >= t->org){
		n = org-t->org;
		frdelete(&t->fr, 0, n);
		/*
		 * Since frdelete() does not know what follows, it can leave
		 * the end of the last line in the wrong selection mode.
		 */
		fixup = (n < t->fr.nchars);
	}else if(n=t->org-org, n<t->fr.nchars){
		r = runemalloc(n);
		bufread(&t->file->b, org, r, n);
		frinsert(&t->fr, r, r+n, 0);
		free(r);
	}else
		frdelete(&t->fr, 0, t->fr.nchars);
	t->org = org;
	textfill(t);
	textscrdraw(t);
	textsetselect(t, t->q0, t->q1);
	if(fixup && t->fr.p1>t->fr.p0)
		frdrawsel(&t->fr, frptofchar(&t->fr, t->fr.p1-1), t->fr.p1-1, t->fr.p1, 1);
}

void
textreset(Text *t)
{
	t->file->seq = 0;
	t->eq0 = ~0;
	/* do t->delete(0, t->nc, TRUE) without building backup stuff */
	textsetselect(t, t->org, t->org);
	frdelete(&t->fr, 0, t->fr.nchars);
	t->org = 0;
	t->q0 = 0;
	t->q1 = 0;
	filereset(t->file);
	bufreset(&t->file->b);
}
