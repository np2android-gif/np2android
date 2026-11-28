/**
 * @file	pcm86c.c
 * @brief	Implementation of the 86-PCM
 */

#include "compiler.h"
#include "pcm86.h"
#include "pccore.h"
#include "cpucore.h"
#include "iocore.h"
#include "fmboard.h"

/* ƒTƒ“ƒvƒŠƒ“ƒOƒŒ[ƒg‚É8Š|‚¯‚½•¨ */
const UINT pcm86rate8[] = {352800, 264600, 176400, 132300,
							88200,  66150,  44010,  33075};

/* 32,24,16,12, 8, 6, 4, 3 - Å­Œö”{”: 96 */
/*  3, 4, 6, 8,12,16,24,32 */

static const UINT clk25_128[] = {
					0x00001bde, 0x00002527, 0x000037bb, 0x00004a4e,
					0x00006f75, 0x0000949c, 0x0000df5f, 0x00012938};
static const UINT clk20_128[] = {
					0x000016a4, 0x00001e30, 0x00002d48, 0x00003c60,
					0x00005a8f, 0x000078bf, 0x0000b57d, 0x0000f17d};


	PCM86CFG	pcm86cfg;


void pcm86gen_initialize(UINT rate)
{
	pcm86cfg.rate = rate;
}

void pcm86gen_setvol(UINT vol)
{
	pcm86cfg.vol = vol;
	pcm86gen_update();
}

void pcm86_reset(void)
{
	PCM86 pcm86 = &g_pcm86;

	memset(pcm86, 0, sizeof(*pcm86));
	pcm86->fifosize = 0x80;
	pcm86->dactrl = 0x32;
	pcm86->stepmask = (1 << 2) - 1;
	pcm86->stepbit = 2;
	pcm86->stepclock = (pccore.baseclock << 6);
	pcm86->stepclock /= 44100;
	pcm86->stepclock *= pccore.multiple;
	pcm86->rescue = (PCM86_RESCUE * 32) << 2;
	pcm86->irq = 0xff;
}

void pcm86gen_update(void)
{
	PCM86 pcm86 = &g_pcm86;

	pcm86->volume = pcm86cfg.vol * pcm86->vol5;
	pcm86_setpcmrate(pcm86->fifo);
}

void pcm86_setpcmrate(REG8 val)
{
	PCM86 pcm86 = &g_pcm86;
	SINT32	rate;

	rate = pcm86rate8[val & 7];
	pcm86->stepclock = (pccore.baseclock << 6);
	pcm86->stepclock /= rate;
	pcm86->stepclock *= (pccore.multiple << 3);
	if (pcm86cfg.rate)
	{
		pcm86->div = (rate << (PCM86_DIVBIT - 3)) / pcm86cfg.rate;
		pcm86->div2 = (pcm86cfg.rate << (PCM86_DIVBIT + 3)) / rate;
	}
}

void pcm86_cb(NEVENTITEM item)
{
	PCM86 pcm86 = &g_pcm86;

	if (pcm86->reqirq)
	{
		sound_sync();
//		RECALC_NOWCLKP;
		if (pcm86->virbuf <= pcm86->fifosize)
		{
			pcm86->reqirq = 0;
			pcm86->irqflag = 1;
			if (pcm86->irq != 0xff)
			{
				pic_setirq(pcm86->irq);
			}
		}
		else
		{
			pcm86_setnextintr();
		}
	}
	(void)item;
}

void pcm86_setnextintr(void) {

	PCM86 pcm86 = &g_pcm86;
	SINT32	cnt;
	SINT64	clk;	/* ANDROID-CLOCK: SINT32 ‚Å‚Í 111 ”{‚©‚ç‚ ‚Ó‚ê‚é‚Ì‚Å 64 ƒrƒbƒg‚ÅŒvŽZ‚·‚é (Q17) */

	if (pcm86->fifo & 0x80)
	{
		cnt = pcm86->virbuf - pcm86->fifosize;
		if (cnt > 0)
		{
			cnt += pcm86->stepmask;
			cnt >>= pcm86->stepbit;
//			cnt += 4;								/* ‚¿‚å‚Á‚Æ‰„‘Ø‚³‚¹‚é */
			/* ‚±‚±‚Å clk = pccore.realclock * cnt / 86pcm_rate */
			/* clk = ((pccore.baseclock / 86pcm_rate) * cnt) * pccore.multiple */
			if (pccore.cpumode & CPUMODE_8MHZ) {
				clk = clk20_128[pcm86->fifo & 7];
			}
			else {
				clk = clk25_128[pcm86->fifo & 7];
			}
			/* cnt‚ÍÅ‘å 8000h ‚Å 32bit‚ÅŽû‚Ü‚é‚æ‚¤‚Éc */
			clk *= cnt;
			clk >>= 7;
//			clk++;						/* roundup */
			clk *= pccore.multiple;
			/* ANDROID-CLOCK: nevent ‚ÌŽžŠÔ‚Í SINT32 ‚ÅAnevent_set ‚Í¡‚ÌŒo‰ß•ª‚ð‘«‚·‚Ì‚Å—]—T‚ðŽc‚µ‚ÄŽ~‚ß‚éB
			   128 ”{‚Å FIFO ‚ª–ž”t‚Ì 4.1kHz (–ñ 8 •bæ) ‚Ì‚Æ‚«‚¾‚¯‰z‚¦‚éBŽ~‚ß‚é‚Ææ‚É pcm86_cb ‚ª—ˆ‚ÄA
			   ‚»‚±‚ÅŽc‚è‚ðŒvŽZ‚µ’¼‚·‚Ì‚ÅAÅŒã‚ÌŠ„‚èž‚Ý‚ÌŽž‚Í•Ï‚í‚ç‚È‚¢ */
			if (clk > 0x40000000) {
				clk = 0x40000000;
			}
			nevent_set(NEVENT_86PCM, (SINT32)clk, pcm86_cb, NEVENT_ABSOLUTE);
		}
	}
}

void SOUNDCALL pcm86gen_checkbuf(PCM86 pcm86)
{
	long	bufs;
	UINT32	past;

	past = CPU_CLOCK + CPU_BASECLOCK - CPU_REMCLOCK;
	past <<= 6;
	past -= pcm86->lastclock;
	if (past >= pcm86->stepclock)
	{
		past = past / pcm86->stepclock;
		pcm86->lastclock += (past * pcm86->stepclock);
		RECALC_NOWCLKWAIT(past);
	}

	bufs = pcm86->realbuf - pcm86->virbuf;
	if (bufs < 0)									/* ˆ——Ž‚¿‚Ä‚éc */
	{
		bufs &= ~3;
		pcm86->virbuf += bufs;
		if (pcm86->virbuf <= pcm86->fifosize)
		{
			pcm86->reqirq = 0;
			pcm86->irqflag = 1;
			if (pcm86->irq != 0xff)
			{
				pic_setirq(pcm86->irq);
			}
		}
		else
		{
			pcm86_setnextintr();
		}
	}
	else
	{
		bufs -= PCM86_EXTBUF;
		if (bufs > 0)
		{
			bufs &= ~3;
			pcm86->realbuf -= bufs;
			pcm86->readpos += bufs;
		}
	}
}

BOOL pcm86gen_intrq(void)
{
	PCM86 pcm86 = &g_pcm86;

	if (pcm86->irqflag)
	{
		return TRUE;
	}
	if (pcm86->fifo & 0x20)
	{
		sound_sync();
		if ((pcm86->reqirq) && (pcm86->virbuf <= pcm86->fifosize))
		{
			pcm86->reqirq = 0;
			pcm86->irqflag = 1;
			return TRUE;
		}
	}
	return FALSE;
}
