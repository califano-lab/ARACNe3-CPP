/*
 Consolidator for ARACNe3.  Contains various functions only needed in the consolidation step, such as calculation of the p-value for an edge based on the number of subnetworks it appeared in, the calculation of the SCC for an edge, etc.
 */

#include "ARACNe3.hpp"

extern uint16_t tot_num_samps_pre_subsample;
extern uint16_t tot_num_samps;
extern uint16_t tot_num_regulators;
extern genemap global_gm;
extern genemap_r global_gm_r;
extern uint16_t num_subnets;

float consolidate_scc(const std::vector<uint16_t>& x_ranked, const std::vector<uint16_t>& y_ranked) {
	float sigma = 0, sigmaxy = 0, sigmasq = 0;
	for (uint16_t i = 0; i < x_ranked.size(); ++i) {
		sigma += x_ranked[i]; // same for x and y
		sigmaxy += x_ranked[i]*y_ranked[i];
		sigmasq += x_ranked[i]*x_ranked[i]; // same for x and y
	}
	return (x_ranked.size() * sigmaxy - sigma*sigma)/
			(float) (x_ranked.size() * sigmasq - sigma*sigma);
}

double lchoose(const uint16_t &n, const uint16_t &k) {
	return std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
}

double right_tail_binomial_p(const uint16_t& num_occurrences) {
	float theta = 1.5E-4f;
	double p = 0.0;
	if (num_subnets == 1)
		return std::numeric_limits<double>::quiet_NaN(); // cannot have a p-value for 1 subnet (1 network)
	for (uint16_t i = num_subnets; i >= num_occurrences; --i)
		p += std::exp(lchoose(num_subnets, num_occurrences) + num_occurrences * std::log(theta) + (num_subnets - num_occurrences) * std::log(1-theta));
	return p;
}

std::vector<consolidated_df> consolidate(std::vector<reg_web> &subnets) {
	std::vector<consolidated_df> final_df;
	const auto tot_poss_edgs = tot_num_regulators*global_gm.size()-tot_num_regulators;
	final_df.reserve(tot_poss_edgs);
	
	std::vector<map_map> subnets_mpmp;
	for (uint16_t i = 0; i < subnets.size(); ++i) {
		subnets_mpmp.emplace_back(regweb_to_mapmap(subnets[i]));
	}
	
	
		for (uint16_t reg = 0; reg < tot_num_regulators; ++reg) {
			for (uint16_t tar = 0; tar < global_gm.size(); ++tar) {
				uint16_t num_occurrences = 0;
				for (uint16_t sn = 0; sn < subnets.size(); ++sn) {
					if (subnets_mpmp[sn][reg].contains(tar))
						++num_occurrences;
				}
				if (num_occurrences > 0) {
					const float final_mi = APMI(global_gm[reg], global_gm[tar]);
					const float final_scc = consolidate_scc(global_gm_r[reg], global_gm_r[tar]);
					const double final_p = right_tail_binomial_p(num_occurrences);
					final_df.emplace_back(reg, tar, final_mi, final_scc, num_occurrences, final_p);
				}
		}
	}
	
	return final_df;
}
