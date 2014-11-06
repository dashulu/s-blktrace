#include <stdlib.h>
#include <unistd.h>
//#include <stddef.h>

#include"IntList.h"

int IntList_init(struct IntList* list, int elementsize) {
	if(list == NULL)
		return -1;
	list->size = 0;
	list->first_node = NULL;
	list->last_node = NULL;
	return 0;
}

//if pop success, return 0 and the node's value stroe in *value;
//if failure, return -1
int IntList_pop(struct IntList* list, int* value) {
	struct IntListNode* tmp;

	if(list == NULL)
		return -1;
	if(list->size == 0)
		return -1;

	tmp = list->first_node;
	list->first_node = list->first_node->next;
	*value = tmp->value;
	free(tmp);
	list->size--;
	return 0;
}

int IntList_push(struct IntList* list, int value) {
	struct IntListNode* node = (struct IntListNode*)malloc(sizeof(struct IntListNode));
	node->value = value;

	if(list == NULL) {
		free(node);
		return -1;
	}

	if(list->size == 0) {
		list->first_node = node;
		list->last_node = node;
		node->next = NULL;
	} else {
		list->last_node->next = node;
		list->last_node = node;
	}
	list->size++;
	return 0;
}

int IntList_size(struct IntList* list) {
	if(list == NULL)
		return -1;
	else
		return list->size;
}

int int_to_string(int value, char* buf, int size) {
	int length = 0;

	if(buf == NULL || size <= 0)
		return 0;

	if(value < 0)
		value = -value;

	if(value == 0) {
		buf[0] = 48;
		return 1;
	}

	while(value > 0) {
		if(length < size) {
			buf[length++] = (value%10) + 48;
		} else {
			return length;
		}
		value /= 10;
	}
	return length;
}
//transfer list to string, for example {1,2,3} to "1 2 3 ";
//return the length of buf after transfer.
int IntList_tostring(struct IntList* list, char* buf, int size) {
	int str_len = 0;
	int tmp_len = 0;
	char tmp[20];
	int value;
	int i;

	if(size <= 0 || list == NULL || buf == NULL)
		return 0;

	while(list->size > 0) {
		if(IntList_pop(list, &value) < 0)
			break;
		tmp_len = int_to_string(value, tmp, 20);
		if(tmp_len == 0 || tmp_len + 2 + str_len > size)
			break;
		for(i = 0;i < tmp_len;i++) {
			buf[str_len++] = tmp[tmp_len - 1 - i];
		}
		buf[str_len++] = ' ';
	}
	buf[str_len] = '\0';
	return str_len;
}
