#include <errno.h>
#include <stdio.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>

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

typedef struct {
	i16 status_code;
	String_Node *fields;
} HTTP_Response_Header;

global const String line_delimiter = {
	.str = "\r\n",
	.len = 2,
};
global const String header_terminator = {
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

internal HTTP_Response_Header
parse_header(Arena *arena, String str)
{
	HTTP_Response_Header header = { .status_code = -1 };

	String_List lines = string_strsplit(arena, str, line_delimiter);
	if (lines.head) {
		String status_line = lines.head->string;
		String_List ls_status_line = string_split(arena, status_line, ' ');
		if (ls_status_line.list_size == 3) {
			String status_code = ls_status_line.head->next->string;
			header.status_code = string_to_int(status_code, 10);
		}
		header.fields = lines.head->next;
	}

	return header;
}

internal String
format_get_request(Arena *arena, URL url)
{
	// NOTE(ariel) It seems necessary to spoof the user agent field to receive a
	// response from most servers today. Update: I don't think this is true
	// anymore. There was a different issue with the client, and I guess I fixed
	// it in tandem with these changes.
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
get_content_length(HTTP_Response_Header header)
{
	local_persist String content_length_field = {
		.str = "Content-Length",
		.len = 14,
	};
	i32 content_length = -1;

	String_Node *field_node = header.fields;
	while (field_node && content_length < 0) {
		String field = field_node->string;

		i32 colon_index = string_find_ch(field, ':');
		if (colon_index > 0) {
			String name = string_prefix(field, colon_index);
			if (string_match(name, content_length_field)) {
				String remainder = string_suffix(field, colon_index + 1);
				String value = string_trim_spaces(remainder);
				content_length = string_to_int(value, 10);
			}
		}

		field_node = field_node->next;
	}

	return content_length;
}

internal String
get_transfer_encoding(HTTP_Response_Header header)
{
	local_persist String transfer_encoding_field = {
		.str = "Transfer-Encoding",
		.len = 17,
	};
	String transfer_encoding = {0};

	String_Node *field_node = header.fields;
	while (field_node && !transfer_encoding.str) {
		String field = field_node->string;

		i32 colon_index = string_find_ch(field, ':');
		if (colon_index > 0) {
			String name = string_prefix(field, colon_index);
			if (string_match(name, transfer_encoding_field)) {
				String remainder = string_suffix(field, colon_index + 1);
				transfer_encoding = string_trim_spaces(remainder);
			}
		}

		field_node = field_node->next;
	}

	return transfer_encoding;
}

internal String
decode_chunked_encoding(Arena *persistent_arena, Arena *scratch_arena, String encoded)
{
	String decoded = {0};
	String_List decoded_chunks = {0};

	String_List encoded_chunks = string_strsplit(scratch_arena, encoded, line_delimiter);
	String_Node *encoded_chunk = encoded_chunks.head;
	while (encoded_chunk) {
		i32 decoded_length = 0;
		i32 expected_length = string_to_int(encoded_chunk->string, 16);

		encoded_chunk = encoded_chunk->next;
		while (encoded_chunk && decoded_length < expected_length) {
			decoded_length += encoded_chunk->string.len;
			push_string_node(&decoded_chunks, encoded_chunk);
			encoded_chunk = encoded_chunk->next;
		}
	}

	decoded = string_list_concat(persistent_arena, decoded_chunks);
	return decoded;
}

internal i32
receive(Arena *arena, BIO *bio, String *read_buffer)
{
	i32 upper_bound = read_buffer->len + READ_BUF_SIZ;
	read_buffer->str = arena_realloc(arena, upper_bound);

	i32 nbytes = BIO_read(bio, read_buffer->str + read_buffer->len, READ_BUF_SIZ);
	if (nbytes > 0) {
		read_buffer->len += nbytes;
		read_buffer->str = arena_realloc(arena, read_buffer->len);
	}

	return nbytes;
}

String
download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr)
{
	String body = {0};

	BIO *bio = 0;
	SSL_CTX *ssl_ctx = 0;
	URL url = parse_http_url(urlstr);

	char *domain = string_terminate(scratch_arena, url.domain);
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
	if (BIO_do_connect(bio) <= 0) goto exit;

	String request = format_get_request(scratch_arena, url);
	BIO_write(bio, request.str, request.len);
	BIO_flush(bio);

	// NOTE(ariel) Keep reallocating the same string to extend it as needed.
	// It's important than no other allocations occur using _this_ arena. The
	// code below assumes the string remains contiguous.
	String raw_header = {0};
	String response = {
		.str = arena_alloc(persistent_arena, 0),
		.len = 0,
	};

	i32 delimiter_index = -1;
	do {
		receive(persistent_arena, bio, &response);
		delimiter_index = string_find_substr(response, header_terminator);
		if (delimiter_index >= 0) {
			raw_header.str = response.str;
			raw_header.len = delimiter_index;
		}
	} while (delimiter_index < 0);

	HTTP_Response_Header header = parse_header(scratch_arena, raw_header);
	if (header.status_code != 200) goto exit;

	body.str = response.str + raw_header.len + header_terminator.len;
	if (response.len > raw_header.len + header_terminator.len) {
		// NOTE(ariel) Only set the length if it's valid, and if the pointer isn't
		// valid now, it will be by the end of the function.
		body.len = response.len - raw_header.len - header_terminator.len;
	}
	assert(body.len >= 0);

	i32 content_length = get_content_length(header);
	if (content_length != -1) {
		while (body.len < content_length) {
			i32 nbytes = receive(persistent_arena, bio, &response);
			body.len += nbytes;
		}
		assert(content_length == body.len);
	} else {
		// NOTE(ariel) Only handle chuncked encoding for now -- no compression of
		// any sort.
		String transfer_encoding = get_transfer_encoding(header);
		local_persist String chunked = {
			.str = "chunked",
			.len = 7
		};
		if (!string_match(transfer_encoding, chunked)) goto exit;

		i32 nzeros = 0;
		String chunked_encoding = string_duplicate(scratch_arena, body);
		for (;;) {
			i32 nbytes = receive(scratch_arena, bio, &chunked_encoding);
			// TODO(ariel) Replace with BIO_should_retry()?
			if (nbytes == 0) {
				++nzeros;
				if (nzeros == 3) goto exit;
			} else {
				nzeros = 0;
			}

			local_persist String chunk_terminator = {
				.str = "\r\n0\r\n\r\n",
				.len = 7,
			};
			String trailing_bytes = string_suffix(
				chunked_encoding, chunked_encoding.len - chunk_terminator.len);
			if (string_match(trailing_bytes, chunk_terminator)) {
				chunked_encoding.len -= chunk_terminator.len;
				break;
			}
		}
		body = decode_chunked_encoding(persistent_arena, scratch_arena, chunked_encoding);
	}

exit:
	if (bio) BIO_free_all(bio);
	if (ssl_ctx) SSL_CTX_free(ssl_ctx);
	return body;
}

#ifdef DEBUG
// NOTE(ariel) I only use these function to conveniently output length-based
// strings in GDB.

void
print_string(String s)
{
	fprintf(stdout, "%.*s\n", s.len, s.str);
}

void
log_string(String s)
{
	FILE *file = fopen("./log", "w+");
	fprintf(file, "%.*s\n", s.len, s.str);
	fclose(file);
}
#endif
