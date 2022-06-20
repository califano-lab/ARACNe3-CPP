#include "ARACNe3.hpp"

using namespace std;

/*
 Compression (gene (string) to uint16_t) and decompression (uint16_t back to gene (string)) mapping.  When doing the matrix/regulator list IO, this application automatically compresses all gene identifiers into unsigned short (2B) 0-65535, as this substantially can decrease the memory load of data structures which must copy the initial values (strings, for the gene identifiers these are anywhere from 5B-25B), or refer to them through pointers (8B).  Reading less memory also can speed up computation.
 */
static std::unordered_map<std::string, uint16_t> compression_map;
static std::vector<std::string> decompression_map;

/*
 Global variables are passed from ARACNe3.cpp, which are the user-defined parameters.
 */
uint16_t tot_num_samps_pre_subsample = 0;
uint16_t tot_num_samps = 0;
uint16_t tot_num_regulators = 0;
genemap global_gm;

extern bool verbose;
extern uint32_t global_seed;
extern uint16_t num_subnets;
extern double subsampling_percent;

/*
 Will automatically checked if there already is a directory.  Then creates the output directory.
 */
void makeDir(const std::string &dir_name) {
	if (!std::filesystem::exists(dir_name)) {
		if (std::filesystem::create_directory(dir_name)) {
			if (verbose) std::cout << "Directory Created: " + dir_name << std::endl;
		} else {
			std::cerr << "failed to create directory: " + dir_name << std::endl;
			std::exit(2);
		}
	}
	return;
}

/*
 A ranking is formed in the following way.  Indices index = [1,size) are sorted based on the ranking of vec[index-1], so that we get some new sorted set of indexes (5, 2, 9, ... ) that is the rank of each element in vec.  Rank is 1 for smallest, size for largest.
 
 For a lambda function, brackets indicate the scope of the function.
 */
std::vector<uint16_t> rank_vals(const std::vector<float>& vec) {
	static std::mt19937 rd{global_seed++};
	std::vector<uint16_t> rank_vec(vec.size());
	std::iota(rank_vec.begin(), rank_vec.end(), 1U); /* 1, 2, ..., size */
	std::sort(rank_vec.begin(), rank_vec.end(), [&vec](const uint16_t &num1, const uint16_t &num2) -> bool { return vec[num1-1] != vec[num2-1] ? vec[num1-1] < vec[num2-1] : rd() % 2;}); /* sort ascending */
	return rank_vec;
}


/*
 Reads a newline-separated regulator list and sets the decompression mapping, as well as the compression mapping, as file static variables hidden to the rest of the app.
 */
void readRegList(std::string filename) {
	std::fstream f {filename};
	std::vector<std::string> regs;
	
	if (!f.is_open()) {
        	std::cerr << "error: file open failed " << filename << ".\n";
		std::exit(2);
	}
	
	std::string reg;
	
	while (std::getline(f, reg, '\n')) {
		if (reg.back() == '\r') /* Alert! We have a Windows dweeb! */
			reg.pop_back();
		regs.push_back(reg);
	}
	
	compression_map.reserve(regs.size());
	/* NOTE** This map starts from values i+1 because we are only using it to make the compression step below faster.  The compression map is redundant for every uint16_t greater than the number of regulators (i.e., contents are emptied after readExpMatrix
	*/
	for (uint16_t i = 0; i < regs.size(); ++i)
		compression_map[regs[i]] = i+1; //NOTE** it's "regulator" -> i+1
	
	/* The original regs string vector is also the decompression map.  We index this vector with uint16_t -> "gene", and hence this is how we decompress.
	 */
	tot_num_regulators = regs.size();
	decompression_map = regs;
	return;
}

/* Reads a normalized (CPM, TPM) tab-separated (G+1)x(N+1) gene expression matrix and outputs a pair containing the genemap for the entire expression matrix (non-subsampled) as well as a subsampled version for every subnetwork. 
 */
std::vector<genemap> readExpMatrix(std::string filename) {
	fstream f{filename};
	genemap gm;
	std::vector<genemap> gm_folds(num_subnets);
	if (!f.is_open()) {
        	cerr << "error: file open failed " << filename << ".\n";
		std::exit(2);
	}

	// for the first line, we simply want to count the number of samples
	string line;
	getline(f, line, '\n');
	if (line.back() == '\r') /* Alert! We have a Windows dweeb! */
		line.pop_back();
	
	/*
	 Count number of samples from the number of columns in the first line
	 */ 
	for (size_t pos = 0; (pos = line.find_first_of("\t, ", pos)) != string::npos; ++pos)
		++tot_num_samps;
	
	// find subsample number **NOTE** must update tot_num_samps after subsampling
	uint16_t subsample_quant = std::ceil(subsampling_percent * tot_num_samps);
	if (subsample_quant >= tot_num_samps || subsample_quant < 0)
		subsample_quant = tot_num_samps;
	std::vector<uint16_t> samps_idx(tot_num_samps);
	
	std::iota(samps_idx.begin(), samps_idx.end(), 0U); /* 0, 1, ..., size-1 */
	// now, fold is a vector with subsample_quant indices sampled from [0,tot_num_samps).
	/* verify that seeding is done properly.  Replaced std::random_device{}() w/ global_seed*/
	
	// This is a vector of folds for every subnet requested
	std::vector<std::vector<uint16_t>> folds(num_subnets, std::vector<uint16_t>(subsample_quant));
	for (uint16_t i = 0; i < num_subnets; ++i) {
		std::sample(samps_idx.begin(), samps_idx.end(), folds[i].begin(), subsample_quant, std::mt19937{global_seed++});
	}
	
	// now, we can more efficiently load
	while(std::getline(f, line, '\n')) {
		if (line.back() == '\r') /* Alert! We have a Windows dweeb! */
			line.pop_back();
		std::vector<float> expr_vec;
		expr_vec.reserve(tot_num_samps);
		
		std::size_t prev = 0U, pos = line.find_first_of("\t, ", prev);
		std::string gene = line.substr(prev, pos-prev);
		prev = pos + 1;
		while ((pos = line.find_first_of("\t, ", prev)) != string::npos) {
			if (pos > prev) {
				expr_vec.emplace_back(stof(line.substr(prev, pos-prev)));
			}
			prev = pos + 1;
		}
		expr_vec.emplace_back(stof(line.substr(prev, string::npos)));
		
		// subsample. create expr_vec subsamples for each "fold" (subnetwork) requested.
		std::vector<std::vector<float>> expr_vec_folds;
		for (uint16_t subnet_idx = 0; subnet_idx < num_subnets; ++subnet_idx) {
			vector <float> expr_vec_sampled;
			expr_vec_sampled.reserve(subsample_quant);
			for (uint16_t i = 0U; i < subsample_quant; ++i)
				expr_vec_sampled.emplace_back(expr_vec[folds[subnet_idx][i]]);
			
			std::vector<uint16_t> rank_vec = rank_vals(expr_vec_sampled);
			
			/*
			 This function copula transforms the expr_vec_sampled values.  It's a brain teaser to think about, but rank_vec spits out the index of the rank r'th element.  So rank_vec[0] is the index of expr_vec_sampled for the least value, and we set that accordingly, in the manner below.
			 */
			for (uint16_t r = 0; r < subsample_quant; ++r)
				expr_vec_sampled[rank_vec[r]-1 /*sub 1 b/c rank starts from 1*/] = (r + 1 /*add 1 bc r starts from 0*/)/((float)subsample_quant + 1); // must sub 1 because rank_vec is idx+1
			expr_vec_folds.push_back(expr_vec_sampled);
		}
		
		// copula-transform expr_vec values
		std::vector<uint16_t> rank_vec = rank_vals(expr_vec);
		for (uint16_t r = 0; r < tot_num_samps; ++r)
			expr_vec[rank_vec[r]-1] = (r + 1)/((float)tot_num_samps + 1);
		
		/*
		 This compression works as follows.  When you input a key (gene) not in the table, it is immediately value initialized to uint16_t = 0.  However, no values are 0 in the table, as we added 1 to the index (see NOTE** above).  Note that *as soon as* we try to check if there exists 'gene' as a KEY, it is instantaneously made into a "key" with its own bin.
		 */
		if (compression_map[gene] == 0) {
			// we must have a target
			decompression_map.push_back(gene);
			// the last index of decompression_vec is the new uint16_t
			gm[decompression_map.size()-1] = expr_vec;
			for (uint16_t i = 0; i < num_subnets; ++i)
				gm_folds[i][decompression_map.size()-1] = expr_vec_folds[i];
		} else {
			/* we already mapped this regulator, so we must use the string map to find its compression value.  We do -1 because of NOTE** above */
			gm[compression_map[gene]-1] = expr_vec;
			for (uint16_t i = 0; i < num_subnets; ++i)
				gm_folds[i][compression_map[gene]-1] = expr_vec_folds[i];
		}

	}
	
	if (verbose) {
		std::cout << "\nInitial Num Samples: " + std::to_string(tot_num_samps) << std::endl;
		std::cout << "Sampled Num Samples: " + std::to_string(subsample_quant) << std::endl;
	}
	
	// update tot_num_samps because of **NOTE** above
	tot_num_samps_pre_subsample = tot_num_samps;
	tot_num_samps = subsample_quant;
	global_gm = gm;
	return gm_folds;
}

/*
 Function that prints the Regulator, Target, and MI to the output_dir given the output_suffix.  Does not print to the console.  The data structure input is a reg_web, which is defined in "ARACNe3.hpp".
 */
void writeNetworkRegTarMI(const reg_web &network, const std::string &output_dir, const std::string &output_suffix) {
	const string filename = output_dir + "output_" + output_suffix + ".txt";
	ofstream ofs{filename};
	if (!ofs) {
		cerr << "error: could not write to file: " << filename << ".\n";
		cerr << "Try making the output directory subdirectory of the working directory. Example \"-o ./runs\"." << endl;
		std::exit(2);
	}
	
	ofs << "REGULATOR\tTARGET\tMI\t" << std::endl;
	for (auto it = network.begin(); it != network.end(); ++it) {
		for (auto &edge_tar : it->second) {
				ofs << decompression_map[it->first] << '\t' << decompression_map[edge_tar.target] << '\t' << edge_tar.mi << '\t' << std::endl;
		}
	}
}

void writeConsolidatedNetwork(const std::vector<consolidated_df>& final_df, const std::string& output_dir) {
	const std::string filename = output_dir + "finalNet.txt";
	ofstream ofs{filename};
	if (!ofs) {
		std::cerr << "error: could not write to file: " << filename << ".\n";
		std::cerr << "Try making the output directory subdirectory of the working directory. Example \"-o ./runs\"." << std::endl;
		std::exit(2);
	}
	ofs << "REGULATOR\tTARGET\tMI\tSCC\t# SUBNETS INCIDENT\tP-VALUE (# SUBNETS INCIDENT)" << std::endl;
	for (const auto& edge : final_df)
		ofs << 
		decompression_map[edge.regulator] << '\t' <<
		decompression_map[edge.target] << '\t' <<
		edge.final_mi << '\t' <<
		edge.final_scc << '\t' <<
		edge.num_subnets_incident << '\t' <<
		edge.final_p << '\t' << std::endl;
}
