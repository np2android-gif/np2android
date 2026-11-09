/**
 * @file	safesave.c
 * @brief	ファイルを壊さずに書き換えるための補助 (safesave.h 参照)
 */

#include "compiler.h"
#include <android/log.h>
#include "dosio.h"
#include "statsave.h"
#include "safesave.h"

void safesave_tmpname(char *tmp, int size, const char *path)
{
	SDL_snprintf(tmp, size, "%s.tmp", path);
}

BRESULT safesave_commit(const char *tmp, const char *path, BOOL ok)
{
	if (ok) {
		if (rename(tmp, path) == 0) {
			return SUCCESS;
		}
		__android_log_print(ANDROID_LOG_ERROR, "np2", "rename %s -> %s: %s", tmp, path, strerror(errno));
	}
	remove(tmp);
	return FAILURE;
}

int safesave_statsave(const char *path)
{
	char tmp[MAX_PATH + 8];
	int ret;

	safesave_tmpname(tmp, sizeof(tmp), path);
	ret = statsave_save(tmp);
	if (safesave_commit(tmp, path, (ret == STATFLAG_SUCCESS)) != SUCCESS) {
		/* path にあった前のセーブは残る */
		ret |= STATFLAG_FAILURE;
	}
	return ret;
}
