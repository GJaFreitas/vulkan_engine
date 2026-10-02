#include "vector.h"

void	_vector_init_internal(Vector *v, Allocator *allocator, u64 elem_size, u64 cap) {
	assert(v != NULL);
	assert(elem_size > 0);
	assert(allocator != NULL);
	assert(cap > 0);

	*v = (Vector){
		.allocator = allocator,
		.elems = NULL,
		.elem_size = elem_size,
		.capacity = (u32)cap,
		.used = 0,
	};

	v->elems = allocator->fp_allocation(allocator, elem_size * cap, DEFAULT_ALIGN);
}

void	*_vector_get_internal(Vector *v, u32 index) {
	assert(v != NULL);
	assert(index < v->used && "Vector index out of bounds");

	return (u8 *)v->elems + (index * v->elem_size);
}

void	*_vector_push_internal(Vector *v, const void *elem) {
	assert(v != NULL);
	assert(elem != NULL);

	if (v->used >= v->capacity) {
		u32 new_cap = (v->capacity == 0) ? 8 : (v->capacity * 2);
		u64 old_bytes = v->elem_size * v->capacity;
		u64 new_bytes = v->elem_size * new_cap;

		v->elems = v->allocator->fp_reallocation(v->allocator, v->elems, old_bytes, new_bytes, DEFAULT_ALIGN);
		assert(v->elems != NULL && "Reallocation failed");
		v->capacity = new_cap;
	}

	u8 *dst = (u8 *)v->elems + (v->used * v->elem_size);
	memcpy(dst, elem, v->elem_size);
	v->used++;

	return dst;
}

void	_vector_append_multiple_internal(Vector *v, u32 num_elems, const void *elems) {
	assert(v != NULL);
	assert(elems != NULL);

	u32 req_cap = v->used + num_elems;
	if (req_cap > v->capacity) {
		u32 new_cap = (v->capacity == 0) ? 8 : v->capacity;
		while (new_cap < req_cap) {
			new_cap *= 2;
		}

		u64 old_bytes = v->elem_size * v->capacity;
		u64 new_bytes = v->elem_size * new_cap;

		v->elems = v->allocator->fp_reallocation(v->allocator, v->elems, old_bytes, new_bytes, DEFAULT_ALIGN);
		assert(v->elems != NULL && "Reallocation failed");
		v->capacity = new_cap;
	}

	u8 *dst = (u8 *)v->elems + (v->used * v->elem_size);
	memcpy(dst, elems, num_elems * v->elem_size);
	v->used += num_elems;
}

void	*_vector_pop_last_internal(Vector *v) {
	assert(v != NULL);
	assert(v->used > 0 && "Vector underflow on pop");

	v->used--;
	return (u8 *)v->elems + (v->used * v->elem_size);
}

void	_vector_destroy_internal(Vector *v) {
	assert(v != NULL);
	assert(v->elems != NULL);

	v->allocator->fp_free(v->allocator, v->elems);
	v->elems = NULL;
	v->capacity = 0;
	v->used = 0;
}

void	_vector_exec_internal(Vector *v, void *udata, void (*FP_VectorExecFunc)(void *elem, void *udata)) {
	assert(v != NULL);
	assert(FP_VectorExecFunc != NULL);

	u8 *curr = (u8 *)v->elems;
	u64 stride = v->elem_size;
	u32 count = v->used;

	for (u32 i = 0; i < count; i++) {
		FP_VectorExecFunc(curr, udata);
		curr += stride;
	}
}

void	_vector_insert_index_internal(Vector *v, const void *elem, u32 idx) {
	assert(v != NULL);
	assert(elem != NULL);
	assert(idx <= v->used && "Index out of bounds");

	if (v->used >= v->capacity) {
		u32 new_cap = (v->capacity == 0) ? 8 : (v->capacity * 2);
		u64 old_bytes = v->elem_size * v->capacity;
		u64 new_bytes = v->elem_size * new_cap;

		v->elems = v->allocator->fp_reallocation(v->allocator, v->elems, old_bytes, new_bytes, DEFAULT_ALIGN);
		assert(v->elems != NULL && "Reallocation failed");
		v->capacity = new_cap;
	}

	u8 *base = (u8 *)v->elems;
	u8 *src = base + (idx * v->elem_size);
	u8 *dst = src + v->elem_size;
	u32 move_count = v->used - idx;

	if (move_count > 0) {
		memmove(dst, src, move_count * v->elem_size);
	}

	memcpy(src, elem, v->elem_size);
	v->used++;
}

void	_vector_resize_internal(Vector *v, u64 size) {
	assert(v != NULL);

	u32 new_cap = (u32)size;
	if (new_cap != v->capacity) {
		u64 old_bytes = v->elem_size * v->capacity;
		u64 new_bytes = v->elem_size * new_cap;

		if (new_cap == 0) {
			if (v->elems) {
				v->allocator->fp_free(v->allocator, v->elems);
				v->elems = NULL;
			}
		} else {
			v->elems = v->allocator->fp_reallocation(v->allocator, v->elems, old_bytes, new_bytes, DEFAULT_ALIGN);
			assert(v->elems != NULL && "Reallocation failed");
		}

		v->capacity = new_cap;
		if (v->used > new_cap) {
			v->used = new_cap;
		}
	}
}

void	_vector_reset_internal(Vector *v) {
	assert(v != NULL);
	v->used = 0;
}

bool	_vector_is_empty_internal(Vector *v) {
	assert(v != NULL);
	return v->used == 0;
}
