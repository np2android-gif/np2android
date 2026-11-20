/**
 * @file	dmac.c
 * @brief	Implementation of the DMA controller
 */

#include "compiler.h"
#include "dmac.h"
#include	"cpucore.h"
#include	"iocore.h"
#include	"sound.h"
#include	"cs4231.h"
#include	"sasiio.h"


void DMACCALL dma_dummyout(REG8 data) {

	(void)data;
}

REG8 DMACCALL dma_dummyin(void) {

	return(0xff);
}

REG8 DMACCALL dma_dummyproc(REG8 func) {

	(void)func;
	return(0);
}

static const DMAPROC dmaproc[] = {
		{dma_dummyout,		dma_dummyin,		dma_dummyproc},		// NONE
		{fdc_datawrite,		fdc_dataread,		fdc_dmafunc},		// 2HD
		{fdc_datawrite,		fdc_dataread,		fdc_dmafunc},		// 2DD
#if defined(SUPPORT_SASI)
		{sasi_datawrite,	sasi_dataread,		sasi_dmafunc},		// SASI
#else
		{dma_dummyout,		dma_dummyin,		dma_dummyproc},		// SASI
#endif
		{dma_dummyout,		dma_dummyin,		dma_dummyproc},		// SCSI
#if !defined(DISABLE_SOUND)
		{dma_dummyout,		dma_dummyin,		cs4231dmafunc},		// CS4231
#else
		{dma_dummyout,		dma_dummyin,		dma_dummyproc},		// SASI
#endif
};


// ----

void dmac_check(void) {

	BOOL	workchg;
	DMACH	ch;
	REG8	bit;

	workchg = FALSE;
	ch = dmac.dmach;
	bit = 1;
	do {
		if ((!(dmac.mask & bit)) && (ch->ready)) {
			if (!(dmac.work & bit)) {
				dmac.work |= bit;
				if (ch->proc.extproc(DMAEXT_START)) {
					dmac.stat &= ~bit;
					dmac.working |= bit;
					workchg = TRUE;
				}
			}
		}
		else {
			if (dmac.work & bit) {
				dmac.work &= ~bit;
				dmac.working &= ~bit;
				ch->proc.extproc(DMAEXT_BREAK);
				workchg = TRUE;
			}
		}
		bit <<= 1;
		ch++;
	} while(bit & 0x0f);
	if (workchg) {
		nevent_forceexit();
	}
}

UINT dmac_getdatas(DMACH dmach, UINT8 *buf, UINT size) {

	UINT	leng;
	UINT32	addr;
	UINT	i;

	leng = min(dmach->leng.w, size);
	if (leng) {
		addr = dmach->adrs.d;					// + mask
		if (!(dmach->mode & 0x20)) {			// dir +
			for (i=0; i<leng; i++) {
				buf[i] = MEMP_READ8(addr + i);
			}
			dmach->adrs.d += leng;
		}
		else {									// dir -
			for (i=0; i<leng; i++) {
				buf[i] = MEMP_READ8(addr - i);
			}
			dmach->adrs.d -= leng;
		}
		dmach->leng.w -= leng;
		if (dmach->leng.w == 0) {
			dmach->proc.extproc(DMAEXT_END);
		}
	}
	return(leng);
}


// ---- I/O

static void IOOUTCALL dmac_o01(UINT port, REG8 dat) {

	DMACH	dmach;
	int		lh;

	dmach = dmac.dmach + ((port >> 2) & 3);
	lh = dmac.lh;
	dmac.lh = (UINT8)(lh ^ 1);
	dmach->adrs.b[lh + DMA32_LOW] = dat;
	dmach->adrsorg.b[lh] = dat;
}

static void IOOUTCALL dmac_o03(UINT port, REG8 dat) {

	int		ch;
	DMACH	dmach;
	int		lh;

	ch = (port >> 2) & 3;
	dmach = dmac.dmach + ch;
	lh = dmac.lh;
	dmac.lh = lh ^ 1;
	dmach->leng.b[lh] = dat;
	dmach->lengorg.b[lh] = dat;
	dmac.stat &= ~(1 << ch);
}

static void IOOUTCALL dmac_o13(UINT port, REG8 dat) {

	dmac.dmach[dat & 3].sreq = dat;
	(void)port;
}

static void IOOUTCALL dmac_o15(UINT port, REG8 dat) {

	if (dat & 4) {
		dmac.mask |= (1 << (dat & 3));
	}
	else {
		dmac.mask &= ~(1 << (dat & 3));
	}
	dmac_check();
	(void)port;
}

static void IOOUTCALL dmac_o17(UINT port, REG8 dat) {

	dmac.dmach[dat & 3].mode = dat;
	(void)port;
}

static void IOOUTCALL dmac_o19(UINT port, REG8 dat) {

	dmac.lh = DMA16_LOW;
	(void)port;
	(void)dat;
}

static void IOOUTCALL dmac_o1b(UINT port, REG8 dat) {

	dmac.mask = 0x0f;
	(void)port;
	(void)dat;
}

static void IOOUTCALL dmac_o1f(UINT port, REG8 dat) {

	dmac.mask = dat;
	dmac_check();
	(void)port;
}

static void IOOUTCALL dmac_o21(UINT port, REG8 dat) {

	DMACH	dmach;

	dmach = dmac.dmach + (((port >> 1) + 1) & 3);
#if defined(CPUCORE_IA32)
	dmach->adrs.b[DMA32_HIGH + DMA16_LOW] = dat;
	/* ANDROID-FDC: 24～31 ビットは 0E05h～0E0Bh で入れる。バンクを書いたら 0 に戻す (NP21/W rev107 の dmac_o21 と同じ) */
	dmach->adrs.b[DMA32_HIGH + DMA16_HIGH] = 0;
#else
	// IA16では ver0.75で無効、ver0.76で修正
	dmach->adrs.b[DMA32_HIGH + DMA16_LOW] = dat & 0x0f;
#endif
}

#if defined(CPUCORE_IA32)
/* ANDROID-FDC: DMA の番地の 24～31 ビット (0E05h=ch0、0E07h=ch1、0E09h=ch2、0E0Bh=ch3)。NP21/W rev107 の dmac_oe05 から。
 * Windows NT/2000 は FD のバッファを 16MB より上に置き、ここに上位を書く。無いと 16MB より下の別の場所に転送していた */
static void IOOUTCALL dmac_oe05(UINT port, REG8 dat) {

	DMACH	dmach;

	dmach = dmac.dmach + (((port - 0x05) & 0x07) >> 1);
	dmach->adrs.b[DMA32_HIGH + DMA16_HIGH] = dat;
}
#endif

static void IOOUTCALL dmac_o29(UINT port, REG8 dat) {

	DMACH	dmach;

	dmach = dmac.dmach + (dat & 3);
	dmach->bound = dat;
	(void)port;
}

static REG8 IOINPCALL dmac_i01(UINT port) {

	DMACH	dmach;
	int		lh;

	dmach = dmac.dmach + ((port >> 2) & 3);
	lh = dmac.lh;
	dmac.lh = lh ^ 1;
	return(dmach->adrs.b[lh + DMA32_LOW]);
}

static REG8 IOINPCALL dmac_i03(UINT port) {

	DMACH	dmach;
	int		lh;

	dmach = dmac.dmach + ((port >> 2) & 3);
	lh = dmac.lh;
	dmac.lh = lh ^ 1;
	return(dmach->leng.b[lh]);
}

static REG8 IOINPCALL dmac_i11(UINT port) {

	(void)port;
	return(dmac.stat);												// ToDo!!
}


// ---- I/F

static const IOOUT dmaco00[16] = {
					dmac_o01,	dmac_o03,	dmac_o01,	dmac_o03,
					dmac_o01,	dmac_o03,	dmac_o01,	dmac_o03,
					NULL,		dmac_o13,	dmac_o15,	dmac_o17,
					dmac_o19,	dmac_o1b,	NULL,		dmac_o1f};

static const IOINP dmaci00[16] = {
					dmac_i01,	dmac_i03,	dmac_i01,	dmac_i03,
					dmac_i01,	dmac_i03,	dmac_i01,	dmac_i03,
					dmac_i11,	NULL,		NULL,		NULL,
					NULL,		NULL,		NULL,		NULL};

static const IOOUT dmaco21[8] = {
					dmac_o21,	dmac_o21,	dmac_o21,	dmac_o21,
					dmac_o29,	NULL,		NULL,		NULL};

void dmac_reset(const NP2CFG *pConfig) {

	ZeroMemory(&dmac, sizeof(dmac));
	dmac.lh = DMA16_LOW;
	dmac.mask = 0xf;
	dmac_procset();
//	TRACEOUT(("sizeof(_DMACH) = %d", sizeof(_DMACH)));

	(void)pConfig;
}

void dmac_bind(void) {

	iocore_attachsysoutex(0x0001, 0x0ce1, dmaco00, 16);
	iocore_attachsysinpex(0x0001, 0x0ce1, dmaci00, 16);
	iocore_attachsysoutex(0x0021, 0x0cf1, dmaco21, 8);
#if defined(CPUCORE_IA32)
	iocore_attachout(0x0e05, dmac_oe05);		/* ANDROID-FDC */
	iocore_attachout(0x0e07, dmac_oe05);
	iocore_attachout(0x0e09, dmac_oe05);
	iocore_attachout(0x0e0b, dmac_oe05);
#endif
}


// ----

static void dmacset(REG8 channel) {

	DMADEV		*dev;
	DMADEV		*devterm;
	UINT		dmadev;

	dev = dmac.device;
	devterm = dev + dmac.devices;
	dmadev = DMADEV_NONE;
	while(dev < devterm) {
		if (dev->channel == channel) {
			dmadev = dev->device;
		}
		dev++;
	}
	if (dmadev >= NELEMENTS(dmaproc)) {
		dmadev = 0;
	}
//	TRACEOUT(("dmac set %d - %d", channel, dmadev));
	dmac.dmach[channel].proc = dmaproc[dmadev];
}

void dmac_procset(void) {

	REG8	i;

	for (i=0; i<4; i++) {
		dmacset(i);
	}
}

void dmac_attach(REG8 device, REG8 channel) {

	dmac_detach(device);

	if (dmac.devices < NELEMENTS(dmac.device)) {
		dmac.device[dmac.devices].device = device;
		dmac.device[dmac.devices].channel = channel;
		dmac.devices++;
		dmacset(channel);
	}
}

void dmac_detach(REG8 device) {

	DMADEV	*dev;
	DMADEV	*devterm;
	REG8	ch;

	dev = dmac.device;
	devterm = dev + dmac.devices;
	while(dev < devterm) {
		if (dev->device == device) {
			break;
		}
		dev++;
	}
	if (dev < devterm) {
		ch = dev->channel;
		dev++;
		while(dev < devterm) {
			*(dev - 1) = *dev;
			dev++;
		}
		dmac.devices--;
		dmacset(ch);
	}
}

