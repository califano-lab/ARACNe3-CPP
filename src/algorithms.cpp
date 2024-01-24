#include "algorithms.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

extern float DEVELOPER_mi_cutoff;

static float q_thresh;
static uint16_t size_thresh;

/**
 * @brief Calculate the Mutual Information (MI) for a square struct.
 *
 * @param s The square structure for which MI is calculated.
 *
 * @return A float representing the MI of the input square struct.
 */
float calcMI(const square &s) {
  const float pxy = s.num_pts / (float)s.tot_num_pts, marginal = s.width,
              mi = pxy * std::log(pxy / (marginal * marginal));
  return std::isfinite(mi) ? mi : 0.0f;
}

/**
 * @brief Perform recursive tessellation of XY plane and MI calculation at
 * dead-ends.
 *
 * This function recursively tessellates the XY plane and calculates the Mutual
 * Information (MI) at dead-ends. The function divides the plane into four
 * quadrants and computes the chi-square statistic for the distribution of
 * points across the quadrants. If the chi-square statistic exceeds a
 * predefined threshold or if the function is operating on the initial square,
 * the function continues to subdivide the plane. Otherwise, it calculates the
 * MI for the current square.
 *
 * Note: The use of alloca is not safe from stack overflow, strictly speaking,
 * but stack overflow is only conceivable in a contrived case, and any generic
 * usage of MINDy3 should never create a stack allocation too large.
 *
 * @param x_ptr Pointer to the x-coordinate data.
 * @param y_ptr Pointer to the y-coordinate data.
 * @param s The square struct on which to perform a tessellation.
 *
 * @return A float value representing the result of MI calculations, or
 * performs
 * recursion, depending on the condition.
 */
const float calcAPMISplit(const float *const x_ptr, const float *const y_ptr,
                          const square s) {
  // if we have less points in the square than size_thresh, calc MI
  if (s.num_pts < size_thresh) {
    return calcMI(s);
  }

  // thresholds for potential partition of XY plane
  const float x_thresh = s.x_bound1 + s.width * 0.5f,
              y_thresh = s.y_bound1 + s.width * 0.5f;

  // indices for quadrants, to test chi-square, with num_pts for each
  uint16_t *tr_pts, *br_pts, *bl_pts, *tl_pts, tr_num_pts = 0U, br_num_pts = 0U,
                                               bl_num_pts = 0U, tl_num_pts = 0U;
  tr_pts = (uint16_t *)alloca(s.num_pts * sizeof(uint16_t));
  br_pts = (uint16_t *)alloca(s.num_pts * sizeof(uint16_t));
  bl_pts = (uint16_t *)alloca(s.num_pts * sizeof(uint16_t));
  tl_pts = (uint16_t *)alloca(s.num_pts * sizeof(uint16_t));

  // points that belong to each quadrant are discovered and sorted
  // outer for loop will iterate through the pts array
  for (uint16_t i = 0U; i < s.num_pts; ++i) {
    // we must pull the actual point index from the pts array
    const uint16_t p = s.pts[i];
    const bool top = y_ptr[p] >= y_thresh, right = x_ptr[p] >= x_thresh;
    if (top && right) {
      tr_pts[tr_num_pts++] = p;
    } else if (right) {
      br_pts[br_num_pts++] = p;
    } else if (top) {
      tl_pts[tl_num_pts++] = p;
    } else {
      bl_pts[bl_num_pts++] = p;
    }
  }

  // compute chi-square, more efficient not to use pow()
  const float E = s.num_pts * 0.25f,
              chisq = ((tr_num_pts - E) * (tr_num_pts - E) +
                       (br_num_pts - E) * (br_num_pts - E) +
                       (bl_num_pts - E) * (bl_num_pts - E) +
                       (tl_num_pts - E) * (tl_num_pts - E)) /
                      E;

  // partition if chi-square or if initial square
  if (chisq > q_thresh || s.num_pts == s.tot_num_pts) {
    const square tr{x_thresh, y_thresh,   s.width * 0.5f,
                    tr_pts,   tr_num_pts, s.tot_num_pts},
        br{x_thresh, s.y_bound1, s.width * 0.5f,
           br_pts,   br_num_pts, s.tot_num_pts},
        bl{s.x_bound1, s.y_bound1, s.width * 0.5f,
           bl_pts,     bl_num_pts, s.tot_num_pts},
        tl{s.x_bound1, y_thresh,   s.width * 0.5f,
           tl_pts,     tl_num_pts, s.tot_num_pts};

    return calcAPMISplit(x_ptr, y_ptr, tr) + calcAPMISplit(x_ptr, y_ptr, br) +
           calcAPMISplit(x_ptr, y_ptr, bl) + calcAPMISplit(x_ptr, y_ptr, tl);
  } else {
    // if we don't partition, then we calc MI
    return calcMI(s);
  }
}

float calcAPMI(const std::vector<float> &x_vec, const std::vector<float> &y_vec,
               const float q_thresh, const uint16_t size_thresh) {
  // Set file static variables
  ::size_thresh = size_thresh;
  ::q_thresh = q_thresh;

  uint16_t tot_num_pts = x_vec.size();

  uint16_t *all_pts = (uint16_t *)alloca(tot_num_pts * sizeof(uint16_t));
  std::iota(all_pts, &all_pts[tot_num_pts], 0U);

  // Initialize plane and calc all MIs
  const square init{0.0f, 0.0f, 1.0f, &all_pts[0U], tot_num_pts, tot_num_pts};
  float *x_ptr = (float *)alloca(x_vec.size() * sizeof(float)),
        *y_ptr = (float *)alloca(y_vec.size() * sizeof(float));

  std::copy(x_vec.begin(), x_vec.end(), x_ptr);
  std::copy(y_vec.begin(), y_vec.end(), y_ptr);

  return calcAPMISplit(x_ptr, y_ptr, init);
}

std::vector<uint16_t> rankIndices(const std::vector<float> &vec,
                                  std::mt19937 &rand) {
  std::vector<uint16_t> idx_ranks(vec.size());
  std::iota(idx_ranks.begin(), idx_ranks.end(), 0U); /* 0, 1, ..., size-1 */
  std::sort(idx_ranks.begin(), idx_ranks.end(),
            [&vec](const uint16_t &num1, const uint16_t &num2) -> bool {
              return vec[num1] < vec[num2];
            }); /* sort ascending */
  for (uint16_t r = 0U; r < idx_ranks.size();) {
    uint16_t same_range = 1U;
    while (r + same_range < idx_ranks.size() &&
           vec[idx_ranks[r]] == vec[idx_ranks[r + same_range]])
      ++same_range; // same_range is off-end index
    if (same_range > 1U) {
      std::shuffle(idx_ranks.begin() + r, idx_ranks.begin() + r + same_range,
                   rand);
      r = r + same_range;
    } else {
      ++r;
    }
  }
  return idx_ranks;
}

float calcSCC(const std::vector<uint16_t> &x_ranked,
              const std::vector<uint16_t> &y_ranked) {
  const auto &n = x_ranked.size();
  double sigma_dxy = 0; // Use double to prevent overflow!
  for (uint16_t i = 0; i < n; ++i) {
    int diff = static_cast<int>(x_ranked[i]) - static_cast<int>(y_ranked[i]);
    sigma_dxy += diff * diff;
  }
  return 1. - 6. * sigma_dxy / n / (n * n - 1);
}

double lchoose(const uint16_t &n, const uint16_t &k) {
  return std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
}

double rightTailBinomialP(uint16_t n, uint16_t k, float theta) {
  // If k is 0, the right-tail probability includes all possibilities, which sum to 1.
  if (k == 0) return 1.0;

  double p = 0.0;
  // Start from k and go up to n to avoid underflow.
  for (uint16_t i = k; i <= n; ++i)
    p += std::exp(lchoose(n, i) + i * std::log(theta) + (n - i) * std::log(1 - theta));

  return p;
}

double lRightTailBinomialP(uint16_t n, uint16_t k, float theta) {
    // If k is 0, the right-tail probability includes all possibilities, which is p = 1.
    if (k == 0) return std::log(1.);

    double max_log_p = -std::numeric_limits<double>::infinity();
    std::vector<double> log_ps;

    // Calculate log probabilities and find the maximum log probability
    for (uint16_t i = k; i <= n; ++i) {
        double log_p = lchoose(n, i) + i * std::log(theta) + (n - i) * std::log(1. - theta);
        max_log_p = std::max(max_log_p, log_p);
        log_ps.push_back(log_p);
    }

    // Apply log-sum-exp trick to sum in log domain
    double log_sum_exp = 0.;
    for (double log_p : log_ps) {
        log_sum_exp += std::exp(log_p - max_log_p);
    }

    return max_log_p + std::log(log_sum_exp);
}

/**
 * @brief Calculate linear regression to find the slope and y-intercept.
 *
 * This function takes two float vector parameters, x and y, representing data
 * points on a two-dimensional plane. It returns a pair of floats where the
 * first float is the slope (m) and the second float is the y-intercept (b)
 * from the line equation y = mx + b.
 *
 * @param x A vector of x floats.
 * @param y A vector of y floats.
 *
 * @return A pair of floats (m,b).
 */
std::pair<float, float> OLS(const std::vector<float> &x_vec,
                            const std::vector<float> &y_vec) {
    const size_t n = x_vec.size();

    if (x_vec.size() != y_vec.size())
        throw std::runtime_error(
            "Cannot perform regression on vectors of unequal size");

    float x_mean =
        std::reduce(x_vec.cbegin(), x_vec.cend()) / static_cast<float>(n);
    float y_mean =
        std::reduce(y_vec.cbegin(), y_vec.cend()) / static_cast<float>(n);

    float ssr_x = std::reduce(x_vec.cbegin(), x_vec.cend(), 0.f,
                              [&x_mean](float a, float cur) {
                                return a + (cur - x_mean) * (cur - x_mean);
                              });

    float sum_prod = 0.f;
    for (size_t i = 0U; i < n; ++i)
        sum_prod += (x_vec[i] - x_mean) * (y_vec[i] - y_mean);

    float slope = sum_prod / ssr_x;
    float intercept = y_mean - slope * x_mean;

    return {slope, intercept};
}

std::vector<float> copulaTransform(const std::vector<float> &data,
                                   std::mt19937 &rand) {
  uint32_t n = data.size();
  std::vector<uint32_t> indices(n);
  std::iota(indices.begin(), indices.end(), 0U);

  // Shuffle indices for random tie breaking
  std::shuffle(indices.begin(), indices.end(), rand);

  // Sort indices based on corresponding data values
  std::sort(indices.begin(), indices.end(),
            [&](uint32_t i1, uint32_t i2) { return data[i1] < data[i2]; });

  // Assign ranks (idx + 1) to the values, ratio by n + 1
  std::vector<float> ranks(n);
  for (uint32_t i = 0U; i != n; ++i)
    ranks[indices[i]] = static_cast<float>(i + 1) / (n + 1);

  return ranks;
}
