#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "typedefs.h"
#include "allocators.h"

typedef struct Vector
{
	Allocator	*allocator;
	void		*elems;

	u64		elem_size;	// The size of each element
	u32		capacity;	// The amount of elements that can be inserted into the vector
	u32		used;		// How many elements have already been inserted
}	Vector;

void	*vectorGet(Vector *v, u32 idx);
Vector	*vectorCreate(u32 initial_capacity, u64 elem_size, Allocator *a);
// Doesnt check if the pointer passed in is correct, copies v->elem_size data;
void	vectorAppend(Vector *v, void *elem);
void	vectorAppendMultiple(Vector *v, u32 num_elems, void *elems);
void	*vectorPopLast(Vector *v);
void	vectorDestroy(Vector *v);
void	vectorExec(Vector *v, void *udata, void (*FP_VectorExecFunc)(void *elem, void *udata));
void	vectorInsertIndex(Vector *v, void *elem, u32 idx);
void	vectorResize(Vector *v, u64 size);
void	vectorReset(Vector *v);
bool	vectorIsEmpty(Vector *v);


void	testVector(void);
