#pragma once

#include <map>
#include <string>
#include <vector>

#include "roles.h"

// A what-if evaluation of a hand-picked alliance. The headline `predicted_score`
// is the OPR-based estimate (sum of member contributions). `synergy_score` layers
// a transparent complementarity adjustment on top: alliances that cover more
// roles, carry a defender, and avoid stacking redundant endgame specialists are
// worth a little more than the raw sum suggests.
struct AllianceEvaluation {
    std::vector<std::string> teams;
    double predicted_score = 0.0;   // sum of member OPRs (imputed where unknown)
    double auto_total = 0.0;
    double teleop_total = 0.0;
    double endgame_total = 0.0;
    double best_defense = 0.0;      // lowest member DPR (the alliance's best defender)
    int role_diversity = 0;         // distinct primary roles among members
    bool has_defender = false;
    int endgame_specialists = 0;
    double synergy_score = 0.0;
    bool has_phase_data = false;
    std::string note;               // human-readable composition summary
};

// Weights for the complementarity adjustment. Small relative to the score so the
// OPR estimate stays the headline; these only break ties between similar lineups.
struct SynergyWeights {
    double diversity = 3.0;        // bonus per extra distinct role
    double defender = 2.0;         // bonus for having a defensive specialist
    double endgame_redundancy = 4.0; // penalty per redundant endgame specialist
};

// Evaluate a single alliance. `oprs` and `roles` come from the event; teams not
// present are imputed with `baseline_opr` so unknown picks are not understated.
AllianceEvaluation evaluate_alliance(const std::vector<std::string>& teams,
                                     const std::map<std::string, double>& oprs,
                                     const std::map<std::string, TeamRole>& roles,
                                     double baseline_opr,
                                     const SynergyWeights& weights = SynergyWeights{});
