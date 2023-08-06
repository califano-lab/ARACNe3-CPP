#include "ARACNe3.hpp"
#include <omp.h>

// for MaxEnt pruning, regulator -> regulator -> mi
extern uint16_t tot_num_regulators;
extern uint16_t nthreads;
extern bool adaptive;


/*
 Prune the network according to the MaxEnt weakest-edge reduction.
 */
gene_to_edge_tars pruneMaxEnt(gene_to_edge_tars& network, gene_to_gene_to_float tftfNetwork, uint32_t &size_of_network) {
	/* Since the same weakest edge may be identified multiple times, we use std::set to store the edges we know to remove, so that they are all removed at the end.  However, since the same set is being accessed by multiple threads at once, this creates clashing errors.  Hence, for now, each thread gets its own set in the vector below, which will be 'sifted' through by the master thread.
	 */
	std::vector<std::vector<std::set<gene_id>>> removedEdgesForThread(nthreads, std::vector<std::set<gene_id>>(tot_num_regulators));
	for (gene_id reg = 0; reg < tot_num_regulators; ++reg)
		for (uint16_t th = 0; th < nthreads; ++th)
			removedEdgesForThread[th][reg] = std::set<gene_id>(); // TODO: Can we remove this initialization entirely?
	
	/* Our 'network' is a hash map of vectors that contain the MI and target information.  This is good for several of our uses, but it is better to make a hash map of hashmaps for the MaxEnt pruning, as in ARACNe-AP.  This is because finding whether a pair of regulators share a target is O(1), as we just hash the targets in the regulator hashmap, as opposed to searching for the target which would be necessary in the 'gene_to_edge_tars' type.
	 
	 The operation below converts between data structures.
	 */
			
	gene_to_gene_to_float finalNet = regweb_to_mapmap(network); 
	
#pragma omp parallel for firstprivate(finalNet, tftfNetwork) num_threads(nthreads) schedule(static,1) 
	// We schedule the parallelization like this because we have a triangular matrix.  reg2 is always reg1+1, so the first block of regulators will have the largest groups to iterate over under standard scheduling.
	for (int reg1 = 0; reg1 < tot_num_regulators; ++reg1) {
		if (tftfNetwork.find(reg1) != tftfNetwork.end()) {
			std::unordered_map<gene_id, float> &fin1 = finalNet[reg1];
			std::unordered_map<gene_id, float> &tft1 = tftfNetwork[reg1]; 
			std::set<gene_id> &rem1 = removedEdgesForThread[omp_get_thread_num()][reg1];
			for (gene_id reg2 = reg1 + 1; reg2 < tot_num_regulators; ++reg2) {
				if (tft1.find(reg2) != tft1.end()) {
					std::unordered_map<gene_id, float> &fin2 = finalNet[reg2];
					std::set<gene_id> &rem2 = removedEdgesForThread[omp_get_thread_num()][reg2];
					const float &tftfMI = tft1[reg2];
					for(const auto &[target, v2] : fin2) {
						if (fin1.find(target) != fin1.end()) {
							const float v1 = fin1[target];
							if (v1 < tftfMI && v1 < v2)
								rem1.insert(target);
							else if (v2 < tftfMI && v2 < v1)
								rem2.insert(target);
							else {
								rem1.insert(reg2);
								rem2.insert(reg1);
							}
						}
					}
				}
			}
		}
	}
	
	/* This is currently how all the vectors of sets are consolidated.  It 'collapses' the thread dimension, though this is an inefficiency.
	 */
	std::vector<std::set<gene_id>> removedEdges(tot_num_regulators);
	for (gene_id reg = 0; reg < tot_num_regulators; ++reg)
		for (uint16_t th = 0; th < nthreads; ++th)
			for (const auto &tar : removedEdgesForThread[th][reg])
				removedEdges[reg].insert(tar);
	
	for (const auto &removedSet : removedEdges) 
		 size_of_network -= removedSet.size();
	
	gene_to_edge_tars pruned_net;
	pruned_net.reserve(tot_num_regulators);
	for (const auto &[reg, tarmap] : finalNet) {
		pruned_net[reg].reserve(network[reg].size());
		std::set<gene_id> &rem = removedEdges[reg];
		for (const auto &[tar, mi] : tarmap)
			if (rem.find(tar) == rem.end())
				pruned_net[reg].emplace_back(tar, mi);
	}

	return pruned_net;
}
