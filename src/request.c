#include <ctype.h>
#include <stdio.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "arena.h"
#include "base.h"
#include "request.h"
#include "str.h"

#define HTTP_PORT  "80"
#define HTTPS_PORT "443"

enum { READ_BUF_SIZ = KB(8) };

typedef struct HTTP_Response_Header HTTP_Response_Header;
struct HTTP_Response_Header
{
	i16 status_code;
	String_Node *fields;
};

global const String line_delimiter = static_string_literal("\r\n");
global const String header_terminator = static_string_literal("\r\n\r\n");

URL
parse_http_url(String urlstr)
{
	URL url = {0};

	{
		String scheme = {0};
		String http = string_literal("http://");
		String https = string_literal("https://");

		scheme = string_prefix(urlstr, http.len);
		if (string_match(scheme, http))
		{
			url.scheme = scheme;
		}

		scheme = string_prefix(urlstr, https.len);
		if (string_match(scheme, https))
		{
			url.scheme = scheme;
		}
	}

	if (url.scheme.str)
	{
		url.domain = string_suffix(urlstr, url.scheme.len);
		i32 delimiter = string_find_ch(url.domain, '/');
		if (delimiter > 0)
		{
			url.domain = string_prefix(url.domain, delimiter);
		}
	}

	if (url.scheme.str)
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
	if (lines.head)
	{
		String status_line = lines.head->string;
		String_List ls_status_line = string_split(arena, status_line, ' ');
		if (ls_status_line.list_size == 3)
		{
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
	local_persist String accept = static_string_literal(
		"Accept: text/html,application/rss+xml,application/xhtml+xml,application/xml\r\n");
	local_persist String accept_encoding = static_string_literal("Accept-Encoding: identity\r\n");
	local_persist String connection = static_string_literal("Connection: close\r\n");

	String_List ls = {0};

	string_list_push_string(arena, &ls, string_literal("GET "));
	string_list_push_string(arena, &ls, url.path);
	string_list_push_string(arena, &ls, string_literal(" HTTP/1.1\r\n"));
	string_list_push_string(arena, &ls, string_literal("Host: "));
	string_list_push_string(arena, &ls, url.domain);
	string_list_push_string(arena, &ls, line_delimiter);
	string_list_push_string(arena, &ls, accept);
	string_list_push_string(arena, &ls, accept_encoding);
	string_list_push_string(arena, &ls, connection);
	string_list_push_string(arena, &ls, line_delimiter);

	String request = string_list_concat(arena, ls);
	return request;
}

internal i32
get_content_length(HTTP_Response_Header header)
{
	local_persist String content_length_field = static_string_literal("content-length");
	i32 content_length = -1;

	String_Node *field_node = header.fields;
	while (field_node && content_length < 0)
	{
		String field = field_node->string;

		i32 colon_index = string_find_ch(field, ':');
		if (colon_index > 0)
		{
			String name = string_prefix(field, colon_index);
			string_lower(name);
			if (string_match(name, content_length_field))
			{
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
	local_persist String transfer_encoding_field = static_string_literal("transfer-encoding");
	String transfer_encoding = {0};

	String_Node *field_node = header.fields;
	while (field_node && !transfer_encoding.str)
	{
		String field = field_node->string;

		i32 colon_index = string_find_ch(field, ':');
		if (colon_index > 0)
		{
			String name = string_prefix(field, colon_index);
			string_lower(name);
			if (string_match(name, transfer_encoding_field))
			{
				String remainder = string_suffix(field, colon_index + 1);
				transfer_encoding = string_trim_spaces(remainder);
			}
		}

		field_node = field_node->next;
	}

	return transfer_encoding;
}

internal i32
receive(Arena *arena, BIO *bio, String *read_buffer)
{
	i32 upper_bound = read_buffer->len + READ_BUF_SIZ;
	read_buffer->str = arena_realloc(arena, upper_bound);

	i32 nbytes = BIO_read(bio, read_buffer->str + read_buffer->len, READ_BUF_SIZ);
	if (nbytes > 0)
	{
		read_buffer->len += nbytes;
		read_buffer->str = arena_realloc(arena, read_buffer->len);
	}

	return nbytes;
}

Resource
download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr)
{
	local_persist thread_local SSL_CTX *ssl_ctx;
	local_persist thread_local BIO *https_bio;
	local_persist thread_local BIO *http_bio;

	Resource resource = {0};

	BIO *bio = 0;
	URL url = parse_http_url(urlstr);
	if (!url.scheme.str || !url.domain.str)
	{
		resource.error = string_literal("failed to parse url");
		goto exit;
	}
	char *domain = string_terminate(scratch_arena, url.domain);

	if (url.scheme.len == 8)
	{
		if (!ssl_ctx)
		{
			const SSL_METHOD *method = TLS_client_method();
			if (!method)
			{
				resource.error = string_literal("failed to initailize TLS client method");
				goto exit;
			}

			ssl_ctx = SSL_CTX_new(method);
			if (!ssl_ctx || !SSL_CTX_set_default_verify_paths(ssl_ctx))
			{
				resource.error = string_literal("failed to initailize TLS context");
				goto exit;
			}
		}

		if (!https_bio)
		{
			https_bio = BIO_new_ssl_connect(ssl_ctx);
			if (!https_bio)
			{
				resource.error = string_literal("failed to intiailize BIO for HTTPS");
				goto exit;
			}
		}
		bio = https_bio;

		SSL *ssl = 0;
		BIO_get_ssl(bio, &ssl);
		SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
		SSL_set_tlsext_host_name(ssl, domain);

		BIO_set_conn_port(bio, HTTPS_PORT);
	}
	else
	{
		if (!http_bio)
		{
			http_bio = BIO_new(BIO_s_connect());
			if (!http_bio)
			{
				resource.error = string_literal("failed to initialize BIO for HTTP");
				goto exit;
			}
		}
		bio = http_bio;
		BIO_set_conn_port(bio, HTTP_PORT);
	}

	BIO_set_conn_hostname(bio, domain);
	if (BIO_do_connect(bio) <= 0)
	{
		resource.error = string_literal("failed to connect");
		goto exit;
	}

	String request = format_get_request(scratch_arena, url);
	BIO_write(bio, request.str, request.len);
	BIO_flush(bio);

	// NOTE(ariel) Keep reallocating the same string to extend it as needed.
	// It's important than no other allocations occur using _this_ arena. The
	// code below assumes the string remains contiguous.
	String raw_header = {0};
	String response =
	{
		.str = arena_alloc(persistent_arena, 0),
		.len = 0,
	};

	i32 delimiter_index = -1;
	do
	{
		receive(persistent_arena, bio, &response);
		delimiter_index = string_find_substr(response, header_terminator);
		if (delimiter_index >= 0)
		{
			raw_header.str = response.str;
			raw_header.len = delimiter_index;
		}
	} while (delimiter_index < 0);

	HTTP_Response_Header header = parse_header(scratch_arena, raw_header);
	if (header.status_code != 200)
	{
		resource.error = string_literal("client received status code != 200");
		goto exit;
	}

	resource.result.str = response.str + raw_header.len + header_terminator.len;
	if (response.len > raw_header.len + header_terminator.len)
	{
		// NOTE(ariel) Only set the length if it's valid, and if the pointer isn't
		// valid now, it will be by the end of the function.
		resource.result.len = response.len - raw_header.len - header_terminator.len;
	}
	assert(resource.result.len >= 0);

	i32 content_length = get_content_length(header);
	if (content_length != -1)
	{
		while (resource.result.len < content_length)
		{
			i32 nbytes = receive(persistent_arena, bio, &response);
			resource.result.len += nbytes;
		}
		assert(content_length == resource.result.len);
	}
	else
	{
		// NOTE(ariel) Only handle chunked encoding for now -- no compression of
		// any sort.
		String transfer_encoding = get_transfer_encoding(header);
		if (!string_match(string_literal("chunked"), transfer_encoding))
		{
			resource.error = string_literal("failed to recognize transfer encoding directive");
			goto exit;
		}

		String decoded =
		{
			.str = arena_alloc(persistent_arena, 0),
		};
		String_List chunks = string_strsplit(scratch_arena, resource.result, line_delimiter);
		String_Node *node = chunks.head;
		while (node)
		{
			for (i32 i = 0; i < node->string.len; ++i)
			{
				if (!isxdigit(node->string.str[i]))
				{
					String_List error = {0};
					string_list_push_string(scratch_arena, &error, node->string);
					string_list_push_string(scratch_arena, &error,
						string_literal(" does not specify the length of chunk in the response"));
					resource.error = string_list_concat(scratch_arena, error);
					goto exit;
				}
			}

			i32 expected_chunk_size = string_to_int(node->string, 16);
			if (expected_chunk_size == 0)
			{
				resource.result = decoded;
				break;
			}

			decoded.str = arena_realloc(persistent_arena, decoded.len + expected_chunk_size);
			while (expected_chunk_size > 0)
			{
				if (!node->next)
				{
					String buf = {0};
					buf.str = arena_alloc(scratch_arena, expected_chunk_size);
					buf.len = BIO_read(bio, buf.str, expected_chunk_size);
					buf.str = arena_realloc(scratch_arena, buf.len);
					string_list_push_string(scratch_arena, &chunks, buf);
				}
				node = node->next;

				if (node->string.len > 0)
				{
					memcpy(decoded.str + decoded.len, node->string.str, node->string.len);
					decoded.len += node->string.len;
					expected_chunk_size -= node->string.len;
				}
			}

			assert(expected_chunk_size >= 0);
			while (!node->next)
			{
				String buf = {0};
				buf.str = arena_alloc(scratch_arena, 16);
				buf.len = BIO_read(bio, buf.str, 16);

				String_List more_chunks = string_strsplit(scratch_arena, buf, line_delimiter);
				if (more_chunks.head)
				{
					String_Node *n = more_chunks.head;
					while (n)
					{
						// NOTE(ariel) Skip chunk terminator.
						if (n->string.len > 0)
						{
							string_list_push_node(&chunks, n);
						}
						n = n->next;
					}
				}
			}
			node = node->next;
		}
	}

exit:
	if (bio)
	{
		BIO_reset(bio);
	}
	return resource;
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
	if (!file)
	{
		abort();
	}
	fprintf(file, "%.*s\n", s.len, s.str);
	fclose(file);
}
#endif
