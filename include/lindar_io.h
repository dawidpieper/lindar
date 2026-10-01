#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Byte-oriented input/output wrapper; close owned handles with IoFree. */
typedef struct LND_IO LND_IO;

/** Write size bytes from data for user.
 *
 * @param user Borrowed callback context.
 * @param data Input bytes borrowed for the operation.
 * @param size Buffer length in bytes.
 * @return Bytes written; a short result indicates incomplete output.
 */
typedef size_t (*LND_IO_WRITE_PROC)(void *user, const void *data, size_t size);

/** Seek user's output to absolute byte position.
 *
 * @param user Borrowed callback context.
 * @param position Absolute byte position.
 * @return LND_OK or a negative error.
 */
typedef int32_t (*LND_IO_SEEK_PROC)(void *user, uint64_t position);

/** Output callbacks copied into IO; only write is required. Close releases callback-owned resources. */
typedef struct LND_IO_OUTPUT_PROCS {
    LND_IO_WRITE_PROC write; /**< Required byte writer; returns bytes accepted. */
    LND_IO_SEEK_PROC seek; /**< Optional absolute-byte seek; returns LND_OK or a negative error. */
    /** Optional buffer flush for user.
     *
     * @param user Borrowed callback context.
     * @return LND_OK or a negative error.
     */
    int32_t (*flush)(void *user);
    /** Optional cleanup for user.
     *
     * @param user Borrowed callback context.
     * @return LND_OK or a negative error.
     */
    int32_t (*close)(void *user);
} LND_IO_OUTPUT_PROCS;

/** Random-access read callback sentinel for failure, distinct from EOF. */
#define LND_IO_READ_ERROR SIZE_MAX

/** Random-access read callbacks copied into IO; user storage stays valid until close. */
typedef struct LND_IO_INPUT_PROCS {
    /** Read up to size bytes at absolute offset into dst.
     *
     * @param user Borrowed callback context.
     * @param offset Absolute byte offset.
     * @param dst Writable buffer with room for size bytes.
     * @param size Buffer length in bytes.
     * @return Bytes, 0 at EOF, or LND_IO_READ_ERROR.
     */
    size_t (*read_at)(void *user, uint64_t offset, void *dst, size_t size);
    /** Optional cleanup for user.
     *
     * @param user Borrowed callback context.
     */
    void (*close)(void *user);
} LND_IO_INPUT_PROCS;

/** Streaming callbacks copied into IO; distinguish temporary waiting from end of input. */
typedef struct LND_IO_STREAM_INPUT_PROCS {
    /** Read up to size bytes into dst.
     *
     * @param user Borrowed callback context.
     * @param dst Writable buffer with room for size bytes.
     * @param size Buffer length in bytes.
     * @return Bytes, 0 while waiting, LND_READ_EOF at end, or a negative error.
     */
    int64_t (*read)(void *user, void *dst, size_t size);
    /** Optional absolute-byte seek for user.
     *
     * @param user Borrowed callback context.
     * @param position Absolute byte position.
     * @return LND_OK or a negative error.
     */
    int32_t (*seek)(void *user, uint64_t position);
    /** Optional cleanup for user.
     *
     * @param user Borrowed callback context.
     */
    void (*close)(void *user);
} LND_IO_STREAM_INPUT_PROCS;

/** Byte length is unknown; do not interpret it as an actual size. */
#define LND_IO_SIZE_UNKNOWN UINT64_MAX

/** Copy random-access procs with borrowed user and byte size, or SIZE_UNKNOWN.
 *
 * @param procs Callback table copied during creation.
 * @param user Borrowed callback context.
 * @param size Input length in bytes, or LND_IO_SIZE_UNKNOWN.
 * @return Owned IO or NULL; LND_IoFree calls close.
 */
LND_API LND_IO *LND_IoCreateInput(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size);

/** Wrap size bytes of data without copying; retain data until LND_IoFree.
 *
 * @param data Input bytes borrowed until LND_IoFree.
 * @param size Buffer length in bytes.
 * @return Owned IO or NULL.
 */
LND_API LND_IO *LND_IoOpenMemory(const void *data, size_t size);

/** Copy output procs and retain borrowed user until close.
 *
 * @param procs Callback table copied during creation.
 * @param user Borrowed callback context.
 * @return Owned IO or NULL; release with LND_IoFree.
 */
LND_API LND_IO *LND_IoCreateOutput(const LND_IO_OUTPUT_PROCS *procs, void *user);

/** Copy streaming procs with borrowed user and byte size, or SIZE_UNKNOWN.
 *
 * @param procs Callback table copied during creation.
 * @param user Borrowed callback context.
 * @param size Input length in bytes, or LND_IO_SIZE_UNKNOWN.
 * @return Owned IO or NULL; LND_IoFree calls close.
 */
LND_API LND_IO *LND_IoCreateStream(const LND_IO_STREAM_INPUT_PROCS *procs, void *user, uint64_t size);

/** Read up to size bytes into dst without filling short reads.
 *
 * @param io IO handle to operate on.
 * @param dst Writable buffer with room for size bytes.
 * @param size Buffer length in bytes.
 * @return Bytes read or a negative error; GetStatus distinguishes waiting from EOF.
 */
LND_API int64_t LND_IoReadSome(LND_IO *io, void *dst, size_t size);

/** Get io's READY, WAITING or EOF state.
 *
 * @param io IO handle to operate on.
 * @return Io's READY, WAITING or EOF state, or a negative error.
 */
LND_API int32_t LND_IoGetStatus(const LND_IO *io);

/** Flush io's buffered output without closing it.
 *
 * @param io IO handle to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_IoFlush(LND_IO *io);

/** Close io and release its wrapper.
 *
 * @param io IO handle to operate on.
 * @return LND_OK or a negative close error.
 */
LND_API int32_t LND_IoFree(LND_IO *io);

/** Read up to size bytes into dst, filling short random-access reads.
 *
 * @param io IO handle to operate on.
 * @param dst Writable buffer with room for size bytes.
 * @param size Buffer length in bytes.
 * @return Bytes read or a negative error; streaming waits do not busy-loop.
 */
LND_API int64_t LND_IoRead(LND_IO *io, void *dst, size_t size);

/** Write size bytes from data to io.
 *
 * @param io IO handle to operate on.
 * @param data Input bytes borrowed for the operation.
 * @param size Buffer length in bytes.
 * @return Bytes accepted, possibly short; inspect GetStatus on failure.
 */
LND_API size_t LND_IoWrite(LND_IO *io, const void *data, size_t size);

/** Seek io to absolute byte position.
 *
 * @param io IO handle to operate on.
 * @param position Absolute byte position.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_IoSeekBytes(LND_IO *io, uint64_t position);

/** Get io's current byte position.
 *
 * @param io IO handle to operate on.
 * @return Io's current byte position, or zero for NULL.
 */
LND_API uint64_t LND_IoGetPositionBytes(const LND_IO *io);

/** Get io's known byte size.
 *
 * @param io IO handle to operate on.
 * @return Io's known byte size, or LND_IO_SIZE_UNKNOWN when unknown.
 */
LND_API uint64_t LND_IoGetSizeBytes(const LND_IO *io);

/** Check whether the IO handle supports seeking.
 *
 * @param io IO handle to operate on.
 * @return True if io supports absolute byte seeking; false otherwise.
 */
LND_API bool LND_IoCanSeek(const LND_IO *io);

#ifdef __cplusplus
}
#endif
