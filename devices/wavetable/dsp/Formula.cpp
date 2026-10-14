#include "Formula.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fd::devices::wavetable
{
struct Formula::Node
{
    enum class Kind
    {
        Number,
        X,
        T,
        Negate,
        Add,
        Subtract,
        Multiply,
        Divide,
        Power,
        Call
    } kind = Kind::Number;
    double value = 0.0;
    std::string function;
    std::vector<std::shared_ptr<const Node>> args;

    [[nodiscard]] double evaluate(double x, double t) const noexcept;
};

namespace
{
using NodePtr = std::shared_ptr<const Formula::Node>;

double frac(double v) noexcept
{
    return v - std::floor(v);
}

/// A fixed random value in -1 ... 1 for each whole number.
double randomAt(double v) noexcept
{
    auto n = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(v)));
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return static_cast<double>(n) / 2147483647.5 - 1.0;
}

struct FunctionInfo
{
    const char* name;
    int arity;
};
constexpr FunctionInfo kFunctions[] = {
    {"sin", 1}, {"cos", 1}, {"tan", 1},   {"asin", 1},  {"acos", 1}, {"atan", 1}, {"abs", 1},  {"sqrt", 1},
    {"exp", 1}, {"log", 1}, {"log2", 1},  {"floor", 1}, {"ceil", 1}, {"frac", 1}, {"sign", 1}, {"min", 2},
    {"max", 2}, {"pow", 2}, {"clamp", 3}, {"tri", 1},   {"saw", 1},  {"sq", 1},   {"rand", 1}};

class Parser
{
public:
    explicit Parser(const std::string& text) : s_(text) {}

    NodePtr parse(std::string* error)
    {
        try
        {
            auto node = expression();
            skipSpaces();
            if (at_ < s_.size())
                fail("unexpected \"" + std::string(1, s_[at_]) + "\"");
            return node;
        }
        catch (const std::string& message)
        {
            if (error != nullptr)
                *error = message;
            return nullptr;
        }
    }

private:
    [[noreturn]] void fail(const std::string& what) const
    {
        throw what + " at character " + std::to_string(at_ + 1);
    }

    void skipSpaces()
    {
        while (at_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[at_])) != 0)
            ++at_;
    }

    bool take(char c)
    {
        skipSpaces();
        if (at_ < s_.size() && s_[at_] == c)
        {
            ++at_;
            return true;
        }
        return false;
    }

    static NodePtr make(Formula::Node::Kind kind, std::vector<NodePtr> args = {}, double value = 0.0,
                        std::string function = {})
    {
        auto n = std::make_shared<Formula::Node>();
        n->kind = kind;
        n->args = std::move(args);
        n->value = value;
        n->function = std::move(function);
        return n;
    }

    NodePtr expression()
    {
        auto left = term();
        for (;;)
        {
            if (take('+'))
                left = make(Formula::Node::Kind::Add, {left, term()});
            else if (take('-'))
                left = make(Formula::Node::Kind::Subtract, {left, term()});
            else
                return left;
        }
    }

    NodePtr term()
    {
        auto left = unary();
        for (;;)
        {
            if (take('*'))
                left = make(Formula::Node::Kind::Multiply, {left, unary()});
            else if (take('/'))
                left = make(Formula::Node::Kind::Divide, {left, unary()});
            else
                return left;
        }
    }

    NodePtr unary()
    {
        if (take('-'))
            return make(Formula::Node::Kind::Negate, {unary()});
        if (take('+'))
            return unary();
        return power();
    }

    NodePtr power()
    {
        auto base = primary();
        if (take('^'))
            return make(Formula::Node::Kind::Power, {base, unary()}); // right-associative
        return base;
    }

    NodePtr primary()
    {
        skipSpaces();
        if (at_ >= s_.size())
            fail("the formula ends too soon");
        const char c = s_[at_];
        if (c == '(')
        {
            ++at_;
            auto inner = expression();
            if (!take(')'))
                fail("a \")\" is missing");
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.')
        {
            size_t used = 0;
            const double value = std::stod(s_.substr(at_), &used);
            at_ += used;
            return make(Formula::Node::Kind::Number, {}, value);
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0)
        {
            const size_t start = at_;
            while (at_ < s_.size() &&
                   (std::isalnum(static_cast<unsigned char>(s_[at_])) != 0 || s_[at_] == '_'))
                ++at_;
            const auto name = s_.substr(start, at_ - start);
            if (name == "x")
                return make(Formula::Node::Kind::X);
            if (name == "t")
                return make(Formula::Node::Kind::T);
            if (name == "pi")
                return make(Formula::Node::Kind::Number, {}, std::numbers::pi);
            if (name == "e")
                return make(Formula::Node::Kind::Number, {}, std::numbers::e);
            const auto* info = std::find_if(std::begin(kFunctions), std::end(kFunctions),
                                            [&name](const FunctionInfo& f) { return name == f.name; });
            if (info == std::end(kFunctions))
            {
                at_ = start;
                fail("\"" + name + "\" isn't known (x, t, pi, e or a function)");
            }
            if (!take('('))
                fail(name + " needs its arguments in brackets");
            std::vector<NodePtr> args{expression()};
            while (take(','))
                args.push_back(expression());
            if (!take(')'))
                fail("a \")\" is missing");
            if (static_cast<int>(args.size()) != info->arity)
                fail(name + " takes " + std::to_string(info->arity) +
                     (info->arity == 1 ? " argument" : " arguments"));
            return make(Formula::Node::Kind::Call, std::move(args), 0.0, name);
        }
        fail("unexpected \"" + std::string(1, c) + "\"");
    }

    const std::string& s_;
    size_t at_ = 0;
};
} // namespace

double Formula::Node::evaluate(double x, double t) const noexcept
{
    const auto arg = [&](size_t i) { return args[i]->evaluate(x, t); };
    switch (kind)
    {
    case Kind::Number:
        return value;
    case Kind::X:
        return x;
    case Kind::T:
        return t;
    case Kind::Negate:
        return -arg(0);
    case Kind::Add:
        return arg(0) + arg(1);
    case Kind::Subtract:
        return arg(0) - arg(1);
    case Kind::Multiply:
        return arg(0) * arg(1);
    case Kind::Divide:
    {
        const double d = arg(1);
        return d == 0.0 ? 0.0 : arg(0) / d;
    }
    case Kind::Power:
        return std::pow(arg(0), arg(1));
    case Kind::Call:
        break;
    }
    const double a = arg(0);
    const auto& f = function;
    if (f == "sin")
        return std::sin(a);
    if (f == "cos")
        return std::cos(a);
    if (f == "tan")
        return std::tan(a);
    if (f == "asin")
        return std::asin(std::clamp(a, -1.0, 1.0));
    if (f == "acos")
        return std::acos(std::clamp(a, -1.0, 1.0));
    if (f == "atan")
        return std::atan(a);
    if (f == "abs")
        return std::abs(a);
    if (f == "sqrt")
        return a > 0.0 ? std::sqrt(a) : 0.0;
    if (f == "exp")
        return std::exp(std::min(a, 700.0));
    if (f == "log")
        return a > 0.0 ? std::log(a) : 0.0;
    if (f == "log2")
        return a > 0.0 ? std::log2(a) : 0.0;
    if (f == "floor")
        return std::floor(a);
    if (f == "ceil")
        return std::ceil(a);
    if (f == "frac")
        return frac(a);
    if (f == "sign")
        return a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0);
    if (f == "min")
        return std::min(a, arg(1));
    if (f == "max")
        return std::max(a, arg(1));
    if (f == "pow")
        return std::pow(a, arg(1));
    if (f == "clamp")
        return std::clamp(a, std::min(arg(1), arg(2)), std::max(arg(1), arg(2)));
    if (f == "tri")
        return 1.0 - 4.0 * std::abs(frac(a + 0.25) - 0.5);
    if (f == "saw")
        return 2.0 * frac(a + 0.5) - 1.0;
    if (f == "sq")
        return frac(a) < 0.5 ? 1.0 : -1.0;
    if (f == "rand")
        return randomAt(a);
    return 0.0;
}

std::optional<Formula> Formula::parse(const std::string& text, std::string* error)
{
    Parser parser(text);
    auto root = parser.parse(error);
    if (root == nullptr)
        return std::nullopt;
    Formula f;
    f.text_ = text;
    f.root_ = std::move(root);
    return f;
}

double Formula::evaluate(double x, double t) const noexcept
{
    const double v = root_ != nullptr ? root_->evaluate(x, t) : 0.0;
    return std::isfinite(v) ? v : 0.0;
}
} // namespace fd::devices::wavetable
