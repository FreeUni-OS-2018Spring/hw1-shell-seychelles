#include "vector.h"
#include <assert.h>
#include <search.h>

void grow(vector *v) {
	v->allocLen += v->initAlloc;
	v->elems = realloc(v->elems, v->allocLen * v->elemSize);
	assert(v->elems != NULL);
}

void VectorNew(vector *v, int elemSize, VectorFreeFunction freeFn, int initialAllocation) {
	assert(elemSize > 0);
	assert(initialAllocation >= 0);
	if(initialAllocation == 0) v->initAlloc = 4;
	else v->initAlloc = initialAllocation;
	v->allocLen = v->initAlloc;
	v->logLen = 0;
	v->elemSize = elemSize;
	v->free = freeFn;
	v->elems = malloc(v->allocLen * v->elemSize);
	assert(v->elems != NULL);
}

void VectorDispose(vector *v) {
	if(v->free != NULL) {
		for(int i = 0; i < v->logLen; i++) {
			void* tmp = (char*)v->elems + i * v->elemSize;
			v->free(tmp);
		}
	}
	free(v->elems);
}

int VectorLength(const vector *v) {
	return v->logLen;
}

void *VectorNth(const vector *v, int position) {
	assert(position >= 0 && position < v->logLen);
	return (char*)v->elems + position * v->elemSize;
}

void VectorReplace(vector *v, const void *elemAddr, int position) {
	assert(position >= 0 && position < v->logLen);
	if(v->free != NULL)
		v->free((char*)v->elems + position * v->elemSize);
	memcpy((char*)v->elems + position * v->elemSize, elemAddr, v->elemSize);
}

void VectorInsert(vector *v, const void *elemAddr, int position) {
	assert(position >= 0 && position <= v->logLen);
	if(v->allocLen == v->logLen)
		grow(v);
	if(position != v->logLen)
		memmove((char*)v->elems + (position + 1) * v->elemSize, (char*)v->elems + position * v->elemSize, (v->logLen - position) * v->elemSize);
	memcpy((char*)v->elems + position * v->elemSize, elemAddr, v->elemSize);
	v->logLen++;
}

void VectorAppend(vector *v, const void *elemAddr) {
	VectorInsert(v, elemAddr, v->logLen);
}

void VectorDelete(vector *v, int position) {
	assert(position >= 0 && position < v->logLen);
	if(v->free != NULL)
		v->free((char*)v->elems + position * v->elemSize);
	if(position != v->logLen - 1)
		memmove((char*)v->elems + position * v->elemSize, (char*)v->elems + (position + 1) * v->elemSize, (v->logLen - position - 1) * v->elemSize);
	v->logLen--;
}

void VectorSort(vector *v, VectorCompareFunction compare) {
	assert(compare != NULL);
	qsort(v->elems, v->logLen, v->elemSize, compare);
}

void VectorMap(vector *v, VectorMapFunction mapFn, void *auxData) {
	assert(mapFn != NULL);
	for(int i = 0; i < v->logLen; i++) {
		mapFn((char*)v->elems + i * v->elemSize, auxData);
	}
}

static const int kNotFound = -1;
int VectorSearch(const vector *v, const void *key, VectorCompareFunction searchFn, int startIndex, bool isSorted) {
	assert(startIndex >= 0 && startIndex <= v->logLen);
	assert(key != NULL && searchFn != NULL);
	void* found = NULL;
	if(isSorted) 
		found = bsearch(key, (char*)v->elems + startIndex * v->elemSize, v->logLen - startIndex, v->elemSize, searchFn);
	else {
		size_t size = v->logLen - startIndex;
		found = lfind(key, (char*)v->elems + startIndex * v->elemSize, &size, v->elemSize, searchFn);
	}
	if(found != NULL)
		return ((char*)found - (char*)v->elems) / v->elemSize;
	return kNotFound;
} 
