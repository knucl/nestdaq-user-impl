#ifndef TriggerMap_cxx
#define TriggerMap_cxx

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// One LUT entry contains the truth values of all independent triggers.
// OR inside a formula is a boolean operation, never a flag boundary.
class TriggerMap {
public:
    void MakeTable(const std::string& configuration);
    bool LookUp(uint32_t value) const { return LookUpTriggerFlags(value) != 0; }
    uint32_t LookUpTriggerFlags(uint32_t value) const {
        // Registered groups not referenced by any formula must not veto it.
        return fTriggerFlagsLut.empty() ? 0 : fTriggerFlagsLut[value & fInputMask];
    }
    unsigned GetTriggerCount() const { return fDefinitions.size(); }
    void Dump() const;
private:
    struct Definition {
        unsigned id;
        std::string expression;
        std::vector<std::string> commands;
    };
    static std::string Trim(const std::string& value);
    static unsigned Number(const std::string& value, unsigned limit);
    static std::vector<std::string> Compile(const std::string& expression);
    static bool Evaluate(const std::vector<std::string>& commands, uint32_t value,
                         std::vector<uint8_t>& stack);
    std::vector<Definition> fDefinitions;
    std::vector<uint32_t> fTriggerFlagsLut;
    uint32_t fInputMask = 0;
};

std::string TriggerMap::Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

unsigned TriggerMap::Number(const std::string& value, unsigned limit) {
    if (value.empty() || !std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return std::isdigit(c); }))
        throw std::invalid_argument("Expected a decimal ID, got: " + value);
    const auto n = std::stoul(value);
    if (n >= limit) throw std::invalid_argument("ID out of range: " + value);
    return static_cast<unsigned>(n);
}

std::vector<std::string> TriggerMap::Compile(const std::string& expression) {
    const auto text = Trim(expression);
    if (text.empty()) throw std::invalid_argument("Empty trigger expression");
    const bool rpn = text.compare(0, 3, "RPN") == 0 || text.compare(0, 3, "rpn") == 0;
    const auto body = rpn ? text.substr(3) : text;
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < body.size();) {
        const unsigned char c = body[i];
        if (std::isspace(c)) { ++i; continue; }
        if (std::isdigit(c)) {
            const auto begin = i++;
            while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i]))) ++i;
            auto token = body.substr(begin, i - begin);
            Number(token, 32);
            tokens.push_back(std::move(token));
        } else if (c == '&' || c == '|' || c == '!' || (!rpn && (c == '(' || c == ')'))) {
            tokens.emplace_back(1, static_cast<char>(c)); ++i;
        } else {
            throw std::invalid_argument("Unsupported trigger expression token: " + body.substr(i, 1));
        }
    }
    std::vector<std::string> result;
    if (rpn) {
        result = std::move(tokens);
    } else {
        std::vector<std::string> operators;
        bool operand = true;
        const auto precedence = [](const std::string& op) {
            return op == "!" ? 3 : op == "&" ? 2 : op == "|" ? 1 : 0;
        };
        for (const auto& token : tokens) {
            if (std::isdigit(static_cast<unsigned char>(token[0]))) {
                if (!operand) throw std::invalid_argument("Missing operator before " + token);
                result.push_back(token); operand = false;
            } else if (token == "(" || token == "!") {
                if (!operand) throw std::invalid_argument("Missing operator before " + token);
                operators.push_back(token);
            } else if (token == ")") {
                if (operand) throw std::invalid_argument("Missing operand before ')'");
                while (!operators.empty() && operators.back() != "(") {
                    result.push_back(operators.back()); operators.pop_back();
                }
                if (operators.empty()) throw std::invalid_argument("Unmatched ')'");
                operators.pop_back();
            } else {
                if (operand) throw std::invalid_argument("Missing operand before " + token);
                while (!operators.empty() && operators.back() != "(" &&
                       precedence(operators.back()) >= precedence(token)) {
                    result.push_back(operators.back()); operators.pop_back();
                }
                operators.push_back(token); operand = true;
            }
        }
        if (operand) throw std::invalid_argument("Incomplete trigger expression");
        while (!operators.empty()) {
            if (operators.back() == "(") throw std::invalid_argument("Unmatched '('");
            result.push_back(operators.back()); operators.pop_back();
        }
    }
    std::size_t depth = 0;
    for (const auto& token : result) {
        if (token == "!") {
            if (depth == 0) throw std::invalid_argument("Missing operand for '!'");
        } else if (token == "&" || token == "|") {
            if (depth < 2) throw std::invalid_argument("Missing operands for " + token);
            --depth;
        } else { ++depth; }
    }
    if (depth != 1) throw std::invalid_argument("Invalid trigger expression result stack");
    return result;
}

bool TriggerMap::Evaluate(const std::vector<std::string>& commands, uint32_t value,
                          std::vector<uint8_t>& stack) {
    stack.clear();
    for (const auto& token : commands) {
        if (token == "!") stack.back() = !stack.back();
        else if (token == "&" || token == "|") {
            const bool right = stack.back(); stack.pop_back();
            const bool left = stack.back();
            stack.back() = token == "&" ? left && right : left || right;
        } else stack.push_back((value >> Number(token, 32)) & 1u);
    }
    return stack.back();
}

void TriggerMap::MakeTable(const std::string& configuration) {
    std::vector<Definition> definitions;
    uint32_t assigned = 0;
    const auto add = [&](unsigned id, const std::string& expression) {
        const uint32_t bit = uint32_t{1} << id;
        if (assigned & bit) throw std::invalid_argument("Duplicate trigger ID: " + std::to_string(id));
        assigned |= bit;
        definitions.push_back({id, expression, Compile(expression)});
    };
    if (configuration.find(':') == std::string::npos && configuration.find(';') == std::string::npos) {
        add(0, Trim(configuration)); // Legacy single expression: only trigger 0.
    } else {
        std::string lines = configuration;
        std::replace(lines.begin(), lines.end(), ';', '\n');
        std::istringstream input(lines);
        std::string line;
        while (std::getline(input, line)) {
            line = Trim(line);
            if (line.empty()) continue;
            const auto colon = line.find(':');
            if (colon == std::string::npos || line.find(':', colon + 1) != std::string::npos)
                throw std::invalid_argument("Use 'trigger-ID: expression' on each line");
            add(Number(Trim(line.substr(0, colon)), 24), Trim(line.substr(colon + 1)));
        }
    }
    if (definitions.empty()) throw std::invalid_argument("No trigger definitions");
    unsigned nSignals = 0;
    for (const auto& definition : definitions)
        for (const auto& token : definition.commands)
            if (std::isdigit(static_cast<unsigned char>(token[0])))
                nSignals = std::max(nSignals, Number(token, 32) + 1);
    if (nSignals >= std::numeric_limits<std::size_t>::digits)
        throw std::invalid_argument("Trigger LUT size exceeds address space");
    const std::size_t mapSize = std::size_t{1} << nSignals;
    std::vector<uint32_t> table(mapSize, 0);
    std::vector<uint8_t> stack;
    for (const auto& definition : definitions) stack.reserve(std::max(stack.capacity(), definition.commands.size()));
    for (std::size_t value = 0; value < mapSize; ++value)
        for (const auto& definition : definitions)
            if (Evaluate(definition.commands, static_cast<uint32_t>(value), stack))
                table[value] |= uint32_t{1} << definition.id;
    fDefinitions = std::move(definitions);
    fTriggerFlagsLut = std::move(table);
    fInputMask = static_cast<uint32_t>(mapSize - 1);
    for (const auto& definition : fDefinitions)
        std::cout << "# Trigger ID " << definition.id << ": " << definition.expression << '\n';
}

void TriggerMap::Dump() const {
    for (std::size_t value = 0; value < fTriggerFlagsLut.size(); ++value)
        std::cout << value << " : " << fTriggerFlagsLut[value] << '\n';
}
#endif
