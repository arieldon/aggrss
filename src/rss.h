#ifndef RSS_H
#define RSS_H

#include "arena.h"

/* NOTE(ariel)
 * There's a key difference between START and OPEN as technical terms in this
 * context. START refers to any tag that marks the beginning of an element.
 * OPEN refers to the beginning of a tag (including closing tags), typically
 * denoted by '<' and optionally one or more additional characters.
 *
 * The names in the code (mostly?) abides by these definitions. Actually, I
 * don't think it does, but it should.
 */
typedef enum {
	RSS_TOKEN_START_LOWER_BOUND,
	RSS_TOKEN_STAG_OPEN,  // <
	RSS_TOKEN_CDATA_OPEN, // <![CDATA[
	RSS_TOKEN_DECL_OPEN,  // <!
	RSS_TOKEN_PI_OPEN,    // <?
	RSS_TOKEN_START_UPPER_BOUND,
	RSS_TOKEN_ETAG_OPEN,  // </

	RSS_TOKEN_EMPTY_TAG_CLOSE, // />
	RSS_TOKEN_TAG_CLOSE,       //  >
	RSS_TOKEN_PI_CLOSE,        // ?>
	RSS_TOKEN_CDATA_CLOSE,     // ]]>

	RSS_TOKEN_COMMENT,   // <!-- -->

	RSS_TOKEN_NAME,
	RSS_TOKEN_ATTRIBUTE_VALUE,
	RSS_TOKEN_CONTENT,
	RSS_TOKEN_CDATA,

	RSS_TOKEN_EQUAL,
	RSS_TOKEN_ERROR,
	RSS_TOKEN_END,
} RSS_Token_Type;

typedef struct RSS_Token_Node {
	struct RSS_Token_Node *next;
	RSS_Token_Type type;
	String text;
} RSS_Token_Node;

typedef struct {
	RSS_Token_Node *first;
	RSS_Token_Node *last;
} RSS_Token_List;

typedef struct RSS_Node_Attribute {
	struct RSS_Node_Attribute *next;
	String name;
	String value;
} RSS_Node_Attribute;

typedef struct {
	RSS_Node_Attribute *first;
} RSS_Attribute_List;

typedef struct RSS_Tree_Node {
	struct RSS_Tree_Node *first_child;
	struct RSS_Tree_Node *last_child;
	struct RSS_Tree_Node *prev_sibling;
	struct RSS_Tree_Node *next_sibling;
	RSS_Attribute_List attrs;
	RSS_Token_Node *token;
	String content;
} RSS_Tree_Node;

typedef struct RSS_Error {
	struct RSS_Error *next;
	RSS_Token_Node *token_location;
	RSS_Tree_Node *tree_location;
	String text;
} RSS_Error;

typedef struct {
	RSS_Error *first;
	RSS_Error *last;
} RSS_Error_List;

typedef struct RSS_Tree {
	struct RSS_Tree *next;

	RSS_Tree_Node *root;
	RSS_Tree_Node *feed_title;
	RSS_Tree_Node *first_item;

	RSS_Error_List errors;
} RSS_Tree;

typedef struct {
	RSS_Tree *first;
	RSS_Tree *last;
} RSS_Tree_List;

RSS_Token_List tokenize_rss(Arena *arena, String source);
RSS_Tree *parse_rss(Arena *arena, RSS_Token_List tokens);

RSS_Tree_Node *find_feed_title(Arena *arena, RSS_Tree_Node *root);
RSS_Tree_Node *find_item_title(RSS_Tree_Node *item);
RSS_Tree_Node *find_item_node(Arena *arena, RSS_Tree_Node *root);

#endif
