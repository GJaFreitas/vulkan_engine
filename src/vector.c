#include "vector.h"

static void	_increaseCapacity(Vector *v, u32 number_additions)
{
	const u32	cur_capacity = v->capacity;
	const u32	needed_capacity = v->capacity + number_additions;

	u32	new_capacity = cur_capacity;
	while (new_capacity < needed_capacity) {
		new_capacity *= 2;

		// OVERFLOW
		if (new_capacity <= cur_capacity) {
			new_capacity = UINT32_MAX;
			break ;
		}
	}

	if (new_capacity < needed_capacity) {
		fprintf(stderr, "Well shit vector exceeded capacity for u32, change to u64\n");
		exit(2);
	}

	void	*new_ptr = v->allocator->fp_allocation(v->allocator, v->elem_size * new_capacity, DEFAULT_ALIGN);
	memcpy(new_ptr, v->elems, v->elem_size * v->used);
	v->allocator->fp_free(v->allocator, v->elems);
	v->elems = new_ptr;
	v->capacity = new_capacity;
}

void	*vectorGet(Vector *v, u32 idx)
{
	if (idx > v->used) {
		fprintf(stderr, "vectorGet() called with invalid index, crashing\n");
		exit(2);
	}

	u8	*arr = (u8 *)v->elems;
	void	*elem = &arr[v->elem_size * idx];
	return (elem);
}

Vector	*vectorCreate(u32 initial_capacity, u64 elem_size, Allocator *a)
{
	Vector	*v = a->fp_allocation(a, sizeof(Vector), DEFAULT_ALIGN);

	*v = (Vector) {
		.allocator = a,
		.elems = NULL,
		.elem_size = elem_size,
		.capacity = initial_capacity,
		.used = 0,
	};

	v->elems = a->fp_allocation(a, elem_size * initial_capacity, DEFAULT_ALIGN);
	return (v);
}

// Doesnt check if the pointer passed in is correct, copies v->elem_size data
void	vectorAppend(Vector *v, void *elem)
{
	// No capacity left in vector
	if (v->used + 1 == v->capacity) {
		_increaseCapacity(v, 1);
	}

	// NOTE: There is no check for if the pointer passed as 'elem' is the correct size and v->elem_size bytes are copied,
	// this can be a source of bugs if the wrong type is passed (2026-07-23)
	memcpy(vectorGet(v, v->used), elem, v->elem_size);
	v->used += 1;
}

void	vectorAppendMultiple(Vector *v, u32 num_elems, void *elems)
{
	if (v->used + num_elems == v->capacity) {
		_increaseCapacity(v, 1);
	}

	memcpy(vectorGet(v, v->used), elems, v->elem_size * num_elems);
	v->used += num_elems;
}

void	*vectorPopLast(Vector *v)
{
	if (v->used == 0) {
		return (NULL);
	}

	void	*elem = vectorGet(v, v->used - 1);
	v->used -= 1;
	return (elem);
}

void	vectorDestroy(Vector *v)
{
	v->allocator->fp_free(v->allocator, v->elems);
	v->allocator->fp_free(v->allocator, v);
}

// Executes the passed function for every element in the vector
void	vectorExec(Vector *v, void *udata, void (*FP_VectorExecFunc)(void *elem, void *udata))
{
	for (u32 i = 0; i < v->used; i++) {
		void	*elem = vectorGet(v, i);
		FP_VectorExecFunc(elem, udata);
	}
}

void	vectorInsertIndex(Vector *v, void *elem, u32 idx)
{
	if (idx > v->used) {
		return ;
	}

	void	*array_pos = vectorGet(v, idx);
	memcpy(array_pos, elem, v->elem_size);
}

void	vectorResize(Vector *v, u64 size)
{
	u64	new_capacity;
	if (v->used > size) {
		new_capacity = v->used;
	} else {
		new_capacity = size;
	}
	void	*new_ptr = v->allocator->fp_allocation(v->allocator, v->elem_size * new_capacity, DEFAULT_ALIGN);
	memcpy(new_ptr, v->elems, v->elem_size * v->used);
	v->allocator->fp_free(v->allocator, v->elems);
	v->elems = new_ptr;
	v->capacity = new_capacity;
}

bool	vectorIsEmpty(Vector *v)
{
	return (v->used == 0);
}

void	vectorReset(Vector *v)
{
	v->used = 0;
}

void	printFunc(void *elem, void *udata)
{
	(void)udata;
	u64	*n_cast = elem;
	printf("Number: %zu\n", *n_cast);
}

void	testVector(void)
{
	u64	n1 = UINT16_MAX;
	u64	n2 = 428917481;
	u64	n3 = 0;

	Allocator	a = newArenaAllocator(1 << 17, NULL, DEFAULT_ALIGN);
	Vector		*v = vectorCreate(16, sizeof(u64), &a);

	vectorAppend(v, &n1);
	vectorAppend(v, &n2);
	vectorAppend(v, &n3);

	printf("First Print:\n");
	vectorExec(v, NULL, printFunc);
	printf("End Print\n");

	u64	numbers[6] = {1, 2, 3, 4, 5, 6};

	vectorAppendMultiple(v, sizeofarray(numbers), numbers);

	printf("Second Print:\n");
	vectorExec(v, NULL, printFunc);
	printf("End Print\n");

	u64	over_capacity = 666999;

	vectorAppend(v, &over_capacity);

	printf("Third Print:\n");
	vectorExec(v, NULL, printFunc);
	printf("End Print\n");

	u64	number = 256;
	for (u32 i = 20; v->used < i;) {
		vectorAppend(v, &number);
	}

	u64	final_num = UINT64_MAX;
	vectorAppend(v, &final_num);

	printf("4th Print:\n");
	vectorExec(v, NULL, printFunc);
	printf("End Print\n");

	vectorPopLast(v);
	vectorInsertIndex(v, &final_num, 1);
	vectorInsertIndex(v, &final_num, 2);
	vectorInsertIndex(v, &final_num, 3);

	printf("5th Print:\n");
	vectorExec(v, NULL, printFunc);
	printf("End Print\n");
}
