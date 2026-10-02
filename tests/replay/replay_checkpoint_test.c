#include "qcx/replay_checkpoint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int Kind(unsigned scope, const char *name)
{
	(void)scope;
	if (!strcmp(name, "health")) {
		return QCX_REPLAY_F32;
	}
	if (!strcmp(name, "origin")) {
		return QCX_REPLAY_VEC3;
	}
	if (!strcmp(name, "think")) {
		return QCX_REPLAY_SYMBOL;
	}
	if (!strcmp(name, "name")) {
		return QCX_REPLAY_STRING;
	}
	return 0;
}

int main(void)
{
	/* Named logical values, not a raw struct with pointers/padding. */
	uint8_t logical[] = {'Q','C','G','D',1,0,0,0,
		0,0,0,0, 1,0,0,0, 7,0,0,0, 4,0,0,0,
		6,0,0,0,'h','e','a','l','t','h', 0,0,200,66,
		6,0,0,0,'o','r','i','g','i','n', 0,0,128,63,0,0,0,0,0,0,0,0,
		5,0,0,0,'t','h','i','n','k', 1,0,0,0,4,0,0,0,'i','d','l','e',
		4,0,0,0,'n','a','m','e', 3,0,0,0,'B','o','b'};
	uint8_t other[sizeof(logical)];
	memcpy(other, logical, sizeof(other));
	uint64_t a, b;
	assert(QCX_ReplayLogicalHash(logical, sizeof(logical), Kind, &a));
	assert(QCX_ReplayLogicalHash(other, sizeof(other), Kind, &b) && a == b);
	other[52] ^= 1; /* Position changes. */
	assert(QCX_ReplayLogicalHash(other, sizeof(other), Kind, &b) && a != b);
	memcpy(other, logical, sizeof(other));
	other[36] ^= 1; /* Health changes. */
	assert(QCX_ReplayLogicalHash(other, sizeof(other), Kind, &b) && a != b);
	memcpy(other, logical, sizeof(other));
	other[77] = 'w'; /* Callback symbolic name, never its code address. */
	assert(QCX_ReplayLogicalHash(other, sizeof(other), Kind, &b) && a != b);
	assert(!QCX_ReplayLogicalHash(logical, sizeof(logical)-1, Kind, &b));
	uint64_t counts[] = {100, 50};
	a = QCX_ReplayHashBytes(1, counts, sizeof(counts));
	++counts[0];
	assert(a != QCX_ReplayHashBytes(1, counts, sizeof(counts)));
	--counts[0];
	++counts[1];
	assert(a != QCX_ReplayHashBytes(1, counts, sizeof(counts)));
	puts("checkpoint: decoded logical values/names independent of addresses");
}
