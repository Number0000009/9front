/*
 * allocate Blocks from uncached memory
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Bdead		= 0x51494F42,	/* "QIOB" */
};

struct
{
	Lock;
	ulong	bytes;
} ucialloc;

static Block*
_ucallocb(int size)
{
	Block *b;
	uintptr addr;

	if((b = ucalloc(sizeof(Block)+size+Hdrspc)) == nil)
		return nil;

	b->next = nil;
	b->list = nil;
	b->free = nil;
	b->flag = 0;

	/* align start of data portion by rounding up */
	addr = (uintptr)b;
	addr = ROUND(addr + sizeof(Block), BLOCKALIGN);
	b->base = (uchar*)addr;

	/* align end of data portion by rounding down */
	b->lim = (uchar*)b + msize(b);
	addr = (uintptr)b->lim;
	addr &= ~(BLOCKALIGN-1);
	b->lim = (uchar*)addr;

	/* leave sluff at beginning for added headers */
	b->rp = b->lim - ROUND(size, BLOCKALIGN);
	if(b->rp < b->base)
		panic("_ucallocb");
	b->wp = b->rp;

	return b;
}

Block*
ucallocb(int size)
{
	Block *b;

	/*
	 * Check in a process and wait until successful.
	 * Can still error out of here, though.
	 */
	if(up == nil)
		panic("ucallocb without up: %#p", getcallerpc(&size));
	if((b = _ucallocb(size)) == nil)
		panic("ucallocb: no memory for %d bytes", size);
	setmalloctag(b, getcallerpc(&size));

	return b;
}

Block*
uciallocb(int size)
{
	Block *b;
	static int m1, m2, mp;

	if((b = _ucallocb(size)) == nil){
		if(0 && (m2++%10000)==0){
			if(mp++ > 1000)
				panic("uciallocb: out of memory");
			iprint("uciallocb: no memory %lud/%lud\n",
				ucialloc.bytes, conf.ialloc);
		}
		return nil;
	}
	setmalloctag(b, getcallerpc(&size));
	b->flag = BINTR;

	ilock(&ucialloc);
	ucialloc.bytes += b->lim - b->base;
	iunlock(&ucialloc);

	return b;
}

void
ucfreeb(Block *b)
{
	void *dead = (void*)Bdead;

	if(b == nil)
		return;

	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if(b->free != nil) {
		b->free(b);
		return;
	}
	if(b->flag & BINTR) {
		ilock(&ucialloc);
		ucialloc.bytes -= b->lim - b->base;
		iunlock(&ucialloc);
	}

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	ucfree(b);
}
