// TODO(ariel) Remove later.
#include <stdio.h>

#include <sys/socket.h>

#include "arena.h"
#include "base.h"
#include "err.h"
#include "request.h"
#include "str.h"

typedef struct {
	String scheme;
	String domain;
	String path;
} URL;

internal URL
parse_http_url(String urlstr)
{
	URL url = {0};

	// NOTE(ariel) Parse scheme.
	{
		// FIXME(ariel) Handle HTTPS?
		static String http_scheme = {
			.str = "http://",
			.len =  7,
		};
		url.scheme = string_prefix(urlstr, 7);
		if (!string_equal(url.scheme, http_scheme)) {
			err_exit("failed to match HTTP scheme for url %s", urlstr);
		}
	}

	// NOTE(ariel) Parse domain.
	{
		url.domain = string_suffix(urlstr, 7);
		i32 delimiter = string_find_ch(url.domain, '/');
		if (delimiter > 0) url.domain = string_prefix(url.domain, delimiter);
	}

	// NOTE(ariel) Parse path.
	{
		url.path = string_suffix(urlstr, 7 + url.domain.len);
	}

	return url;
}

internal String
format_get_request(String urlstr)
{
	/* NOTE(ariel) Format of a HTTP/1.1 GET request.
	 *
	 * GET INSERT_SOURCE_HERE HTTP/1.1\r\n
	 * Host: HOSTNAME_OF_WEBSITE
	 */
	URL url = parse_http_url(urlstr);

	fprintf(stderr, "SCHEME: %.*s | DOMAIN: %.*s | PATH: %.*s\n",
		url.scheme.len, url.scheme.str,
		url.domain.len, url.domain.str,
		url.path.len, url.path.str);

	// TODO(ariel) Convert to an snprintf() equivalent?
	String_List ls = {0};
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ "GET ", 4 });
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ url.path.str, url.path.len });
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ " HTTP/1.1\r\n", 9 });
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ "Host: ", 6 });
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ url.domain.str, url.domain.len });
	// push_string(THREAD_LOCAL_ARENA, &ls, (String){ "\r\n\r\n", 2 });
	// String request = string_list_concat(THREAD_LOCAL_ARENA, ls);
	String request = {0};
	return request;
}

String
request_http_resource(String url)
{
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		err_exit("failed to open socket for %s", url);
	}

	String req_buf = format_get_request(url);
	if (send(sockfd, req_buf.str, req_buf.len, 0) == -1) {
		err_exit("failed to send request to %s", url);
	}

	isize len = recv(sockfd, 0, 0, MSG_TRUNC | MSG_PEEK);
	String recv_buf = {
		// .str = arena_alloc(&THREAD_LOCAL_ARENA, len),
		.str = 0,
		.len = len,
	};
	if (recv(sockfd, recv_buf.str, recv_buf.len, 0) == -1) {
		err_exit("failed to receive response from %s", url);
	}

	return recv_buf;
}

String
parse_http_response(String response)
{
	(void)response;
	return (String){0};
}
