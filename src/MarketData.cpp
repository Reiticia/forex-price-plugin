#include "MarketData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fxprice
{
namespace
{
struct JsonValue
{
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type{Type::Null};
    bool boolean{};
    std::string text;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;
};

class JsonParser
{
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool Parse(JsonValue& output, std::string& error)
    {
        SkipWhitespace();
        if (!ParseValue(output, 0))
        {
            error = error_.empty() ? "JSON 格式无效" : error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size())
        {
            error = "JSON 末尾包含多余内容";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& output, int depth)
    {
        if (depth > 64) return Fail("JSON 嵌套过深");
        SkipWhitespace();
        if (position_ >= input_.size()) return Fail("JSON 意外结束");
        const char character = input_[position_];
        if (character == '{') return ParseObject(output, depth + 1);
        if (character == '[') return ParseArray(output, depth + 1);
        if (character == '"')
        {
            output.type = JsonValue::Type::String;
            return ParseString(output.text);
        }
        if (character == 't') return ParseLiteral("true", JsonValue::Type::Boolean, output, true);
        if (character == 'f') return ParseLiteral("false", JsonValue::Type::Boolean, output, false);
        if (character == 'n') return ParseLiteral("null", JsonValue::Type::Null, output, false);
        if (character == '-' || std::isdigit(static_cast<unsigned char>(character)))
            return ParseNumber(output);
        return Fail("JSON 包含无法识别的值");
    }

    bool ParseObject(JsonValue& output, int depth)
    {
        output.type = JsonValue::Type::Object;
        output.object.clear();
        ++position_;
        SkipWhitespace();
        if (Consume('}')) return true;
        for (;;)
        {
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (!Consume(':')) return Fail("JSON 对象缺少冒号");
            JsonValue value;
            if (!ParseValue(value, depth)) return false;
            output.object[std::move(key)] = std::move(value);
            SkipWhitespace();
            if (Consume('}')) return true;
            if (!Consume(',')) return Fail("JSON 对象缺少逗号");
            SkipWhitespace();
        }
    }

    bool ParseArray(JsonValue& output, int depth)
    {
        output.type = JsonValue::Type::Array;
        output.array.clear();
        ++position_;
        SkipWhitespace();
        if (Consume(']')) return true;
        for (;;)
        {
            JsonValue value;
            if (!ParseValue(value, depth)) return false;
            output.array.push_back(std::move(value));
            SkipWhitespace();
            if (Consume(']')) return true;
            if (!Consume(',')) return Fail("JSON 数组缺少逗号");
            SkipWhitespace();
        }
    }

    bool ParseString(std::string& output)
    {
        if (!Consume('"')) return Fail("JSON 字符串缺少引号");
        output.clear();
        while (position_ < input_.size())
        {
            const char character = input_[position_++];
            if (character == '"') return true;
            if (static_cast<unsigned char>(character) < 0x20)
                return Fail("JSON 字符串包含控制字符");
            if (character != '\\')
            {
                output.push_back(character);
                continue;
            }
            if (position_ >= input_.size()) return Fail("JSON 转义序列不完整");
            const char escaped = input_[position_++];
            switch (escaped)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
                if (position_ + 4 > input_.size()) return Fail("JSON Unicode 转义不完整");
                position_ += 4;
                output.push_back('?');
                break;
            default: return Fail("JSON 转义序列无效");
            }
        }
        return Fail("JSON 字符串未结束");
    }

    bool ParseNumber(JsonValue& output)
    {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])))
            ++position_;
        if (position_ < input_.size() && input_[position_] == '.')
        {
            ++position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
        {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
        }
        output.type = JsonValue::Type::Number;
        output.text = std::string(input_.substr(start, position_ - start));
        return !output.text.empty();
    }

    bool ParseLiteral(std::string_view literal, JsonValue::Type type,
                      JsonValue& output, bool boolean)
    {
        if (input_.substr(position_, literal.size()) != literal)
            return Fail("JSON 字面量无效");
        position_ += literal.size();
        output.type = type;
        output.boolean = boolean;
        return true;
    }

    void SkipWhitespace()
    {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])))
            ++position_;
    }

    bool Consume(char expected)
    {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool Fail(const char* message)
    {
        if (error_.empty()) error_ = message;
        return false;
    }

    std::string_view input_;
    std::size_t position_{};
    std::string error_;
};

const JsonValue* Member(const JsonValue& value, const char* key)
{
    if (value.type != JsonValue::Type::Object) return nullptr;
    const auto found = value.object.find(key);
    return found == value.object.end() ? nullptr : &found->second;
}

bool ReadString(const JsonValue& object, const char* key, std::string& output)
{
    const JsonValue* value = Member(object, key);
    if (!value || value->type != JsonValue::Type::String) return false;
    output = value->text;
    return true;
}

bool ParseDecimal(const std::string& text, double& output)
{
    char* end = nullptr;
    output = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(output);
}

int DecimalPlaces(const std::string& text)
{
    const std::size_t point = text.find('.');
    if (point == std::string::npos) return 0;
    const std::size_t exponent = text.find_first_of("eE", point + 1);
    return static_cast<int>((exponent == std::string::npos ? text.size() : exponent) - point - 1);
}

bool ReadNumber(const JsonValue& value, double& output)
{
    return value.type == JsonValue::Type::Number && ParseDecimal(value.text, output);
}
} // namespace

std::string BuildScannerRequest(const std::vector<std::wstring>& symbols)
{
    std::string request = R"({"symbols":{"tickers":[)";
    bool first = true;
    for (const std::wstring& raw : symbols)
    {
        const std::wstring symbol = NormalizeSymbol(raw);
        if (!IsValidSymbol(symbol)) continue;
        if (!first) request += ',';
        first = false;
        request += "\"FX_IDC:" + WideToAscii(symbol) + "\"";
    }
    request += R"(],"query":{"types":[]}},"columns":["name","description","bid","ask","change"]})";
    return request;
}

bool ParseScannerResponse(const std::string& json,
                          std::unordered_map<std::string, Quote>& quotes,
                          std::string& error)
{
    JsonValue root;
    if (!JsonParser(json).Parse(root, error)) return false;
    const JsonValue* data = Member(root, "data");
    if (!data || data->type != JsonValue::Type::Array)
    {
        error = "TradingView 响应缺少 data 数组";
        return false;
    }

    quotes.clear();
    for (const JsonValue& row : data->array)
    {
        std::string providerSymbol;
        const JsonValue* columns = Member(row, "d");
        if (!ReadString(row, "s", providerSymbol) || !columns ||
            columns->type != JsonValue::Type::Array || columns->array.size() < 5)
            continue;

        const std::size_t separator = providerSymbol.rfind(':');
        std::string symbol = separator == std::string::npos
            ? providerSymbol : providerSymbol.substr(separator + 1);
        if (columns->array[0].type == JsonValue::Type::String)
            symbol = columns->array[0].text;
        const std::wstring wideSymbol(symbol.begin(), symbol.end());
        if (!IsValidSymbol(wideSymbol)) continue;

        Quote quote;
        quote.symbol = symbol;
        if (columns->array[1].type == JsonValue::Type::String)
            quote.description = columns->array[1].text;
        if (!ReadNumber(columns->array[2], quote.bid) ||
            !ReadNumber(columns->array[3], quote.ask) || quote.bid <= 0.0 || quote.ask <= 0.0)
            continue;
        quote.midpoint = (quote.bid + quote.ask) / 2.0;
        quote.precision = std::clamp((std::max)(DecimalPlaces(columns->array[2].text),
                                                DecimalPlaces(columns->array[3].text)),
                                     0, 10);
        double change = 0.0;
        if (ReadNumber(columns->array[4], change)) quote.dailyChangePercent = change;
        quotes[quote.symbol] = std::move(quote);
    }

    if (quotes.empty())
    {
        error = "TradingView 响应中没有有效的外汇买卖报价";
        return false;
    }
    error.clear();
    return true;
}
} // namespace fxprice
