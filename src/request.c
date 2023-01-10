#include <stdio.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "arena.h"
#include "base.h"
#include "err.h"
#include "request.h"
#include "str.h"

#define HTTP_PORT "80"

enum {
	DEFAULT_BUFFER_SIZE = 512,
};

typedef struct {
	String scheme;
	String domain;
	String path;
} URL;

global String line_delimiter = {
	.str = "\r\n",
	.len = 2,
};

internal URL
parse_http_url(String urlstr)
{
	URL url = {0};

	// NOTE(ariel) Parse scheme.
	{
		local_persist String http_scheme = {
			.str = "http://",
			.len =  7,
		};
		url.scheme = string_prefix(urlstr, 7);
		if (!string_match(url.scheme, http_scheme)) {
			local_persist String https_scheme = {
				.str = "https://",
				.len = 8,
			};
			url.scheme = string_prefix(urlstr, 8);
			if (!string_match(url.scheme, https_scheme)) {
				err_exit("failed to match scheme of %.*s to HTTP or HTTPS",
					url.scheme.len, url.scheme.str);
			}
		}
	}

	// NOTE(ariel) Parse domain.
	{
		url.domain = string_suffix(urlstr, url.scheme.len);
		i32 delimiter = string_find_ch(url.domain, '/');
		if (delimiter > 0) url.domain = string_prefix(url.domain, delimiter);
	}

	// NOTE(ariel) Parse path.
	{
		url.path = string_suffix(urlstr, url.scheme.len + url.domain.len);
	}

	return url;
}

internal String
format_get_request(Arena *arena, URL url)
{
	// NOTE(ariel) It seems necessary to spoof the user agent field to receive a
	// response from most servers today.
	local_persist String user_agent = {
		.str = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:102.0) Gecko/20100101 Firefox/102.0\r\n",
		.len = 84,
	};

	// NOTE(ariel) Here's an example of an HTTP request sent by Firefox.
	// GET /atom.xml HTTP/1.1\r\n
	// Host: ratfactor.com\r\n
	// User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:102.0) Gecko/20100101 Firefox/102.0\r\n
	// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n
	// Accept-Language: en-US,en;q=0.5\r\n
	// Accept-Encoding: gzip, deflate\r\n
	// DNT: 1\r\n
	// Connection: keep-alive\r\n
	// Upgrade-Insecure-Requests: 1\r\n
	// Sec-GPC: 1\r\n
	// If-Modified-Since: Sun, 01 Jan 2023 21:44:08 GMT\r\n
	// If-None-Match: "5886-5f13abc071dae"\r\n
	// \r\n
	// [Full request URI: http://ratfactor.com/atom.xml]
	// [HTTP request 1/1]
	// [Response in frame: 39]

	String_List ls = {0};

	push_string(arena, &ls, (String){ "GET ", 4 });
	push_string(arena, &ls, url.path);
	push_string(arena, &ls, (String){ " HTTP/1.1\r\n", 11 });

	push_string(arena, &ls, (String){ "Host: ", 6 });
	push_string(arena, &ls, url.domain);
	push_string(arena, &ls, line_delimiter);

	push_string(arena, &ls, user_agent);
	push_string(arena, &ls, line_delimiter);

	String request = string_list_concat(arena, ls);
	return request;
}

String
request_http_resource(Arena *arena, String urlstr)
{
	String recv_buf = {0};

	// FIXME(ariel) For now, only format and send request to sites that don't
	// require a secure connection.
	URL url = parse_http_url(urlstr);
	if (url.scheme.len == 8) goto exit;

	struct addrinfo *result = 0;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE,
		.ai_protocol = 0,
	};
	char *domain = string_terminate(arena, url.domain);
	i32 status = getaddrinfo(domain, HTTP_PORT, &hints, &result);
	if (status) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		goto exit;
	}

	int sockfd = 0;
	struct addrinfo *p = result;
	for (; p; p = p->ai_next) {
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1) continue;
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) != -1) break;
		close(sockfd);
	}
	freeaddrinfo(result);
	if (!p) {
		fprintf(stderr, "failed to connect for %.*s\n", urlstr.len, urlstr.str);
		goto exit;
	}

	String req_buf = format_get_request(arena, url);
	if (!req_buf.len) goto exit;
	if (send(sockfd, req_buf.str, req_buf.len, 0) == -1) {
		err_msg("failed to send request to %s", url);
		goto exit;
	}

	isize nbytes = 0;
	String_List recvs = {0};
	for (;;) {
		String buf = {
			.str = arena_alloc(arena, BUFSIZ),
			.len = BUFSIZ,
		};

		nbytes = recv(sockfd, buf.str, buf.len, 0);
		if (nbytes == -1) {
			err_exit("failed to receive response from %s", url);
			goto exit;
		} else if (nbytes == 0) {
			// NOTE(ariel) Pop previous allocation.
			arena_realloc(arena, 0);
			break;
		}

		assert(nbytes <= BUFSIZ);
		buf.str = arena_realloc(arena, nbytes);
		buf.len = nbytes;

		push_string(arena, &recvs, buf);
	}
	if (nbytes == -1) {
		err_msg("failed to receive response from %s", url);
		goto exit;
	}
	recv_buf = string_list_concat(arena, recvs);

exit:
	close(sockfd);
	return recv_buf;
}

String
parse_http_response(Arena *arena, String response)
{
	local_persist String ok_status_code = {
		.str = "200",
		.len = 3,
	};

	String response_body = {0};
	String_List lines = string_strsplit(arena, response, line_delimiter);

	String status_line = lines.head->string;
	String_List status_line_elements = string_split(arena, status_line, ' ');
	if (status_line_elements.list_size != 3) {
		fprintf(stderr, "failed to parse status line of HTTP response\n");
		return response_body;
	}
	String status_code = status_line_elements.head->next->string;
	if (!string_match(status_code, ok_status_code)) {
		fprintf(stderr, "request failed, received unsuccessful status code\n");
		return response_body;
	}

	response_body = lines.tail->string;
	return response_body;
}
