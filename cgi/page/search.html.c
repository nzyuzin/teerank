#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>

#include "teerank.h"
#include "cgi.h"
#include "html.h"
#include "player.h"
#include "server.h"
#include "clan.h"

/* Too many results is meaningless */
#define MAX_RESULTS 50

static void start_player_list(void)
{
	html_start_player_list(1, 1, 0);
}

static void print_player(unsigned pos, void *data)
{
	struct player *p = data;
	html_player_list_entry(p, NULL, 0);
}
static void print_clan(unsigned pos, void *data)
{
	struct clan *clan = data;
	html_clan_list_entry(pos, clan->name, clan->nmembers);
}
static void print_server(unsigned pos, void *data)
{
	html_server_list_entry(pos, data);
}

/*
 * The following compute the relevance of a string in a query, using the
 * following prioprities: exact match, prefix, suffix, anything else.
 */
#define IS_RELEVANT(col) \
	" " col " LIKE '%' || ? || '%' "
#define RELEVANCE(col) \
	" CASE" \
	"  WHEN " col " LIKE ? THEN 0" \
	"  WHEN " col " LIKE ? || '%' THEN 1" \
	"  WHEN " col " LIKE '%' || ? THEN 2" \
	"  ELSE 3" \
	" END "

struct search_info {
	void (*start_list)(void);
	void (*end_list)(void);
	void (*read_row)(sqlite3_stmt *res, void *data);
	void (*print_result)(unsigned pos, void *data);
	const char *emptylist;

	enum section_tab tab;
	const char *sprefix;

	const char *count_query;
	const char *search_query;
};

static const struct search_info PLAYER_SINFO = {
	start_player_list,
	html_end_player_list,
	read_player,
	print_player,
	"No players found",

	PLAYERS_TAB,
	"/players",

	"SELECT COUNT(1)"
	" FROM players"
	" WHERE" IS_RELEVANT("name")
	" LIMIT ?",

	"SELECT" ALL_PLAYER_COLUMNS
	" FROM players"
	" WHERE" IS_RELEVANT("name")
	" ORDER BY" RELEVANCE("name") ", elo"
	" LIMIT ?"
};

static const struct search_info CLAN_SINFO = {
	html_start_clan_list,
	html_end_clan_list,
	read_clan,
	print_clan,
	"No clans found",

	CLANS_TAB,
	"/clans",

	"SELECT COUNT(DISTINCT clan)"
	" FROM players"
	" WHERE" IS_VALID_CLAN "AND" IS_RELEVANT("clan")
	" LIMIT ?",

	"SELECT clan, COUNT(1) AS nmembers"
	" FROM players"
	" WHERE" IS_VALID_CLAN "AND" IS_RELEVANT("clan")
	" GROUP BY clan"
	" ORDER BY" RELEVANCE("clan") ", nmembers"
	" LIMIT ?"
};

static const struct search_info SERVER_SINFO = {
	html_start_server_list,
	html_end_server_list,
	read_extended_server,
	print_server,
	"No servers found",

	SERVERS_TAB,
	"/servers",

	"SELECT COUNT(1)"
	" FROM servers"
	" WHERE" IS_VANILLA_CTF_SERVER "AND" IS_RELEVANT("name")
	" LIMIT ?",

	"SELECT" ALL_EXTENDED_SERVER_COLUMNS
	" FROM servers"
	" WHERE" IS_VANILLA_CTF_SERVER "AND" IS_RELEVANT("name")
	" ORDER BY" RELEVANCE("name") ", num_clients"
	" LIMIT ?"
};

static int search(const struct search_info *sinfo, const char *value)
{
	unsigned nrow;
	sqlite3_stmt *res;
	union {
		struct player player;
		struct clan clan;
		struct server server;
	} row;

#define binds "ssssi",	\
	value, value, value, value, MAX_RESULTS

	sinfo->start_list();
	foreach_row(sinfo->search_query, sinfo->read_row, &row, binds)
		sinfo->print_result(nrow+1, &row);
	sinfo->end_list();

#undef binds

	if (!res)
		return 0;
	if (!nrow)
		html("%s", sinfo->emptylist);

	return 1;
}

int main_html_search(int argc, char **argv)
{
	unsigned tabvals[SECTION_TABS_COUNT];
	const char *squery;

	const struct search_info *sinfo, **s, *sinfos[] = {
		&PLAYER_SINFO, &CLAN_SINFO, &SERVER_SINFO, NULL
	};

	if (argc != 3) {
		fprintf(stderr, "usage: %s players|clans|servers <query>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "players") == 0)
		sinfo = &PLAYER_SINFO;
	else if (strcmp(argv[1], "clans") == 0)
		sinfo = &CLAN_SINFO;
	else if (strcmp(argv[1], "servers") == 0)
		sinfo = &SERVER_SINFO;
	else {
		fprintf(stderr, "%s: Should be either \"players\", \"clans\" or \"servers\"\n", argv[1]);
		return EXIT_FAILURE;
	}

	squery = argv[2];

	for (s = sinfos; *s; s++)
		tabvals[(*s)->tab] = count_rows((*s)->count_query, "si", squery, MAX_RESULTS);

	CUSTOM_TAB.name = "Search results";
	CUSTOM_TAB.href = "";
	html_header(&CUSTOM_TAB, "Search results", sinfo->sprefix, squery);
	print_section_tabs(sinfo->tab, squery, tabvals);

	if (!search(sinfo, squery))
		return EXIT_FAILURE;

	html_footer(NULL, NULL);

	return EXIT_SUCCESS;
}
