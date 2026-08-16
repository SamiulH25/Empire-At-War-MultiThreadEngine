#include "core/perception.h"

#include "core/lua_host.h"
#include "core/xml.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace eaw {

namespace {

class PerceptionError : public std::runtime_error {
public:
    explicit PerceptionError(const std::string& msg) : std::runtime_error(msg) {}
};

// Category masks appear as GameObjectCategoryType[A | B | C] inside
// Parameter_Category values. We keep them as-is; matching checks membership.
bool categoryMatches(const std::vector<std::string>& cats,
                     const std::string& mask) {
    if (cats.empty()) return false;
    std::istringstream ss(mask);
    std::string tok;
    while (ss >> tok) {
        // strip pipes
        size_t b = tok.find_first_not_of("| \t");
        size_t e = tok.find_last_not_of("| \t");
        if (b == std::string::npos) continue;
        std::string one = tok.substr(b, e - b + 1);
        for (const auto& c : cats) {
            if (c == one) return true;
        }
    }
    return false;
}

bool isAllyPair(const SimState& sim, int a, int b) { return sim.isAlly(a, b); }

} // namespace

// ---- tokenizer / parser --------------------------------------------------

bool PerceptionSystem::Parser::eof() const { return pos >= s.size(); }

char PerceptionSystem::Parser::peek() const {
    return pos < s.size() ? s[pos] : '\0';
}

void PerceptionSystem::Parser::skipWs() {
    while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++pos;
}

std::string PerceptionSystem::Parser::parseIdent() {
    skipWs();
    std::string out;
    while (!eof()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            out += c;
            ++pos;
        } else {
            break;
        }
    }
    return out;
}

PerceptionSystem::ExprPtr PerceptionSystem::Parser::parseExpr() {
    ExprPtr l = parseTerm();
    skipWs();
    while (peek() == '+' || peek() == '-') {
        char op = peek();
        ++pos;
        ExprPtr r = parseTerm();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Binary;
        e->op = op;
        e->l = std::move(l);
        e->r = std::move(r);
        l = std::move(e);
        skipWs();
    }
    return l;
}

PerceptionSystem::ExprPtr PerceptionSystem::Parser::parseTerm() {
    ExprPtr l = parseFactor();
    skipWs();
    while (peek() == '*' || peek() == '/') {
        char op = peek();
        ++pos;
        ExprPtr r = parseFactor();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Binary;
        e->op = op;
        e->l = std::move(l);
        e->r = std::move(r);
        l = std::move(e);
        skipWs();
    }
    return l;
}

PerceptionSystem::ExprPtr PerceptionSystem::Parser::parseFactor() {
    // Comparisons bind looser than * / but tighter than + -; the game's
    // equations use them inside terms: (A > B), (X - Y > 0).
    ExprPtr l = parseAtom();
    skipWs();
    char c = peek();
    if (c == '>' || c == '<' || c == '=') {
        std::string op;
        if (c == '=' ) { op = "=="; ++pos; }
        else {
            op += c;
            ++pos;
            if (peek() == '=') { op += '='; ++pos; }
        }
        ExprPtr r = parseAtom();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Binary;
        e->query = "cmp";
        e->l = std::move(l);
        e->r = std::move(r);
        e->params.emplace_back("__op", op);
        return e;
    }
    return l;
}

PerceptionSystem::ExprPtr PerceptionSystem::Parser::parseAtom() {
    skipWs();
    if (peek() == '(') {
        ++pos;
        ExprPtr inner = parseExpr();
        skipWs();
        if (peek() != ')') throw PerceptionError("missing ')' in equation");
        ++pos;
        return inner;
    }
    if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
        // number
        size_t start = pos;
        while (!eof() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) ++pos;
        double v = std::strtod(s.c_str() + start, nullptr);
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Num;
        e->num = v;
        return e;
    }
    // identifier path (Variable_Self.Health, Game.Age, Function_X.Evaluate)
    std::string ident = parseIdent();
    if (ident.empty()) throw PerceptionError("unexpected character in equation");
    skipWs();
    if (peek() == '{') {
        // call with parameters: Query { Parameter_X = "v", ... }
        ++pos;
        std::vector<std::pair<std::string, std::string>> params;
        for (;;) {
            skipWs();
            if (peek() == '}') { ++pos; break; }
            std::string key = parseIdent();
            skipWs();
            if (peek() != '=') throw PerceptionError("expected '=' in call params");
            ++pos;
            skipWs();
            std::string val;
            if (peek() == '"') {
                ++pos;
                while (!eof() && peek() != '"') { val += peek(); ++pos; }
                if (!eof()) ++pos;
            } else {
                size_t start = pos;
                while (!eof() && peek() != ',' && peek() != '}') { ++pos; }
                val = s.substr(start, pos - start);
            }
            params.emplace_back(key, val);
            skipWs();
            if (peek() == ',') { ++pos; continue; }
            if (peek() == '}') { ++pos; break; }
        }
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::FnCall;
        e->query = ident;
        e->params = std::move(params);
        return e;
    }
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Chain;
    e->query = ident;
    return e;
}

// ---- evaluation ----------------------------------------------------------

double PerceptionSystem::Expr::eval(const PerceptionSystem& sys,
                                    const PerceptionContext& ctx) const {
    switch (kind) {
        case Kind::Num:
            return num;
        case Kind::Chain: {
            // Function_<name>.Evaluate chains to another equation.
            if (query.rfind("Function_", 0) == 0) {
                std::string name = query.substr(9);
                // strip trailing .Evaluate
                size_t dot = name.rfind(".Evaluate");
                if (dot != std::string::npos) name = name.substr(0, dot);
                return sys.evalFn(name, ctx);
            }
            return sys.evalQuery(query, ctx, {});
        }
        case Kind::FnCall:
            return sys.evalQuery(query, ctx, params);
        case Kind::Binary: {
            if (query == "cmp") {
                double a = l->eval(sys, ctx);
                double b = r->eval(sys, ctx);
                const std::string& op = params[0].second;
                if (op == ">") return a > b ? 1.0 : 0.0;
                if (op == "<") return a < b ? 1.0 : 0.0;
                if (op == ">=") return a >= b ? 1.0 : 0.0;
                if (op == "<=") return a <= b ? 1.0 : 0.0;
                if (op == "==") return a == b ? 1.0 : 0.0;
                return 0.0;
            }
            double a = l->eval(sys, ctx);
            double b = r->eval(sys, ctx);
            switch (op) {
                case '+': return a + b;
                case '-': return a - b;
                case '*': return a * b;
                case '/': return b != 0.0 ? a / b : 0.0;
            }
            return 0.0;
        }
        case Kind::Unary:
            return 0.0;
    }
    return 0.0;
}

double PerceptionSystem::evaluate(const std::string& name,
                                  const PerceptionContext& ctx) const {
    auto it = equations_.find(name);
    if (it == equations_.end()) return 0.0;
    return it->second->eval(*this, ctx);
}

double PerceptionSystem::evalFn(const std::string& name,
                                const PerceptionContext& ctx) const {
    auto it = equations_.find(name);
    if (it == equations_.end()) return 0.0;
    return it->second->eval(*this, ctx);
}

// Resolves the object a query path starts from: Variable_Self / Variable_Target
// / Game / Location.
const GameObject* resolveBase(const PerceptionContext& ctx, const std::string& path,
                              bool& isGame, bool& isLocation) {
    isGame = false;
    isLocation = false;
    if (path.rfind("Variable_Self.", 0) == 0) return ctx.self;
    if (path.rfind("Variable_Target.", 0) == 0) return ctx.target;
    if (path.rfind("Game.", 0) == 0) { isGame = true; return nullptr; }
    if (path.rfind("Location.", 0) == 0) { isLocation = true; return nullptr; }
    return nullptr;
}

double PerceptionSystem::evalQuery(
    const std::string& query, const PerceptionContext& ctx,
    const std::vector<std::pair<std::string, std::string>>& params) const {
    // Category filters from params.
    std::string catMask;
    for (const auto& [k, v] : params) {
        if (k == "Parameter_Category" || k == "Parameter_Type") {
            catMask = v;
        }
    }
    bool isGame = false, isLocation = false;
    const GameObject* obj = resolveBase(ctx, query, isGame, isLocation);
    if (isGame) {
        if (query == "Game.Age") return ctx.gameAge;
        if (query == "Game.IsCampaignGame") return ctx.isCampaign ? 1.0 : 0.0;
        return 0.0;
    }
    if (isLocation) {
        // Location.<X> delegates to the target force's planet or the target
        // object's position; for our tier, treat as target queries.
        std::string sub = query.substr(9);
        return evalQuery("Variable_Target." + sub, ctx, params);
    }
    if (!obj || !ctx.sim) return 0.0;
    const ObjectType* t = ctx.sim->type(obj->typeName);
    std::string field = query;
    // strip the Variable_Self./Variable_Target. prefix
    size_t dot = field.find('.');
    if (dot != std::string::npos) field = field.substr(dot + 1);

    if (field == "Health") return obj->hull;
    if (field == "Shield") return obj->shield;
    if (field == "IsDefender") return obj->ordersLocked ? 1.0 : 0.0;
    if (field == "BaseLevel") {
        return t ? t->techLevel : 0.0;
    }
    if (field == "ContainsHero") {
        for (int id : obj->garrisonedUnits) {
            const GameObject* g = ctx.sim->object(id);
            const ObjectType* gt = g ? ctx.sim->type(g->typeName) : nullptr;
            if (gt && gt->hero) return 1.0;
        }
        return 0.0;
    }
    if (field == "Force" || field == "FriendlyForce" || field == "EnemyForce" ||
        field == "ForceNBTD" || field == "EnemyForceNBTD") {
        // Sum of hull of nearby units (a force-strength measure). Friendly
        // vs enemy relative to the object's owner.
        double sum = 0.0;
        bool friendly = field == "FriendlyForce" || field == "Force";
        bool enemy = field == "EnemyForce" || field == "EnemyForceNBTD";
        for (const GameObject* o : ctx.sim->allObjects()) {
            if (!o->alive) continue;
            bool ally = isAllyPair(*ctx.sim, obj->playerId, o->playerId);
            const ObjectType* ot = ctx.sim->type(o->typeName);
            bool catOk = catMask.empty() || (ot && categoryMatches(ot->categories, catMask));
            if (!catOk) continue;
            if (friendly && ally) {
                if (obj->position.distanceTo(o->position) <= 2000.0) sum += o->hull;
            }
            if (enemy && !ally) {
                if (obj->position.distanceTo(o->position) <= 2000.0) sum += o->hull;
            }
        }
        return sum;
    }
    if (field == "DistanceToNearestFriendly" || field == "DistanceToNearestEnemy") {
        bool friendly = field == "DistanceToNearestFriendly";
        double best = 1e18;
        for (const GameObject* o : ctx.sim->allObjects()) {
            if (!o->alive || o->id == obj->id) continue;
            if (!catMask.empty()) {
                const ObjectType* ot = ctx.sim->type(o->typeName);
                if (!ot || !categoryMatches(ot->categories, catMask)) continue;
            }
            bool ally = isAllyPair(*ctx.sim, obj->playerId, o->playerId);
            if (friendly != ally) continue;
            double d = obj->position.distanceTo(o->position);
            if (d < best) best = d;
        }
        // No match: a huge distance (comparisons like "1000 > dist" fail),
        // matching the game's behavior of scoring nothing nearby.
        return best >= 1e17 ? 1e18 : best;
    }
    if (field == "IsType") {
        return t && t->name == catMask ? 1.0 : 0.0;
    }
    if (field == "TimeLastSeen" || field == "TimeLastSeenUnnormalized") {
        // Fog of war: time since the evaluating player saw the target.
        // (0 while visible, growing after it leaves sight.)
        if (ctx.self) {
            return ctx.sim->timeSinceSeen(ctx.self->playerId, obj->id, ctx.gameAge);
        }
        return 0.0;
    }
    if (field.rfind("Hints.", 0) == 0) {
        return 0.0; // story hints not modeled
    }
    if (field == "IsEnemyStartLocation") return 0.0;
    if (field == "HardPointHealth") return 0.0;
    return 0.0;
}

// ---- loading -------------------------------------------------------------

int PerceptionSystem::loadEquations(const std::string& xmlText) {
    XmlNode root = ParseXml(xmlText);
    if (root.name != "Equations") {
        throw PerceptionError("expected <Equations> root, got <" + root.name + ">");
    }
    int count = 0;
    for (const XmlNode& eq : root.children) {
        if (eq.name.empty() || eq.text.empty()) continue;
        Parser p(eq.text);
        try {
            ExprPtr e = p.parseExpr();
            p.skipWs();
            if (!p.eof()) {
                // tolerate trailing garbage (comments inside equations)
            }
            equations_[eq.name] = std::move(e);
            order_.push_back(eq.name);
            ++count;
        } catch (const PerceptionError&) {
            // skip malformed equations (the game is tolerant too)
        }
    }
    return count;
}

std::vector<std::string> PerceptionSystem::equationNames() const {
    return order_;
}

} // namespace eaw
