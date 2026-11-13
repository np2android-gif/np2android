#include	"compiler.h"
// #include	<sys/time.h>
// #include	<signal.h>
// #include	<unistd.h>
#include	"scrnmng.h"
#include	"scrndraw.h"
#include	"vramhdl.h"
#include	"menubase.h"

static SDL_Window *s_sdlWindow;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static SDL_Surface *s_surface;

typedef struct {
	BOOL		enable;
	int			width;
	int			height;
	int			bpp;
	SDL_Surface	*surface;
	VRAMHDL		vram;
} SCRNMNG;

typedef struct {
	int		width;
	int		height;
} SCRNSTAT;

static const char app_name[] = "Neko Project II";

static	SCRNMNG		scrnmng;
static	SCRNSTAT	scrnstat;
static	SCRNSURF	scrnsurf;

/*
 * ANDROID-SCRN: 描画面は 640x480 で作り、画面に出す高さ (s_disph) はゲストが使う高さに合わせて
 * 400 か 480 にする。ゲストが 400 ラインより多く描くとき (Windows 2000 のセットアップなどの
 * 640x480) だけ 480 にし、それ以外は今までどおり 640x400 を出す。高さは xnp2 本体の
 * dispsync.c が scrnmng_setheight() で知らせる (dsync.scrnymax)。
 */
static	int			s_disph = 400;
static	int			s_logh = 400;		/* SDL の論理サイズの高さ (400 か 480) */

typedef struct {
	int		xalign;
	int		yalign;
	int		width;
	int		height;
	int		srcpos;
	int		dstpos;
} DRAWRECT;

static BRESULT calcdrawrect(SDL_Surface *surface,
								DRAWRECT *dr, VRAMHDL s, const RECT_T *rt) {

	int		pos;

	dr->xalign = surface->format->BytesPerPixel;
	dr->yalign = surface->pitch;
	dr->srcpos = 0;
	dr->dstpos = 0;
	dr->width = min(scrnmng.width, s->width);
	dr->height = min(scrnmng.height, s->height);
	if (rt) {
		pos = max(rt->left, 0);
		dr->srcpos += pos;
		dr->dstpos += pos * dr->xalign;
		dr->width = min(rt->right, dr->width) - pos;

		pos = max(rt->top, 0);
		dr->srcpos += pos * s->width;
		dr->dstpos += pos * dr->yalign;
		dr->height = min(rt->bottom, dr->height) - pos;
	}
	if ((dr->width <= 0) || (dr->height <= 0)) {
		return(FAILURE);
	}
	return(SUCCESS);
}


void scrnmng_initialize(void) {

	scrnstat.width = 640;
	scrnstat.height = 400;
}

BRESULT scrnmng_create(int width, int height) {

	SDL_Surface		*surface;
	SDL_PixelFormat	*fmt;
	BOOL			r;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
		fprintf(stderr, "Error: SDL_Init: %s\n", SDL_GetError());
		return(FAILURE);
	}
	/*
	 * Android: 全画面のウィンドウとして作る。0 (全画面でない) だと SDL の Java 側が
	 * FLAG_FORCE_NOT_FULLSCREEN を付け、Android 10 以前ではアプリ側でシステムバーを隠しても
	 * ステータスバーが表示されたままになり、エミュレータ画面の上端と重なっていた
	 * (Android 11 以降はこのフラグが無視されるので問題が出ない)。
	 * Android では画面モードの切り替えは起きず、描画面の大きさも変わらない。
	 */
	s_sdlWindow = SDL_CreateWindow(app_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_FULLSCREEN_DESKTOP);
	s_renderer = SDL_CreateRenderer(s_sdlWindow, -1, 0);
	SDL_RenderSetLogicalSize(s_renderer, width, s_logh);	/* ANDROID-SCRN: 表示は 400 ラインから */
#if defined(SCREEN_BPP) && (SCREEN_BPP == 32)
	/*
	 * Android: GLES2 レンダラーは RGB565 テクスチャを持てず、SDL_UpdateTexture のたびに CPU で
	 * 32bit へ変換していた (Blit_RGB565_32、CPU 時間の約 4%)。np2 の RGB32 (メモリ上 B,G,R,X) と
	 * 同じ SDL_PIXELFORMAT_RGB888 で描けば変換なしで転送できる。
	 */
	s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, width, height);
	s_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
#else
	s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, width, height);
	s_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 16, 0xf800, 0x07e0, 0x001f, 0);
#endif

	surface = s_surface;
	r = FALSE;
	fmt = surface->format;
#if defined(SUPPORT_8BPP)
	if (fmt->BitsPerPixel == 8) {
		r = TRUE;
	}
#endif
#if defined(SUPPORT_16BPP)
	if ((fmt->BitsPerPixel == 16) && (fmt->Rmask == 0xf800) &&
		(fmt->Gmask == 0x07e0) && (fmt->Bmask == 0x001f)) {
		r = TRUE;
	}
#endif
#if defined(SUPPORT_24BPP)
	if (fmt->BitsPerPixel == 24) {
		r = TRUE;
	}
#endif
#if defined(SUPPORT_32BPP)
	if (fmt->BitsPerPixel == 32) {
		r = TRUE;
	}
#endif
#if defined(SCREEN_BPP)
	if (fmt->BitsPerPixel != SCREEN_BPP) {
		r = FALSE;
	}
#endif
	if (r) {
		scrnmng.enable = TRUE;
		scrnmng.width = width;
		scrnmng.height = height;
		scrnmng.bpp = fmt->BitsPerPixel;
		return(SUCCESS);
	}
	else {
		fprintf(stderr, "Error: Bad screen mode");
		return(FAILURE);
	}
}

void scrnmng_destroy(void) {

	scrnmng.enable = FALSE;
}

RGB16 scrnmng_makepal16(RGB32 pal32) {

	RGB16	ret;

	ret = (pal32.p.r & 0xf8) << 8;
#if defined(SIZE_QVGA)
	ret += (pal32.p.g & 0xfc) << (3 + 16);
#else
	ret += (pal32.p.g & 0xfc) << 3;
#endif
	ret += pal32.p.b >> 3;
	return(ret);
}

void scrnmng_setwidth(int posx, int width) {

	scrnstat.width = width;
}

void scrnmng_setheight(int posy, int height) {

	scrnstat.height = height;
}

/* ANDROID-SCRN: 画面に出している高さ (400 か 480)。タッチの移動量の換算 (port/taskmng.c) に使う */
int scrnmng_getdispheight(void) {

	return(s_disph);
}

/* ANDROID-SCRN: 描画面のうち、ゲストが使う高さの分だけを画面に出す。メニューを開いている間は
   メニューを作ったときの高さのまま変えない。
   ウィンドウが 640x480 の比より縦長のとき (縦画面。Kotlin 側が枠を 4:3 にする) は、論理サイズを
   常に 640x480 にして絵を上に寄せる。640x400 のときは下に 80 ライン分の黒い帯が出るが、絵の大きさと
   上端は 640x480 のときと変わらない。横長のとき (横画面) は論理サイズを表示する高さに合わせ、
   高さいっぱいに出す */
static void present(void) {

	SDL_Rect	rect;
	int			h;
	int			ww;
	int			wh;
	int			logh;

	if (menuvram == NULL) {
		h = (scrnstat.height > 400) ? 480 : 400;
		if (h != s_disph) {
			s_disph = h;
		}
	}
	ww = 0;
	wh = 0;
	SDL_GetRendererOutputSize(s_renderer, &ww, &wh);
	logh = ((ww > 0) && (wh * scrnmng.width >= ww * 480)) ? 480 : s_disph;
	if (logh != s_logh) {
		s_logh = logh;
		SDL_RenderSetLogicalSize(s_renderer, scrnmng.width, logh);
	}
	rect.x = 0;
	rect.y = 0;
	rect.w = scrnmng.width;
	rect.h = s_disph;
	SDL_RenderClear(s_renderer);
	SDL_RenderCopy(s_renderer, s_texture, &rect, &rect);
	SDL_RenderPresent(s_renderer);
}

const SCRNSURF *scrnmng_surflock(void) {

	SDL_Surface	*surface;

	if (scrnmng.vram == NULL) {
		surface = s_surface;
		if (surface == NULL) {
			return(NULL);
		}
		SDL_LockSurface(surface);
		scrnmng.surface = surface;
		scrnsurf.ptr = (UINT8 *)surface->pixels;
		scrnsurf.xalign = surface->format->BytesPerPixel;
		scrnsurf.yalign = surface->pitch;
		scrnsurf.bpp = surface->format->BitsPerPixel;
	}
	else {
		scrnsurf.ptr = scrnmng.vram->ptr;
		scrnsurf.xalign = scrnmng.vram->xalign;
		scrnsurf.yalign = scrnmng.vram->yalign;
		scrnsurf.bpp = scrnmng.vram->bpp;
	}
	scrnsurf.width = min(scrnstat.width, 640);
	scrnsurf.height = min(scrnstat.height, 480);		/* ANDROID-SCRN */
	scrnsurf.extend = 0;
	return(&scrnsurf);
}

static void draw_onmenu(void) {

	RECT_T		rt;
	SDL_Surface	*surface;
	DRAWRECT	dr;
const UINT8		*p;
	UINT8		*q;
const UINT8		*a;
	int			salign;
	int			dalign;
	int			x;

	rt.left = 0;
	rt.top = 0;
	rt.right = min(scrnstat.width, 640);
	rt.bottom = s_disph;		/* ANDROID-SCRN */
#if defined(SIZE_QVGA)
	rt.right >>= 1;
	rt.bottom >>= 1;
#endif

	surface = s_surface;
	if (surface == NULL) {
		return;
	}
	SDL_LockSurface(surface);
	if (calcdrawrect(surface, &dr, menuvram, &rt) == SUCCESS) {
		switch(scrnmng.bpp) {
#if defined(SUPPORT_16BPP)
			case 16:
				p = scrnmng.vram->ptr + (dr.srcpos * 2);
				q = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x] == 0) {
							*(UINT16 *)q = *(UINT16 *)(p + (x * 2));
						}
						q += dr.xalign;
					} while(++x < dr.width);
					p += salign * 2;
					q += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
#if defined(SUPPORT_24BPP)
			case 24:
				p = scrnmng.vram->ptr + (dr.srcpos * 3);
				q = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x] == 0) {
							q[0] = p[x*3+0];
							q[1] = p[x*3+1];
							q[2] = p[x*3+2];
						}
						q += dr.xalign;
					} while(++x < dr.width);
					p += salign * 3;
					q += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
#if defined(SUPPORT_32BPP)
			case 32:
				p = scrnmng.vram->ptr + (dr.srcpos * 4);
				q = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x] == 0) {
							*(UINT32 *)q = *(UINT32 *)(p + (x * 4));
						}
						q += dr.xalign;
					} while(++x < dr.width);
					p += salign * 4;
					q += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
		}
	}
	SDL_UnlockSurface(surface);

	SDL_UpdateTexture(s_texture, NULL, surface->pixels, surface->pitch);
	present();		/* ANDROID-SCRN */
}

void scrnmng_surfunlock(const SCRNSURF *surf) {

	SDL_Surface	*surface;

	if (surf) {
		if (scrnmng.vram == NULL) {
			if (scrnmng.surface != NULL) {
				surface = scrnmng.surface;
				scrnmng.surface = NULL;
				SDL_UnlockSurface(surface);

				SDL_UpdateTexture(s_texture, NULL, surface->pixels, surface->pitch);
				present();		/* ANDROID-SCRN */
			}
		}
		else {
			if (menuvram) {
				draw_onmenu();
			}
		}
	}
}


// ----

BRESULT scrnmng_entermenu(SCRNMENU *smenu) {

	if (smenu == NULL) {
		goto smem_err;
	}
	vram_destroy(scrnmng.vram);
	scrnmng.vram = vram_create(scrnmng.width, s_disph, FALSE,	/* ANDROID-SCRN */
																scrnmng.bpp);
	if (scrnmng.vram == NULL) {
		goto smem_err;
	}
	scrndraw_redraw();
	smenu->width = scrnmng.width;
	smenu->height = s_disph;		/* ANDROID-SCRN */
	smenu->bpp = (scrnmng.bpp == 32)?24:scrnmng.bpp;
	return(SUCCESS);

smem_err:
	return(FAILURE);
}

void scrnmng_leavemenu(void) {

	VRAM_RELEASE(scrnmng.vram);
}

void scrnmng_menudraw(const RECT_T *rct) {

	SDL_Surface	*surface;
	DRAWRECT	dr;
const UINT8		*p;
const UINT8		*q;
	UINT8		*r;
	UINT8		*a;
	int			salign;
	int			dalign;
	int			x;

	if ((!scrnmng.enable) && (menuvram == NULL)) {
		return;
	}
	surface = s_surface;
	if (surface == NULL) {
		return;
	}
	SDL_LockSurface(surface);
	if (calcdrawrect(surface, &dr, menuvram, rct) == SUCCESS) {
		switch(scrnmng.bpp) {
#if defined(SUPPORT_16BPP)
			case 16:
				p = scrnmng.vram->ptr + (dr.srcpos * 2);
				q = menuvram->ptr + (dr.srcpos * 2);
				r = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x]) {
							if (a[x] & 2) {
								*(UINT16 *)r = *(UINT16 *)(q + (x * 2));
							}
							else {
								a[x] = 0;
								*(UINT16 *)r = *(UINT16 *)(p + (x * 2));
							}
						}
						r += dr.xalign;
					} while(++x < dr.width);
					p += salign * 2;
					q += salign * 2;
					r += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
#if defined(SUPPORT_24BPP)
			case 24:
				p = scrnmng.vram->ptr + (dr.srcpos * 3);
				q = menuvram->ptr + (dr.srcpos * 3);
				r = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x]) {
							if (a[x] & 2) {
								r[RGB24_B] = q[x*3+0];
								r[RGB24_G] = q[x*3+1];
								r[RGB24_R] = q[x*3+2];
							}
							else {
								a[x] = 0;
								r[0] = p[x*3+0];
								r[1] = p[x*3+1];
								r[2] = p[x*3+2];
							}
						}
						r += dr.xalign;
					} while(++x < dr.width);
					p += salign * 3;
					q += salign * 3;
					r += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
#if defined(SUPPORT_32BPP)
			case 32:
				p = scrnmng.vram->ptr + (dr.srcpos * 4);
				q = menuvram->ptr + (dr.srcpos * 3);
				r = (UINT8 *)surface->pixels + dr.dstpos;
				a = menuvram->alpha + dr.srcpos;
				salign = menuvram->width;
				dalign = dr.yalign - (dr.width * dr.xalign);
				do {
					x = 0;
					do {
						if (a[x]) {
							if (a[x] & 2) {
								((RGB32 *)r)->p.b = q[x*3+0];
								((RGB32 *)r)->p.g = q[x*3+1];
								((RGB32 *)r)->p.r = q[x*3+2];
							//	((RGB32 *)r)->p.e = 0;
							}
							else {
								a[x] = 0;
								*(UINT32 *)r = *(UINT32 *)(p + (x * 4));
							}
						}
						r += dr.xalign;
					} while(++x < dr.width);
					p += salign * 4;
					q += salign * 3;
					r += dalign;
					a += salign;
				} while(--dr.height);
				break;
#endif
		}
	}
	SDL_UnlockSurface(surface);

	SDL_UpdateTexture(s_texture, NULL, surface->pixels, surface->pitch);
	present();		/* ANDROID-SCRN */
}

