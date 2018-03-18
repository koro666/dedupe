#include <sys/ioctl.h>
#include <sys/mman.h>
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
#include <stdbool.h>
#include <getopt.h>
#include <time.h>
#include <talloc.h>
#ifdef __FreeBSD__
#include <sha256.h>
#else
#include <openssl/sha.h>
#endif

struct dedupe_state
{
	bool verbose;
	bool dryrun;
	bool relaxed;

	size_t dircount;
	char** dirs;

	bool tty;
	int width;
	time_t last;

	struct hash_map* inode_lookup;
	struct hash_map* size_lookup;

	size_t tohash_count;
	struct inode_entry** tohash_list;

	struct hash_map* hash_lookup;
};

struct gather_tohash_state
{
	struct dedupe_state* state0;
	size_t capacity;
};

struct hash_map
{
	const struct hash_descriptor* descriptor;
	size_t item_count;
	size_t bucket_count;
	struct hash_bucket** buckets;
};

struct hash_descriptor
{
	size_t (*hash_function)(void*);
	bool (*equal_function)(void*, void*);
};

struct hash_bucket
{
	struct hash_bucket* next;
	void* key;
	size_t dummy;
	void* value;
};

struct inode_entry
{
	struct inode_entry* next_by_size;
	struct inode_entry* next_by_hash;

	struct stat buffer;
	unsigned char hash[32];
	struct path_entry* paths;
};

struct path_entry
{
	struct path_entry* next;
	char* path;
};

static int parse_cmdline(struct dedupe_state*, int, char**);
static void check_terminal(struct dedupe_state*);
static void scan_directory(struct dedupe_state*, int, char*, char*);
static void bucketize_by_size(void*, struct hash_bucket*);
static void gather_tohash(struct dedupe_state*);
static void gather_tohash_walkcb(void*, struct hash_bucket*);
static int gather_tohash_sortcb(const void*, const void*);
static void hash_inode(struct dedupe_state*, size_t, struct inode_entry*);
static void print_progress(struct dedupe_state*, const char*, size_t, size_t);

static struct hash_map* hash_map_create(void*, const struct hash_descriptor*, size_t);
static struct hash_bucket* hash_map_insert(struct hash_map*, void*);
static void hash_map_rehash(struct hash_map*);
static void hash_map_walk(struct hash_map*, void*, void(*)(void*, struct hash_bucket*));

static size_t hash_ptr_hash(void*);
static bool hash_ptr_equals(void*, void*);

static size_t hash_buf32_hash(void*);
static bool hash_buf32_equals(void*, void*);

static bool is_prime(size_t);
static size_t next_prime(size_t);

static struct hash_descriptor hash_ptr_descriptor =
{
	hash_ptr_hash,
	hash_ptr_equals
};

static struct hash_descriptor hash_buf32_descriptor =
{
	hash_buf32_hash,
	hash_buf32_equals
};

int main(int argc, char** argv)
{
	struct dedupe_state* state = talloc_zero(
		talloc_autofree_context(),
		struct dedupe_state);

	if (parse_cmdline(state, argc, argv))
		return 1;

	check_terminal(state);

	if (state->verbose && state->tty)
	{
		fputs("\n\n\e[2A\e[s", stdout);
		fflush(stdout);
	}

	state->inode_lookup = hash_map_create(state, &hash_ptr_descriptor, 0);
	for (size_t i = 0; i < state->dircount; ++i)
		scan_directory(state, AT_FDCWD, state->dirs[i], state->dirs[i]);

	state->size_lookup = hash_map_create(state, &hash_ptr_descriptor, state->inode_lookup->item_count);
	hash_map_walk(state->inode_lookup, state, bucketize_by_size);

	gather_tohash(state);

	state->hash_lookup = hash_map_create(state, &hash_buf32_descriptor, state->tohash_count);
	for (size_t i = 0; i < state->tohash_count; ++i)
		hash_inode(state, i, state->tohash_list[i]);

	if (state->verbose && state->tty)
	{
		fputs("\e[u\e[J", stdout);
		fflush(stdout);
	}

	// TODO:

	return 0;
}

static int parse_cmdline(struct dedupe_state* state, int argc, char** argv)
{
	static const struct option long_options[] =
	{
		{"verbose", no_argument, NULL, 'v'},
		{"dry-run", no_argument, NULL, 'n'},
		{"relaxed", no_argument, NULL, 'x'},
		{}
	};

	size_t argsz = argc * sizeof(char*);
	char** nargv = alloca(argsz);
	memcpy(nargv, argv, argsz);

	int result, index;
	while ((result = getopt_long(argc, nargv, "vnx", long_options, &index)) != -1)
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

static void scan_directory(struct dedupe_state* state, int pfd, char* dpath, char* dname)
{
	print_progress(state, dpath, 0, -1);

	int fd = openat(pfd, dname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (fd == -1)
	{
		perror(dpath);
		return;
	}

	DIR* d = fdopendir(fd);
	if (!d)
	{
		perror(dpath);
		close(fd);
		return;
	}

	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;

		char* fullpath = talloc_asprintf(dpath, "%s/%s", dpath, e->d_name);

		if (e->d_type == DT_DIR)
		{
			scan_directory(state, fd, fullpath, e->d_name);
			talloc_free(fullpath);
		}
		else if (e->d_type == DT_REG)
		{
			struct hash_bucket* bucket = hash_map_insert(state->inode_lookup, (void*)e->d_ino);
			struct inode_entry* ientry;

			if (bucket->value)
			{
				ientry = bucket->value;
			}
			else
			{
				ientry = talloc_zero(state, struct inode_entry);

				if (fstatat(fd, e->d_name, &ientry->buffer, AT_SYMLINK_NOFOLLOW) == -1)
				{
					perror(fullpath);
					talloc_free(ientry);
					talloc_free(fullpath);
					continue;
				}

				bucket->value = ientry;
			}

			struct path_entry* pentry = talloc(ientry, struct path_entry);
			pentry->next = ientry->paths;
			ientry->paths = pentry;

			pentry->path = fullpath;
			talloc_steal(pentry, fullpath);
		}
	}

	closedir(d);
}

static void bucketize_by_size(void* state0, struct hash_bucket* inode_bucket)
{
	struct dedupe_state* state = state0;
	struct inode_entry* ientry = inode_bucket->value;

	struct hash_bucket* size_bucket = hash_map_insert(state->size_lookup, (void*)ientry->buffer.st_size);
	++size_bucket->dummy;
	ientry->next_by_size = size_bucket->value;
	size_bucket->value = ientry;
}

static void gather_tohash(struct dedupe_state* state0)
{
	struct gather_tohash_state state1 = { state0, 16 };

	state0->tohash_count = 0;
	state0->tohash_list = talloc_array(state0, struct inode_entry*, state1.capacity);

	hash_map_walk(state0->size_lookup, &state1, gather_tohash_walkcb);
	qsort(state0->tohash_list, state0->tohash_count, sizeof(struct inode_entry*), gather_tohash_sortcb);
}

static void gather_tohash_walkcb(void* state0, struct hash_bucket* size_bucket)
{
	if (size_bucket->dummy < 2)
		return;

	struct gather_tohash_state* state1 = state0;
	struct dedupe_state* state2 = state1->state0;

	for (struct inode_entry* inode = size_bucket->value; inode; inode = inode->next_by_size)
	{
		if (state1->capacity == state2->tohash_count)
		{
			state1->capacity *= 2;
			state2->tohash_list = talloc_realloc(state2, state2->tohash_list, struct inode_entry*, state1->capacity);
		}

		state2->tohash_list[state2->tohash_count++] = inode;
	}
}

static int gather_tohash_sortcb(const void* p1, const void* p2)
{
	struct inode_entry
		*inode0 = *((struct inode_entry* const*)p1),
		*inode1 = *((struct inode_entry* const*)p2);

	off_t sz0 = inode0->buffer.st_size,
		sz1 = inode1->buffer.st_size;

	if (sz0 < sz1)
		return -1;
	else if (sz0 > sz1)
		return 1;
	else
		return 0;
}

static void hash_inode(struct dedupe_state* state, size_t progress, struct inode_entry* inode)
{
	int fd = -1;
	char* fpath = NULL;
	for (struct path_entry* path = inode->paths; path; path = path->next)
	{
		fpath = path->path;
		fd = open(fpath, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
		if (fd != -1)
			break;
	}

	if (fd == -1)
	{
		fprintf(stderr, "#%lu: Cannot open any path", (unsigned long)inode->buffer.st_ino);
		return;
	}

	print_progress(state, fpath, progress, state->tohash_count);

	void* data = mmap(NULL, inode->buffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED)
	{
		perror(fpath);
		close(fd);
		return;
	}

	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, inode->buffer.st_size);
	SHA256_Final(inode->hash, &ctx);

	munmap(data, inode->buffer.st_size);
	close(fd);

	struct hash_bucket* bucket = hash_map_insert(state->hash_lookup, inode->hash);
	inode->next_by_hash = bucket->value;
	bucket->value = inode;
}

static void print_progress(struct dedupe_state* state, const char* status, size_t count, size_t max)
{
	if (!state->verbose)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	if (state->last == ts.tv_sec)
		return;

	bool progress = max != -1;
	if (state->tty)
	{
		fputs("\e[u\e[J", stdout);

		if (progress)
		{
			size_t cmax = (state->width >= 3 ? state->width - 3 : 1);
			size_t ccount = (count * cmax) / max;
			size_t cncount = cmax - ccount;

			char *buf0 = alloca(ccount + 1);
			memset(buf0, 0x7C, ccount);
			buf0[ccount] = 0;

			char* buf1 = alloca(cncount + 1);
			memset(buf1, 0x20, cncount);
			buf1[cncount] = 0;

			printf("\e[0;1m[\e[0;32;42m%s\e[0m%s\e[1m]\n[%zu/%zu]\e[0m ", buf0, buf1, count, max);
		}
		else
		{
			printf("\e[0;1m[%c]\e[0m ", "-\\|/"[ts.tv_sec & 3]);
		}

		printf("%s\n", status);
	}
	else
	{
		if (progress)
			printf("[%zu/%zu] ", count, max);

		printf("%s\n", status);
	}

	state->last = ts.tv_sec;
	fflush(stdout);
}

static struct hash_map* hash_map_create(void* ctx, const struct hash_descriptor* descriptor, size_t expected)
{
	struct hash_map* result = talloc(ctx, struct hash_map);
	result->descriptor = descriptor;
	result->item_count = 0;
	result->bucket_count = next_prime(expected);
	result->buckets = talloc_zero_array(result, struct hash_bucket*, result->bucket_count);
	return result;
}

static struct hash_bucket* hash_map_insert(struct hash_map* map, void* key)
{
	size_t hash = map->descriptor->hash_function(key);
	size_t bucket = hash % map->bucket_count;

	struct hash_bucket* result;
	for (result = map->buckets[bucket]; result; result = result->next)
	{
		if (map->descriptor->equal_function(result->key, key))
			break;
	}

	if (!result)
	{
		result = talloc_zero(map->buckets, struct hash_bucket);
		result->next = map->buckets[bucket];
		result->key = key;
		++map->item_count;
		map->buckets[bucket] = result;
	}

	hash_map_rehash(map);
	return result;
}

static void hash_map_rehash(struct hash_map* map)
{
	if ((map->item_count / 2) <= map->bucket_count)
		return;

	size_t bucket_count_old = map->bucket_count;
	map->bucket_count = next_prime(map->item_count * 2);

	struct hash_bucket** buckets_old = map->buckets;
	map->buckets = talloc_zero_array(map, struct hash_bucket*, map->bucket_count);

	for (size_t bucket_old = 0; bucket_old < bucket_count_old; ++bucket_old)
	{
		struct hash_bucket* next;
		for (struct hash_bucket* current = buckets_old[bucket_old]; current; current = next)
		{
			next = current->next;

			size_t hash = map->descriptor->hash_function(current->key);
			size_t bucket = hash % map->bucket_count;

			talloc_steal(map->buckets, current);
			current->next = map->buckets[bucket];
			map->buckets[bucket] = current;
		}
	}

	talloc_free(buckets_old);
}

static void hash_map_walk(struct hash_map* map, void* context, void(*cb)(void*, struct hash_bucket*))
{
	for (size_t bucket = 0; bucket < map->bucket_count; ++bucket)
	{
		for (struct hash_bucket* current = map->buckets[bucket]; current; current = current->next)
			cb(context, current);
	}
}

static size_t hash_ptr_hash(void* p)
{
	return (size_t)p;
}

static bool hash_ptr_equals(void* p1, void* p2)
{
	return p1 == p2;
}

static size_t hash_buf32_hash(void* p)
{
	size_t result = 0;
	for (size_t *current = p, *end = ((size_t*)p) + (32 / sizeof(size_t)); current < end; ++current)
		result ^= *current;
	return result;
}

static bool hash_buf32_equals(void* p1, void* p2)
{
	return !memcmp(p1, p2, 32);
}

// https://stackoverflow.com/a/5694432 impl #5
static bool is_prime(size_t x)
{
	size_t o = 4;
	for (size_t i = 5; 1; i += o)
	{
		size_t q = x / i;
		if (q < i)
			return true;
		if (x == q * i)
			return false;
		o ^= 6;
	}

	return true;
}

static size_t next_prime(size_t x)
{
	switch (x)
	{
		case 0:
		case 1:
		case 2:
			return 2;
		case 3:
			return 3;
		case 4:
		case 5:
			return 5;
	}

	size_t k = x / 6;
	size_t i = x - 6 * k;
	size_t o = i < 2 ? 1 : 5;

	x = 6 * k + o;
	for (i = (3 + o) / 2; !is_prime(x); x += i)
		i ^= 6;

	return x;
}
