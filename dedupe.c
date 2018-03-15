#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#ifdef __linux__
#include <alloca.h>
#endif
#include <stdio.h>
#include <getopt.h>
#include <time.h>
#include <talloc.h>

struct dedupe_state
{
	int verbose;
	int dryrun;
	int relaxed;

	void (*hash_function)(const void*, size_t, unsigned char*);
	size_t hashsize;

	size_t dircount;
	char** dirs;

	int tty;
	int width;
	time_t last;
};

static int parse_cmdline(struct dedupe_state*, int, char**);
static void check_terminal(struct dedupe_state*);
static void scan_directory(struct dedupe_state*, char*);
static void print_progress(struct dedupe_state*, const char*, int, int);

static void hash_md5(const void*, size_t, unsigned char*);
static void hash_sha1(const void*, size_t, unsigned char*);
static void hash_sha256(const void*, size_t, unsigned char*);

static void talloc_pfree_char(char**);
static void pclosedir(DIR**);

int main(int argc, char** argv)
{
	struct dedupe_state* state = talloc_zero(
		talloc_autofree_context(),
		struct dedupe_state);

	if (parse_cmdline(state, argc, argv))
		return 1;

	check_terminal(state);

	for (size_t i = 0; i < state->dircount; ++i)
		scan_directory(state, state->dirs[i]);

	// TODO:

	if (state->verbose && state->tty)
		puts("\e[K");

	return 0;
}

static int parse_cmdline(struct dedupe_state* state, int argc, char** argv)
{
	static const struct option long_options[] =
	{
		{"verbose", no_argument, NULL, 'v'},
		{"dry-run", no_argument, NULL, 'n'},
		{"hash", required_argument, NULL, 'h'},
		{"relaxed", no_argument, NULL, 'x'},
		{}
	};

	size_t argsz = argc * sizeof(char*);
	char** nargv = alloca(argsz);
	memcpy(nargv, argv, argsz);

	state->hash_function = hash_sha1;
	state->hashsize = 20;

	int result, index;
	while ((result = getopt_long(argc, nargv, "vnh:x", long_options, &index)) != -1)
	{
		switch (result)
		{
			case 'v':
				state->verbose = 1;
				break;
			case 'n':
				state->dryrun = 1;
				break;
			case 'x':
				state->relaxed = 1;
				break;
			case 'h':
				if (!strcmp(optarg, "md5"))
				{
					state->hash_function = hash_md5;
					state->hashsize = 16;
				}
				else if (!strcmp(optarg, "sha1"))
				{
					state->hash_function = hash_sha1;
					state->hashsize = 20;
				}
				else if (!strcmp(optarg, "sha256"))
				{
					state->hash_function = hash_sha256;
					state->hashsize = 32;
				}
				else
				{
					fprintf(stderr, "%s: invalid hash: \'%s\'\n", argv[0], optarg);
					return 1;
				}
				break;
			default:
				return 1;
		}
	}

	state->dirs = talloc_array(state, char*, argc);
	for (int i = optind; i < argc; ++i)
	{
		char* dir = talloc_strdup(state->dirs, nargv[i]);
		size_t length = strlen(dir);
		while (length && dir[--length] == '/')
			dir[length] = 0;
		state->dirs[state->dircount++] = dir;
	}

	if (!state->dircount)
		state->dirs[state->dircount++] = talloc_strdup(state->dirs, ".");

	return 0;
}

static void check_terminal(struct dedupe_state* state)
{
	if ((state->tty = isatty(STDOUT_FILENO)))
	{
		struct winsize w;
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
			state->width = w.ws_col;
	}

	if (state->width < 1)
		state->width = 80;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	state->last = ts.tv_sec;
}

static void scan_directory(struct dedupe_state* state, char* directory)
{
	print_progress(state, directory, 0, -1);

	__attribute__((cleanup(pclosedir)))
	DIR* d = opendir(directory);
	if (!d)
	{
		perror(directory);
		return;
	}

	int fd = dirfd(d);
	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;

		__attribute__((cleanup(talloc_pfree_char)))
		char* fullpath = talloc_asprintf(directory, "%s/%s", directory, e->d_name);

		if (e->d_type == DT_DIR)
		{
			scan_directory(state, fullpath);
		}
		else if (e->d_type == DT_REG)
		{
			// TODO: Find / insert into per-inode map

			struct stat buffer;
			if (fstatat(fd, e->d_name, &buffer, AT_SYMLINK_NOFOLLOW) == -1)
			{
				perror(fullpath);
				continue;
			}

			// TODO: Find / insert into per-size map
		}
	}
}

static void print_progress(struct dedupe_state* state, const char* status, int count, int max)
{
	if (!state->verbose)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	if (state->last == ts.tv_sec)
		return;

	int progress = max > 0;
	if (state->tty)
	{
		// TODO: Render fancy ANSI-colored progress bar
		if (progress)
			printf("\e[K[%d/%d] %s\r", count, max, status);
		else
			printf("\e[K%s\r", status);
	}
	else
	{
		if (progress)
			printf("[%d/%d] %s\n", count, max, status);
		else
			printf("%s\n", status);
	}

	state->last = ts.tv_sec;
	fflush(stdout);
}

static void hash_md5(const void* data, size_t size, unsigned char* hash)
{
	// TODO:
	memset(hash, 0, 16);
}

static void hash_sha1(const void* data, size_t size, unsigned char* hash)
{
	// TODO:
	memset(hash, 0, 20);
}

static void hash_sha256(const void* data, size_t size, unsigned char* hash)
{
	// TODO:
	memset(hash, 0, 32);
}

static void talloc_pfree_char(char** pp)
{
	talloc_free(*pp);
}

static void pclosedir(DIR** pd)
{
	if (*pd)
		closedir(*pd);
}
