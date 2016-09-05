#ifndef CGI_H
#define CGI_H

#include <stdlib.h>

/* Used by pages */
#define EXIT_NOT_FOUND 2

void error(int code, char *fmt, ...);

#define MAX_DOMAIN_LENGTH 1024

extern struct cgi_config {
	const char *name;
	const char *port;

	char domain[MAX_DOMAIN_LENGTH];
} cgi_config;

int page_graph_main(int argc, char **argv);
int page_about_main(int argc, char **argv);
int page_search_main(int argc, char **argv);

int page_player_html_main(int argc, char **argv);
int page_player_json_main(int argc, char **argv);
int page_player_json_html_main(int argc, char **argv);

int page_clan_html_main(int argc, char **argv);
int page_clan_json_main(int argc, char **argv);
int page_clan_json_html_main(int argc, char **argv);

int page_player_list_html_main(int argc, char **argv);
int page_player_list_json_main(int argc, char **argv);
int page_player_list_json_html_main(int argc, char **argv);

int page_clan_list_html_main(int argc, char **argv);
int page_clan_list_json_main(int argc, char **argv);
int page_clan_list_json_html_main(int argc, char **argv);

int page_server_list_html_main(int argc, char **argv);
int page_server_list_json_main(int argc, char **argv);
int page_server_list_json_html_main(int argc, char **argv);

int page_robots_main(int argc, char **argv);
int page_sitemap_main(int argc, char **argv);

char *get_url(char *buf, size_t size, const char *fmt, ...);

#endif /* CGI_H */
