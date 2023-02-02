/* Copyright © 2012 Brandon L Black <blblack@gmail.com> and Jay Reitz <jreitz@gmail.com>
 *
 * This file is part of gdnsd.
 *
 * gdnsd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gdnsd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gdnsd.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GDNSD_DNAME_H
#define GDNSD_DNAME_H

#include <gdnsd/compiler.h>
#include <gdnsd/alloc.h>
#include <gdnsd/log.h>

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/*
 * Notes about Domain Names in general:
 * All domainnames are composed from labels.
 *
 * In the wire format:
 *  (compression is not considered here)
 *  The maximum length of a label is 63 bytes.
 *  Each label is prefixed by a length byte with a value
 *   of 1-63 inclusive.
 *  The domainname as a whole is terminated by a NUL byte (which is
 *   technically a label of length zero).
 *  The domainname for the root of the DNS is simply a NUL byte
 *   with no preceding labels.
 *  The maximum length of a domainname is 255 bytes in wire-encoded
 *   form, including the final NUL.
 *
 * In ASCII format without considering escaping:
 *  The maximum label length is still 63 bytes
 *  Labels have no explicit length prefix, and instead are
 *   separated by a single period.
 *  A final trailing dot is often used to signify that the
 *   name is fully qualified (as opposed to a name which is
 *   intended to have the local domain appended automatically).
 *  In some cases the final trailing dot is basically ignored
 *   and names are considered fully-qualified regardless.
 *  The maximum overall length is ultimately determined by the
 *   wire format, not this format, but works out to 254 in practice:
 *   63 . 63 . 63 . 63   -> illegal (257 in wire form)
 *   63 . 63 . 63 . 62   -> illegal (256 in wire form)
 *   63 . 63 . 63 . 61   -> 253 (255 in wire form)
 *   63 . 63 . 63 . 61 . -> 254 (255 in wire form)
 *    1 .  1 .. 1 .  1   -> 253 (255 in wire form) (127 1-byte labels)
 *    1 .  1 .. 1 .  1 . -> 254 (255 in wire form) (127 1-byte labels)
 *
 * Escaping in the ASCII format:
 *  RFC1035 specifies an escaping method for zonefiles in general, which
 *   allows embedding strange characters (including dots) within labels.
 *  The escapes come in two forms: "\X" and "\DDD".  In the \X case, the
 *   X character is taken to have no special meaning (e.g. "\." is a dot
 *   as part of label data, instead of as a label separator).  In the
 *   \DDD case, DDD are interpreted as a 3-digit base 10 number from the
 *   range 0-255, and the escape represents a byte of that same value
 *   (again with no special meaning).
 *  The maximum width of an escape (representing a single "real" byte)
 *   is 4 (the "\DDD" form).
 *  The maximum width of an ASCII label in escaped form is 252 (63 * 4).
 *  When considering whole domainnames, one would multiply out the label
 *   bytes, but not the separator dots.  Therefore those names which pack
 *   the maximal overall length into the minimum set of labels see the
 *   most escape-expansion.  Reusing the maximal pair above:
 *   63 . 63 . 63 . 61   -> 253 (255 in wire form)
 *     ((63 + 63 + 63 + 61) * 4) + 3 = 1003
 *   63 . 63 . 63 . 61 . -> 254 (255 in wire form)
 *     ((63 + 63 + 63 + 61) * 4) + 4 = 1004
 *
 * Keep in mind these are data lengths only.  If your ASCII strings are
 *  NUL-terminated, that's an extra byte.
 *
 * So to recap:
 *  ASCII with escapes:
 *   Max label storage (NUL-terminated): 252 (253)
 *   Max name storage (NUL-terminated): 1004 (1005)
 *  ASCII without escapes:
 *   Max label storage (NUL-terminated): 63 (64)
 *   Max name storage (NUL-terminated): 254 (255)
 *  Wire:
 *   Max label len: 63
 *   Max overall len: 255
 *
 * BUT:
 *  The fact that an ASCII representation of a full name
 *   fits within 1004 (1005 for ASCIIZ) bytes does *not*
 *   imply that it is legal (that it encodes to a wire
 *   format name of legal length).  This is obvious because
 *   the 1004-byte ASCII name might contain no escapes.
 * For that matter, even if escapes are not allowed and the
 *   individual labels are checked for the 63 limit, fitting
 *   within 254 (255 for ASCIIZ) doesn't imply legality
 *   either.  Example:
 *   63 . 63 . 63 . 62 -> 254 (256, illegal, in wire form)
 *
 */

/*
 * Notes on the "dname" format:
 *
 * "dname"-formatted data is the common representation of
 *  a domainname throughout the gdnsd source code.
 *
 * The first byte is always overall len of the rest of the string.
 * The rest of the string is a wire-format domainname without
 *  compression pointers.
 * A full dname should be \x00 terminated as it would be in wire form.
 * Partial dnames (in the process of being constructed, e.g. a dname
 *  for a relative name that had no trailing dot in ASCII form, before
 *  the origin is appended) are signalled by replacing the trailing \0
 *  with a trailing \xff.  These are not valid for passing as "dname"
 *  data to the core code, but are useful during intermediate stages
 *  of construction.
 * The maximum required storage size is 256 bytes for a maximally long
 *  name.  e.g. ("uint8_t dname[256];" or "uint8_t* dname = xmalloc(256);").
 * As a general rule, all generic dname storage a plugin creates should
 *  be allocated for the full 256 bytes if you are going to construct
 *  them using the helper functions here.  However, keep in mind that
 *  constant dname arguments passed down from the core code could be
 *  (and often are) allocated to their exact size. (e.g. 2 bytes for the
 *  first example below).
 * Examples:
 *  "." -> \x01\x00
 *  "com." -> \x05\x03com\x00
 *  "a.com." -> \x07\x01a\x03com\x00
 *  "www" -> \x05\x03www\xff (not yet fully qualified)
 */

// Status of a dname after some of the operations below.
//  DNAME_VALID means the operation succeeded and the result is fully qualified (ends in \0).
//  DNAME_PARTIAL means the operation succeeded, but the result is not yet fully qualified (ends in \xff).
//  DNAME_INVALID means the operation failed (invalid input), output contents are undefined.
//  "PARTIAL" might be valid for your code for intermediate purposes, but is not considered
//    a valid dname for normal use with the core code.
typedef enum {
    DNAME_VALID = 0,
    DNAME_PARTIAL,
    DNAME_INVALID,
} gdnsd_dname_status_t;

// Unescape the string "in" into the storage at "out", using
//  DNS zonefile escaping rules.
// Return value is output len, which will be <= input len
// input len must be >0
// Note: you are responsible for allocating output storage of at least "len" at "out".
// This function cannot fail given correct inputs.
// If it is given invalid/dangling escapes, it will return
//   the error indication by returning a zero length
// Note the use of "restrict": out and in cannot overlap
F_NONNULL
unsigned gdnsd_dns_unescape(char* restrict out, const char* restrict in, const unsigned len);

// Parse a uint8_t* human-readable string into a dname.  len is the length of the input.
// Escapes will be unescaped and any uppercase ASCII characters will be normalized to lowercase.
// If there is no trailing dot, the result will be left in the PARTIAL state.
// It is assumed you have allocated a full 256 bytes at "dname" for storage of the
//  result.  Any less could result in random crashy bugs.
// Note the use of "restrict": dname and instr cannot overlap
F_NONNULL
gdnsd_dname_status_t gdnsd_dname_from_string(uint8_t* restrict dname, const char* restrict instr, const unsigned len);

// Turns a dname back into a printable string
// "dname" must be DNAME_VALID or DNAME_PARTIAL
// "str" must be preallocated to at least 1024 bytes,
// retval is the length of the string stored to "str", including the terminal NUL
F_NONNULL
unsigned gdnsd_dname_to_string(const uint8_t* restrict dname, char* restrict str);

// Concatenate one dname onto the end of another.  Invalid inputs are not allowed.
//  You are responsible for ensuring this is the case before calling, or crashes
//  could ensue.  As before, "dn1" must have a full 256 bytes of storage.
// If the concantenation would result in an illegally-long dname, the return
//  value will be INVALID.  Otherwise the return value should mirror
//  the status of the "origin" argument (VALID vs PARTIAL).
// Note the use of "restrict": dn1 and dn2 cannot overlap
F_NONNULL
gdnsd_dname_status_t gdnsd_dname_cat(uint8_t* restrict dn1, const uint8_t* restrict dn2);

// Check the status/validity of a dname.  The dname will be carefully parsed,
//  and should handle any random input (although most random inputs will give
//  DNAME_INVALID) assuming dname has at least a 256 byte memory allocation.
// If dname is allocated to less than 256 bytes, then crashes are possible with
//  input that looks like a name longer than the allocation.
F_NONNULL F_PURE
gdnsd_dname_status_t gdnsd_dname_status(const uint8_t* dname);

// As above, but takes a second argument specifying the known length of the
// memory allocation at "dname", and pre-checks for running off the end of the
// buffer, returning DNAME_INVALID if so.  Still requires that "dname" is a
// non-NULL pointer, but a buflen of zero is legal (which will always return
// DNAME_INVALID).
F_NONNULL F_PURE F_UNUSED
static gdnsd_dname_status_t gdnsd_dname_status_buflen(const uint8_t* dname, size_t buflen)
{
    if (!buflen || ((*dname) + 1U) > buflen)
        return DNAME_INVALID;
    return gdnsd_dname_status(dname);
}

// Terminate a DNAME_PARTIAL name, converting it to DNAME_VALID.  Idempotent
//  and thus harmless on names which are already DNAME_VALID.  Invalid input
//  could cause crashes.
F_NONNULL F_UNUSED
static void gdnsd_dname_terminate(uint8_t* dname)
{
    const unsigned len = *dname;
    gdnsd_assert(len > 0U);
    gdnsd_assert(len < 256U);
    dname[len] = 0;
}

// Check the status of a known-good dname.  It is assumed that the dname was
//  constructed correctly by other code, and merely differentiates quickly
//  between the partial and fully-qualfied cases.  If the input is invalid,
//  it could crash.
F_NONNULL F_UNUSED
static bool gdnsd_dname_is_partial(const uint8_t* dname)
{
    gdnsd_assert(*dname);
    return dname[*dname] == 255;
}

// Trim a dname's storage to the minimum required size.  Assumes storage was
//  originally allocated with xmalloc() or equivalent.  Note that after trimming
//  you cannot perform operations like dname_cat() on this directly.
F_WUNUSED F_NONNULL F_UNUSED F_RETNN
static uint8_t* gdnsd_dname_trim(uint8_t* dname)
{
    gdnsd_assert(*dname);
    return xrealloc(dname, *dname + 1U);
}

// Copy "source" dname to the storage at dest, which must be allocated
//  large enough (256 max).
F_NONNULL F_UNUSED
static void gdnsd_dname_copy(uint8_t* dest, const uint8_t* source)
{
    const unsigned len = *source;
    gdnsd_assert(len > 0U);
    gdnsd_assert(len < 256U);
    memcpy(dest, source, len + 1U);
}

// Allocate new storage (via xmalloc()), clone the input dname into it, and return.
// Allocates exactly enough storage, so you can't edit it afterwards.
// Also works for labels, hence the define:
F_MALLOC F_NONNULL F_UNUSED F_RETNN
static uint8_t* gdnsd_dname_dup(const uint8_t* dname)
{
    const size_t sz = *dname + 1U;
    uint8_t* out = xmalloc(sz);
    memcpy(out, dname, sz);
    return out;
}
#define gdnsd_label_dup gdnsd_dname_dup

// Returns memcmp()-like return values, primary sort is
//  on overall length (<0 means dn1 is shorter than dn2).
// dn1 and dn2 must be DNAME_VALID or DNAME_PARTIAL
// When lengths are equal the return values will make
//  for a stable sort assuming no duplicates, but are
//  somewhat meaningless.
// Equality (0) will only be returned on a complete match,
//  including the difference between VALID and PARTIAL.
F_NONNULL F_PURE F_UNUSED
static int gdnsd_dname_cmp(const uint8_t* dn1, const uint8_t* dn2)
{
    gdnsd_assert(gdnsd_dname_status(dn1) != DNAME_INVALID);
    gdnsd_assert(gdnsd_dname_status(dn2) != DNAME_INVALID);
    const uint8_t len1 = *dn1++;
    const uint8_t len2 = *dn2++;
    int rv = len1 - len2;
    if (!rv)
        rv = memcmp(dn1, dn2, len1);
    return rv;
}

// As above but for labels (no dname_status() assertion)
F_NONNULL F_PURE F_UNUSED
static int gdnsd_label_cmp(const uint8_t* label1, const uint8_t* label2)
{
    const uint8_t len1 = *label1++;
    const uint8_t len2 = *label2++;
    int rv = len1 - len2;
    if (!rv)
        rv = memcmp(label1, label2, len1);
    return rv;
}

// returns true if dname is within zone
// returns true if they are identical
// dname and zone must be DNAME_VALID (fully-qualified).
F_NONNULL F_PURE F_UNUSED
static bool gdnsd_dname_isinzone(const uint8_t* parent, const uint8_t* child)
{
    gdnsd_assert(gdnsd_dname_status(parent) == DNAME_VALID);
    gdnsd_assert(gdnsd_dname_status(child) == DNAME_VALID);

    // Grab the overall lens and advance the pointer to the start of the first
    // label in both cases:
    const unsigned plen = *parent++;
    const unsigned clen = *child++;
    gdnsd_assert(plen); // implied by DNAME_VALID check above
    gdnsd_assert(clen); // implied by DNAME_VALID check above

    // if child shorter than parent, cannot be in zone:
    if (plen > clen)
        return false;

    // Quick memcmp check: if the same bytes as "parent" exist at the end of
    // "child", this *might* be in-zone, otherwise definitely not.
    const unsigned parent_start = clen - plen;
    if (memcmp(&child[parent_start], parent, plen))
        return false;

    // Now we know the final bytes match, but we need to ensure the match
    // starts at a label boundary in the child...
    unsigned child_label_idx = 0;
    while (child_label_idx < parent_start) {
        const unsigned llen = child[child_label_idx++];
        child_label_idx += llen;
    }

    // If we land right on the money, the child is in the zone!
    return child_label_idx == parent_start;
}

// both arguments must be DNAME_VALID, and dname must be known
//   to be a child of (or equal to) parent (e.g. via gdnsd_dname_isinzone()).
// chops the zone part off the end of dname, re-rooting it as a valid name.
// this is used for the LHS of in-zone records that are fully qualified
//   during zonefile scanning, since insertion is rooted at the top of the
//   zone.
F_NONNULL F_UNUSED
static void gdnsd_dname_drop_zone(uint8_t* dname, const uint8_t* zroot)
{
    gdnsd_assert(gdnsd_dname_status(dname) == DNAME_VALID);
    gdnsd_assert(gdnsd_dname_status(zroot) == DNAME_VALID);
    gdnsd_assert((*dname) >= (*zroot));
    const unsigned newterm = (*dname) - ((*zroot) - 1U);
    gdnsd_assert(dname[newterm] == zroot[1]);
    dname[0] = newterm;
    dname[newterm] = 0;
}

// Returns true if dname is a wildcard name (first label is a lone "*").
// Argument must be DNAME_VALID or DNAME_PARTIAL
F_NONNULL F_PURE F_UNUSED
static bool gdnsd_dname_iswild(const uint8_t* dname)
{
    gdnsd_assert(gdnsd_dname_status(dname) != DNAME_INVALID);
    if (dname[1] == 1 && dname[2] == '*')
        return true;
    return false;
}

typedef gdnsd_dname_status_t dname_status_t;
#define dns_unescape gdnsd_dns_unescape
#define dname_from_string gdnsd_dname_from_string
#define dname_cat gdnsd_dname_cat
#define dname_terminate gdnsd_dname_terminate
#define dname_status gdnsd_dname_status
#define dname_status_buflen gdnsd_dname_status_buflen
#define dname_is_partial gdnsd_dname_is_partial
#define dname_trim gdnsd_dname_trim
#define dname_copy gdnsd_dname_copy
#define dname_dup gdnsd_dname_dup
#define dname_cmp gdnsd_dname_cmp
#define dname_isinzone gdnsd_dname_isinzone
#define dname_iswild gdnsd_dname_iswild
#define label_cmp gdnsd_label_cmp
#define label_dup gdnsd_dname_dup

#endif // GDNSD_DNAME_H
