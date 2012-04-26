
#define SOCKET

#ifdef SOCKET
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#if defined(__CYGWIN__)
#include <cygwin/in.h>
#elif defined(__linux__)
#include <netinet/in.h>
#endif

#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#ifdef SOCKET
static int ssock;
static struct sockaddr_in saddr;
static int csock = -1;
static struct sockaddr_in caddr;
static socklen_t clen;
static int crc = 1;
#else
static const char * buf = "GET /index.php?search=foobar&lang=en HTTP/1.1\r\nHost: foobar.com\r\nContent-Length: 10\r\n\r\nabc=def&ghi=jkl\r\n";
static const char * p;
#endif

/* {{{ */
static int client_connected(void)
{
#ifdef SOCKET
	return csock >= 0;
#else
	return 1;
#endif
}

static int client_available(void)
{
#ifdef SOCKET
	return crc > 0;
#else
	return p <= buf + strlen(buf);
#endif
}

static char client_read(void)
{
#ifdef SOCKET
	char c = 0;
	crc = read(csock, &c, sizeof(c));
	return c;
#else
	return *p++;
#endif
}
/* }}} */

struct query_t {
	char val[24];
};

struct request_t {
	char method[8];
	char protocol[12];
	char url[32];
	size_t nquery;
	struct query_t query[8];
	int content_length;
};

static int str_append(char * s, size_t len, char c)
{
	size_t l = strlen(s);
	if (l < len) {
		s[l]= c;
		return 0;
	}
	return -1;
}

static int method_append(struct request_t * r, char c)
{
	return str_append(r->method, sizeof(r->method)-1, c);
}

static int protocol_append(struct request_t * r, char c)
{
	return str_append(r->protocol, sizeof(r->protocol)-1, c);
}

static void request_clear(struct request_t * r)
{
	memset(r, 0, sizeof(struct request_t));
}

static int url_append(struct request_t * r, char c)
{
	return str_append(r->url, sizeof(r->url)-1, c);
}

static int query_append(struct request_t * r, char c)
{
	if (r->nquery >= sizeof(r->query)/sizeof(struct query_t)) return -1;
	return str_append(r->query[r->nquery].val, sizeof(r->query[r->nquery].val)-1, c);
}

static int query_next(struct request_t * r)
{
	if (r->nquery >= sizeof(r->query)/sizeof(struct query_t)) return -1;
	r->nquery++;
	return 0;
}

static void clear(char * s, size_t len)
{
	memset(s, 0, len);
}

static int append(char * s, size_t len, char c)
{
	return str_append(s, len, c);
}

int parse(struct request_t * r)
{
	int state = 0;
	int next = 1;
	int timeout = 10;
	char c = 0;
	char s[128];
	int c_len = -1;

	request_clear(r);
	clear(s, sizeof(s));
	while (client_connected()) {
		if (next) {
			if (!client_available() || c_len == 0) {
				/* TODO: timeout, delay?, check for length?
				--timeout;
				if (timeout <= 0) return -1;
				continue;
				*/
				return 0;
			}
			c = client_read();
			if (c_len > 0) --c_len;
			next = 0;
			timeout = 10;
		}
		switch (state) {
			case 0: /* kill leading spaces */
				if (isspace(c)) {
					next = 1;
				} else {
					state = 1;
				}
				break;
			case 1: /* method */
				if (isspace(c)) {
					state = 2;
				} else {
					if (method_append(r, c)) return -state;
					next = 1;
				}
				break;
			case 2: /* kill spaces */
				if (isspace(c)) {
					next = 1;
				} else {
					state = 3;
				}
				break;
			case 3: /* url */
				if (isspace(c)) {
					state = 5;
				} else if (c == '?') {
					next = 1;
					state = 4;
				} else {
					if (url_append(r, c)) return -state;
					next = 1;
				}
				break;
			case 4: /* queries */
				if (isspace(c)) {
					if (query_next(r)) return -state;
					state = 5;
				} else if (c == '&') {
					if (query_next(r)) return -state;
					next = 1;
				} else {
					if (query_append(r, c)) return -state;
					next = 1;
				}
				break;
			case 5: /* kill spaces */
				if (isspace(c)) {
					next = 1;
				} else {
					state = 6;
				}
				break;
			case 6: /* protocol */
				if (isspace(c)) {
					state = 7;
				} else {
					if (protocol_append(r, c)) return -state;
					next = 1;
				}
				break;
			case 7: /* kill spaces */
				if (isspace(c)) {
					next = 1;
				} else {
					clear(s, sizeof(s));
					state = 8;
				}
				break;
			case 8: /* header line key */
				if (c == ':') {
					if (strcmp(s, "Content-Length") == 0) c_len = -2;
printf("[%s] : ", s);
					clear(s, sizeof(s));
					state = 9;
					next = 1;
				} else {
					if (append(s, sizeof(s)-1, c)) return -state;
					next = 1;
				}
				break;
			case 9: /* kill spaces */
				if (isspace(c)) {
					next = 1;
				} else {
					clear(s, sizeof(s));
					state = 10;
				}
				break;
			case 10: /* header line value */
				if (c == '\r') {
					if (c_len != -2) c_len = strtol(s, 0, 0);
printf("[%s]\n", s);
					clear(s, sizeof(s));
					state = 11;
					next = 1;
				} else {
					if (append(s, sizeof(s)-1, c)) return -state;
					next = 1;
				}
				break;
			case 11:
				if (c == '\n') {
					next = 1;
				} else if (c == '\r') {
					state = 12;
					next = 1;
				} else {
					state = 8;
				}
				break;
			case 12: /* end of header */
				if (c == '\n') {
					if (c_len > 0) {
						state = 13;
						next = 1;
					} else {
						return 0; /* end of header, no content => end of request */
					}
				} else {
					state = 8;
				}
				break;
			case 13: /* content (POST queries) */
				if (c == '&') {
					if (query_next(r)) return -state;
					next = 1;
				} else if (c == '\r') {
					if (query_next(r)) return -state;
					next = 1;
				} else if (c == '\n') {
					next = 1;
				} else {
					if (query_append(r, c)) return -state;
					next = 1;
				}
				break;
		}
	}
	return -99;
}


static void print_req(int rc, struct request_t * r)
{
	size_t i;
	if (rc) {
		printf("\nERROR: invalid request: %d", rc);
	} else {
		printf("MET:[%s]\n", r->method);
		printf("PRT:[%s]\n", r->protocol);
		printf("URL:[%s]\n", r->url);
		for (i = 0; i < r->nquery; ++i) printf("QRY:[%s]\n", r->query[i].val);
	}
	printf("\n");
}

static void server(void)
{
#ifdef SOCKET
	static const char * RESPONSE = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n<html><body>Welcome</body></html>\r\n";
	static const char * BAD_REQUEST = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n<html><body>Bad Request</body></html>\r\n";

	struct request_t r;
	int rc;

	for (;;) {
		csock = accept(ssock, (struct sockaddr *)&caddr, &clen);
		if (csock < 0) return;
		rc = parse(&r);
		print_req(rc, &r);
		if (rc) {
			rc = write(csock, BAD_REQUEST, strlen(BAD_REQUEST));
		} else {
			rc = write(csock, RESPONSE, strlen(RESPONSE));
		}
		shutdown(csock, SHUT_WR);
		close(csock);
		csock = -1;
	}
#endif
}

int main()
{
#ifdef SOCKET
	ssock = socket(AF_INET, SOCK_STREAM, 0);
	if (ssock < 0) {
		perror("socket");
		return -1;
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(8080);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(ssock, (const struct sockaddr *)&saddr, sizeof(saddr))) {
		perror("bind");
		return -1;
	}
	if (listen(ssock, 2)) {
		perror("listen");
		return -1;
	}
	server();
#else
	struct request_t r;
	p = buf;
	print_req(parse(&r), &r);
#endif

	return 0;
}

