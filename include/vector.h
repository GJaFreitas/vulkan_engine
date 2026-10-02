#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "typedefs.h"
#include "allocators.h"

//
// Typechecked generic data structures:
// https://danielchasehooper.com/posts/typechecked-generic-c-data-structures/
//

typedef struct Vector
{
	Allocator	*allocator;
	void		*elems;

	u64		elem_size;	// The size of each element
	u32		capacity;	// The amount of elements that can be inserted into the vector
	u32		used;		// How many elements have already been inserted
}	Vector;

#define Vector(T) \
	union { \
		Vector vec; \
		T *payload; \
	}

/* Internal API Declarations */
void	_vector_init_internal(Vector *v, Allocator *allocator, u64 elem_size, u64 cap);
void	*_vector_get_internal(Vector *v, u32 index);
void	*_vector_push_internal(Vector *v, const void *elem);
void	_vector_append_multiple_internal(Vector *v, u32 num_elems, const void *elems);
void	*_vector_pop_last_internal(Vector *v);
void	_vector_destroy_internal(Vector *v);
void	_vector_exec_internal(Vector *v, void *udata, void (*FP_VectorExecFunc)(void *elem, void *udata));
void	_vector_insert_index_internal(Vector *v, const void *elem, u32 idx);
void	_vector_resize_internal(Vector *v, u64 size);
void	_vector_reset_internal(Vector *v);
bool	_vector_is_empty_internal(Vector *v);

/* Type-Safe Macro Interface */

#define vectorInit(v, allocator, cap) \
	_vector_init_internal(&(v)->vec, (allocator), sizeof(*(v)->payload), (cap))

#define vectorGet(v, index) \
	((__typeof__((v)->payload))_vector_get_internal(&(v)->vec, (index)))

#define vectorPush(v, elem_ptr) \
	((__typeof__((v)->payload))_vector_push_internal( \
		&(v)->vec, \
		(1 ? (elem_ptr) : (v)->payload) \
	))

#define vectorAppendMultiple(v, num_elems, elems_ptr) \
	_vector_append_multiple_internal( \
		&(v)->vec, \
		(num_elems), \
		(1 ? (elems_ptr) : (v)->payload) \
	)

#define vectorPopLast(v) \
	((__typeof__((v)->payload))_vector_pop_last_internal(&(v)->vec))

#define vectorDestroy(v) \
	_vector_destroy_internal(&(v)->vec)

#define vectorExec(v, udata, exec_func) \
	_vector_exec_internal( \
		&(v)->vec, \
		(udata), \
		(void (*)(void *, void *))(exec_func) \
	)

#define vectorInsertIndex(v, elem_ptr, idx) \
	_vector_insert_index_internal( \
		&(v)->vec, \
		(1 ? (elem_ptr) : (v)->payload), \
		(idx) \
	)

#define vectorResize(v, size) \
	_vector_resize_internal(&(v)->vec, (size))

#define vectorReset(v) \
	_vector_reset_internal(&(v)->vec)

#define vectorIsEmpty(v) \
	_vector_is_empty_internal(&(v)->vec)

#define vectorCount(v) (((Vector *)(v))->used)

#define vectorElements(v) (((Vector *)(v))->elems)
