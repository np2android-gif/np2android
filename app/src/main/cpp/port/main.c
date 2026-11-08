/**
 * @file	main.c
 * @brief	Android entry point (called from SDLActivity via libmain.so)
 */

#include "compiler.h"
#include "dosio.h"
#include "np2.h"
#include "np2bridge.h"
#include <android/log.h>

int SDL_main(int argc, char *argv[])
{
	char path[MAX_PATH];
	const char *ext;
	int ret;

	/*
	 * アプリ専用の外部ストレージ (Android/data/<pkg>/files) を作業ディレクトリにする。
	 * 権限不要で、scoped storage 環境でも読み書きできる。
	 */
	ext = SDL_AndroidGetExternalStoragePath();
	if (ext == NULL) {
		ext = SDL_AndroidGetInternalStoragePath();
	}
	if (ext == NULL) {
		__android_log_print(ANDROID_LOG_ERROR, "np2", "no storage path: %s", SDL_GetError());
		return 1;
	}
	SDL_snprintf(path, sizeof(path), "%s/", ext);
	__android_log_print(ANDROID_LOG_INFO, "np2", "data dir: %s", path);

	file_setcd(path);
	ret = np2_main(argc, argv);
	np2bridge_onexit();
	return ret;
}
