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
#include "dat.h"
#include "fns.h"

enum
{
	None	= 0,
	Fore	= '+',
	Back	= '-'
};

enum
{
	Char,
	Line
};

int
isaddrc(Rune r)
{
	return r!=0 && utfrune("0123456789+-/$.#,;?", r)!=nil;
}

/*
 * Quite hard: could be almost anything but white space, but we
 * are a little conservative, aiming for regular expressions of
 * alphanumerics and no white space.
 */
int
isregexc(Rune r)
{
	return r!=0 && (isalnum(r) || utfrune("^+-.*?#,;[]()$", r)!=nil);
}

/*
 * Start at q and advance nl lines, being careful not to walk
 * past the end of the text, and then nr chars, being careful
 * not to walk past the end of the line.
 */
uint
nlcounttopos(Text *t, uint q, long nl, long nr)
{
	uint end;

	end = t->file->b.nc;
	while(nl>0 && q<end)
		if(textreadc(t, q++) == '\n')
			nl--;
	while(nr>0 && q<end && textreadc(t, q)!='\n'){
		q++;
		nr--;
	}
	return q;
}

static int
atlb(Text *t, uint q)
{
	/* Both extremities of the text count as line boundaries, too. */
	return q==0 || q==t->file->b.nc || textreadc(t, q-1)=='\n';
}

static uint
nextlb(Text *t, uint q)
{
	uint end;

	end = t->file->b.nc;
	while(q<end && textreadc(t, q++)!='\n')
		continue;
	return q;
}

static uint
prevlb(Text *t, uint q)
{
	if(q > 0)
		q--;	/* \n if at a line boundary, or something we would skip anyway */
	while(q>0 && textreadc(t, q-1)!='\n')
		q--;
	return q;
}

static Range
number(uint showerr, Text *t, Range r, uint n, int dir, int size, int *evalp)
{
	uint end, q0, q1;

	end = t->file->b.nc;
	if(dir == None){
		q0 = q1 = 0;
		dir = Fore;
	}else{
		q0 = r.q0;
		q1 = r.q1;
	}
	if(size == Char){
		if(dir == Fore){
			if(q1>end || end-q1<n)
				goto Rescue;
			q0 = q1 += n;
		}else if(dir == Back){
			if(q0==0 && n>0)	/* going backwards from 0 wraps around */
				q0 = end;
			if(q0 < n)
				goto Rescue;
			q1 = q0 -= n;
		}
	}else if(size == Line){
		if(dir == Fore){
			if(!atlb(t, q1))
				q1 = nextlb(t, q1);
			q0 = q1;
			while(n>0 && q1<end){
				q0 = q1;
				q1 = nextlb(t, q1);
				n--;
			}
			if(n == 1)	/* going past the end by one is allowed and taken as :$ */
				q0 = end;
		}else if(dir == Back){
			if(!atlb(t, q0))
				q0 = prevlb(t, q0);
			q1 = q0;
			while(n>0 && q0>0){
				q1 = q0;
				q0 = prevlb(t, q0);
				n--;
			}
			if(n == 1)	/* going over the top by one is allowed and taken as :0 */
				q1 = 0;
		}
		if(n > 1)
			goto Rescue;
	}
	*evalp = TRUE;
	return range(q0, q1);

Rescue:
	if(showerr)
		warning(nil, "address out of range\n");
	*evalp = FALSE;
	return r;
}

static Range
regexp(uint showerr, Text *t, Range lim, Range r, Rune *pat, int dir, int *foundp)
{
	int found;
	Rangeset sel;
	int q;

	if(pat[0]=='\0' && rxnull()){
		if(showerr)
			warning(nil, "no previous regular expression\n");
		*foundp = FALSE;
		return r;
	}
	if(pat[0]!='\0' && !rxcompile(pat)){
		*foundp = FALSE;
		return r;
	}
	if(dir == Back)
		found = rxbexecute(t, r.q0, &sel);
	else{
		if(lim.q0 < 0)
			q = Infinity;
		else
			q = lim.q1;
		found = rxexecute(t, nil, r.q1, q, &sel);
	}
	if(!found && showerr)
		warning(nil, "no match for regexp\n");
	*foundp = found;
	return sel.r[0];
}

Range
address(uint showerr, Text *t, Range lim, Range ar, void *a, uint q0, uint q1, Rune (*getc)(void*, uint),  int *evalp, uint *qp)
{
	Range r, nr;
	uint q, n, npat, npatalloc;
	int dir, size;
	Rune c, prevc, nextc, delim, *pat;

	r = ar;
	q = q0;
	dir = None;
	size = Line;
	c = '\0';
	while(q < q1){
		prevc = c;
		c = (*getc)(a, q++);
		switch(c){
		default:
			*qp = q-1;
			return r;
		case ';':
			ar = r;
			/* fall through */
		case ',':
			if(prevc == '\0')	/* lhs defaults to 0 */
				r.q0 = 0;
			if(q>=q1 && t!=nil && t->file!=nil)	/* rhs defaults to $ */
				r.q1 = t->file->b.nc;
			else{
				nr = address(showerr, t, lim, ar, a, q, q1, getc, evalp, &q);
				r.q1 = nr.q1;
			}
			*qp = q;
			return r;
		case '+':
		case '-':
			if(*evalp && (prevc=='+' || prevc=='-')){
				nextc = (*getc)(a, q);
				if(nextc!='#' && nextc!='/' && nextc!='?')
					r = number(showerr, t, r, 1, prevc, Line, evalp);	/* do previous one */
			}
			dir = c;
			break;
		case '.':
		case '$':
			if(q != q0+1){
				*qp = q-1;
				return r;
			}
			if(*evalp){
				if(c == '.')
					r = ar;
				else
					r = range(t->file->b.nc, t->file->b.nc);
			}
			if(q < q1)
				dir = Fore;
			else
				dir = None;
			break;
		case '#':
			if(q==q1 || (nextc=(*getc)(a, q), nextc<'0' || '9'<nextc)){
				*qp = q-1;
				return r;
			}
			c = nextc, q++;
			size = Char;
			/* fall through */
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = c-'0';
			while(q < q1){
				nextc = (*getc)(a, q);
				if(nextc<'0' || '9'<nextc)
					break;
				c = nextc, q++;
				n = n*10+(c-'0');
			}
			if(*evalp)
				r = number(showerr, t, r, n, dir, size, evalp);
			dir = None;
			size = Line;
			break;
		case '?':
			dir = Back;
			/* fall through */
		case '/':
			delim = c;
			npat = 0;
			npatalloc = 64;
			pat = runemalloc(npatalloc);
			while(q < q1){
				nextc = (*getc)(a, q);
				if(nextc == '\n')
					break;
				c = nextc, q++;
				if(c == delim)
					break;
				if(npatalloc-npat < 3)	/* space for two pattern runes and '\0' */
					pat = runerealloc(pat, npatalloc*=2);
				pat[npat++] = c;
				if(c=='\\' && q<q1)
					pat[npat++] = c = (*getc)(a, q++);
			}
			pat[npat] = '\0';
			if(*evalp)
				r = regexp(showerr, t, lim, r, pat, dir, evalp);
			free(pat);
			dir = None;
			size = Line;
			break;
		}
	}
	if(*evalp && dir!=None)
		r = number(showerr, t, r, 1, dir, Line, evalp);	/* do previous one */
	*qp = q;
	return r;
}
