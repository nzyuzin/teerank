#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>

#include "io.h"
#include "config.h"

static char *reason_phrase(int code)
{
	switch (code) {
	case 200: return "OK";
	case 404: return "Not Found";
	case 500: return "Internal Server Error";
	default:  return "";
	}
}

static void print_error(int code)
{
	printf("Content-type: text/html\n");
	printf("Status: %d %s\n\n", code, reason_phrase(code));
	printf("<h1>%d %s</h1>\n", code, reason_phrase(code));
}

static void error(int code, char *fmt, ...)
{
	va_list ap;

	print_error(code);
	if (fmt) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	} else {
		fprintf(stderr, "%d %s\n", code, reason_phrase(code));
	}

	exit(EXIT_FAILURE);
}

struct file {
	char *name;
	char **args;
	char *source;
};

struct directory {
	char *name;

	struct file *files;
	struct file *(*generator)(char *name);
	struct directory *dirs;
};

static void remove_extension(char *str, char *ext)
{
	char *tmp;

	tmp = strrchr(str, '.');
	if (!tmp || strcmp(tmp+1, ext))
		return;
	*tmp = '\0';
}

static char *get_source_from_name(char *name, char *dirtree)
{
	static char source[PATH_MAX];

	if (snprintf(source, PATH_MAX, "%s/%s/%s",
	             config.root, dirtree, name) >= PATH_MAX)
		error(404, NULL);
	remove_extension(source, "html");

	return source;
}

static char *get_raw_source_from_name(char *name)
{
	static char source[PATH_MAX];

	if (*stpncpy(source, name, PATH_MAX) != '\0')
		error(404, NULL);
	remove_extension(source, "html");

	return source;
}

static struct file *pages_generator(char *name)
{
	static struct file file;
	static char *args[] = { "teerank-generate-rank-page", "full-page", NULL };

	file.name = name;
	file.source = get_source_from_name(name, "pages");
	file.args = args;

	return &file;
}

static struct file *clans_generator(char *name)
{
	static struct file file;
	static char *args[] = { "teerank-generate-clan-page", NULL, NULL };

	file.name = name;
	file.source = get_source_from_name(name, "clans");
	file.args = args;
	file.args[1] = get_raw_source_from_name(name);

	return &file;
}

static const struct directory root = {
	"", (struct file[]) {
		{ "index.html", (char*[]){ "teerank-generate-index", NULL }, NULL },
		{ "about.html", (char*[]){ "teerank-generate-about", NULL }, NULL },
		{ NULL }
	}, NULL, (struct directory[]) {
		{ "pages", NULL, pages_generator, NULL },
		{ "clans", NULL, clans_generator, NULL },
		{ NULL }
	}
};

static struct directory *find_directory(const struct directory *parent, char *name)
{
	struct directory *dir;

	assert(parent != NULL);
	assert(name != NULL);

	if (parent->dirs)
		for (dir = parent->dirs; dir->name; dir++)
			if (!strcmp(name, dir->name))
				return dir;

	return NULL;
}

static struct file *find_file(const struct directory *dir, char *name)
{
	struct file *file;

	assert(dir != NULL);
	assert(name != NULL);

	if (dir->files)
		for (file = dir->files; file->name; file++)
			if (!strcmp(file->name, name))
				return file;

	if (dir->generator)
		return dir->generator(name);
	return NULL;
}

static int is_cached(char *path, char *dep)
{
	struct stat statpath, statdep;

	assert(path != NULL);

	if (!dep)
		return 0;
	if (stat(path, &statpath) == -1)
		return 0;
	if (stat(dep, &statdep) == -1)
		return 0;
	if (statdep.st_mtim.tv_sec > statpath.st_mtim.tv_sec)
		return 0;

	return 1;
}

static void generate_file(struct file *file, char *prefix)
{
	int dest, src = -1, err[2];
	char tmpname[PATH_MAX], path[PATH_MAX];

	assert(file != NULL);
	assert(prefix != NULL);
	assert(file->name != NULL);
	assert(file->args != NULL);
	assert(file->args[0] != NULL);

	/*
	 * Files are generated by calling a program that write on stdout the
	 * content of the page.
	 *
	 * The program have to write on stdout, and therefor we need to
	 * redirect stdout.  We redirect it to a temporary file created on
	 * purpose, and then use rename() to move the temporary file to his
	 * proper location in cache.  This is done to avoid any race conditions
	 * when two instances of this CGI generate the same file in cache.
	 *
	 * The program may fail and in this case we return an error 500 and
	 * dump the content of stderr.  For that purpose we need to redirect
	 * stderr to a pipe so that the parent can retrieve errors, if any.
	 *
	 * Eventually, the child may need stdin feeded with some data.
	 */

	/*
	 * Store the full pathname first so we can abort if it's too long.
	 * Plus we need it to check if it is already cached.
	 */
	if (snprintf(path, PATH_MAX, "%s/%s/%s", config.cache_root,
	             prefix, file->name) >= PATH_MAX)
		error(404, NULL);

	/* Do not generate if is already cached */
	if (is_cached(path, file->source))
		return;

	/*
	 * Open src before fork() because if the file doesn't exist, then we
	 * raise a 404, and if it does but cannot be opened, we raise a 500.
	 */
	if (file->source) {
		if ((src = open(file->source, O_RDONLY)) == -1) {
			if (errno == ENOENT)
				error(404, NULL);
			else
				error(500, "%s: %s\n", file->source, strerror(errno));
		}
	}

	/* The destination file is a temporary file */
	if (snprintf(tmpname, PATH_MAX, "%s/tmp-teerank-XXXXXX", config.tmp_root) >= PATH_MAX)
		error(500, "Pathname for temporary file is too long (>= %d)\n", PATH_MAX);
	if ((dest = mkstemp(tmpname)) == -1)
		error(500, "mkstemp(): %s\n", strerror(errno));

	if (pipe(err) == -1)
		error(500, "pipe(): %s\n", strerror(errno));

	/*
	 * Parent process wait for it's child to terminate and dump the
	 * content of the pipe if child's exit status is different than 0.
	 */
	if (fork() > 0) {
		int c;
		FILE *pipefile;

		close(err[1]);
		close(dest);

		if (file->source)
			close(src);

		wait(&c);
		if (WIFEXITED(c) && WEXITSTATUS(c) == EXIT_SUCCESS) {
			if (rename(tmpname, path) == -1)
				error(500, "rename(%s, %s): %s\n",
				      tmpname, path, strerror(errno));
			return;
		}

		/* Report error */
		print_error(500);
		fprintf(stderr, "%s: ", file->args[0]);
		pipefile = fdopen(err[0], "r");
		while ((c = fgetc(pipefile)) != EOF)
			fputc(c, stderr);
		fclose(pipefile);
	}

	/* Redirect stderr to the write side of the pipe */
	dup2(err[1], STDERR_FILENO);
	close(err[0]);

	/* Redirect stdin */
	if (file->source) {
		dup2(src, STDIN_FILENO);
		close(src);
	}

	/* Redirect stdout to the temporary file */
	dup2(dest, STDOUT_FILENO);
	close(dest);

	/* Eventually, run the program */
	execvp(file->args[0], file->args);
	fprintf(stderr, "execvp(%s): %s\n", file->args[0], strerror(errno));
	exit(EXIT_FAILURE);
}

/* Undo the effect of strtok() */
static void restore_path(char *path, char *last)
{
	assert(path != NULL);

	for (; last != path; last--)
		if (*last == '\0')
			*last = '/';
}

static void generate(char *_path)
{
	const struct directory *dir, *current = &root;
	struct file *file = NULL;
	char *name;
	static char path[PATH_MAX];

	/* _path should not be changed as it is the return value of getenv() */
	strcpy(path, _path);

	if (*path != '/')
		error(500, "Path should begin with '/'\n");

	name = strtok(path, "/");
	while (name) {
		if ((dir = find_directory(current, name)))
			current = dir;
		else if ((file = find_file(current, name)))
			break;
		else
			error(404, NULL);
		name = strtok(NULL, "/");
	}

	if (!file)
		error(404, NULL);
	restore_path(path, name);
	generate_file(file, dirname(path));
}

static void print(const char *name)
{
	FILE *file;
	int c;
	char path[PATH_MAX];

	assert(name != NULL);

	/* It should not fail because generate() would have failed otherwise */
	if (snprintf(path, PATH_MAX, "%s/%s", config.cache_root, name) >= PATH_MAX)
		error(404, NULL);

	if (!(file = fopen(path, "r")))
		error(500, "%s: %s\n", path, strerror(errno));
	printf("Content-Type: text/html\n\n");
	while ((c = fgetc(file)) != EOF)
		putchar(c);
	fclose(file);
}

static void create_directory(char *fmt, ...)
{
	va_list ap;
	char path[PATH_MAX];
	int ret;

	va_start(ap, fmt);
	vsprintf(path, fmt, ap);
	va_end(ap);

	if ((ret = mkdir(path, 0777)) == -1)
		if (errno != EEXIST)
			error(500, "%s: %s\n", path, strerror(errno));
}

static void init_cache(void)
{
	create_directory("%s", config.cache_root);
	create_directory("%s/pages", config.cache_root);
	create_directory("%s/clans", config.cache_root);
}

int main(int argc, char **argv)
{
	char *path;

	load_config();
	init_cache();

	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		fprintf(stderr, "This program expect $DOCUMENT_URI to be set to a valid to-be-generated file.\n");
		error(500, NULL);
	}

	if (!(path = getenv("DOCUMENT_URI")))
		error(500, "$DOCUMENT_URI not set\n");

	generate(path);
	print(path);

	return EXIT_SUCCESS;
}
