#include <errno.h>
#include <stdio.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "arena.h"
#include "base.h"
#include "err.h"
#include "request.h"
#include "str.h"

#define HTTP_PORT  "80"
#define HTTPS_PORT "443"

enum { READ_BUF_SIZ = KB(8) };

typedef struct {
	String scheme;
	String domain;
	String path;
} URL;

global String line_delimiter = {
	.str = "\r\n",
	.len = 2,
};
global String header_delimiter = {
	.str = "\r\n\r\n",
	.len = 4,
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
	local_persist String accept = {
		.str = "Accept: text/html,application/rss+xml,application/xhtml+xml,application/xml\r\n",
		.len = 77,
	};
	local_persist String accept_encoding = {
		.str = "Accept-Encoding: identity\r\n",
		.len = 27
	};
	local_persist String connection = {
		.str = "Connection: close\r\n",
		.len = 19,
	};

	String_List ls = {0};

	push_string(arena, &ls, (String){ "GET ", 4 });
	push_string(arena, &ls, url.path);
	push_string(arena, &ls, (String){ " HTTP/1.1\r\n", 11 });
	push_string(arena, &ls, (String){ "Host: ", 6 });
	push_string(arena, &ls, url.domain);
	push_string(arena, &ls, line_delimiter);
	push_string(arena, &ls, user_agent);
	push_string(arena, &ls, accept);
	push_string(arena, &ls, accept_encoding);
	push_string(arena, &ls, connection);
	push_string(arena, &ls, line_delimiter);

	String request = string_list_concat(arena, ls);
	return request;
}

internal i32
get_content_length(Arena *arena, String header)
{
	local_persist String content_length_field = {
		.str = "Content-Length",
		.len = 14,
	};
	i32 content_length = -1;

	String_List header_fields = string_strsplit(arena, header, line_delimiter);
	String_Node *status_line = header_fields.head;
	if (!status_line) return content_length;

	String_Node *field_node = status_line->next;
	while (field_node && content_length < 0) {
		String field = field_node->string;

		i32 name_length = string_find_ch(field, ':');
		if (name_length > 0) {
			String name = string_prefix(field, name_length);
			if (string_match(name, content_length_field)) {
				// NOTE(ariel) +1 to skip ':'.
				String remainder = string_suffix(field, name_length + 1);
				String value = string_trim_spaces(remainder);
				content_length = string_to_int(value, 10);
			}
		}

		field_node = field_node->next;
	}

	return content_length;
}

String
request_http_resource(Arena *arena, String urlstr)
{
	String recv_buf = {0};

	BIO *bio = 0;
	SSL_CTX *ssl_ctx = 0;
	URL url = parse_http_url(urlstr);

	char *domain = string_terminate(arena, url.domain);
	if (url.scheme.len == 8) {
		const SSL_METHOD *method = TLS_client_method();
		if (!method) goto exit;

		ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!ssl_ctx) goto exit;

		if (!SSL_CTX_set_default_verify_paths(ssl_ctx)) goto exit;

		bio = BIO_new_ssl_connect(ssl_ctx);
		if (!bio) goto exit;

		SSL *ssl = 0;
		BIO_get_ssl(bio, &ssl);
		SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
		SSL_set_tlsext_host_name(ssl, domain);

		BIO_set_conn_port(bio, HTTPS_PORT);
	} else {
		bio = BIO_new(BIO_s_connect());
		BIO_set_conn_port(bio, HTTP_PORT);
	}

	BIO_set_conn_hostname(bio, domain);
	if (BIO_do_connect(bio) <= 0) {
		ERR_print_errors_fp(stderr);
		goto exit;
	}

	String req_buf = format_get_request(arena, url);
	BIO_write(bio, req_buf.str, req_buf.len);
	BIO_flush(bio);

	// NOTE(ariel) BIO_read() enough times (perhaps only once) to receive the
	// entire header.
	String header = {0};
	i32 header_length = 0;
	String_List recvs = {0};
	do {
		String buf = {
			.str = arena_alloc(arena, READ_BUF_SIZ),
			.len = READ_BUF_SIZ,
		};

		i32 nbytes = BIO_read(bio, buf.str, buf.len);
		if (nbytes < 0) goto exit;

		buf.str = arena_realloc(arena, nbytes);
		buf.len = nbytes;

		header_length = string_find_substr(buf, header_delimiter);
		header = string_prefix(buf, header_length);

		push_string(arena, &recvs, buf);
	} while (header_length < 0);

	// NOTE(ariel) Eat bytes remaining according to the `Content-Length` field.
	// TODO(ariel) Handle `Transfer-Encoding` field -- it's an alternative to
	// `Content-Length` that a smaller subset of web servers use.
	i32 content_length = get_content_length(arena, header);
	if (content_length < 0) goto exit;
	while (recvs.total_len - header_length < content_length) {
		String buf = {
			.str = arena_alloc(arena, BUFSIZ),
			.len = BUFSIZ
		};

		i32 nbytes = BIO_read(bio, buf.str, buf.len);
		if (nbytes < 0) goto exit;

		buf.str = arena_realloc(arena, nbytes);
		buf.len = nbytes;

		push_string(arena, &recvs, buf);
	}
	assert(header_length + content_length + header_delimiter.len == recvs.total_len);
	recv_buf = string_list_concat(arena, recvs);

exit:
	if (bio) BIO_free_all(bio);
	if (ssl_ctx) SSL_CTX_free(ssl_ctx);
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
		fprintf(stderr, "request failed, received status code that does not indicate success\n");
		return response_body;
	}

	response_body = lines.tail->string;
	return response_body;
}

#ifdef DEBUG
// NOTE(ariel) I only use this function to print length-based strings in GDB
// conveniently.
void
print_string(String s)
{
	fprintf(stdout, "%.*s\n", s.len, s.str);
}
#endif
