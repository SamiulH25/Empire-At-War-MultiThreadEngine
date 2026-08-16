#include "core/xml.h"

#include <cctype>
#include <sstream>

namespace eaw {

const XmlNode* XmlNode::firstChild(const std::string& n) const {
    for (const auto& c : children)
        if (c.name == n) return &c;
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(const std::string& n) const {
    std::vector<const XmlNode*> out;
    for (const auto& c : children)
        if (c.name == n) out.push_back(&c);
    return out;
}

std::string XmlNode::attr(const std::string& key) const {
    for (const auto& [k, v] : attrs)
        if (k == key) return v;
    return "";
}

namespace {

struct Parser {
    const std::string& s;
    size_t pos = 0;
    size_t line = 1;

    explicit Parser(const std::string& text) : s(text) {}

    [[noreturn]] void fail(const std::string& msg) const {
        throw XmlError(msg + " (line " + std::to_string(line) + ")");
    }

    char peek() const { return pos < s.size() ? s[pos] : '\0'; }
    char get() {
        if (pos >= s.size()) fail("unexpected end of file");
        char c = s[pos++];
        if (c == '\n') ++line;
        return c;
    }
    bool eof() const { return pos >= s.size(); }

    void skipWs() {
        while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n'))
            get();
    }

    void expect(const std::string& tok) {
        for (char c : tok) {
            if (eof() || get() != c) fail("expected '" + tok + "'");
        }
    }

    // Skips an XML comment <!-- ... --> (must be positioned at '<' '!' '-' '-')
    void skipComment() {
        size_t startLine = line;
        // consume <!--
        get(); get(); get(); get();
        // consume until -->
        while (true) {
            if (eof()) fail("unterminated comment");
            if (peek() == '-' && pos + 2 < s.size() && s[pos + 1] == '-' && s[pos + 2] == '>') {
                get(); get(); get();
                return;
            }
            get();
        }
    }

    // Skips a declaration <? ... ?> or <!DOCTYPE ... >
    void skipDeclaration() {
        // consume <?
        get(); get();
        while (!eof()) {
            if (peek() == '?' && pos + 1 < s.size() && s[pos + 1] == '>') {
                get(); get();
                return;
            }
            get();
        }
        fail("unterminated declaration");
    }

    std::string parseName() {
        std::string name;
        while (!eof()) {
            char c = peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':') {
                name += get();
            } else {
                break;
            }
        }
        if (name.empty()) fail("expected element name");
        return name;
    }

    std::string parseAttrValue() {
        // positioned at opening quote
        char quote = get();
        std::string v;
        while (!eof() && peek() != quote) {
            char c = get();
            if (c == '&' && pos + 1 < s.size()) {
                // decode the 5 basic entities
                std::string ent;
                while (!eof() && peek() != ';') ent += get();
                if (!eof()) get(); // consume ';'
                if (ent == "amp") v += '&';
                else if (ent == "lt") v += '<';
                else if (ent == "gt") v += '>';
                else if (ent == "quot") v += '"';
                else if (ent == "apos") v += '\'';
                else v += "&" + ent + ";";
            } else {
                v += c;
            }
        }
        if (eof()) fail("unterminated attribute value");
        get(); // closing quote
        return v;
    }

    XmlNode parseElement() {
        size_t startLine = line;
        get(); // '<'
        std::string name = parseName();
        XmlNode node;
        node.name = name;
        node.line = startLine;

        // attributes
        while (true) {
            skipWs();
            char c = peek();
            if (c == '/' || c == '>') break;
            std::string aname = parseName();
            skipWs();
            if (peek() != '=') fail("expected '=' after attribute name");
            get();
            skipWs();
            if (peek() != '"' && peek() != '\'') fail("expected quoted attribute value");
            node.attrs.emplace_back(aname, parseAttrValue());
        }

        if (peek() == '/') { // self-closing
            get();
            expect(">");
            return node;
        }
        expect(">");

        // content: text + child elements until closing tag
        std::string text;
        while (true) {
            if (eof()) fail("unterminated element <" + name + ">");
            char c = peek();
            if (c == '<') {
                if (pos + 1 < s.size() && s[pos + 1] == '/') { // closing
                    get(); get();
                    std::string closeName = parseName();
                    if (closeName != name) fail("mismatched closing tag </" + closeName + "> for <" + name + ">");
                    skipWs();
                    expect(">");
                    break;
                }
                if (pos + 1 < s.size() && s[pos + 1] == '!') {
                    if (pos + 3 < s.size() && s[pos + 2] == '-' && s[pos + 3] == '-') {
                        skipComment();
                        continue;
                    }
                    fail("unexpected '<!' in content");
                }
                if (pos + 1 < s.size() && s[pos + 1] == '?') {
                    skipDeclaration();
                    continue;
                }
                // child element
                node.children.push_back(parseElement());
            } else if (c == '&' && pos + 1 < s.size()) {
                // decode the 5 basic entities in text
                get(); // consume '&'
                std::string ent;
                while (!eof() && peek() != ';') ent += get();
                if (!eof()) get(); // consume ';'
                if (ent == "amp") text += '&';
                else if (ent == "lt") text += '<';
                else if (ent == "gt") text += '>';
                else if (ent == "quot") text += '"';
                else if (ent == "apos") text += '\'';
                else text += "&" + ent + ";";
            } else {
                text += get();
            }
        }
        node.text = text;
        return node;
    }

    XmlNode parseDocument() {
        skipWs();
        // optional declaration
        if (peek() == '<' && pos + 1 < s.size() && s[pos + 1] == '?') {
            skipDeclaration();
            skipWs();
        }
        // optional comments before root
        while (peek() == '<' && pos + 3 < s.size() && s[pos + 1] == '!' && s[pos + 2] == '-' && s[pos + 3] == '-') {
            skipComment();
            skipWs();
        }
        XmlNode root = parseElement();
        skipWs();
        if (!eof()) fail("trailing content after root element");
        return root;
    }
};

} // namespace

XmlNode ParseXml(const std::string& text) {
    Parser p(text);
    return p.parseDocument();
}

} // namespace eaw
