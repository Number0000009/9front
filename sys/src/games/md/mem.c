#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

u16int ram[32768], vram[32768];
u16int cram[64], vsram[40];
u32int cramc[64];
u8int zram[8192];
u8int reg[32];
u8int ctl[15];

u8int dma;
u8int vdplatch;
u16int vdpaddr, vdpdata;

u8int z80bus = RESET;
u16int z80bank;

u8int
regread(u16int a)
{
	u16int v;

	switch(a | 1){
	case 0x0001: return 0xa0;
	case 0x0003:
		v = keys;
		if((ctl[0] & 0x40) == 0)
			v >>= 8;
		return ctl[0] & 0xc0 | v & 0x3f;
	case 0x0005:
		return ctl[1] & 0xc0 | 0x3f;
	case 0x0007:
	case 0x0009: case 0x000b: case 0x000d:
		return ctl[a-3>>1];
	case 0x1101: return (~z80bus & BUSACK) << 7;
	}
	sysfatal("read from 0xa1%.4ux (pc=%#.6ux)", a, curpc);
	return 0;
}

void
regwrite(u16int a, u16int v)
{
	switch(a | 1){
	case 0x0003: case 0x0005: case 0x0007:
	case 0x0009: case 0x000b: case 0x000d:
		ctl[a-3>>1] = v;
		return;
	case 0x1101:
		z80bus = z80bus & ~BUSREQ | v >> 8 & BUSREQ;
		return;
	case 0x1201:
		if((v & 1<<8) == 0){
			z80bus |= RESET;
			z80bus &= ~BUSACK;
		}else
			z80bus &= ~RESET;
		return;
	}
	sysfatal("write to 0xa1%.4x", a);
}

void
vdpwrite(u16int v)
{
	u8int a;

	if((vdplatch & 0x80) == 0){
		if((v & 0xc000) == 0x8000){
			a = v >> 8 & 0x1f;
			reg[a] = v & 0xff;
			if(a == 0x0c)
				vdpmode();
			vdplatch = 0;
			return;
		}
		vdplatch = vdplatch & 0xfc | v >> 14 | 0x80;
		vdpaddr = vdpaddr & 0xc000 | v & 0x3fff;
	}else{
		vdplatch = vdplatch & 0x03 | v >> 2 & 0x1c;
		vdpaddr = vdpaddr & 0x3fff | v << 14 & 0xc000;
		if((v & 0x80) != 0 && (reg[MODE2] & DMAEN) != 0){
			dma = reg[23] >> 6 & 3;
			if(dma == 0)
				dma++;
		}
	}
}

void
cramwrite(u16int a, u16int v)
{
	u32int w;

	cram[a/2] = v;
	w = v << 12 & 0xe00000 | v << 8 & 0xe000 | v << 4 & 0xe0;
	cramc[a/2] = w;
}

u16int
memread(u32int a)
{
	u16int v;

	switch(a >> 21 & 7){
	case 0: case 1: return prg[(a % nprg) / 2];
	case 5:
		switch(a >> 16 & 0xff){
		case 0xa0:
			if((z80bus & BUSACK) != 0)
				v = z80read(a & 0x7fff);
			else
				v = 0;
			return v << 8 | v;
		case 0xa1:
			v = regread(a);
			return v << 8 | v;
		}
		goto invalid;
	case 6:
		if((a & 0xe700e0) != 0xc00000)
			goto invalid;
		switch(a & 30){
		case 0: case 2:
			vdplatch &= 0x7f;
			switch(vdplatch & 0xf){
			case 0:
				v = vram[vdpaddr/2];
				vdpaddr += reg[AUTOINC];
				break;
			case 4:
				v = vdpaddr & 0x7f;
				if(v < 80)
					v = vsram[v / 2];
				else
					v = 0;
				vdpaddr = (vdpaddr + reg[AUTOINC]) & 0x7f;
				break;
			case 8:
				v = cram[(vdpaddr & 0x7f) / 2];
				vdpaddr = (vdpaddr + reg[AUTOINC]) & 0x7f;
				break;
			default: v = 0;
			}
			return v;
		case 4: case 6:
			vdplatch &= 0x7f;
			v = vdpstat;
			if(dma != 0 && dma != 2)
				v |= STATDMA;
			if(vdpx >= 0xe4 || vdpx < 0x08)
				v |= STATHBL;
			return v;
		default:
			goto invalid;
		}
	case 7: return ram[((u16int)a) / 2];
	default:
	invalid:
		sysfatal("read from %#.6ux (pc=%#.6ux)", a, curpc);
		return 0;
	}
}

void
memwrite(u32int a, u16int v, u16int m)
{
	u16int *p;
	u16int w;

	switch((a >> 21) & 7){
	case 5:
		switch(a >> 16 & 0xff){
		case 0xa0:
			if((z80bus & BUSACK) != 0)
				z80write(a & 0x7fff, v >> 8);
			return;
		case 0xa1:
			regwrite(a, v >> 8);
			return;
		default:
			goto invalid;
		}
	case 6:
		if((a & 0xe700e0) != 0xc00000)
			goto invalid;
		switch(a & 30){
		case 0: case 2:
			if(dma == 2){
				dma = 4;
				vdpdata = v >> 8;
				p = &vram[vdpaddr / 2];
				if((vdpaddr & 1) != 0)
					*p = *p & 0xff | v << 8;
				else
					*p = *p & 0xff00 | v & 0xff;
				return;
			}
			vdplatch &= 0x7f;
			switch(vdplatch & 0xf){
			case 1:
				if((vdpaddr & 1) != 0)
					v = v << 8 | v >> 8;
				p = &vram[vdpaddr / 2];
				*p = *p & ~m | v & m;
				vdpaddr += reg[AUTOINC];
				return;
			case 3:
				cramwrite(vdpaddr & 0x7f, v);
				vdpaddr = (vdpaddr + reg[AUTOINC]) & 0x7f;
				return;
			case 5:
				w = vdpaddr & 0x7f;
				if(w < 80)
					vsram[w / 2] = v;
				vdpaddr = (vdpaddr + reg[AUTOINC]) & 0x7f;
				return;
			default:
				return;
			}
		case 4: case 6:
			vdpwrite(v);
			return;
		case 16: case 18: case 20: case 22:
			return;
		default:
			goto invalid;
		}
	case 7:
		p = &ram[((u16int)a) / 2];
		*p = *p & ~m | v & m;
		break;
	default:
	invalid:
		sysfatal("write to %#.6x (pc=%#.6x)", a, curpc);
	}
}

void
dmastep(void)
{
	u16int v, *p;
	u32int a;
	
	switch(dma){
	case 1:
		a = reg[DMASRC0] << 1 | reg[DMASRC1] << 9 | reg[DMASRC2] << 17;
		v = memread(a);
		if(++reg[DMASRC0] == 0)
			reg[DMASRC1]++;
		switch(vdplatch & 0x7){
		case 1:
			if((vdpaddr & 1) != 0)
				v = v >> 8 | v << 8;
			vram[vdpaddr / 2] = v;
			break;
		case 3:
			if(vdpaddr > 0x7f)
				dma = 0;
			else
				cramwrite(vdpaddr, v);
			break;
		case 5:
			if(vdpaddr < 80)
				vsram[vdpaddr / 2] = v;
			break;
		}
		break;
	case 2:
		return;
	case 3:
		a = reg[DMASRC0] | reg[DMASRC1] << 8;
		v = vram[a / 2];
		if((a & 1) == 0)
			v = v >> 8;
		if(++reg[DMASRC0] == 0)
			reg[DMASRC1]++;
		p = &vram[vdpaddr / 2];
		if((vdpaddr & 1) != 0)
			*p = *p & 0xff00 | v & 0xff;
		else
			*p = *p & 0xff | v << 8;
		break;
	case 4:
		p = &vram[vdpaddr / 2];
		if((vdpaddr & 1) == 0)
			*p = *p & 0xff00 | vdpdata;
		else
			*p = *p & 0xff | vdpdata << 8;
		break;
	}
	vdpaddr += reg[AUTOINC];
	if(--reg[DMACL] == 0 && reg[DMACH]-- == 0)
		dma = 0;
}

u8int
z80read(u16int a)
{
	switch(a >> 13){
	case 0:
	case 1:
		return zram[a & 0x1fff];
	case 2:
	case 3:
		sysfatal("z80 read from %#.4x\n", a);
	default:
		return memread(z80bank << 15 | a & 0x7fff);
	}
}

void
z80write(u16int a, u8int v)
{
	switch(a >> 13){
	case 0:
	case 1:
		zram[a & 0x1fff] = v;
		return;
	case 2:
	case 3:
		sysfatal("z80 write to %#.4x\n", a);
	default:
		memwrite(z80bank << 15 | a & 0x7ffe, v << 8 | v, (a & 1) != 0 ? 0xff : 0xff00);
	}
}

u32int irql[8] = {[6] INTVBL};

int
intack(int l)
{
	switch(l){
	case 4:
		irq &= ~INTHOR;
		break;
	case 6:
		irq &= ~INTVBL;
		break;
	}
	return 24 + l;
}
