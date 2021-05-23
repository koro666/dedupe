`dedupe`: identical file hardlinker
==================================

`dedupe` is a simple tool written in C that detects identical files by their SHA256 hash and hardlinks all duplicates to the oldest version of that file, based on modified time.

It is very fast, only hashing files when there are more than one of the same size. It also knows not to cross mount points.

Build
-----

`dedupe` builds on Linux and FreeBSD.

- On Linux, it depends on `openssl` and `talloc`. Simply run `make`.
- On FreeBSD, it depends on `talloc`, and requires `gmake` to build.

Usage
-----

Simply pass a list of directories to scan as arguments.

There are a few options that can be passed as well:

- `-v` or `--verbose` will print a nice colorful progress as it scans files, as well as for duplicates it found.
- `-n` or `--dry-run` will not actually do modifications.
- `-i` or `--interactive` will ask what to do with each duplicate found.
- `-e` or `--exclude` to exclude file or directory whose names matches the pattern.
- `-x` or `--use-xattrs` will use extended attributes to cache the computed file hashes.

There are a few more options, run `dedupe -h` for information.
