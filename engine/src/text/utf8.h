/*
 * UTF-8 decoding (ADR 0021, E1): the bytes of a string to the characters
 * (codepoints) they encode. Pure.
 *
 * UTF-8 writes a codepoint in 1 to 4 bytes. The first (the lead) says how
 * many follow; each following one (a continuation) starts with the bits 10:
 *
 *   U+0000..U+007F      0xxxxxxx
 *   U+0080..U+07FF      110xxxxx 10xxxxxx
 *   U+0800..U+FFFF      1110xxxx 10xxxxxx 10xxxxxx
 *   U+10000..U+10FFFF   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 */
#ifndef RB_UTF8_H
#define RB_UTF8_H

#include <reborn/defines.h>

/* What invalid input decodes to: U+FFFD, the "replacement character". */
#define RB_UTF8_REPLACEMENT 0xFFFDu

/*
 * The codepoint starting at *cursor, which then moves past it. *cursor must
 * not point at the terminating '\0': the caller stops there. Never reads past
 * the '\0', which is never a continuation byte.
 *
 * Invalid input gives RB_UTF8_REPLACEMENT and moves past its maximal invalid
 * subpart (the lead and the continuation bytes that were valid so far), as
 * Unicode recommends: one replacement per broken character. Invalid are: a
 * continuation byte without a lead, a truncated sequence, an overlong form
 * (a codepoint written with more bytes than needed, e.g. '/' as C0 AF), a
 * UTF-16 surrogate (U+D800..U+DFFF) and anything beyond U+10FFFF.
 */
u32 rb_utf8_next(const char **cursor);

#endif /* RB_UTF8_H */
