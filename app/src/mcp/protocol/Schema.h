#pragma once

#include "mcp/protocol/Registry.h"

#include <optional>
#include <string>
#include <vector>

namespace fd::mcp
{
/// JSON Schema (2020-12), the subset our tools declare: type (one or a list), properties,
/// required, additionalProperties (true/false), items, enum, const, minimum, maximum,
/// minItems, maxItems, minLength, anyOf and oneOf. Annotations (description, title, default,
/// examples, $schema) are ignored.

/// Checks `value` against `schema`. Returns nullopt when it conforms, or a sentence naming the
/// first problem and where it is, e.g. "notes[2].pitch: expected an integer or a string".
[[nodiscard]] std::optional<std::string> validate(const Json& schema, const Json& value);

/// Keywords in `schema` outside the subset above (tests keep every declared schema checkable).
[[nodiscard]] std::vector<std::string> unsupportedKeywords(const Json& schema);
} // namespace fd::mcp
