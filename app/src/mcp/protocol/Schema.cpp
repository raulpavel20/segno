#include "mcp/protocol/Schema.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fd::mcp
{
namespace
{
const std::set<std::string> kKeywords{"type",     "properties", "required", "additionalProperties", "items",
                                      "enum",     "const",      "minimum",  "maximum",              "minItems",
                                      "maxItems", "minLength",  "anyOf",    "oneOf"};
const std::set<std::string> kAnnotations{"description", "title", "default", "examples", "$schema"};

bool hasType(const Json& value, const std::string& type)
{
    if (type == "object")
        return value.is_object();
    if (type == "array")
        return value.is_array();
    if (type == "string")
        return value.is_string();
    if (type == "boolean")
        return value.is_boolean();
    if (type == "null")
        return value.is_null();
    if (type == "number")
        return value.is_number();
    if (type == "integer")
    {
        if (value.is_number_integer())
            return true;
        if (!value.is_number_float())
            return false;
        const double d = value.get<double>();
        return std::isfinite(d) && std::floor(d) == d;
    }
    return false;
}

std::string article(const std::string& type)
{
    if (type == "object" || type == "array" || type == "integer")
        return "an " + type;
    if (type == "null")
        return "null";
    return "a " + type;
}

std::string typeOf(const Json& value)
{
    if (value.is_object())
        return "an object";
    if (value.is_array())
        return "an array";
    if (value.is_string())
        return "a string";
    if (value.is_boolean())
        return "a boolean";
    if (value.is_null())
        return "null";
    return "a number";
}

std::string where(const std::string& path)
{
    return path.empty() ? "The arguments" : path;
}

std::optional<std::string> check(const Json& schema, const Json& value, const std::string& path)
{
    if (schema.is_boolean())
        return schema.get<bool>() ? std::nullopt : std::optional<std::string>(where(path) + ": not allowed");
    if (!schema.is_object())
        return std::nullopt;

    if (auto it = schema.find("type"); it != schema.end())
    {
        std::vector<std::string> types;
        if (it->is_string())
            types.push_back(it->get<std::string>());
        else
            for (const auto& t : *it)
                types.push_back(t.get<std::string>());
        if (std::none_of(types.begin(), types.end(), [&](const auto& t) { return hasType(value, t); }))
        {
            std::string expected;
            for (size_t i = 0; i < types.size(); ++i)
                expected += (i == 0 ? "" : i + 1 == types.size() ? " or " : ", ") + article(types[i]);
            return where(path) + ": expected " + expected + ", got " + typeOf(value);
        }
    }
    if (auto it = schema.find("enum"); it != schema.end())
        if (std::find(it->begin(), it->end(), value) == it->end())
        {
            std::string options;
            for (const auto& option : *it)
                options += (options.empty() ? "" : ", ") + option.dump();
            return where(path) + ": must be one of " + options;
        }
    if (auto it = schema.find("const"); it != schema.end() && *it != value)
        return where(path) + ": must be " + it->dump();

    if (value.is_number())
    {
        const double v = value.get<double>();
        if (auto it = schema.find("minimum"); it != schema.end() && v < it->get<double>())
            return where(path) + ": must be at least " + it->dump();
        if (auto it = schema.find("maximum"); it != schema.end() && v > it->get<double>())
            return where(path) + ": must be at most " + it->dump();
    }
    if (value.is_string())
        if (auto it = schema.find("minLength"); it != schema.end() && value.get<std::string>().size() < it->get<size_t>())
            return where(path) + ": must not be empty";

    if (value.is_array())
    {
        if (auto it = schema.find("minItems"); it != schema.end() && value.size() < it->get<size_t>())
            return where(path) + ": needs at least " + it->dump() + " item" + (it->get<size_t>() == 1 ? "" : "s");
        if (auto it = schema.find("maxItems"); it != schema.end() && value.size() > it->get<size_t>())
            return where(path) + ": takes at most " + it->dump() + " items";
        if (auto it = schema.find("items"); it != schema.end())
            for (size_t i = 0; i < value.size(); ++i)
                if (auto e = check(*it, value[i], path + "[" + std::to_string(i) + "]"))
                    return e;
    }

    if (value.is_object())
    {
        const auto props = schema.find("properties");
        if (auto it = schema.find("required"); it != schema.end())
            for (const auto& name : *it)
                if (!value.contains(name.get<std::string>()))
                    return (path.empty() ? "" : path + ": ") + "missing \"" + name.get<std::string>() + "\"";
        for (const auto& [key, v] : value.items())
        {
            const auto sub = path.empty() ? key : path + "." + key;
            if (props != schema.end() && props->contains(key))
            {
                if (auto e = check((*props)[key], v, sub))
                    return e;
            }
            else if (auto extra = schema.find("additionalProperties"); extra != schema.end())
            {
                if (extra->is_boolean() && !extra->get<bool>())
                    return "Unknown argument \"" + sub + "\"";
                if (auto e = check(*extra, v, sub))
                    return e;
            }
        }
    }

    if (auto it = schema.find("anyOf"); it != schema.end())
    {
        std::optional<std::string> first;
        for (const auto& sub : *it)
        {
            auto e = check(sub, value, path);
            if (!e)
            {
                first.reset();
                break;
            }
            if (!first)
                first = e;
        }
        if (first)
            return first;
    }
    if (auto it = schema.find("oneOf"); it != schema.end())
    {
        int matches = 0;
        std::optional<std::string> first;
        for (const auto& sub : *it)
            if (auto e = check(sub, value, path))
                first = first ? first : e;
            else
                ++matches;
        if (matches == 0)
            return first;
        if (matches > 1)
            return where(path) + ": matches more than one form";
    }
    return std::nullopt;
}

void collectUnsupported(const Json& schema, std::vector<std::string>& out)
{
    if (!schema.is_object())
        return;
    for (const auto& [key, v] : schema.items())
    {
        if (kAnnotations.contains(key))
            continue;
        if (!kKeywords.contains(key))
        {
            out.push_back(key);
            continue;
        }
        if (key == "properties")
            for (const auto& [name, sub] : v.items())
                collectUnsupported(sub, out);
        else if (key == "items" || key == "additionalProperties")
            collectUnsupported(v, out);
        else if (key == "anyOf" || key == "oneOf")
            for (const auto& sub : v)
                collectUnsupported(sub, out);
    }
}
} // namespace

std::optional<std::string> validate(const Json& schema, const Json& value)
{
    return check(schema, value, "");
}

std::vector<std::string> unsupportedKeywords(const Json& schema)
{
    std::vector<std::string> out;
    collectUnsupported(schema, out);
    return out;
}
} // namespace fd::mcp
