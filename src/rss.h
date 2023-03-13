#ifndef RSS_H
#define RSS_H

#include "arena.h"
#include "base.h"
#include "str.h"

typedef struct RSS_Tree_Node RSS_Tree_Node;
struct RSS_Tree_Node
{
	RSS_Tree_Node *parent;
	RSS_Tree_Node *first_child;
	RSS_Tree_Node *last_child;
	RSS_Tree_Node *prev_sibling;
	RSS_Tree_Node *next_sibling;
	String name;
	String content;
};

typedef struct RSS_Error RSS_Error;
struct RSS_Error
{
	RSS_Error *next;
	i32 source_offset;
	String text;
};

typedef struct RSS_Error_List RSS_Error_List;
struct RSS_Error_List
{
	RSS_Error *first;
	RSS_Error *last;
};

typedef struct RSS_Tree RSS_Tree;
struct RSS_Tree
{
	RSS_Tree *next;

	RSS_Tree_Node *root;
	RSS_Tree_Node *feed_title;
	RSS_Tree_Node *first_item;

	RSS_Error_List errors;
};

typedef struct RSS_Tree_List RSS_Tree_List;
struct RSS_Tree_List
{
	RSS_Tree *first;
	RSS_Tree *last;
};

RSS_Tree *parse_rss(Arena *arena, String source);

RSS_Tree_Node *find_feed_title(Arena *arena, RSS_Tree_Node *root);
RSS_Tree_Node *find_item_title(RSS_Tree_Node *item);
RSS_Tree_Node *find_item_node(Arena *arena, RSS_Tree_Node *root);

#endif
