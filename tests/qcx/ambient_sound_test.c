#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#include "qwsvdef.h"

server_t sv;

static int bytes[4];
static float coordinates[3];
static unsigned int byte_count;
static unsigned int coordinate_count;
static char diagnostic[128];
static jmp_buf validation_error;
static qbool expect_error;

void SV_Error(char *error, ...)
{
	(void)error;
	assert(expect_error);
	longjmp(validation_error, 1);
}

void Con_Printf(char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(diagnostic, sizeof(diagnostic), format, args);
	va_end(args);
}

void MSG_WriteByte(sizebuf_t *buffer, int value)
{
	assert(buffer == &sv.signon);
	assert(byte_count < 4U);
	bytes[byte_count++] = value;
}

void MSG_WriteCoord(sizebuf_t *buffer, float value)
{
	assert(buffer == &sv.signon);
	assert(coordinate_count < 3U);
	coordinates[coordinate_count++] = value;
}

/* Keep both the QCX boundary and PF2 implementation real; dead stripping
 * discards unrelated services, as in sprint_test.c. Capture the message sink. */
#include "../../src/qcx/services_network.c"

static void reset_output(void)
{
	byte_count = 0U;
	coordinate_count = 0U;
	diagnostic[0] = '\0';
}

static void test_precached_sound(void)
{
	const float origin[3] = { 1.5f, -2.0f, 3.25f };
	/* An ABI byte span does not need its own trailing NUL. */
	const uint8_t sample[] = { 't', 'e', 's', 't' };
	sv.sound_precache[0] = "";
	sv.sound_precache[1] = "other";
	sv.sound_precache[2] = "test";
	sv.sound_precache[3] = NULL;
	reset_output();
	QCX_AmbientSound(NULL, origin, sample, sizeof(sample), 0.5f, 1.5f);
	assert(byte_count == 4U);
	assert(bytes[0] == svc_spawnstaticsound);
	assert(bytes[1] == 2);
	assert(bytes[2] == 127);
	assert(bytes[3] == 96);
	assert(coordinate_count == 3U);
	assert(coordinates[0] == 1.5f);
	assert(coordinates[1] == -2.0f);
	assert(coordinates[2] == 3.25f);
	assert(diagnostic[0] == '\0');
}

static void test_missing_precache(void)
{
	const float origin[3] = { 0.0f, 0.0f, 0.0f };
	reset_output();
	QCX_AmbientSound(NULL, origin, (const uint8_t *)"missing", 7U, 1.0f, 1.0f);
	assert(byte_count == 0U && coordinate_count == 0U);
	assert(strstr(diagnostic, "no precache:") != NULL);
	assert(strstr(diagnostic, "missing") != NULL);
}

static void test_invalid_sample(void)
{
	const float origin[3] = { 0.0f, 0.0f, 0.0f };
	reset_output();
	expect_error = true;
	if (setjmp(validation_error) == 0) {
		QCX_AmbientSound(NULL, origin, (const uint8_t *)"a\0b", 3U, 1.0f, 1.0f);
		assert(!"embedded NUL accepted");
	}
	if (setjmp(validation_error) == 0) {
		QCX_AmbientSound(NULL, origin, NULL, 1U, 1.0f, 1.0f);
		assert(!"NULL nonempty sample accepted");
	}
	expect_error = false;
	assert(byte_count == 0U && coordinate_count == 0U);
	assert(diagnostic[0] == '\0');
	QCX_AmbientSound(NULL, NULL, (const uint8_t *)"test", 4U, 1.0f, 1.0f);
	assert(byte_count == 0U && coordinate_count == 0U);
}

int main(void)
{
	test_precached_sound();
	test_missing_precache();
	test_invalid_sample();
	return 0;
}
