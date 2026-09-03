#ifndef MVDSV_QCX_STRINGS_H
#define MVDSV_QCX_STRINGS_H

#include <stdint.h>

const char *QCX_BorrowLegacyString(int32_t token);
void QCX_ClearLegacyStringBorrows(void);

#endif
