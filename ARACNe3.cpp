#include "ARACNe3.hpp"

/*
 These variables are tuned according to user preferences.  Some of these the user doesn't choose, such as the cached_dir, which is always the working directory of the ARACNe3 script.
 */
bool prune_alpha = true;
bool adaptive = false;
bool do_not_consolidate = false;
bool go_to_consolidate = false;
float alpha = 0.05f;
double subsampling_percent = 1 - std::exp(-1);
bool prune_MaxEnt = true;
std::string cached_dir;
std::string output_dir;
std::string log_dir;
std::string subnets_dir;
std::string method = "FDR";
float DEVELOPER_mi_cutoff = 0.0f;
uint16_t num_subnets = 1U;
uint16_t num_subnets_to_consolidate = 0U;
uint16_t targets_per_regulator = 30U;
uint16_t nthreads = 1U;

uint32_t global_seed = 0U;

/*
 These variables represent the original data and do not change after matrix files are read.
 */
extern uint16_t tot_num_samps;
extern uint16_t tot_num_subsample;
extern uint16_t tot_num_regulators;
extern uint16_t defined_regulators;
extern genemap global_gm;

extern uint32_t num_null_marginals;

extern float FPR_estimate;
extern std::vector<float> FPR_estimates;

/*
 Convenient function for timing parts of ARACNe3.  It's only used to time from the pipeline function, so it's included in ARACNe3.cpp.
 */
static void sinceLast(decltype(std::chrono::high_resolution_clock::now()) &last, std::ostream &ostream) {
	auto cur = std::chrono::high_resolution_clock::now();
	ostream << std::chrono::duration_cast<std::chrono::seconds>(cur-last).count() << "s" << std::endl;
	last = cur;
}

/*
 This function is the ARACNe3 main pipeline, called from main().  The main function just parses command line arguments and options, and it sets global variables, before calling the ARACNe3 function here.
 */
reg_web ARACNe3_subnet(genemap subnet_matrix, const uint16_t& subnet_num) {
	auto last = std::chrono::high_resolution_clock::now();
	
	// set the individual subnet log file
	std::ofstream log_output(log_dir + "log_subnet" + std::to_string(subnet_num) + ".txt");
	
	/*
	 Log file header
	 */
	std::time_t t = std::time(nullptr);
	log_output << "---------" << std::put_time(std::localtime(&t), "%c %Z") << "---------" << std::endl << std::endl;
	log_output << "Subnetwork #: " + std::to_string(subnet_num) << std::endl;
	log_output << "Total # regulators (with gexp profile defined): " + std::to_string(defined_regulators) << std::endl;
	log_output << "Total # targets: " + std::to_string(subnet_matrix.size()) << std::endl;
	log_output << "Total # samples: " + std::to_string(tot_num_samps) << std::endl;
	log_output << "Subsampled quantity: " + std::to_string(tot_num_subsample) << std::endl;
	log_output << "Total possible edges: " + std::to_string(defined_regulators*subnet_matrix.size()-defined_regulators) << std::endl;
	log_output << "Method of first pruning step: " + method << std::endl;
	log_output << "Alpha: " + std::to_string(alpha) << std::endl;
	log_output << "MaxEnt Pruning: " + std::to_string(prune_MaxEnt) << std::endl;
	log_output << std::endl << "-----------Begin Network Generation-----------" << std::endl;
	
	/*
	 Begin Network computation
	 */
	//-------time module-------
	log_output << std::endl << "Raw network computation time:" << std::endl;
	last = std::chrono::high_resolution_clock::now();
	//-------------------------
	
	
	uint32_t size_of_network = 0;
	std::vector<std::vector<edge_tar>> network_vec(tot_num_regulators); 
#pragma omp parallel for firstprivate(subnet_matrix) num_threads(nthreads)
	for (int reg = 0; reg < tot_num_regulators; ++reg) {
		if (global_gm.contains(reg)) {
			network_vec[reg] = genemapAPMI(subnet_matrix, reg, 7.815, 4);
			size_of_network += network_vec[reg].size();
		}
	}
	reg_web network;
	network.reserve(tot_num_regulators);
	for (gene_id_t reg = 0; reg < tot_num_regulators; ++reg)
		if (global_gm.contains(reg))
			network[reg] = network_vec[reg];
	std::vector<std::vector<edge_tar>>().swap(network_vec);
	
	//-------time module-------
	sinceLast(last, log_output);
	log_output << "Size of network: " << size_of_network << " edges." << std::endl;
	//-------------------------
	
	if (!prune_alpha) alpha = 1.01f; // we must set to 1.01f to preserve all edges; rounding issue.
	
	//-------time module-------
	log_output << std::endl << "Alpha/threshold pruning time (" + method + "): " << std::endl;
	last = std::chrono::high_resolution_clock::now();
	//-------------------------
	
	auto size_prev = size_of_network;
	
	/*
	 We could prune in-network, but that would require many search operations.  It is better to extract edges and reform the entire network, then free memory, it seems.
	 */
	
	std::pair<reg_web, map_map> pair = pruneAlpha(network, size_of_network);
	network = pair.first;
	map_map& tftfNetwork = pair.second;
	
	//-------time module-------
	sinceLast(last, log_output);
	log_output << "Edges removed: " << size_prev - size_of_network << " edges." << std::endl;
	log_output << "Size of network: " << size_of_network << " edges." << std::endl;
	//-------------------------
	
	/*
	 Save for binomial theta
	 */
	uint32_t num_edges_after_threshold_pruning = size_of_network; 
	
	if (prune_MaxEnt) {
		//-------time module-------
		log_output << std::endl << "MaxEnt pruning time:" << std::endl;
		last = std::chrono::high_resolution_clock::now();
		//-------------------------

		size_prev = size_of_network;
		
		network = pruneMaxEnt(network, tftfNetwork, size_of_network);
		
		//-------time module-------
		sinceLast(last, log_output);
		log_output << "Edges removed: " << size_prev - size_of_network << " edges." << std::endl;
		log_output << "Size of network: " << size_of_network << " edges." << std::endl;
		//-------------------------
		
		uint32_t num_edges_after_MaxEnt_pruning = size_of_network;
		if (method == "FDR")
			FPR_estimates.emplace_back((alpha*num_edges_after_MaxEnt_pruning)/(defined_regulators*global_gm.size()-(1-alpha)*num_edges_after_threshold_pruning));
		else if (method == "FWER")
			FPR_estimates.emplace_back((alpha/(defined_regulators*(global_gm.size()-1)))*(num_edges_after_MaxEnt_pruning)/(num_edges_after_threshold_pruning));
	} else {
		if (method == "FDR")
			FPR_estimates.emplace_back((alpha*num_edges_after_threshold_pruning)/(defined_regulators*global_gm.size()-(1-alpha)*num_edges_after_threshold_pruning));
		else if (method == "FWER")
			FPR_estimates.emplace_back(alpha/(defined_regulators*(global_gm.size()-1)));
	}
	
	//-------time module-------
	log_output << std::endl << "Printing network in directory \"" + makeUnixDirectoryNameUniversal(output_dir) + "\"....." << std::endl;
	last = std::chrono::high_resolution_clock::now();
	//-------------------------
	
	// writes the individual subnet output
	writeNetworkRegTarMI(network, subnets_dir, "subnet" + std::to_string(subnet_num));
	
	//-------time module-------
	sinceLast(last, log_output);
	//-------------------------
	
	std::cout << "... subnetwork " + std::to_string(subnet_num) + " completed = " + std::to_string(size_of_network) + " edges returned ..." << std::endl;
	
	return network;
}

//--------------------cmd line parser------------------------

char* getCmdOption(char **begin, char **end, const std::string &option) {
	char **itr = std::find(begin, end, option);
	if (itr != end && ++itr != end)
		return *itr;
	return 0;
}

bool cmdOptionExists(char **begin, char **end, const std::string &option) {
	return std::find(begin, end, option) != end;
}

//-----------------------------------------------------------


/*
 Main function is the command line executable; this primes the global variables and parses the command line.  It will also return usage notes if the user incorrectly calls ./ARACNe3.
 
 Example:
 ./ARACNe3 -e test/matrix.txt -r test/regulators.txt -o test/output --noAlpha -a 0.05 --alpha 0.05 --noMaxEnt --subsample 0.6321 --seed 1 --mithresh 0.2 --numnulls 1000000
 */
int main(int argc, char *argv[]) {
	auto last = std::chrono::high_resolution_clock::now();
	
	if (cmdOptionExists(argv, argv+argc, "-h") || cmdOptionExists(argv, argv+argc, "--help") || !cmdOptionExists(argv, argv+argc, "-e") || !cmdOptionExists(argv, argv+argc, "-r") || !cmdOptionExists(argv, argv+argc, "-o")) {
		std::cout << "usage: " + ((std::string) argv[0]) + makeUnixDirectoryNameUniversal(" -e path/to/matrix.txt -r path/to/regulators.txt -o path/to/output/directory") << std::endl;
		return 1;
	}
	
	//--------------------cmd line parsing------------------------
	
	std::string exp_file = (std::string) getCmdOption(argv, argv+argc, "-e");
	std::string reg_file = (std::string) getCmdOption(argv, argv+argc, "-r");
	
	output_dir = (std::string) getCmdOption(argv, argv+argc, "-o");
	
	// make sure output_dir has a trailing slash
	if (output_dir.back() != directory_slash)
	    output_dir += directory_slash;
	
	if (cmdOptionExists(argv, argv+argc, "--alpha"))
		alpha = std::stof(getCmdOption(argv, argv+argc, "--alpha"));
	if (alpha >= 1.00f || alpha <= 0) {
		std::cout << "alpha not on range [0,1], setting to 1.00" << std::endl;
		alpha = 1.01f;
	}
	
	if (cmdOptionExists(argv, argv+argc, "--seed"))
		global_seed = std::stoi(getCmdOption(argv, argv+argc, "--seed"));
	
	if (cmdOptionExists(argv, argv+argc, "--subsample"))
		subsampling_percent = std::stod(getCmdOption(argv, argv+argc, "--subsample"));
	
	if (subsampling_percent > 1.0000001 || subsampling_percent <= 0) {
			std::cout << "Subsampling percent not on range (0,1]; setting to 1.00." << std::endl;
			subsampling_percent = 1.00;
	}
	
	if (cmdOptionExists(argv, argv+argc, "-x")) 
		num_subnets = targets_per_regulator = num_subnets_to_consolidate = std::stoi(getCmdOption(argv, argv+argc, "-x"));

	if (cmdOptionExists(argv, argv+argc, "--threads"))
		nthreads = std::stoi(getCmdOption(argv, argv+argc, "--threads"));

	if (cmdOptionExists(argv, argv+argc, "--noAlpha"))
	    	prune_alpha = false;
	if (cmdOptionExists(argv, argv+argc, "--noMaxEnt"))
	    	prune_MaxEnt = false;
	if (cmdOptionExists(argv, argv+argc, "--FDR"))
		method = "FDR";
	if (cmdOptionExists(argv, argv+argc, "--FWER"))
		method = "FWER";
	if (cmdOptionExists(argv, argv+argc, "--adaptive"))
		adaptive = true;
	if (cmdOptionExists(argv, argv+argc, "--noconsolidate"))
		do_not_consolidate = true;
	if (cmdOptionExists(argv, argv+argc, "--consolidate"))
		go_to_consolidate = true;

	//----------------------DEVELOPER--------------------------
	
	if (cmdOptionExists(argv, argv+argc, "--mithresh"))
		DEVELOPER_mi_cutoff = std::stof(getCmdOption(argv, argv+argc, "--mithresh"));
	if (DEVELOPER_mi_cutoff < 0)
		DEVELOPER_mi_cutoff = 0.0f;
	
	if (cmdOptionExists(argv, argv+argc, "--numnulls"))
		num_null_marginals = std::stoi(getCmdOption(argv, argv+argc, "--numnulls"));
	if (num_null_marginals < 0) {
		std::cout << "Number of null marginals not on range (0,inf); setting to 1000000." << std::endl;
		num_null_marginals = 1000000;
	}
	
	//------------------------------------------------------------

	cached_dir = "./"+ hiddenfpre + "ARACNe3_cached/";
	
	makeDir(output_dir);
	makeDir(cached_dir);
	
	log_dir = output_dir + "log/";
	makeDir(log_dir);
	
	subnets_dir = output_dir + "subnets/";
	makeDir(subnets_dir);
	
	//-------time module-------
	last = std::chrono::high_resolution_clock::now();
	//-------------------------

	std::ofstream log_output(output_dir + "finalLog.txt");
	
	// print the initial command to the log output
	for (uint16_t i = 0; i < argc; ++i)
		log_output << std::string(argv[i]) << " ";
	log_output << std::endl;
	
	std::time_t t = std::time(nullptr);
	std::cout << "\n---------" << std::put_time(std::localtime(&t), "%c %Z") << "---------" << std::endl;
	log_output << "\n---------" << std::put_time(std::localtime(&t), "%c %Z") << "---------" << std::endl;
	
	std::cout << "Beginning ARACNe3 instance.  See logs and progress reports in \"" + makeUnixDirectoryNameUniversal(output_dir) + "finalLog.txt\"." << std::endl;
	log_output << "Beginning ARACNe3 instance..." << std::endl;
	
	readRegList(reg_file);
	
	readExpMatrix(exp_file);
	
	//-------time module-------
	log_output << std::endl << "Gene expression matrix & regulators list read time:" << std::endl;
	sinceLast(last, log_output);
	//-------------------------
	
	//-------time module-------
	log_output << std::endl << "Mutual Information null model calculation time:" << std::endl;
	last = std::chrono::high_resolution_clock::now();
	//-------------------------
	
	initNullMIs(tot_num_subsample);
	
	//-------time module-------
	sinceLast(last, log_output);
	//-------------------------
	
	// Must exist regardless of whether we skip to consolidation
	std::vector<reg_web> subnets;
	if(!go_to_consolidate) {
		//-------time module-------
		log_output << std::endl << "Creating subnetwork(s) time: " << std::endl;
		//-------------------------
		
		if (adaptive) {
			bool stoppingCriteriaMet = false;
			std::unordered_map<gene_id_t, std::unordered_set<gene_id_t>> regulon_set;
			for (uint16_t reg = 0; reg < tot_num_regulators; ++reg) regulon_set[reg];
			uint16_t i = 0U;
			while (!stoppingCriteriaMet) {
				genemap subnet_matrix = sampleFromGlobalGenemap();
				subnets.push_back(ARACNe3_subnet(subnet_matrix, i+1));
				
				// add any new edges to the regulon_set
				for (const auto &[reg, edge_tars] : subnets[i])
					for (const auto &edge : edge_tars)
						regulon_set[reg].insert(edge.target);
				
				// check stoping criteria
				uint16_t min = 65535U;
				for (const auto &[reg, regulon] : regulon_set)
					if (global_gm.contains(reg))
						if (regulon.size() < min)
							min = regulon.size();
				if (min >= targets_per_regulator) 
					stoppingCriteriaMet = true;
				
				++i;
			}
			num_subnets = i;
		} else {
			if (nthreads > 1 && num_subnets > 1)
				std::cout << "Note: Because more than one thread is used to compute a fixed number of subnetworks (--adaptive not specified), completion times may not be in order.  This is not an error.\n" << std::endl;
			subnets = std::vector<reg_web>(num_subnets);
			for (int i = 0; i < num_subnets; ++i) {
				genemap subnet_matrix = sampleFromGlobalGenemap();
				subnets[i] = ARACNe3_subnet(subnet_matrix, i+1);
			}
		}
		
		//-------time module-------
		sinceLast(last, log_output);
		//-------------------------
		log_output << "Total subnetworks generated: " + std::to_string(num_subnets) << std::endl;
	} else if (go_to_consolidate) {
		//-------time module-------
		log_output << std::endl << "Reading subnetwork(s) time: " << std::endl;
		//-------------------------
		
		for (uint16_t subnet_num = 1; subnet_num <= num_subnets_to_consolidate; ++subnet_num) {
			try {
				subnets.push_back(readSubNetAndUpdateFPRFromLog(output_dir, subnet_num));
				num_subnets = subnet_num;
			} catch (TooManySubnetsRequested e) {
				std::cout << "WARNING: " + std::string(e.what()) << std::endl;
				break;
			}
		}
		
		//-------time module-------
		sinceLast(last, log_output);
		//-------------------------
		log_output << "Total subnets read: " + std::to_string(num_subnets) << std::endl;
	}
	
	// set the FPR estimate
	FPR_estimate = std::accumulate(FPR_estimates.begin(), FPR_estimates.end(), 0.0f) / FPR_estimates.size();
	
	if (!do_not_consolidate) {	
		//-------time module-------
		log_output << std::endl << "Consolidating subnetwork(s) time: " << std::endl;
		//-------------------------
		
		std::vector<consolidated_df> final_df = consolidate_subnets_vec(subnets);
		
		//-------time module-------
		sinceLast(last, log_output);
		//-------------------------
		
		//-------time module-------
		log_output << std::endl << "Writing final network..." << std::endl;
		//-------------------------
		
		writeConsolidatedNetwork(final_df, output_dir + "finalNet_" + std::to_string(num_subnets) + "subnets.txt");
	} else if (do_not_consolidate) {
		//-------time module-------
		log_output << std::endl << "No consolidation requested." << std::endl;
		//-------------------------
	}
	
	/* Now that we know how many subnets, we can rename finalLog to include that. */
	std::string final_log_newname = "finalLog_" + std::to_string(num_subnets) + "subnets.txt";
	//-------time module-------
	log_output << std::endl << "Renaming \"finalLog.txt\" to \"" + final_log_newname + "\"..." << std::endl;
	std::cout << std::endl << "Renaming \"finalLog.txt\" to \"" + final_log_newname + "\"..." << std::endl;
	//-------------------------
	std::filesystem::rename(output_dir + "finalLog.txt", output_dir  + final_log_newname);
	
	using namespace std::string_literals;
	const char* success_A3 =
R"(

                |
                |
                ;                            
                ;                            
                |                            
           ,  / | \   ,
         , ;_/ ,L-, `_;  ,
         \._/.ARACNe3.\_./
           \_./(::)\._/                      
                ''


SUCCESS!
)";
		std::cout << success_A3 << std::endl;
		log_output << success_A3 << std::endl;
	
	return 0;
}
