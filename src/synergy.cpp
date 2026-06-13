#include "synergy.h"

#include <set>

AllianceEvaluation evaluate_alliance(const std::vector<std::string>& teams,
                                     const std::map<std::string, double>& oprs,
                                     const std::map<std::string, TeamRole>& roles,
                                     double baseline_opr,
                                     const SynergyWeights& weights) {
    AllianceEvaluation eval;
    eval.teams = teams;
    if (teams.empty()) {
        return eval;
    }

    std::set<std::string> primary_roles;
    bool best_defense_set = false;
    for (const auto& team : teams) {
        auto opr_it = oprs.find(team);
        eval.predicted_score += opr_it == oprs.end() ? baseline_opr : opr_it->second;

        auto role_it = roles.find(team);
        if (role_it == roles.end()) {
            continue;
        }
        const TeamRole& role = role_it->second;
        eval.auto_total += role.auto_phase;
        eval.teleop_total += role.teleop_phase;
        eval.endgame_total += role.endgame_phase;
        eval.has_phase_data = eval.has_phase_data || role.has_phase_data;
        primary_roles.insert(role.primary);
        if (role.primary == "defense") {
            eval.has_defender = true;
        }
        if (role.primary == "endgame") {
            eval.endgame_specialists += 1;
        }
        // Track the strongest (lowest DPR) defender so the alliance's defensive
        // ceiling is visible.
        if (!best_defense_set || role.defense < eval.best_defense) {
            eval.best_defense = role.defense;
            best_defense_set = true;
        }
    }

    eval.role_diversity = static_cast<int>(primary_roles.size());
    eval.has_defense_data = best_defense_set;

    // Complementarity adjustment: reward role coverage and a dedicated defender,
    // and dock redundant endgame specialists (only so many high-value climbs).
    double adjustment = 0.0;
    if (eval.role_diversity > 1) {
        adjustment += weights.diversity * static_cast<double>(eval.role_diversity - 1);
    }
    if (eval.has_defender) {
        adjustment += weights.defender;
    }
    if (eval.endgame_specialists >= 2) {
        adjustment -= weights.endgame_redundancy
            * static_cast<double>(eval.endgame_specialists - 1);
    }
    eval.synergy_score = eval.predicted_score + adjustment;

    // Compose a short, human-readable summary of the lineup's character.
    if (eval.role_diversity >= 3) {
        eval.note = "well-rounded";
    } else if (eval.role_diversity == 2) {
        eval.note = "complementary";
    } else {
        eval.note = "single-role / redundant";
    }
    if (eval.has_defender) {
        eval.note += "; has defender";
    }
    if (eval.endgame_specialists >= 2) {
        eval.note += "; endgame-stacked";
    }
    return eval;
}
