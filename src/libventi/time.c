#include <u.h>
#include <libc.h>
#include <venti.h>

int
vttimefmt(Fmt *fmt)
{
	vlong ms;
	Tm tm;

	if(fmt->flags & FmtSign){
		ms = va_arg(fmt->args, long);
		ms *= 1000;
	}else
		ms = nsec()/1000/1000;
	tm = *localtime(ms/1000);
	if(fmt->flags & FmtLong){
		return fmtprint(fmt, "%04d/%02d%02d %02d:%02d:%02d.%03d",
			tm.year+1900, tm.mon+1, tm.mday,
			tm.hour, tm.min, tm.sec, (int)(ms%1000));
	}else{
		return fmtprint(fmt, "%04d/%02d%02d %02d:%02d:%02d",
			tm.year+1900, tm.mon+1, tm.mday,
			tm.hour, tm.min, tm.sec);
	}
}
