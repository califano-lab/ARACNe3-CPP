
//#include <boost/math/distributions/chi_squared.hpp>
//#include <Rcpp.h>
#include "ARACNe3.hpp"

using namespace std;


extern float DEVELOPER_mi_cutoff;

/*
 * File static  variables will be modified.  There are many file static
 * variables here, and so calculating the APMI should invoke this file uniquely,
 * each time the APMI is calculated, discarding all products.
 */
static float q_thresh;
static uint16_t tot_num_pts, size_thresh;
static std::vector<uint16_t> all_pts;
#pragma omp threadprivate(tot_num_pts, all_pts)

/*
 * Calculate the MI for a square struct
 */
float calcMI(const square &s) {
	const float pxy = s.num_pts/(float)tot_num_pts, &marginal = s.width, mi = pxy*std::log(pxy/(marginal*marginal));
	return std::isfinite(mi) ? mi : 0.0;
}

/*
 * Recursive tessellation of XY plane and mi calculation at dead-ends
 * 
 * s a pointer to the square we are considering to partition
 * mis a pointer to a vector of mis 
 *
 * returns nothing; values computed from pointers to original
 */
const float APMI_split(const std::vector<float>& vec_x, const std::vector<float>& vec_y, const square &s) {
	// extract values; memory disadvantage but runtime advantage
	const float x_bound1=s.x_bound1, y_bound1=s.y_bound1, width=s.width;
	const uint16_t *pts=s.pts, num_pts=s.num_pts;

	// if we have less points in the square than size_thresh, calc MI
	if (num_pts < size_thresh) { return calcMI(s);}

	// thresholds for potential partition of XY plane
	const float x_thresh = x_bound1 + width*0.5,
	      y_thresh = y_bound1 + width*0.5;

	// indices for quadrants, to test chi-square, with num_pts for each
	uint16_t tr_pts[num_pts], br_pts[num_pts], bl_pts[num_pts], 
	      tl_pts[num_pts], tr_num_pts=0, br_num_pts=0, bl_num_pts=0, 
	      tl_num_pts=0;

	// points that belong to each quadrant are discovered and sorted
	// outer for loop will iterate through the pts array
	for (uint16_t i = 0; i < num_pts; ++i) {
		// we must pull the actual point index from the pts array
		const uint16_t p = pts[i];
		const bool top = vec_y[p] >= y_thresh,
		      right = vec_x[p] >= x_thresh;
		if (top && right) { tr_pts[tr_num_pts++] = p; } 
		else if (right) { br_pts[br_num_pts++] = p; } 
		else if (top) { tl_pts[tl_num_pts++] = p; } 
		else { bl_pts[bl_num_pts++] = p; }
	}

	// compute chi-square, more efficient not to use pow()
	const float E = num_pts*0.25, chisq = ((tr_num_pts-E)*(tr_num_pts-E) +
		(br_num_pts-E)*(br_num_pts-E) +
		(bl_num_pts-E)*(bl_num_pts-E) +
		(tl_num_pts-E)*(tl_num_pts-E))/E;

	// partition if chi-square or if initial square
	if (chisq > q_thresh || num_pts == tot_num_pts) {
		const square tr{x_thresh, y_thresh, width*0.5, tr_pts, tr_num_pts},
		       br{x_thresh, y_bound1, width*0.5, br_pts, br_num_pts},
		       bl{x_bound1, y_bound1, width*0.5, bl_pts, bl_num_pts},
		       tl{x_bound1, y_thresh, width*0.5, tl_pts, tl_num_pts};

		return APMI_split(vec_x, vec_y, tr) + APMI_split(vec_x, vec_y, br) + APMI_split(vec_x, vec_y, bl) + APMI_split(vec_x, vec_y, tl);
	} else {
		// if we don't partition, then we calc MI
		return calcMI(s);
	}
}

/* Takes in two expression vectors (regulator-target) and computes APMI for 
 * each partition of the observation space
 *                                                                           
 * vec_x an X expression vector
 * vec_y a Y expression vector
 * q_thresh q-value for chi-square bin independence
 * size_thresh minimum points in a tile to consider chi-square and partition
 *                                                                           
 * returns the APMI 
 */
// [[Rcpp::export]]
float APMI(const vector<float>& vec_x, const vector<float>& vec_y, 
		const float& q_thresh,
		const uint16_t& size_thresh) {
	// Set file static variables
	::size_thresh = size_thresh;
	::q_thresh = q_thresh;
	if (tot_num_pts != vec_x.size()) {
		::tot_num_pts = vec_x.size();
		// Make an array of all indices, to be partitioned later
		std::vector<uint16_t> all_pts(tot_num_pts);
		for (uint16_t i = 0; i < tot_num_pts; i++) { all_pts[i] = i; }
		::all_pts = all_pts;
	}


	
	// Initialize plane and calc all MIs
	const square init{0.0, 0.0, 1.0, &all_pts[0], tot_num_pts};	
	
	return APMI_split(vec_x, vec_y, init);
}


/* Computes the APMI between a regulator and all targets in the genemap.  This
 * function is intended to reduce the number of times the regulator vector is
 * passed in memory, with the advance knowledge that we need to use the same
 * vec_x many times.  It assumes a particular usage case in the ARACNe3.cpp main
 * function.  
 *
 * Inputs are the entire genemap of gene->expression, the regulator name, and
 * then the q_thresh and size_thresh same as above
 * 
 * Returns a vector of 'edge' structs
 * corresponding to each edge and their MI.
 */
std::vector<edge_tar> genemapAPMI(genemap &matrix, const gene_id_t& reg,
		    const float& q_thresh,
		    const uint16_t& size_thresh) {
	// set file static variables
	::size_thresh = size_thresh;
	::q_thresh = q_thresh;
	const std::vector<float>& vec_x = matrix[reg];
	if (tot_num_pts != vec_x.size()) {
		::tot_num_pts = vec_x.size();
		std::vector<uint16_t> all_pts(tot_num_pts);
		for (uint16_t i = 0; i < tot_num_pts; ++i) { all_pts[i] = i; }
		::all_pts = all_pts;
	}
	const square init{0.0, 0.0, 1.0, &all_pts[0], tot_num_pts};
	

	
	std::vector<edge_tar> edges;
	edges.reserve(matrix.size() - 2); // minus 1 because size, minus 1 because reg->reg not an edge
	for (const auto &[tar, vec_y] : matrix) {
		if (tar != reg) {
			const float mi = APMI_split(vec_x, vec_y, init);
			if (mi >= DEVELOPER_mi_cutoff)
				edges.emplace_back(tar, mi);
		}
	}

	return edges;
}
	
/*
 * Computes the MI for a reference vector ref and a vector of vector targets.
 * Similar to genemapAPMI, but faster run time and used when creating edge
 * structs is not necessary, i.e. when computing 1 million null MI values.
 *
 * ref is the vector that will be referenced for all MI calculations
 * targets is a vector of vectors that we permute with ref
 * q_thresh is the chi-squared independence threshold
 * size_thresh is the smallest number of points allowed in a partition
 *
 * returns a float vector of targets.size() MI values, in the order of 'targets'
 */

const std::vector<float> permuteAPMI(const std::vector<float> &ref_vec_x,
		const std::vector<vector<float>> &targets, const float &q_thresh, const uint16_t &size_thresh) {
	// set file static variables
	::size_thresh = size_thresh;
	::q_thresh = q_thresh;
	if (tot_num_pts != ref_vec_x.size()) {
		::tot_num_pts = ref_vec_x.size();
		std::vector<uint16_t> all_pts(tot_num_pts);
		for (uint16_t i = 0; i < tot_num_pts; ++i) { all_pts[i] = i; }
		::all_pts = all_pts;
	}

	std::vector<float> mi_vec;
	mi_vec.reserve(targets.size());

	const square init{0.0, 0.0, 1.0, &all_pts[0], tot_num_pts};

	for (uint32_t i = 0; i < targets.size(); ++i)
		mi_vec.emplace_back(APMI_split(ref_vec_x, targets[i], init));

	return mi_vec;
}
