// PerceptionSystem — the AI's perception-equation engine.
//
// The game's AI scoring (docs/research/06-threading-design.md: "Perception
// evaluation is PARTIALLY NOW") is driven by perception equations: XML
// blocks of arithmetic expressions over perception tokens, evaluated per
// candidate target. This module parses the equation DSL (the format used by
// AI/PERCEPTUALEQUATIONS/*.XML) into ASTs and evaluates them against the
// sim.
//
// Token surface (matching the game's equation files):
//   numbers, arithmetic (+ - * /, parens)
//   comparisons (> < >= <= ==) yielding 1.0 / 0.0
//   Variable_Self.<query>     — the evaluating unit/force
//   Variable_Target.<query>   — the candidate target
//   Game.Age                  — sim time in seconds
//   <query> { Parameter_* = ... }  — parameterized calls (category/type
//                                    filters, hardpoint types)
//   Function_<name>.Evaluate  — evaluate another named equation (chaining)
//   Variable_Self.IsTargetingPriority  — 1.0 when the candidate's category is
//                                    the self object's top targeting priority
//                                    (Set_Targeting_Priorities), else 0.0.
//                                    Lets mod perception equations consume the
//                                    priority tables; the engine's own scoring
//                                    applies the same tables as a tie-break.
//
// Queries supported over the sim's object model: Health, Shield, Force
// (unit count / hull sum in range), FriendlyForce/EnemyForce (by category
// filter), DistanceToNearestFriendly/Enemy, IsType, IsDefender, BaseLevel,
// ContainsHero, TimeLastSeen, Hints.*, Location.* (delegates to the force
// or planet), plus category masks in GameObjectCategoryType[...].
//
// Evaluations are pure reads over the sim — safe to run in parallel across
// candidates (design doc: read-shared, write-partitioned).
#pragma once

#include "core/job_system.h"
#include "core/object_model.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

struct PerceptionContext {
    const SimState* sim = nullptr;
    const GameObject* self = nullptr;   // Variable_Self (may be null)
    const GameObject* target = nullptr; // Variable_Target (may be null)
    const TaskForce* selfForce = nullptr;
    const TaskForce* targetForce = nullptr;
    double gameAge = 0.0;
    bool isCampaign = false;
};

class PerceptionSystem {
public:
    // Parses an <Equations> XML document (as shipped in the game). Returns
    // the number of equations parsed. Throws XmlError/PerceptionError on
    // malformed input.
    int loadEquations(const std::string& xmlText);

    // Evaluates a named equation against the context. Returns the score.
    // Unknown equations or null required variables evaluate to 0.0
    // (matching the game's tolerant behavior).
    double evaluate(const std::string& name, const PerceptionContext& ctx) const;

    // Names of all loaded equations (for tooling/tests).
    std::vector<std::string> equationNames() const;

private:
    struct Expr;
    using ExprPtr = std::unique_ptr<Expr>;

    struct Expr {
        enum class Kind { Num, Query, FnCall, Binary, Unary, Chain };
        Kind kind = Kind::Num;
        double num = 0.0;
        std::string query;              // Query / FnCall / Chain root
        std::vector<std::pair<std::string, std::string>> params; // FnCall args
        char op = 0;                    // Binary/Unary operator
        ExprPtr l, r;                   // Binary children / FnCall args
        std::string fnName;             // Chain: Function_<name>
        double eval(const PerceptionSystem& sys, const PerceptionContext& ctx) const;
    };

    // Parser state.
    struct Parser {
        const std::string& s;
        size_t pos = 0;
        explicit Parser(const std::string& src) : s(src) {}
        bool eof() const;
        char peek() const;
        void skipWs();
        std::string parseIdent();       // [A-Za-z_][A-Za-z0-9_.]*
        ExprPtr parseExpr();            // + -
        ExprPtr parseTerm();            // * /
        ExprPtr parseFactor();          // atom, comparison handled here
        ExprPtr parseAtom();            // number, ident-path, parens
        ExprPtr parseCall();            // ident { params } / ident(args?) chain
    };

    // Query evaluation over the sim (pure reads).
    double evalQuery(const std::string& query, const PerceptionContext& ctx,
                     const std::vector<std::pair<std::string, std::string>>& params) const;
    double evalFn(const std::string& fnName, const PerceptionContext& ctx) const;

    std::unordered_map<std::string, ExprPtr> equations_;
    std::vector<std::string> order_; // parse order (determinism)
};

} // namespace eaw
