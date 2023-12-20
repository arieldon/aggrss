#ifndef RSS_H
#define RSS_H

typedef struct RSS_Attribute RSS_Attribute;
struct RSS_Attribute
{
	RSS_Attribute *next;
	string name;
	string value;
};

typedef struct RSS_Tree_Node RSS_Tree_Node;
struct RSS_Tree_Node
{
	RSS_Tree_Node *parent;
	RSS_Tree_Node *first_child;
	RSS_Tree_Node *last_child;
	RSS_Tree_Node *prev_sibling;
	RSS_Tree_Node *next_sibling;
	RSS_Attribute *attributes;
	string name;
	string content;
};

typedef struct RSS_Error RSS_Error;
struct RSS_Error
{
	RSS_Error *next;
	s32 source_offset;
	string text;
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

static RSS_Tree *parse_rss(arena *Arena, string source);

static RSS_Tree_Node *find_feed_title(arena *Arena, RSS_Tree_Node *root);
static RSS_Tree_Node *find_item_child_node(RSS_Tree_Node *item, string name);
static RSS_Tree_Node *find_item_title(RSS_Tree_Node *item);
static RSS_Tree_Node *find_item_link(RSS_Tree_Node *item);
static RSS_Tree_Node *find_item_node(arena *Arena, RSS_Tree_Node *root);
static string find_link(RSS_Tree_Node *item);

#ifdef PRINT_TREE_SUPPORT
void print_rss_tree(RSS_Tree *tree, FILE *stream);
#endif

#endif
