#pragma once

#include "lindar_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Open UTF-8 path for reading.
 * Android asset: and content: URIs need application IO adapters, not filesystem paths.
 *
 * @param path UTF-8 file path.
 * @return An owned IO or NULL; release with LND_IoFree.
 */
LND_API LND_IO *LND_IoOpenFile(const char *path);

/** Create or truncate UTF-8 path for output.
 *
 * @param path UTF-8 file path.
 * @return An owned IO or NULL; release with LND_IoFree.
 */
LND_API LND_IO *LND_IoCreateFile(const char *path);

/** Create a temporary IO in directory, or the system default for NULL.
 *
 * @param directory UTF-8 directory path; NULL selects the system temporary directory.
 * @return Owned IO or NULL; LND_IoFree removes its file.
 */
LND_API LND_IO *LND_IoCreateTemporary(const char *directory);

#ifdef __cplusplus
}
#endif
