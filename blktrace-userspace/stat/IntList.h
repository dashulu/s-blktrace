#ifndef INTLIST_H
#define INTLIST_H

struct IntListNode{
	void *value;
	struct IntListNode* next;
};

struct IntList{
	struct IntListNode* first_node;
	struct IntListNode* last_node;
	int size;
};

int IntList_init(struct IntList* list, int elementsize);
//if pop success, return 0 and the node's value stroe in *value;
//if failure, return -1

int IntList_pop(struct IntList* list, int* value);
int IntList_push(struct IntList* list, int value);
int IntList_size(struct IntList* list);

//transfer list to string, for example {1,2,3} to "1 2 3 ";
//return the length of buf after transfer.
int IntList_tostring(struct IntList* list, char* buf, int size);

#endif