#ifndef REQUEST_H
#define REQUEST_H

#include "base.h"

typedef enum {
	HTTP_METHOD_ERR,
	HTTP_METHOD_GET,
	HTTP_METHOD_PUT,
	HTTP_METHOD_DEL,
} HTTP_Method;

typedef struct {
	HTTP_Method method;
	char *uri;
	char *version;
	// struct dict *header;
	char *body;
} Request;

String request_http_resource(String url);
String parse_http_response(String response);

#endif
