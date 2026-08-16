// Minimal XML DOM parser for EAW data files.
//
// The game's XML (GameConstants.xml, unit/faction configs) is simple:
// elements, attributes, text, comments, XML declaration, self-closing tags.
// This parser is intentionally small and dependency-free; it does not
// implement the full XML spec (no DTDs, entities beyond the 5 basics,
// CDATA is treated as text).
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace eaw {

class XmlError : public std::runtime_error {
public:
    explicit XmlError(const std::string& msg) : std::runtime_error(msg) {}
};

struct XmlNode {
    std::string name;                 // element name
    std::vector<std::pair<std::string, std::string>> attrs; // attribute list
    std::string text;                 // concatenated character data
    std::vector<XmlNode> children;    // child elements
    size_t line = 0;                  // line in source (for errors)

    // Convenience accessors
    const XmlNode* firstChild(const std::string& n) const;
    std::vector<const XmlNode*> childrenNamed(const std::string& n) const;
    std::string attr(const std::string& key) const; // "" if missing
};

// Parses an XML document. Returns the root element node.
// Throws XmlError with line info on malformed input.
XmlNode ParseXml(const std::string& text);

} // namespace eaw
