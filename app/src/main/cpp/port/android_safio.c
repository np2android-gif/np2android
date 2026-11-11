/**
 * @file	android_safio.c
 * @brief	SAF の fd を仮想パスで開く (android_safio.h 参照)
 */

#include "compiler.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#include "dosio.h"
#include "android_safio.h"
#include "np2bridge.h"

#define LOG_TAG		"NP2SAF"
#define LOGI(...)	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* 履歴の上限より多めに取る */
#define SAFIO_MAX	32
#define SAFIO_IDLEN	40

typedef struct {
	char	id[SAFIO_IDLEN];
	char	path[MAX_PATH];
	int		fd;				/* -1 = 空き */
	BOOL	readonly;
	dev_t	dev;			/* 書き込みエラーの FILE* がどのエントリーか調べる用 */
	ino_t	ino;
	UINT32	lasterror;		/* 最後にエラーを通知した時刻 (連続して通知しない) */
	int		subfd;			/* 付属ファイル (.cue の .bin)。-1 = 無し。読み取り専用 */
} SAFENTRY;

static SAFENTRY			s_entry[SAFIO_MAX];
static BOOL				s_init;
static pthread_mutex_t	s_lock = PTHREAD_MUTEX_INITIALIZER;

/* s_lock を取ってから呼ぶ */
static void init_locked(void)
{
	int i;

	if (!s_init) {
		for (i = 0; i < SAFIO_MAX; i++) {
			s_entry[i].fd = -1;
			s_entry[i].subfd = -1;
		}
		s_init = TRUE;
	}
}

static SAFENTRY *find_id_locked(const char *id)
{
	int i;

	for (i = 0; i < SAFIO_MAX; i++) {
		if ((s_entry[i].fd >= 0) && (strcmp(s_entry[i].id, id) == 0)) {
			return &s_entry[i];
		}
	}
	return NULL;
}

static SAFENTRY *find_path_locked(const char *path)
{
	int i;

	for (i = 0; i < SAFIO_MAX; i++) {
		if ((s_entry[i].fd >= 0) && (strcmp(s_entry[i].path, path) == 0)) {
			return &s_entry[i];
		}
	}
	return NULL;
}

/*
 * 付属ファイルを持つエントリーで、path が同じ ID の別の名前 ("saf:/<id>/<何か>") なら返す。
 * .cue の FILE 行の名前は、大文字小文字やフォルダ名の付け方が SAF の表示名と合わないことが
 * あるので、名前は比べない (付属ファイルは 1 つだけなので取り違えない)。
 */
static SAFENTRY *find_sub_locked(const char *path)
{
	size_t len;
	int i;

	if (!safio_isvirtual(path)) {
		return NULL;
	}
	path += sizeof(SAFIO_PREFIX) - 1;
	for (i = 0; i < SAFIO_MAX; i++) {
		if ((s_entry[i].fd >= 0) && (s_entry[i].subfd >= 0)) {
			len = strlen(s_entry[i].id);
			if ((strncmp(path, s_entry[i].id, len) == 0) && (path[len] == '/')) {
				return &s_entry[i];
			}
		}
	}
	return NULL;
}

static void close_sub_locked(SAFENTRY *e)
{
	if (e->subfd >= 0) {
		close(e->subfd);
		e->subfd = -1;
	}
}

BOOL safio_isvirtual(const char *path)
{
	return ((path != NULL) && (strncmp(path, SAFIO_PREFIX, sizeof(SAFIO_PREFIX) - 1) == 0)) ? TRUE : FALSE;
}

static BOOL valid_id(const char *id)
{
	size_t len;
	const char *p;

	len = strlen(id);
	if ((len == 0) || (len >= SAFIO_IDLEN)) {
		return FALSE;
	}
	for (p = id; *p; p++) {
		if (!(((*p >= '0') && (*p <= '9')) || ((*p >= 'a') && (*p <= 'z')) ||
			  ((*p >= 'A') && (*p <= 'Z')) || (*p == '-') || (*p == '_'))) {
			return FALSE;
		}
	}
	return TRUE;
}

/* UTF-8 の途中で切らないよう、len 以下で文字の境界になる長さを返す */
static size_t utf8_cut(const char *s, size_t len)
{
	while ((len > 0) && ((((unsigned char)s[len]) & 0xc0) == 0x80)) {
		len--;
	}
	return len;
}

/*
 * "saf:/<id>/<name>" を作る。name の '/' は '_' にし、収まらないときは拡張子を残して名前を切り詰める。
 */
static BRESULT make_path(char *out, size_t size, const char *id, const char *name)
{
	char base[MAX_PATH];
	const char *ext;
	size_t head;
	size_t room;
	size_t baselen;
	size_t extlen;
	char *p;

	head = (size_t)SDL_snprintf(out, size, "%s%s/", SAFIO_PREFIX, id);
	if (head + 1 >= size) {
		return FAILURE;
	}
	room = size - head - 1;

	ext = strrchr(name, '.');
	if ((ext == NULL) || (ext == name)) {
		ext = name + strlen(name);
	}
	extlen = strlen(ext);
	if (extlen >= room) {
		return FAILURE;
	}
	baselen = (size_t)(ext - name);
	if (baselen + extlen > room) {
		baselen = utf8_cut(name, room - extlen);
	}
	if (baselen >= sizeof(base)) {
		baselen = utf8_cut(name, sizeof(base) - 1);
	}
	memcpy(base, name, baselen);
	base[baselen] = '\0';
	if (baselen == 0) {
		SDL_strlcpy(base, "disk", sizeof(base));
	}
	SDL_snprintf(out + head, size - head, "%s%s", base, ext);
	for (p = out + head; *p; p++) {
		if (*p == '/') {
			*p = '_';
		}
	}
	return SUCCESS;
}

BRESULT safio_register(const char *id, const char *name, int fd, BOOL readonly,
														char *path, int size)
{
	struct stat sb;
	SAFENTRY *e;
	int newfd;
	int i;

	if ((id == NULL) || (name == NULL) || (path == NULL) || (size <= 0) || (!valid_id(id))) {
		LOGE("register: bad argument (id=%s)", id ? id : "(null)");
		return FAILURE;
	}
	/* パイプやソケット (クラウド系プロバイダーが返すことがある) はランダムアクセスできない */
	if ((fstat(fd, &sb) != 0) || (!S_ISREG(sb.st_mode)) || (lseek(fd, 0, SEEK_CUR) < 0)) {
		LOGE("register: fd %d is not a seekable regular file (id=%s)", fd, id);
		return FAILURE;
	}
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (newfd < 0) {
		LOGE("register: dup(%d) failed: %s", fd, strerror(errno));
		return FAILURE;
	}

	pthread_mutex_lock(&s_lock);
	init_locked();
	e = find_id_locked(id);
	if (e != NULL) {
		LOGI("register: replace id=%s fd %d -> %d", id, e->fd, newfd);
		close(e->fd);
		e->fd = -1;
		close_sub_locked(e);
	}
	else {
		for (i = 0; i < SAFIO_MAX; i++) {
			if (s_entry[i].fd < 0) {
				e = &s_entry[i];
				break;
			}
		}
	}
	if ((e == NULL) || (make_path(e->path, sizeof(e->path), id, name) != SUCCESS)) {
		pthread_mutex_unlock(&s_lock);
		LOGE("register: %s (id=%s)", (e == NULL) ? "table full" : "name too long", id);
		close(newfd);
		return FAILURE;
	}
	SDL_strlcpy(e->id, id, sizeof(e->id));
	e->fd = newfd;
	e->readonly = readonly;
	e->dev = sb.st_dev;
	e->ino = sb.st_ino;
	e->lasterror = 0;
	SDL_strlcpy(path, e->path, size);
	pthread_mutex_unlock(&s_lock);

	LOGI("register: %s fd=%d ro=%d size=%lld", path, newfd, readonly ? 1 : 0, (long long)sb.st_size);
	return SUCCESS;
}

BRESULT safio_register_sub(const char *id, int fd)
{
	struct stat sb;
	SAFENTRY *e;
	int newfd;

	if ((id == NULL) || (!valid_id(id))) {
		return FAILURE;
	}
	if ((fstat(fd, &sb) != 0) || (!S_ISREG(sb.st_mode)) || (lseek(fd, 0, SEEK_CUR) < 0)) {
		LOGE("register sub: fd %d is not a seekable regular file (id=%s)", fd, id);
		return FAILURE;
	}
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (newfd < 0) {
		LOGE("register sub: dup(%d) failed: %s", fd, strerror(errno));
		return FAILURE;
	}
	pthread_mutex_lock(&s_lock);
	init_locked();
	e = find_id_locked(id);
	if (e == NULL) {
		pthread_mutex_unlock(&s_lock);
		LOGE("register sub: id=%s is not registered", id);
		close(newfd);
		return FAILURE;
	}
	close_sub_locked(e);
	e->subfd = newfd;
	pthread_mutex_unlock(&s_lock);
	LOGI("register sub: id=%s fd=%d size=%lld", id, newfd, (long long)sb.st_size);
	return SUCCESS;
}

void safio_unregister(const char *id)
{
	SAFENTRY *e;

	if (id == NULL) {
		return;
	}
	pthread_mutex_lock(&s_lock);
	init_locked();
	e = find_id_locked(id);
	if (e != NULL) {
		LOGI("unregister: %s fd=%d", e->path, e->fd);
		close(e->fd);
		e->fd = -1;
		close_sub_locked(e);
		e->path[0] = '\0';
		e->id[0] = '\0';
	}
	pthread_mutex_unlock(&s_lock);
}

FILE *safio_fopen(const char *path, const char *mode)
{
	SAFENTRY *e;
	BOOL write;
	int srcfd;
	int newfd;
	FILE *fp;

	write = ((strchr(mode, '+') != NULL) || (strchr(mode, 'w') != NULL) ||
			 (strchr(mode, 'a') != NULL)) ? TRUE : FALSE;
	if (strchr(mode, 'w') != NULL) {
		/* 切り詰め・新規作成は扱わない (新規イメージは作業フォルダに作る) */
		LOGE("open: %s: create mode \"%s\" is not supported", path, mode);
		return NULL;
	}

	pthread_mutex_lock(&s_lock);
	init_locked();
	e = find_path_locked(path);
	srcfd = (e != NULL) ? e->fd : -1;
	if (e == NULL) {
		/* .cue から名前で開かれる .bin */
		e = find_sub_locked(path);
		if (e != NULL) {
			srcfd = e->subfd;
			if (write) {
				mode = "rb";
			}
		}
	}
	if (e == NULL) {
		pthread_mutex_unlock(&s_lock);
		LOGE("open: %s: not registered", path);
		return NULL;
	}
	if ((write) && (e->readonly)) {
		mode = "rb";
	}
	/* 位置は dup 元と共有されるが、np2 は読み書きの前に必ずシークするので問題ない */
	newfd = fcntl(srcfd, F_DUPFD_CLOEXEC, 0);
	pthread_mutex_unlock(&s_lock);
	if (newfd < 0) {
		LOGE("open: %s: dup failed: %s", path, strerror(errno));
		return NULL;
	}
	lseek(newfd, 0, SEEK_SET);
	fp = fdopen(newfd, mode);
	if (fp == NULL) {
		LOGE("open: %s: fdopen(%d, %s) failed: %s", path, newfd, mode, strerror(errno));
		close(newfd);
		return NULL;
	}
#if !defined(NDEBUG)
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "open: %s fd=%d mode=%s", path, newfd, mode);
#endif
	return fp;
}

short safio_attr(const char *path)
{
	SAFENTRY *e;
	short attr;

	pthread_mutex_lock(&s_lock);
	init_locked();
	e = find_path_locked(path);
	if (e == NULL) {
		attr = (find_sub_locked(path) != NULL) ? FILEATTR_READONLY : -1;
	}
	else {
		attr = e->readonly ? FILEATTR_READONLY : 0;
	}
	pthread_mutex_unlock(&s_lock);
	return attr;
}

void safio_ioerror(FILE *fp)
{
	struct stat sb;
	char path[MAX_PATH];
	UINT32 now;
	int err;
	int i;

	err = errno;
	if ((fp == NULL) || (fstat(fileno(fp), &sb) != 0)) {
		return;
	}
	path[0] = '\0';
	now = SDL_GetTicks();
	pthread_mutex_lock(&s_lock);
	init_locked();
	for (i = 0; i < SAFIO_MAX; i++) {
		SAFENTRY *e = &s_entry[i];
		if ((e->fd >= 0) && (e->dev == sb.st_dev) && (e->ino == sb.st_ino)) {
			LOGE("write error: %s: %s", e->path, strerror(err));
			/*
			 * 読み取り専用で開いた HDD への書き込みは失敗して当然 (マウント時に知らせてある)。
			 * それ以外で同じイメージへの失敗は続けて起きるので、通知は 10 秒に 1 回まで
			 */
			if ((!e->readonly) &&
				((e->lasterror == 0) || ((now - e->lasterror) > 10000))) {
				e->lasterror = now ? now : 1;
				SDL_strlcpy(path, e->path, sizeof(path));
			}
			break;
		}
	}
	pthread_mutex_unlock(&s_lock);
	if (path[0] != '\0') {
		np2bridge_diskerror(path);
	}
}

void safio_syncall(void)
{
	int i;

	pthread_mutex_lock(&s_lock);
	init_locked();
	for (i = 0; i < SAFIO_MAX; i++) {
		if ((s_entry[i].fd >= 0) && (!s_entry[i].readonly)) {
			if (fsync(s_entry[i].fd) != 0) {
				LOGE("sync: %s: %s", s_entry[i].path, strerror(errno));
			}
		}
	}
	pthread_mutex_unlock(&s_lock);
}
