#include <u.h>
#include <libc.h>
#include "complete.h"

/* Byte length of the longest common prefix of two strings. */
static ulong
lcprefixlen(char *a, char *b, ulong maxlen)
{
	ulong i;

	for(i=0; i<maxlen; i++)
		if(a[i]=='\0' || b[i]=='\0' || a[i]!=b[i])
			break;
	return i;
}

static int
strpcmp(const void *va, const void *vb)
{
	char *a, *b;

	a = *(char**)va;
	b = *(char**)vb;
	return strcmp(a, b);
}

Completion*
complete(char *dirpath, char *base)
{
	int fd;
	Dir *dir;
	long nfile, nmatch, i;
	ulong baselen, advance;
	ulong strsize, farrsize, fdatasize, size;
	char *pc, *pp, *p;
	Completion c;

	for(baselen=0; base[baselen]!='\0'; baselen++)
		if(base[baselen] == '/'){
			werrstr("the prefix to complete contains a slash character");
			return nil;
		}

	fd = open(dirpath, OREAD);
	if(fd < 0)
		return nil;
	nfile = dirreadall(fd, &dir);
	close(fd);
	if(nfile < 0)
		return nil;

	nmatch = 0;
	advance = -1;
	for(i=0; i<nfile; i++)
		if(strncmp(dir[i].name, base, baselen) == 0){
			dir[nmatch++] = dir[i];
			advance = lcprefixlen(dir[0].name+baselen, dir[i].name+baselen, advance);
		}
	if(nmatch > 0)
		nfile = nmatch;
	else
		advance = 0;

	strsize = advance + (nmatch==1) + 1;	/* plus '/' or ' ', plus '\0' */
	/* Round up to the pointer size to align the following filename array. */
	strsize += -strsize & (sizeof(char*)-1);
	farrsize = nfile * sizeof(char*);
	fdatasize = 0;
	for(i=0; i<nfile; i++)
		fdatasize += strlen(dir[i].name) + !!(dir[i].mode&DMDIR) + 1;	/* plus '/', plus '\0' */

	size = sizeof(Completion) + strsize + farrsize + fdatasize;
	pc = malloc(size);
	if(pc == nil){
		werrstr("cannot allocate a completion result of %uld bytes", size);
		free(dir);
		return nil;
	}
	c.advance = (nmatch==1 || advance>0);
	c.complete = (nmatch == 1);
	c.nmatch = nmatch;
	c.nfile = nfile;

	p = pc + sizeof(Completion);
	c.string = p;
	if(nmatch > 0){
		memmove(p, dir[0].name+baselen, advance);
		p += advance;
		if(nmatch == 1)
			*p++ = (dir[0].mode&DMDIR) ? '/' : ' ';
	}
	*p = '\0';

	/*
	 * The memcpy() shenanigans below are what modern C compilers require to
	 * guarantee that they will not abuse their standard-conferred freedom to
	 * take advantage of strict aliasing. Access via c.filename[i] pointers
	 * in qsort() here — as well as in the caller code — is not problematic,
	 * since aliasing inference does not cross translation unit boundaries.
	 */
	pp = pc + sizeof(Completion) + strsize;
	c.filename = (char**)pp;
	p = pp + farrsize;
	for(i=0; i<nfile; i++){
		memcpy(pp, &p, sizeof(p));
		pp += sizeof(p);
		p += sprint(p, "%s%s", dir[i].name, (dir[i].mode&DMDIR) ? "/" : "") + 1;
	}
	qsort(c.filename, nfile, sizeof(c.filename[0]), strpcmp);

	free(dir);
	memcpy(pc, &c, sizeof(c));
	return (Completion*)pc;
}
