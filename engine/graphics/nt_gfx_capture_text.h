#ifndef NT_GFX_CAPTURE_TEXT_H
#define NT_GFX_CAPTURE_TEXT_H

#include "graphics/nt_gfx.h"

#include <stddef.h>

/* Name of an issued-call detail, e.g. "glBindTexture"; NULL outside the table. */
const char *nt_gfx_gl_call_name(uint32_t call);
/* Names the capture text uses ("DRAW", "CACHE", "TEXTURE", "DEFINITION"); NULL outside their tables. */
const char *nt_gfx_operation_name(uint32_t operation);
const char *nt_gfx_result_name(uint32_t result);
const char *nt_gfx_object_kind_name(uint32_t kind);
const char *nt_gfx_event_kind_name(uint32_t kind);

/* How a field is stored in the record. */
typedef enum {
    NT_GFX_FIELD_U32,
    NT_GFX_FIELD_I32,
    NT_GFX_FIELD_U64,
    NT_GFX_FIELD_F32,
    NT_GFX_FIELD_BOOL,
    NT_GFX_FIELD_U32S, /* a list: trailing zeros trimmed, except the elements an issued call counts */
    NT_GFX_FIELD_F32S,
    NT_GFX_FIELD_STR, /* nt_gfx_capture_string_t */
} nt_gfx_field_type_t;

/* What a field's value is; sub narrows it. */
typedef enum {
    NT_GFX_MEANING_NUMBER, /* a count, index, size in pixels or elements, float or string */
    NT_GFX_MEANING_BOOL,
    NT_GFX_MEANING_BYTES,   /* a byte size or offset */
    NT_GFX_MEANING_HASH,    /* a name hash */
    NT_GFX_MEANING_HANDLE,  /* a frontend handle; sub: nt_gfx_object_kind_t */
    NT_GFX_MEANING_SLOT,    /* a backend slot; sub: nt_gfx_object_kind_t */
    NT_GFX_MEANING_GL_NAME, /* a raw GL name of the record's context segment; sub: nt_gfx_gl_object_t */
    NT_GFX_MEANING_GL_ENUM, /* sub: nt_gfx_capture_domain_t */
    NT_GFX_MEANING_ENUM,    /* an engine enum value; sub: nt_gfx_capture_domain_t */
    NT_GFX_MEANING_FLAGS,   /* a bit set; sub: nt_gfx_capture_domain_t */
    NT_GFX_MEANING_GL_ARGS, /* an issued call's arguments or a cache skip's key: meaning per call (detail) */
} nt_gfx_field_meaning_t;

/* GL object namespaces: a GL name is unique within one kind and one context segment. */
typedef enum {
    NT_GFX_GL_OBJECT_NONE,
    NT_GFX_GL_OBJECT_PROGRAM,
    NT_GFX_GL_OBJECT_SHADER,
    NT_GFX_GL_OBJECT_BUFFER,
    NT_GFX_GL_OBJECT_TEXTURE,
    NT_GFX_GL_OBJECT_SAMPLER,
    NT_GFX_GL_OBJECT_VERTEX_ARRAY,
    NT_GFX_GL_OBJECT_FRAMEBUFFER,
    NT_GFX_GL_OBJECT_QUERY,
} nt_gfx_gl_object_t;

/* Value sets of ENUM, GL_ENUM and FLAGS fields. */
typedef enum {
    NT_GFX_DOMAIN_NONE,
    NT_GFX_DOMAIN_SHADER_TYPE,    /* nt_shader_type_t */
    NT_GFX_DOMAIN_BUFFER_TYPE,    /* nt_buffer_type_t */
    NT_GFX_DOMAIN_BUFFER_USAGE,   /* nt_buffer_usage_t */
    NT_GFX_DOMAIN_INDEX_TYPE,     /* NT_INDEX_* */
    NT_GFX_DOMAIN_TEXTURE_FORMAT, /* nt_texture_format_t */
    NT_GFX_DOMAIN_TEXTURE_FILTER, /* nt_texture_filter_t */
    NT_GFX_DOMAIN_TEXTURE_WRAP,   /* nt_texture_wrap_t */
    NT_GFX_DOMAIN_COMPARE_FUNC,   /* nt_compare_func_t */
    NT_GFX_DOMAIN_VERTEX_TYPE,    /* nt_vertex_type_t */
    NT_GFX_DOMAIN_CULL_MODE,      /* 0 none, 1 back, 2 front */
    NT_GFX_DOMAIN_SAMPLER_CLASS,  /* 0 float, 1 shadow, 2 uint */
    NT_GFX_DOMAIN_GL_FUNC,        /* GL_NEVER..GL_ALWAYS */
    NT_GFX_DOMAIN_GL_BLEND_FACTOR,
    NT_GFX_DOMAIN_GL_BLEND_EQUATION,
    NT_GFX_DOMAIN_GL_TEXTURE_UNIT, /* GL_TEXTURE0 + n */
    NT_GFX_DOMAIN_LOCATIONS,       /* FLAGS: bit n is attribute location n */
    NT_GFX_DOMAIN_COUNT,
} nt_gfx_capture_domain_t;

typedef struct {
    const char *name; /* the capture text key */
    uint16_t offset;  /* into the record */
    uint8_t type;     /* nt_gfx_field_type_t */
    uint8_t size;     /* list capacity; 1 for scalars */
    uint8_t meaning;  /* nt_gfx_field_meaning_t */
    uint8_t sub;      /* object kind, GL object kind or domain, by meaning */
} nt_gfx_capture_field_t;

/* The data fields of a gfx record (kind OP..ARGUMENT) in text order, chosen by its kind, operation, object
 * kind and detail: the one description the text, the parser and readers share. Static storage. */
uint32_t nt_gfx_capture_fields(const nt_gfx_event_t *e, const nt_gfx_capture_field_t **out);

/* Lossless named text of a capture (grammar in the render architecture spec, "Capture text").
 * Returns the bytes the text needs, without the terminator; writes at most cap - 1 of them plus a
 * terminator when cap > 0. Format with cap 0 first to size the buffer. Requires LC_NUMERIC to be the C locale. */
size_t nt_gfx_capture_format(const nt_gfx_capture_view_t *view, char *out, size_t cap);

typedef struct {
    nt_gfx_capture_view_t view; /* events NULL: the header was rejected; else its count records (a valid prefix when error is set) */
    size_t next;                /* offset of the next document in text, or len */
    size_t error_at;            /* byte offset of the error in text */
    const char *error;          /* static message; NULL when the document parsed whole */
} nt_gfx_capture_parsed_t;

/* Parses the document at the start of text (len bytes, not terminated) into events[0..cap) and its strings into
 * strings[0..string_cap), both non-NULL, string_cap > 0. The text is untrusted: nothing in it asserts. A header
 * error rejects the document; a record error, an unknown name included, keeps the records before it. A file holds
 * documents back to back: parse again at next. Every record is one line, so the file's line count bounds the
 * records of all its documents, and its length bounds their string bytes (plus one for an empty file). Requires LC_NUMERIC "C". */
nt_gfx_capture_parsed_t nt_gfx_capture_parse(const char *text, size_t len, nt_gfx_event_t *events, uint32_t cap, char *strings, uint32_t string_cap);

#endif /* NT_GFX_CAPTURE_TEXT_H */
