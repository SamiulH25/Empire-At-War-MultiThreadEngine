// Unit tests for the XML parser and GameConstants loader.
#include "core/xml.h"
#include "core/game_constants.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

void testXmlBasics() {
    auto root = eaw::ParseXml(
        "<?xml version=\"1.0\"?>"
        "<GameConstants>"
        "  <!-- comment -->"
        "  <Debug_Hot_Key_Load_Map> .\\\\map.ted </Debug_Hot_Key_Load_Map>"
        "  <Empty/>"
        "  <WithAttr a=\"1\" b='x'>text</WithAttr>"
        "</GameConstants>");
    check(root.name == "GameConstants", "root name");
    check(root.children.size() == 3, "3 children");
    check(root.children[0].name == "Debug_Hot_Key_Load_Map", "child 0 name");
    check(root.children[0].text.find("map.ted") != std::string::npos, "child 0 text");
    check(root.children[1].name == "Empty" && root.children[1].children.empty(), "self-closing tag");
    check(root.children[2].attr("a") == "1", "attr a");
    check(root.children[2].attr("b") == "x", "attr b single quote");
    check(root.children[2].attr("missing") == "", "missing attr empty");
    check(root.children[2].text == "text", "element text");
}

void testXmlEntities() {
    auto root = eaw::ParseXml("<R><V a=\"&lt;&amp;&gt;\">&amp;</V></R>");
    check(root.firstChild("V")->attr("a") == "<&>", "attr entities");
    check(root.firstChild("V")->text == "&", "text entity");
}

void testXmlNested() {
    auto root = eaw::ParseXml("<A><B><C>x</C></B><B><C>y</C></B></A>");
    auto bs = root.childrenNamed("B");
    check(bs.size() == 2, "two B children");
    check(bs[0]->firstChild("C")->text == "x", "nested C text");
}

void testXmlErrors() {
    bool threw = false;
    try { eaw::ParseXml("<A><B></A>"); }
    catch (const eaw::XmlError&) { threw = true; }
    check(threw, "mismatched close throws");

    threw = false;
    try { eaw::ParseXml("<A>"); }
    catch (const eaw::XmlError&) { threw = true; }
    check(threw, "unterminated throws");
}

void testGameConstants() {
    std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<GameConstants>\n"
        "  <SpacePathfindMaxExpansions> 3500 </SpacePathfindMaxExpansions>\n"
        "  <SpacePathfindFrameDelayDelta> 2 </SpacePathfindFrameDelayDelta>\n"
        "  <SpacePathFailureMaxExpansionsCoefficient> 1.7 </SpacePathFailureMaxExpansionsCoefficient>\n"
        "  <FramesPerCollisionCheck> 4 </FramesPerCollisionCheck>\n"
        "  <Strategic_Queue_Tactical_Battles> True </Strategic_Queue_Tactical_Battles>\n"
        "</GameConstants>\n";
    auto gc = eaw::GameConstants::Parse(xml);
    check(gc.spacePathfindMaxExpansions == 3500, "max expansions");
    check(gc.spacePathfindFrameDelayDelta == 2, "frame delay");
    check(gc.spacePathFailureMaxExpansionsCoefficient == 1.7, "failure coefficient");
    check(gc.framesPerCollisionCheck == 4, "frames per collision check");
    check(gc.values.at("Strategic_Queue_Tactical_Battles") == "True", "generic pass-through");
}

} // namespace

int main() {
    testXmlBasics();
    testXmlEntities();
    testXmlNested();
    testXmlErrors();
    testGameConstants();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
