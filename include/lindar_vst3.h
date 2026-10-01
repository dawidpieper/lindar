#pragma once
#include "lindar_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Copied VST3 audio class identity; id is binary, not a string. */
typedef struct LND_VST3_CLASS {
    uint8_t id[16]; /**< 16-byte binary VST3 class identifier. */
    char name[128]; /**< NUL-terminated plugin class name. */
    char vendor[128]; /**< NUL-terminated vendor name. */
} LND_VST3_CLASS;

/** VST3 settings copied at creation. Control calls, including Get, must use the creating thread; zero
 * class_id selects the first audio class.
 */
typedef struct LND_VST3_OPTIONS {
    const char *path; /**< UTF-8 path to a VST3 module/bundle. */
    uint8_t class_id[16]; /**< Binary class identifier; all zeros selects the first audio class. */
    uint32_t channels; /**< Processing channel count; zero uses the default. */
    uint32_t sample_rate_hz; /**< Processing rate in Hz; zero uses the default. */
    uint32_t block_frames; /**< Maximum processing block length; zero uses the default. */
    uint32_t tail_limit_ms; /**< Maximum tail duration in milliseconds; zero uses the default. */
    bool offline; /**< Request offline rather than realtime plugin processing. */
} LND_VST3_OPTIONS;

/** Copied VST3 processor snapshot, including latency, tail and editor state. */
typedef struct LND_VST3_INFO {
    LND_VST3_CLASS plugin; /**< Copied selected class identity. */
    uint32_t parameter_count; /**< Number of exposed parameters. */
    uint32_t program_list_count; /**< Number of program lists. */
    uint32_t latency_frames; /**< Plugin-reported processing latency. */
    /** Plugin-reported tail length; UINT32_MAX denotes an unlimited tail, separately capped by options. */
    uint32_t tail_frames;
    bool bypass; /**< Host bypass is enabled. */
    bool editor_open; /**< Plugin editor is attached to a host window. */
    bool dirty; /**< Plugin reports state requiring a save/update. */
} LND_VST3_INFO;
enum {
    LND_VST3_AUTOMATABLE = 1, /**< Parameter supports host automation. */
    LND_VST3_READ_ONLY = 2, /**< Parameter cannot be set by the host. */
    LND_VST3_PROGRAM = 4, /**< Parameter selects a program/preset. */
    LND_VST3_BYPASS = 8 /**< Parameter is the plugin's bypass control. */
};
enum {
    LND_VST3_HWND, /**< Editor parent is a Windows HWND. */
    LND_VST3_NSVIEW, /**< Editor parent is a macOS NSView. */
    LND_VST3_X11 /**< Editor parent is an X11 window handle. */
};

/** Copied parameter metadata; values are normalised to [0, 1], independently of displayed units. */
typedef struct LND_VST3_PARAMETER {
    uint32_t id; /**< Plugin-defined parameter ID, not enumeration index. */
    int32_t unit_id; /**< Plugin-defined unit/group ID. */
    int32_t steps; /**< Discrete step count; zero denotes a continuous parameter. */
    uint32_t flags; /**< LND_VST3_AUTOMATABLE, READ_ONLY, PROGRAM and BYPASS bits. */
    double default_value; /**< Normalised default in [0, 1]. */
    char name[512]; /**< NUL-terminated parameter display name. */
    char units[512]; /**< NUL-terminated display units. */
} LND_VST3_PARAMETER;

/** Copied plugin program list; its ID may differ from its enumeration index. */
typedef struct LND_VST3_PROGRAM_LIST {
    int32_t id; /**< Plugin-defined program list ID. */
    uint32_t count; /**< Number of programs in this list. */
    char name[512]; /**< NUL-terminated list display name. */
} LND_VST3_PROGRAM_LIST;

/** Host musical timing copied to the plugin's processing context. */
typedef struct LND_VST3_TRANSPORT {
    int64_t position_frames; /**< Host timeline position at the processing sample rate. */
    double tempo_bpm; /**< Musical tempo in beats per minute. */
    int32_t numerator; /**< Time signature numerator. */
    int32_t denominator; /**< Time signature denominator. */
    bool playing; /**< Host transport is running. */
} LND_VST3_TRANSPORT;

/** Ask user to resize the editor host to width by height pixels.
 *
 * @param user Borrowed callback context.
 * @param width Editor width in pixels.
 * @param height Editor height in pixels.
 * @return LND_OK if accepted or a negative error.
 */
typedef int32_t (*LND_VST3_RESIZE_PROC)(void *user, uint32_t width, uint32_t height);

/** Load/discover the VST3 module at path and write its audio class count.
 *
 * @param path UTF-8 VST3 module path; scanning may load the plugin.
 * @param count Receives the number of entries.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_Vst3ModuleScanClassCount(const char *path, uint32_t *count);

/** Load/discover path and copy its audio class at zero-based index into info.
 *
 * @param path UTF-8 VST3 module path; scanning may load the plugin.
 * @param index Zero-based entry index.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_Vst3ModuleScanClass(const char *path, uint32_t index, LND_VST3_CLASS *info);

/** Create a VST3 processor from options, loading its module and class.
 * Processes main effect buses in planar F32. Auxiliary buses and instrument events are not activated;
 * latency between parallel branches is not compensated.
 *
 * @param options Required settings, borrowed during the call.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateVst3(const LND_VST3_OPTIONS *options);

/** Copy node's VST3 class, latency and state into info.
 *
 * @param node Graph node to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetVst3Info(const LND_NODE *node, LND_VST3_INFO *info);

/** Copy node's VST3 parameter at zero-based index into info.
 *
 * @param node Graph node to operate on.
 * @param index Zero-based entry index.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error; its ID may differ from index.
 */
LND_API int32_t LND_NodeGetVst3Parameter(const LND_NODE *node, uint32_t index, LND_VST3_PARAMETER *info);

/** Write node's normalised VST3 parameter id to value.
 *
 * @param node Graph node to operate on.
 * @param id Plugin parameter identifier, distinct from its index.
 * @param value Receives the normalised parameter value in [0, 1].
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetVst3ParameterValue(const LND_NODE *node, uint32_t id, double *value);

/** Set node's VST3 parameter id to normalised value in [0, 1].
 *
 * @param node Graph node to operate on.
 * @param id Plugin parameter identifier, distinct from its index.
 * @param value Normalised parameter value in [0, 1].
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetVst3ParameterValue(LND_NODE *node, uint32_t id, double value);

/** Format node's normalised parameter value for id into text of capacity bytes.
 *
 * @param node Graph node to operate on.
 * @param id Plugin parameter identifier, distinct from its index.
 * @param value Normalised parameter value in [0, 1].
 * @param text Output buffer for null-terminated UTF-8 text.
 * @param capacity Writable buffer capacity in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeFormatVst3ParameterValue(const LND_NODE *node, uint32_t id, double value, char *text, size_t capacity);

/** Parse text for node's parameter id into normalised value.
 *
 * @param node Graph node to operate on.
 * @param id Plugin parameter identifier, distinct from its index.
 * @param text UTF-8 text to parse.
 * @param value Receives the normalised parameter value in [0, 1].
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeParseVst3ParameterValue(const LND_NODE *node, uint32_t id, const char *text, double *value);

/** Enable or disable node's VST3 bypass.
 *
 * @param node Graph node to operate on.
 * @param bypass True to bypass processing; false to apply it.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetVst3Bypass(LND_NODE *node, bool bypass);

/** Copy node's VST3 program list at zero-based index into info.
 *
 * @param node Graph node to operate on.
 * @param index Zero-based entry index.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetVst3ProgramList(const LND_NODE *node, uint32_t index, LND_VST3_PROGRAM_LIST *info);

/** Copy program index from node's list_id into name of capacity bytes.
 *
 * @param node Graph node to operate on.
 * @param list_id Plugin program-list identifier, distinct from its index.
 * @param index Zero-based entry index.
 * @param name Output buffer for a null-terminated UTF-8 program name.
 * @param capacity Writable buffer capacity in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetVst3ProgramName(const LND_NODE *node, int32_t list_id, uint32_t index, char *name, size_t capacity);

/** Select zero-based program index in node's list_id.
 *
 * @param node Graph node to operate on.
 * @param list_id Plugin program-list identifier, distinct from its index.
 * @param index Zero-based entry index.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetVst3Program(LND_NODE *node, int32_t list_id, uint32_t index);

/** Write node state to data within capacity and report required bytes; NULL data with zero capacity
 * queries size. May apply pending changes.
 *
 * @param node Graph node to operate on.
 * @param data Output buffer; NULL with zero capacity queries the required size.
 * @param capacity Writable buffer capacity in bytes.
 * @param bytes Receives the required state size in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSaveVst3State(LND_NODE *node, void *data, size_t capacity, size_t *bytes);

/** Restore node's state from bytes of data borrowed during the call.
 *
 * @param node Graph node to operate on.
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeLoadVst3State(LND_NODE *node, const void *data, size_t bytes);

/** Copy transport timing into node for subsequent plugin processing.
 *
 * @param node Graph node to operate on.
 * @param transport Transport timing to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetVst3Transport(LND_NODE *node, const LND_VST3_TRANSPORT *transport);

/** Reset node's processing history and end state.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetVst3(LND_NODE *node);

/** Dispatch node's pending plugin/controller work on the host thread.
 * Call periodically while hosting a plugin; the application also supplies the native editor event loop.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeDispatchVst3(LND_NODE *node);

/** Attach node's editor to parent of platform type, using resize and borrowed user.
 * The application must run the native event loop and call LND_NodeDispatchVst3 periodically.
 *
 * @param node Graph node to operate on.
 * @param parent Borrowed native parent window for the selected platform.
 * @param platform LND_VST3_HWND, LND_VST3_NSVIEW or LND_VST3_X11.
 * @param resize Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeOpenVst3Editor(LND_NODE *node, void *parent, int32_t platform, LND_VST3_RESIZE_PROC resize, void *user);

/** Detach and close node's editor.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeCloseVst3Editor(LND_NODE *node);

/** Write node's editor dimensions in pixels to width and height.
 *
 * @param node Graph node to operate on.
 * @param width Receives the editor width in pixels.
 * @param height Receives the editor height in pixels.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetVst3EditorSize(const LND_NODE *node, uint32_t *width, uint32_t *height);

/** Request node's editor size of width by height pixels.
 *
 * @param node Graph node to operate on.
 * @param width Editor width in pixels.
 * @param height Editor height in pixels.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResizeVst3Editor(LND_NODE *node, uint32_t width, uint32_t height);
#ifdef __cplusplus
}
#endif
