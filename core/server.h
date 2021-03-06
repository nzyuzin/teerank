#ifndef SERVER_H
#define SERVER_H

#include <time.h>
#include "master.h"

/* Maximum clients connected to a server */
#define MAX_CLIENTS 16

/*
 * While name and gametype string size is consistently defined accross
 * Teeworlds source code, map string size is on the other hand sometime
 * 32 byte, sometime 64 byte.  We choose 64 byte because the whole file
 * should be less than a block size anyway (>= 512b).
 */
#define SERVERNAME_STRSIZE 256
#define GAMETYPE_STRSIZE 8
#define MAP_STRSIZE 64

#define IP_STRSIZE sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx")
#define PORT_STRSIZE sizeof("00000")
#define ADDR_STRSIZE sizeof("[xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]:00000")

#include "player.h"

#define ALL_SERVER_COLUMNS \
	" ip, port, name, gametype, map, lastseen, expire," \
	" master_node, master_service, max_clients "

#define NUM_CLIENTS_COLUMN \
	" (SELECT COUNT(1)" \
	"  FROM server_clients AS sc" \
	"  WHERE sc.ip = servers.ip" \
	"  AND sc.port = servers.port)" \
	" AS num_clients "

#define ALL_EXTENDED_SERVER_COLUMNS \
	ALL_SERVER_COLUMNS "," NUM_CLIENTS_COLUMN

#define IS_VANILLA_CTF_SERVER \
	" gametype = 'CTF'" \
	" AND map IN ('ctf1', 'ctf2', 'ctf3', 'ctf4', 'ctf5', 'ctf6', 'ctf7')" \
	" AND max_clients <= 16 "

#define bind_server(s) "sssssttssu", \
	(s).ip, (s).port, (s).name, (s).gametype, (s).map, (s).lastseen, \
	(s).expire, (s).master_node, (s).master_service, (s).max_clients

#define foreach_server(query, s, ...) \
	foreach_row((query), read_server, (s), __VA_ARGS__)

#define foreach_extended_server(query, s, ...) \
	foreach_row((query), read_extended_server, (s), __VA_ARGS__)

void read_server(sqlite3_stmt *res, void *s);
void read_extended_server(sqlite3_stmt *res, void *s);

#define ALL_SERVER_CLIENT_COLUMNS \
	" name, clan, score, ingame "

#define SORT_BY_SCORE \
	" ingame DESC, score DESC "

#define bind_client(s, c) "ssssii", \
	(s).ip, (s).port, (c).name, (c).clan, (c).score, (c).ingame

#define foreach_server_client(query, c, ...) \
	foreach_row((query), read_server_client, (c), __VA_ARGS__)

void read_server_client(sqlite3_stmt *res, void *c);

/**
 * @struct server
 *
 * Contains the state of a server at the time "lastseen".
 */
struct server {
	char ip[IP_STRSIZE];
	char port[PORT_STRSIZE];

	char name[SERVERNAME_STRSIZE];
	char gametype[GAMETYPE_STRSIZE];
	char map[MAP_STRSIZE];

	time_t lastseen;
	time_t expire;

	char master_node[NODE_STRSIZE];
	char master_service[SERVICE_STRSIZE];

	int num_clients;
	int max_clients;
	struct client {
		char name[NAME_LENGTH], clan[NAME_LENGTH];
		int score;
		int ingame;
	} clients[MAX_CLIENTS];
};

/**
 * Check if the given server info describe a vanilla ctf server
 */
int is_vanilla_ctf(char *gametype, char *map, unsigned max_clients);

/**
 * Extract IP and port from a full address
 *
 * The given buffer is modified.  IP and port are checked for validity.
 * An invalid IP or port will result in a failure.
 *
 * @param addr Server address
 * @param ip extracted IP
 * @param port extracted port
 * @return 1 on success, 0 on failure
 */
int parse_addr(char *addr, char **ip, char **port);

/**
 * Construct an adress with the given ip and port
 *
 * Adress can be parsed back with parse_addr().  It returns a statically
 * allocated buffer.
 *
 * @param ip Server IP
 * @param port Server port
 * @return A statically allocated string
 */
char *build_addr(const char *ip, const char *port);

/**
 * Read server's clients from the database.
 *
 * The function actually need the provided server structure to contains
 * an IP and a port, and will also use it to store the results.
 *
 * @param server Pointer to a server structure were readed data are stored
 *
 * @return 1 on success, 0 on failure
 */
int read_server_clients(struct server *server);

/**
 * Write a server in the database.
 *
 * @param server Server structure to be written
 *
 * @return 1 on success, 0 on failure
 */
int write_server(struct server *server);

/**
 * Write server's clients in the database
 *
 * @param server Server structure containing clients to be written
 *
 * @return 1 on success, 0 on failure
 */
int write_server_clients(struct server *server);

/**
 * Create an empty server in the database if it doesn't already exists.
 *
 * @param ip Server IP
 * @param port Server port
 * @param ip Master server node
 * @param port Master server service
 *
 * @return A server struct filled with the given values
 */
struct server create_server(
	const char *ip, const char *port,
	const char *master_node, const char *master_service);

/**
 * Remove a server from the database.
 *
 * @param ip Server IP
 * @param port Server port
 */
void remove_server(const char *ip, const char *port);

/**
 * Check if the given server expired.
 *
 * @param server Server to check expiry
 *
 * @return 1 is server expired, 0 otherwise
 */
int server_expired(struct server *server);

/**
 * Count the number of vanilla servers in the database
 */
unsigned count_vanilla_servers(void);

#endif /* SERVER_H */
