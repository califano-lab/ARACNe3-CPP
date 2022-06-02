#include "ARACNe3.hpp"

using namespace std;
// inferred while reading txt files.  Rcpp will have to compensate
uint32_t size_of_network = 0;
uint16_t tot_num_samps = 0;
uint16_t tot_num_regulators = 0;
bool prune_FDR = false;
bool prune_MaxEnt = false;

/*
 Convenient function for timing parts of ARACNe3.  Will set last.
 */
static auto last = std::chrono::high_resolution_clock::now(), cur = std::chrono::high_resolution_clock::now();
static void sinceLast() {
	cur = std::chrono::high_resolution_clock::now();
	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(cur-last).count() << "ms" << std::endl;
	last = cur;
}

/*
 * Assumes that simply the path to the regulator list and the path to the gene
 * expression matrix are commandline arguments
 *
 * e.g. ./ARACNe3 test/regfile.txt test/matrixfile.txt
 */
int main(int argc, char *argv[]) {
	prune_FDR = true;
	prune_MaxEnt = true;
	
	readRegList(string(argv[1]));
	genemap matrix = readTransformedGexpMatrix(string(argv[2]));
	size_of_network = static_cast<uint32_t>(tot_num_regulators*matrix.size()-tot_num_regulators);
	
	//-------time module-------
	cout << "NULL MI MODEL TIME:" << endl;
	last = chrono::high_resolution_clock::now();
	//-------------------------
	
	initNullMIs(tot_num_samps);
	
	//-------time module-------
	sinceLast();
	//-------------------------
	
	//-------time module-------
	cout << "RAW NETWORK COMPUTATION TIME:" << endl;
	last = chrono::high_resolution_clock::now();
	//-------------------------
	
	reg_web network;
	network.reserve(tot_num_regulators);
	for (reg_id_t reg = 0; reg < tot_num_regulators; ++reg) {
		network[reg] = genemapAPMI(matrix, reg, 7.815, 4);
		
	}
	
	//-------time module-------
	sinceLast();
	cout << "SIZE OF NETWORK: " << size_of_network << " EDGES." << endl;
	//-------------------------
	
	if (prune_FDR) {
		//-------time module-------
		cout << "FDR PRUNING TIME:" << endl;
		last = chrono::high_resolution_clock::now();
		//-------------------------
		/*
		 We could prune in-network, but that would require many search operations.  It is better to extract edges and reform the entire network, then free memory, it seems.
		 */
		network = pruneFDR(network, size_of_network, 0.05f);
		
		//-------time module-------
		sinceLast();
		cout << "SIZE OF NETWORK: " << size_of_network << " EDGES." << endl;
		//-------------------------
		
		if (prune_MaxEnt) {
			// No time modules because they are embedded in pruneMaxEnt
			network = pruneMaxEnt(network);
		}
	}
	
	//-------time module-------
	cout << "PRINTING NETWORK REG-TAR-MI TIME:" << endl;
	last = chrono::high_resolution_clock::now();
	//-------------------------
	
	printNetworkRegTarMI(network, "output.txt");
	
	//-------time module-------
	sinceLast();
	//-------------------------
}
