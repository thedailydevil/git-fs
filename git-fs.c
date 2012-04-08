/*
 * fuse + libgit2 = read-only mounting of bare repos
 */

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#include <git2.h>

git_repository *g_repo = NULL;
git_tree *g_tree = NULL;
git_index *g_index = NULL;
git_odb *g_odb;

void debug(const char* format, ...) {
	va_list args;
	if (getenv("DEBUG") == NULL)
		return;
	va_start(args,format);
	vfprintf(stderr, format, args);
	va_end(args);
}

git_index_entry *git_index_entry_by_file(const char *path) {
	unsigned int i, ecount;
	debug( "git_index_entry_by_file for %s\n", path);
	path++;
	debug( "  stripped to %s\n", path);
	ecount = git_index_entrycount(g_index);
	for (i = 0; i < ecount; ++i) {
		git_index_entry *e = git_index_get(g_index, i);
		debug( "  git_index_entry_by_file iterating over %s\n", e->path);

		if (! strcmp(e->path, path) == 0)
			continue;
		return e;
	}
	return NULL;
}

int git_getattr(const char *path, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if ( strcmp(path,"/") == 0 ) {
		debug( "git_getattr for root -> %s\n", path);
		stbuf->st_mode = 0040755;
		stbuf->st_gid = 0;
		stbuf->st_uid = 0;
		stbuf->st_size = 0;
		return 0;
	}
	debug( "git_getattr for %s\n", path);
	git_index_entry *e;
	if ( (e = git_index_entry_by_file(path)) == NULL)
		return debug("path no exist? %s\n", path),-ENOENT;

	stbuf->st_mode = e->mode;
	stbuf->st_gid = e->gid;
	stbuf->st_uid = e->uid;
	stbuf->st_nlink = 1;
	stbuf->st_size = e->file_size;
	return 0;
}

int git_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
	int i,ecount = 0;

	debug( "git_readdir for %s\n", path);
	ecount = git_index_entrycount(g_index);
	for (i = 0; i < ecount; ++i) {
		git_index_entry *e = git_index_get(g_index, i);
		debug( "readdir for '%s' got '%s'\n",path, e->path);
		filler(buf, e->path, NULL, 0);
	}

	return 0;
}

int git_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

int git_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	(void) fi;
	git_index_entry *e;
	git_odb_object *obj;
	git_oid oid;

	debug("git_read for -> %s with offset %d size %d\n", path, (int)offset, (int)size );

	if ( (e = git_index_entry_by_file(path)) == NULL)
		return debug("path no exist? %s\n", path),-ENOENT;

	oid = e->oid;
	git_odb_read(&obj, g_odb, &oid);
	debug( "git_read got %ld bytes\n", (long)(git_odb_object_size(obj)));

	memset(buf, 0, size);
	if (offset >= git_odb_object_size(obj)) {
		size = 0; goto ending;
	}
	if (offset + size > git_odb_object_size(obj))
		size = git_odb_object_size(obj) - offset;
	memcpy(buf, git_odb_object_data(obj), size);

	debug( "git_read copied %d bytes\n", (int)size);
ending:
	git_odb_object_free(obj);
	return size;
}

struct fuse_operations git_oper = {
	.getattr= git_getattr,
	.readdir= git_readdir,
	.open= git_open,
	.read= git_read,
	.readdir= git_readdir
};

int main(int argc, char *argv[])
{
	char *r,*m;
	char *a[]= { argv[0], "-f", argv[2] };
	//git_oid g_oid;
	//char *o;

	r = argv[1];
	m = argv[2];
	//o = argv[3];
	//printf("using repo %s, mnt %s, oid %s\n",r, m, o);
	printf("mounting repo %s on %s\n",r, m);

	if (git_repository_open(&g_repo, r) < 0)
		return perror("repo"), 1;

	//if ( git_oid_fromstrn(&g_oid, o, strlen(o)) < 0)
	//	return debug( "oid %s -> %s", o, strerror(errno)),1;

	/* this fails for some reason?
	 * if ( git_tree_lookup(&g_tree, g_repo, &g_oid) < 0)
		return perror("tree lookup"),1;
		*/
	git_repository_index(&g_index, g_repo);
	git_index_read(g_index);
	git_repository_odb(&g_odb, g_repo);

	return fuse_main(3, a, &git_oper);
}

