/*
 * json.h -- JSON <-> Coil value bridge.
 *
 * json_parse decodes untrusted JSON text into Coil values (null->nil,
 * true/false->bool, numbers->int/float, strings->string, arrays->array,
 * objects->table).  json_stringify renders a value back to JSON.  Both bound
 * recursion depth so adversarial nesting cannot exhaust the C stack.
 */

#ifndef COIL_JSON_H
#define COIL_JSON_H

#include "object.h"

/* On success sets *ok=true and returns the value; on a parse error sets
 * *ok=false and returns nil. */
Value json_parse(Heap *h, const char *text, size_t len, bool *ok);

/* Render v as JSON, or NULL on error (cycle / depth limit / unencodable). */
ObjString *json_stringify(Heap *h, Value v);

#endif /* COIL_JSON_H */
