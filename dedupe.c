#include <unistd.h>
#include <string.h>
#ifdef __linux__
#include <alloca.h>
#endif
#include <stdio.h>
#include <getopt.h>
#include <talloc.h>

struct dedupe_state
{
	int verbose;
	int dryrun;

	void (*hash_function)(const void*, size_t, unsigned char*);
	size_t hashsize;

	size_t dircount;
	char** dirs;
};

static int parse_cmdline(struct dedupe_state*, int, char**);
static void hash_md5(const void*, size_t, unsigned char*);
static void hash_sha1(const void*, size_t, unsigned char*);
static void hash_sha256(const void*, size_t, unsigned char*);

int main(int argc, char** argv)
{
	struct dedupe_state* state = talloc_zero(
		talloc_autofree_context(),
		struct dedupe_state);

	if (parse_cmdline(state, argc, argv))
		return 1;

	// TODO:
	return 0;
}

static int parse_cmdline(struct dedupe_state* state, int argc, char** argv)
{
	static const struct option long_options[] =
	{
		{"verbose", no_argument, NULL, 'v'},
		{"dry-run", no_argument, NULL, 'n'},
		{"hash", required_argument, NULL, 'h'},
		{}
	};

	size_t argsz = argc * sizeof(char*);
	char** nargv = alloca(argsz);
	memcpy(nargv, argv, argsz);

	state->hash_function = hash_sha1;
	state->hashsize = 20;

	int result, index;
	while ((result = getopt_long(argc, nargv, "vnh:", long_options, &index)) != -1)
	{
		switch (result)
		{
			case 'v':
				state->verbose = 1;
				break;
			case 'n':
				state->dryrun = 1;
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

	return 0;
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
