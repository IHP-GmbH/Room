#include "pulse_params.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace core {
namespace {

const std::string *findProperty(const std::vector<Property> &props, const std::string &name)
{
    for (const Property &prop : props) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

std::string pulseParam(const std::vector<Property> &props, std::size_t index)
{
    if (const std::string *v = findProperty(props, "param." + std::to_string(index))) {
        if (!v->empty()) {
            return *v;
        }
    }
    return {};
}

bool isPulseValue(const std::string &value)
{
    return value.find("PULSE") != std::string::npos || value.find("pulse") != std::string::npos;
}

bool isLiteralPulseToken(const std::string &tok)
{
    return !tok.empty() && tok.find('{') == std::string::npos && tok.find('}') == std::string::npos;
}

std::optional<double> parseSpiceTimeSeconds(const std::string &text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t pos = 0;
    while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' ||
                                 text[pos] == '+' || text[pos] == '-')) {
        ++pos;
    }
    if (pos == 0) {
        return std::nullopt;
    }
    double value = 0.0;
    try {
        value = std::stod(text.substr(0, pos));
    } catch (...) {
        return std::nullopt;
    }
    std::string suffix = text.substr(pos);
    for (char &ch : suffix) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    double scale = 1.0;
    if (suffix == "f") {
        scale = 1e-15;
    } else if (suffix == "p") {
        scale = 1e-12;
    } else if (suffix == "n") {
        scale = 1e-9;
    } else if (suffix == "u") {
        scale = 1e-6;
    } else if (suffix == "m") {
        scale = 1e-3;
    } else if (suffix == "k") {
        scale = 1e3;
    } else if (suffix == "meg") {
        scale = 1e6;
    } else if (!suffix.empty()) {
        return std::nullopt;
    }
    return value * scale;
}

std::string formatSpiceTime(double seconds)
{
    if (seconds == 0.0) {
        return "0";
    }
    const struct Unit {
        double scale;
        const char *suffix;
    } units[] = {
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1.0, ""},
    };
    const double abs = std::fabs(seconds);
    for (const Unit &unit : units) {
        const double scaled = abs / unit.scale;
        if (scaled >= 1.0 && scaled < 1000.0) {
            std::ostringstream oss;
            const double rounded = std::copysign(std::round(scaled * 1000.0) / 1000.0, seconds);
            oss << rounded << unit.suffix;
            return oss.str();
        }
    }
    std::ostringstream oss;
    oss << seconds;
    return oss.str();
}

std::optional<std::string> legacyPulseWidthLiteral(const std::string &per, const std::string &td)
{
    const std::optional<double> perSec = parseSpiceTimeSeconds(per);
    const std::optional<double> tdSec = parseSpiceTimeSeconds(td);
    if (!perSec || !tdSec) {
        return std::nullopt;
    }
    const double pwSec = *perSec - 2.0 * *tdSec;
    if (pwSec <= 0.0) {
        return std::nullopt;
    }
    return formatSpiceTime(pwSec);
}

std::optional<std::string> formatCanonicalPulse(const std::string &u1, const std::string &u2, const std::string &td,
                                                const std::string &tr, const std::string &tf, const std::string &pw,
                                                const std::string &per)
{
    if (u1.empty() || u2.empty() || td.empty() || pw.empty()) {
        return std::nullopt;
    }
    std::ostringstream pulse;
    pulse << "PULSE(" << u1 << ' ' << u2 << ' ' << td << ' ' << (tr.empty() ? "0" : tr) << ' '
          << (tf.empty() ? "0" : tf) << ' ' << pw;
    if (!per.empty()) {
        pulse << ' ' << per;
    }
    pulse << ')';
    return pulse.str();
}

} // namespace

bool looksLikeQucsEndTimeExpr(const std::string &s)
{
    return s.find('{') != std::string::npos || s.find('+') != std::string::npos;
}

std::string pulseWidthFromLegacyPeriod(const std::string &per, const std::string &td)
{
    if (const std::optional<std::string> literal = legacyPulseWidthLiteral(per, td)) {
        return *literal;
    }
    return "{" + per + "-2*(" + td + ")}";
}

std::optional<double> parseSpiceTimeValue(const std::string &text)
{
    return parseSpiceTimeSeconds(text);
}

std::vector<std::string> tokenizePulseBody(const std::string &val)
{
    std::string body = val;
    const auto lp = body.find('(');
    const auto rp = body.rfind(')');
    if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
        body = body.substr(lp + 1, rp - lp - 1);
    }
    std::vector<std::string> toks;
    std::istringstream iss(body);
    std::string tok;
    while (iss >> tok) {
        toks.push_back(tok);
    }
    return toks;
}

std::optional<std::string> buildPulseSpiceValue(const std::vector<Property> &props)
{
    std::string u1;
    std::string u2;
    std::string td;
    std::string tr;
    std::string tf;
    std::string pw;
    std::string per;

    if (const std::string *value = findProperty(props, "value"); value && !value->empty() && isPulseValue(*value)) {
        const std::vector<std::string> toks = tokenizePulseBody(*value);
        if (toks.size() >= 6 && isLiteralPulseToken(toks[0]) && isLiteralPulseToken(toks[1]) &&
            isLiteralPulseToken(toks[2]) && isLiteralPulseToken(toks[3]) && isLiteralPulseToken(toks[4]) &&
            isLiteralPulseToken(toks[5])) {
            return formatCanonicalPulse(toks[0], toks[1], toks[2], toks[3], toks[4], toks[5],
                                      toks.size() > 6 ? toks[6] : std::string{});
        }
        if (toks.size() > 5 && isLiteralPulseToken(toks[5])) {
            pw = toks[5];
        }
        if (toks.size() > 6 && isLiteralPulseToken(toks[6])) {
            per = toks[6];
        }
        if (toks.size() > 2 && isLiteralPulseToken(toks[0])) {
            u1 = toks[0];
        }
        if (toks.size() > 1 && isLiteralPulseToken(toks[1])) {
            u2 = toks[1];
        }
        if (toks.size() > 2 && isLiteralPulseToken(toks[2])) {
            td = toks[2];
        }
        if (toks.size() > 3 && isLiteralPulseToken(toks[3])) {
            tr = toks[3];
        }
        if (toks.size() > 4 && isLiteralPulseToken(toks[4])) {
            tf = toks[4];
        }
    }

    if (u1.empty()) {
        u1 = pulseParam(props, 0);
    }
    if (u2.empty()) {
        u2 = pulseParam(props, 1);
    }
    if (td.empty()) {
        td = pulseParam(props, 2);
    }
    if (tr.empty()) {
        tr = pulseParam(props, 4);
    }
    if (tf.empty()) {
        tf = pulseParam(props, 5);
    }
    const std::string perOrEnd = pulseParam(props, 3);
    if (per.empty()) {
        per = pulseParam(props, 6);
    }
    if (per.empty() && !perOrEnd.empty() && !looksLikeQucsEndTimeExpr(perOrEnd)) {
        per = perOrEnd;
    }

    if (pw.empty() && !per.empty()) {
        if (const std::optional<std::string> literal = legacyPulseWidthLiteral(per, td)) {
            pw = *literal;
        }
    }

    if (u1.empty() || u2.empty() || td.empty() || pw.empty()) {
        return std::nullopt;
    }

    return formatCanonicalPulse(u1, u2, td, tr, tf, pw, per);
}

} // namespace core
