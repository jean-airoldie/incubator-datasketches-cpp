/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef KLL_SKETCH_HPP_
#define KLL_SKETCH_HPP_

#include <memory>
#include <limits>
#include <iostream>
#include <iomanip>
#include <functional>
#include <cstring>

#include "kll_quantile_calculator.hpp"
#include "kll_helper.hpp"
#include "serde.hpp"

namespace datasketches {

/*
 * Implementation of a very compact quantiles sketch with lazy compaction scheme
 * and nearly optimal accuracy per retained item.
 * See <a href="https://arxiv.org/abs/1603.05346v2">Optimal Quantile Approximation in Streams</a>.
 *
 * <p>This is a stochastic streaming sketch that enables near-real time analysis of the
 * approximate distribution of values from a very large stream in a single pass, requiring only
 * that the values are comparable.
 * The analysis is obtained using <i>get_quantile()</i> or <i>get_quantiles()</i> functions or the
 * inverse functions get_rank(), get_PMF() (Probability Mass Function), and get_CDF()
 * (Cumulative Distribution Function).
 *
 * <p>Given an input stream of <i>N</i> numeric values, the <i>absolute rank</i> of any specific
 * value is defined as its index <i>(0 to N-1)</i> in the hypothetical sorted stream of all
 * <i>N</i> input values.
 *
 * <p>The <i>normalized rank</i> (<i>rank</i>) of any specific value is defined as its
 * <i>absolute rank</i> divided by <i>N</i>.
 * Thus, the <i>normalized rank</i> is a value between zero and one.
 * In the documentation for this sketch <i>absolute rank</i> is never used so any
 * reference to just <i>rank</i> should be interpreted to mean <i>normalized rank</i>.
 *
 * <p>This sketch is configured with a parameter <i>k</i>, which affects the size of the sketch
 * and its estimation error.
 *
 * <p>The estimation error is commonly called <i>epsilon</i> (or <i>eps</i>) and is a fraction
 * between zero and one. Larger values of <i>k</i> result in smaller values of epsilon.
 * Epsilon is always with respect to the rank and cannot be applied to the
 * corresponding values.
 *
 * <p>The relationship between the normalized rank and the corresponding values can be viewed
 * as a two dimensional monotonic plot with the normalized rank on one axis and the
 * corresponding values on the other axis. If the y-axis is specified as the value-axis and
 * the x-axis as the normalized rank, then <i>y = get_quantile(x)</i> is a monotonically
 * increasing function.
 *
 * <p>The functions <i>get_quantile(rank)</i> and get_quantiles(...) translate ranks into
 * corresponding values. The functions <i>get_rank(value),
 * get_CDF(...) (Cumulative Distribution Function), and get_PMF(...)
 * (Probability Mass Function)</i> perform the opposite operation and translate values into ranks.
 *
 * <p>The <i>getPMF(...)</i> function has about 13 to 47% worse rank error (depending
 * on <i>k</i>) than the other queries because the mass of each "bin" of the PMF has
 * "double-sided" error from the upper and lower edges of the bin as a result of a subtraction,
 * as the errors from the two edges can sometimes add.
 *
 * <p>The default <i>k</i> of 200 yields a "single-sided" epsilon of about 1.33% and a
 * "double-sided" (PMF) epsilon of about 1.65%.
 *
 * <p>A <i>get_quantile(rank)</i> query has the following guarantees:
 * <ul>
 * <li>Let <i>v = get_quantile(r)</i> where <i>r</i> is the rank between zero and one.</li>
 * <li>The value <i>v</i> will be a value from the input stream.</li>
 * <li>Let <i>trueRank</i> be the true rank of <i>v</i> derived from the hypothetical sorted
 * stream of all <i>N</i> values.</li>
 * <li>Let <i>eps = get_normalized_rank_error(false)</i>.</li>
 * <li>Then <i>r - eps &le; trueRank &le; r + eps</i> with a confidence of 99%. Note that the
 * error is on the rank, not the value.</li>
 * </ul>
 *
 * <p>A <i>get_rank(value)</i> query has the following guarantees:
 * <ul>
 * <li>Let <i>r = get_rank(v)</i> where <i>v</i> is a value between the min and max values of
 * the input stream.</li>
 * <li>Let <i>true_rank</i> be the true rank of <i>v</i> derived from the hypothetical sorted
 * stream of all <i>N</i> values.</li>
 * <li>Let <i>eps = get_normalized_rank_error(false)</i>.</li>
 * <li>Then <i>r - eps &le; trueRank &le; r + eps</i> with a confidence of 99%.</li>
 * </ul>
 *
 * <p>A <i>get_PMF()</i> query has the following guarantees:
 * <ul>
 * <li>Let <i>{r1, r2, ..., r(m+1)} = get_PMF(v1, v2, ..., vm)</i> where <i>v1, v2</i> are values
 * between the min and max values of the input stream.
 * <li>Let <i>mass<sub>i</sub> = estimated mass between v<sub>i</sub> and v<sub>i+1</sub></i>.</li>
 * <li>Let <i>trueMass</i> be the true mass between the values of <i>v<sub>i</sub>,
 * v<sub>i+1</sub></i> derived from the hypothetical sorted stream of all <i>N</i> values.</li>
 * <li>Let <i>eps = get_normalized_rank_error(true)</i>.</li>
 * <li>then <i>mass - eps &le; trueMass &le; mass + eps</i> with a confidence of 99%.</li>
 * <li>r(m+1) includes the mass of all points larger than vm.</li>
 * </ul>
 *
 * <p>A <i>get_CDF(...)</i> query has the following guarantees;
 * <ul>
 * <li>Let <i>{r1, r2, ..., r(m+1)} = get_CDF(v1, v2, ..., vm)</i> where <i>v1, v2</i> are values
 * between the min and max values of the input stream.
 * <li>Let <i>mass<sub>i</sub> = r<sub>i+1</sub> - r<sub>i</sub></i>.</li>
 * <li>Let <i>trueMass</i> be the true mass between the true ranks of <i>v<sub>i</sub>,
 * v<sub>i+1</sub></i> derived from the hypothetical sorted stream of all <i>N</i> values.</li>
 * <li>Let <i>eps = get_normalized_rank_error(true)</i>.</li>
 * <li>then <i>mass - eps &le; trueMass &le; mass + eps</i> with a confidence of 99%.</li>
 * <li>1 - r(m+1) includes the mass of all points larger than vm.</li>
 * </ul>
 *
 * <p>From the above, it might seem like we could make some estimates to bound the
 * <em>value</em> returned from a call to <em>get_quantile()</em>. The sketch, however, does not
 * let us derive error bounds or confidences around values. Because errors are independent, we
 * can approximately bracket a value as shown below, but there are no error estimates available.
 * Additionally, the interval may be quite large for certain distributions.
 * <ul>
 * <li>Let <i>v = get_quantile(r)</i>, the estimated quantile value of rank <i>r</i>.</li>
 * <li>Let <i>eps = get_normalized_rank_error(false)</i>.</li>
 * <li>Let <i>v<sub>lo</sub></i> = estimated quantile value of rank <i>(r - eps)</i>.</li>
 * <li>Let <i>v<sub>hi</sub></i> = estimated quantile value of rank <i>(r + eps)</i>.</li>
 * <li>Then <i>v<sub>lo</sub> &le; v &le; v<sub>hi</sub></i>, with 99% confidence.</li>
 * </ul>
 *
 * author Kevin Lang
 * author Alexander Saydakov
 * author Lee Rhodes
 */

typedef std::unique_ptr<void, std::function<void(void*)>> void_ptr_with_deleter;

template <typename T, typename C = std::less<T>, typename S = serde<T>, typename A = std::allocator<T>>
class kll_sketch {
  typedef typename std::allocator_traits<A>::template rebind_alloc<uint32_t> AllocU32;

  public:
    static const uint8_t DEFAULT_M = 8;
    static const uint16_t DEFAULT_K = 200;
    static const uint16_t MIN_K = DEFAULT_M;
    static const uint16_t MAX_K = (1 << 16) - 1;

    explicit kll_sketch(uint16_t k = DEFAULT_K);
    kll_sketch(const kll_sketch& other);
    ~kll_sketch();
    kll_sketch& operator=(kll_sketch other);
    void update(const T& value);
    void update(T&& value);
    void merge(const kll_sketch& other);
    bool is_empty() const;
    uint64_t get_n() const;
    uint32_t get_num_retained() const;
    bool is_estimation_mode() const;
    T get_min_value() const;
    T get_max_value() const;
    T get_quantile(double fraction) const;
    std::unique_ptr<T[], std::function<void(T*)>> get_quantiles(const double* fractions, uint32_t size) const;
    double get_rank(const T& value) const;
    std::unique_ptr<double[], std::function<void(double*)>> get_PMF(const T* split_points, uint32_t size) const;
    std::unique_ptr<double[], std::function<void(double*)>> get_CDF(const T* split_points, uint32_t size) const;
    double get_normalized_rank_error(bool pmf) const;

    // implementation for fixed-size arithmetic types (integral and floating point)
    template<typename TT = T, typename std::enable_if<std::is_arithmetic<TT>::value, int>::type = 0>
    uint32_t get_serialized_size_bytes() const {
      if (is_empty()) { return EMPTY_SIZE_BYTES; }
      if (num_levels_ == 1 and get_num_retained() == 1) {
        return DATA_START_SINGLE_ITEM + sizeof(TT);
      }
      // the last integer in the levels_ array is not serialized because it can be derived
      return DATA_START + num_levels_ * sizeof(uint32_t) + (get_num_retained() + 2) * sizeof(TT);
    }

    // implementation for all other types
    template<typename TT = T, typename std::enable_if<!std::is_arithmetic<TT>::value, int>::type = 0>
    uint32_t get_serialized_size_bytes() const {
      if (is_empty()) { return EMPTY_SIZE_BYTES; }
      if (num_levels_ == 1 and get_num_retained() == 1) {
        return DATA_START_SINGLE_ITEM + S().size_of_item(items_[levels_[0]]);
      }
      // the last integer in the levels_ array is not serialized because it can be derived
      uint32_t size = DATA_START + num_levels_ * sizeof(uint32_t);
      size += S().size_of_item(*min_value_);
      size += S().size_of_item(*max_value_);
      for (auto& it: *this) size += S().size_of_item(it.first);
      return size;
    }

    // this may need to be specialized to return correct size if sizeof(T) does not match the actual serialized size of an item
    // this method is for the user's convenience to predict the sketch size before serialization
    // and is not used in the serialization and deserialization code
    // predicting the size before serialization may not make sense if the item type is not of a fixed size (like string)
    static uint32_t get_max_serialized_size_bytes(uint16_t k, uint64_t n);

    void serialize(std::ostream& os) const;
    std::pair<void_ptr_with_deleter, const size_t> serialize(unsigned header_size_bytes = 0) const;
    static kll_sketch<T, C, S, A> deserialize(std::istream& is);
    static kll_sketch<T, C, S, A> deserialize(const void* bytes, size_t size);

    /*
     * Gets the normalized rank error given k and pmf.
     * k - the configuration parameter
     * pmf - if true, returns the "double-sided" normalized rank error for the get_PMF() function.
     * Otherwise, it is the "single-sided" normalized rank error for all the other queries.
     * Constants were derived as the best fit to 99 percentile empirically measured max error in thousands of trials
     */
    static double get_normalized_rank_error(uint16_t k, bool pmf);

    void to_stream(std::ostream& os, bool print_levels = false, bool print_items = false) const;

    class const_iterator;
    const_iterator begin() const;
    const_iterator end() const;

    #ifdef KLL_VALIDATION
    uint8_t get_num_levels() { return num_levels_; }
    uint32_t* get_levels() { return levels_; }
    T* get_items() { return items_; }
#endif

  private:
    /* Serialized sketch layout:
     *  Adr:
     *      ||    7    |   6   |    5   |    4   |    3   |    2    |    1   |      0       |
     *  0   || unused  |   M   |--------K--------|  Flags |  FamID  | SerVer | PreambleInts |
     *      ||   15    |   14  |   13   |   12   |   11   |   10    |    9   |      8       |
     *  1   ||-----------------------------------N------------------------------------------|
     *      ||   23    |   22  |   21   |   20   |   19   |    18   |   17   |      16      |
     *  2   ||---------------data----------------|-unused-|numLevels|-------min K-----------|
     */

    static const size_t EMPTY_SIZE_BYTES = 8;
    static const size_t DATA_START_SINGLE_ITEM = 8;
    static const size_t DATA_START = 20;

    static const uint8_t SERIAL_VERSION_1 = 1;
    static const uint8_t SERIAL_VERSION_2 = 2;
    static const uint8_t FAMILY = 15;

    enum flags { IS_EMPTY, IS_LEVEL_ZERO_SORTED, IS_SINGLE_ITEM };

    static const uint8_t PREAMBLE_INTS_SHORT = 2; // for empty and single item
    static const uint8_t PREAMBLE_INTS_FULL = 5;

    uint16_t k_;
    uint8_t m_; // minimum buffer "width"
    uint16_t min_k_; // for error estimation after merging with different k
    uint64_t n_;
    uint8_t num_levels_;
    uint32_t* levels_;
    uint8_t levels_size_;
    T* items_;
    uint32_t items_size_;
    T* min_value_;
    T* max_value_;
    bool is_level_zero_sorted_;

    // for deserialization
    // the common part of the preamble was read and compatibility checks were done
    kll_sketch(uint16_t k, uint8_t flags_byte, std::istream& is);

    // for deserialization
    // the common part of the preamble was read and compatibility checks were done
    kll_sketch(uint16_t k, uint8_t flags_byte, const void* bytes, size_t size);

    // common update code
    uint32_t internal_update(const T& value);

    // The following code is only valid in the special case of exactly reaching capacity while updating.
    // It cannot be used while merging, while reducing k, or anything else.
    void compress_while_updating(void);

    uint8_t find_level_to_compact() const;
    void add_empty_top_level_to_completely_full_sketch();
    void sort_level_zero();
    std::unique_ptr<kll_quantile_calculator<T, C, A>, std::function<void(kll_quantile_calculator<T, C, A>*)>> get_quantile_calculator();
    std::unique_ptr<double[], std::function<void(double*)>> get_PMF_or_CDF(const T* split_points, uint32_t size, bool is_CDF) const;
    void increment_buckets_unsorted_level(uint32_t from_index, uint32_t to_index, uint64_t weight,
        const T* split_points, uint32_t size, double* buckets) const;
    void increment_buckets_sorted_level(uint32_t from_index, uint32_t to_index, uint64_t weight,
        const T* split_points, uint32_t size, double* buckets) const;
    void merge_higher_levels(const kll_sketch& other, uint64_t final_n);
    void populate_work_arrays(const kll_sketch& other, T* workbuf, uint32_t* worklevels, uint8_t provisional_num_levels);
    void assert_correct_total_weight() const;
    uint32_t safe_level_size(uint8_t level) const;
    uint32_t get_num_retained_above_level_zero() const;

    static void check_m(uint8_t m);
    static void check_preamble_ints(uint8_t preamble_ints, uint8_t flags_byte);
    static void check_serial_version(uint8_t serial_version);
    static void check_family_id(uint8_t family_id);

    // implementation for floating point types
    template<typename TT = T, typename std::enable_if<std::is_floating_point<TT>::value, int>::type = 0>
    static TT get_invalid_value() {
      return std::numeric_limits<TT>::quiet_NaN();
    }

    // implementation for all other types
    template<typename TT = T, typename std::enable_if<!std::is_floating_point<TT>::value, int>::type = 0>
    static TT get_invalid_value() {
      throw std::runtime_error("getting quantiles from empty sketch is not supported for this type of values");
    }

};

template<typename T, typename C, typename S, typename A>
class kll_sketch<T, C, S, A>::const_iterator: public std::iterator<std::input_iterator_tag, T> {
public:
  friend class kll_sketch<T, C, S, A>;
  const_iterator(const const_iterator& other);
  const_iterator& operator++();
  const_iterator& operator++(int);
  bool operator==(const const_iterator& other) const;
  bool operator!=(const const_iterator& other) const;
  const std::pair<const T&, const uint64_t> operator*() const;
private:
  const T* items;
  const uint32_t* levels;
  const uint8_t num_levels;
  uint32_t index;
  uint8_t level;
  uint64_t weight;
  const_iterator(const T* items, const uint32_t* levels, const uint8_t num_levels);
};

// kll_sketch implementation

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::kll_sketch(uint16_t k) {
  if (k < MIN_K or k > MAX_K) {
    throw std::invalid_argument("K must be >= " + std::to_string(MIN_K) + " and <= " + std::to_string(MAX_K) + ": " + std::to_string(k));
  }
  k_ = k;
  m_ = DEFAULT_M;
  min_k_ = k;
  n_ = 0;
  num_levels_ = 1;
  levels_size_ = 2;
  levels_ = new (AllocU32().allocate(2)) uint32_t[2] {k_, k_};
  items_size_ = k_;
  items_ = A().allocate(items_size_);
  min_value_ = A().allocate(1);
  max_value_ = A().allocate(1);
  is_level_zero_sorted_ = false;
}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::kll_sketch(const kll_sketch& other) {
  k_ = other.k_;
  m_ = other.m_;
  min_k_ = other.min_k_;
  n_ = other.n_;
  num_levels_ = other.num_levels_;
  levels_size_ = other.levels_size_;
  levels_ = AllocU32().allocate(levels_size_);
  std::copy(&other.levels_[0], &other.levels_[levels_size_], levels_);
  items_size_ = other.items_size_;
  items_ = A().allocate(items_size_);
  std::copy(other.items_, &other.items_[items_size_], items_);
  min_value_ = A().allocate(1);
  max_value_ = A().allocate(1);
  new (min_value_) T(*other.min_value_);
  new (max_value_) T(*other.max_value_);
  is_level_zero_sorted_ = other.is_level_zero_sorted_;
}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>& kll_sketch<T, C, S, A>::operator=(kll_sketch other) {
  std::swap(k_, other.k_);
  std::swap(m_, other.m_);
  std::swap(min_k_, other.min_k_);
  std::swap(n_, other.n_);
  std::swap(num_levels_, other.num_levels_);
  std::swap(levels_size_, other.levels_size_);
  std::swap(levels_, other.levels_);
  std::swap(items_size_, other.items_size_);
  std::swap(items_, other.items_);
  std::swap(min_value_, other.min_value_);
  std::swap(max_value_, other.max_value_);
  std::swap(is_level_zero_sorted_, other.is_level_zero_sorted_);
  return *this;
}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::~kll_sketch() {
  const uint32_t begin = levels_[0];
  const uint32_t end = begin + get_num_retained();
  for (uint32_t i = begin; i < end; i++) items_[i].~T();
  if (!is_empty()) {
    min_value_->~T();
    max_value_->~T();
  }
  AllocU32().deallocate(levels_, levels_size_);
  A().deallocate(items_, items_size_);
  A().deallocate(min_value_, 1);
  A().deallocate(max_value_, 1);
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::update(const T& value) {
  const uint32_t next_pos = internal_update(value);
  new (&items_[next_pos]) T(value);
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::update(T&& value) {
  const uint32_t next_pos = internal_update(value);
  new (&items_[next_pos]) T(std::move(value));
}

template<typename T, typename C, typename S, typename A>
uint32_t kll_sketch<T, C, S, A>::internal_update(const T& value) {
  if (is_empty()) {
    new (min_value_) T(value);
    new (max_value_) T(value);
  } else {
    if (C()(value, *min_value_)) *min_value_ = value;
    if (C()(*max_value_, value)) *max_value_ = value;
  }
  if (levels_[0] == 0) compress_while_updating();
  n_++;
  is_level_zero_sorted_ = false;
  const uint32_t next_pos(levels_[0] - 1);
  levels_[0] = next_pos;
  return next_pos;
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::merge(const kll_sketch& other) {
  if (other.is_empty()) return;
  if (m_ != other.m_) {
    throw std::invalid_argument("incompatible M: " + std::to_string(m_) + " and " + std::to_string(other.m_));
  }
  const uint64_t final_n = n_ + other.n_;
  for (uint32_t i = other.levels_[0]; i < other.levels_[1]; i++) {
    update(other.items_[i]);
  }
  if (is_empty()) {
    new (min_value_) T(*other.min_value_);
    new (max_value_) T(*other.max_value_);
  } else {
    if (C()(*other.min_value_, *min_value_)) *min_value_ = *other.min_value_;
    if (C()(*max_value_, *other.max_value_)) *max_value_ = *other.max_value_;
  }
  if (other.num_levels_ >= 2) merge_higher_levels(other, final_n);
  n_ = final_n;
  if (other.is_estimation_mode()) min_k_ = std::min(min_k_, other.min_k_);
  assert_correct_total_weight();
}

template<typename T, typename C, typename S, typename A>
bool kll_sketch<T, C, S, A>::is_empty() const {
  return n_ == 0;
}

template<typename T, typename C, typename S, typename A>
uint64_t kll_sketch<T, C, S, A>::get_n() const {
  return n_;
}

template<typename T, typename C, typename S, typename A>
uint32_t kll_sketch<T, C, S, A>::get_num_retained() const {
  return levels_[num_levels_] - levels_[0];
}

template<typename T, typename C, typename S, typename A>
bool kll_sketch<T, C, S, A>::is_estimation_mode() const {
  return num_levels_ > 1;
}

template<typename T, typename C, typename S, typename A>
T kll_sketch<T, C, S, A>::get_min_value() const {
  if (is_empty()) return get_invalid_value();
  return *min_value_;
}

template<typename T, typename C, typename S, typename A>
T kll_sketch<T, C, S, A>::get_max_value() const {
  if (is_empty()) return get_invalid_value();
  return *max_value_;
}

template<typename T, typename C, typename S, typename A>
T kll_sketch<T, C, S, A>::get_quantile(double fraction) const {
  if (is_empty()) return get_invalid_value();
  if (fraction == 0.0) return *min_value_;
  if (fraction == 1.0) return *max_value_;
  if ((fraction < 0.0) or (fraction > 1.0)) {
    throw std::invalid_argument("Fraction cannot be less than zero or greater than 1.0");
  }
  // has side effect of sorting level zero if needed
  auto quantile_calculator(const_cast<kll_sketch*>(this)->get_quantile_calculator());
  return quantile_calculator->get_quantile(fraction);
}

template<typename T, typename C, typename S, typename A>
std::unique_ptr<T[], std::function<void(T*)>> kll_sketch<T, C, S, A>::get_quantiles(const double* fractions, uint32_t size) const {
  if (is_empty()) return std::unique_ptr<T[], std::function<void(T*)>>();
  std::unique_ptr<kll_quantile_calculator<T, C, A>, std::function<void(kll_quantile_calculator<T, C, A>*)>> quantile_calculator;
  std::unique_ptr<T[], std::function<void(T*)>> quantiles(
    A().allocate(size),
    [size](T* ptr){
      for (uint32_t i = 0; i < size; i++) ptr[i].~T();
      A().deallocate(ptr, size);
    }
  );
  for (uint32_t i = 0; i < size; i++) {
    const double fraction = fractions[i];
    if ((fraction < 0.0) or (fraction > 1.0)) {
      throw std::invalid_argument("Fraction cannot be less than zero or greater than 1.0");
    }
    if      (fraction == 0.0) new (&quantiles[i]) T(*min_value_);
    else if (fraction == 1.0) new (&quantiles[i]) T(*max_value_);
    else {
      if (!quantile_calculator) {
        // has side effect of sorting level zero if needed
        quantile_calculator = const_cast<kll_sketch*>(this)->get_quantile_calculator();
      }
      new (&quantiles[i]) T(quantile_calculator->get_quantile(fraction));
    }
  }
  return std::move(quantiles);
}

template<typename T, typename C, typename S, typename A>
double kll_sketch<T, C, S, A>::get_rank(const T& value) const {
  if (is_empty()) return std::numeric_limits<double>::quiet_NaN();
  uint8_t level(0);
  uint64_t weight(1);
  uint64_t total(0);
  while (level < num_levels_) {
    const auto from_index(levels_[level]);
    const auto to_index(levels_[level + 1]); // exclusive
    for (uint32_t i = from_index; i < to_index; i++) {
      if (C()(items_[i], value)) {
        total += weight;
      } else if ((level > 0) or is_level_zero_sorted_) {
        break; // levels above 0 are sorted, no point comparing further
      }
    }
    level++;
    weight *= 2;
  }
  return (double) total / n_;
}

template<typename T, typename C, typename S, typename A>
std::unique_ptr<double[], std::function<void(double*)>> kll_sketch<T, C, S, A>::get_PMF(const T* split_points, uint32_t size) const {
  return get_PMF_or_CDF(split_points, size, false);
}

template<typename T, typename C, typename S, typename A>
std::unique_ptr<double[], std::function<void(double*)>> kll_sketch<T, C, S, A>::get_CDF(const T* split_points, uint32_t size) const {
  return get_PMF_or_CDF(split_points, size, true);
}

template<typename T, typename C, typename S, typename A>
double kll_sketch<T, C, S, A>::get_normalized_rank_error(bool pmf) const {
  return get_normalized_rank_error(min_k_, pmf);
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::serialize(std::ostream& os) const {
  const bool is_single_item = n_ == 1;
  const uint8_t preamble_ints(is_empty() or is_single_item ? PREAMBLE_INTS_SHORT : PREAMBLE_INTS_FULL);
  os.write((char*)&preamble_ints, sizeof(preamble_ints));
  const uint8_t serial_version(is_single_item ? SERIAL_VERSION_2 : SERIAL_VERSION_1);
  os.write((char*)&serial_version, sizeof(serial_version));
  const uint8_t family(FAMILY);
  os.write((char*)&family, sizeof(family));
  const uint8_t flags_byte(
      (is_empty() ? 1 << flags::IS_EMPTY : 0)
    | (is_level_zero_sorted_ ? 1 << flags::IS_LEVEL_ZERO_SORTED : 0)
    | (is_single_item ? 1 << flags::IS_SINGLE_ITEM : 0)
  );
  os.write((char*)&flags_byte, sizeof(flags_byte));
  os.write((char*)&k_, sizeof(k_));
  os.write((char*)&m_, sizeof(m_));
  const uint8_t unused(0);
  os.write((char*)&unused, sizeof(unused));
  if (is_empty()) return;
  if (!is_single_item) {
    os.write((char*)&n_, sizeof(n_));
    os.write((char*)&min_k_, sizeof(min_k_));
    os.write((char*)&num_levels_, sizeof(num_levels_));
    os.write((char*)&unused, sizeof(unused));
    os.write((char*)levels_, sizeof(levels_[0]) * num_levels_);
    S().serialize(os, min_value_, 1);
    S().serialize(os, max_value_, 1);
  }
  S().serialize(os, &items_[levels_[0]], get_num_retained());
}

template<typename T, typename C, typename S, typename A>
std::pair<void_ptr_with_deleter, const size_t> kll_sketch<T, C, S, A>::serialize(unsigned header_size_bytes) const {
  const bool is_single_item = n_ == 1;
  const size_t size = header_size_bytes + get_serialized_size_bytes();
  typedef typename A::template rebind<char>::other AllocChar;
  void_ptr_with_deleter data_ptr(
    static_cast<void*>(AllocChar().allocate(size)),
    [size](void* ptr) { AllocChar().deallocate(static_cast<char*>(ptr), size); }
  );
  char* ptr = static_cast<char*>(data_ptr.get()) + header_size_bytes;
  const uint8_t preamble_ints(is_empty() or is_single_item ? PREAMBLE_INTS_SHORT : PREAMBLE_INTS_FULL);
  copy_to_mem(&preamble_ints, &ptr, sizeof(preamble_ints));
  const uint8_t serial_version(is_single_item ? SERIAL_VERSION_2 : SERIAL_VERSION_1);
  copy_to_mem(&serial_version, &ptr, sizeof(serial_version));
  const uint8_t family(FAMILY);
  copy_to_mem(&family, &ptr, sizeof(family));
  const uint8_t flags_byte(
      (is_empty() ? 1 << flags::IS_EMPTY : 0)
    | (is_level_zero_sorted_ ? 1 << flags::IS_LEVEL_ZERO_SORTED : 0)
    | (is_single_item ? 1 << flags::IS_SINGLE_ITEM : 0)
  );
  copy_to_mem(&flags_byte, &ptr, sizeof(flags_byte));
  copy_to_mem(&k_, &ptr, sizeof(k_));
  copy_to_mem(&m_, &ptr, sizeof(m_));
  const uint8_t unused(0);
  copy_to_mem(&unused, &ptr, sizeof(unused));
  if (!is_empty()) {
    if (!is_single_item) {
      copy_to_mem(&n_, &ptr, sizeof(n_));
      copy_to_mem(&min_k_, &ptr, sizeof(min_k_));
      copy_to_mem(&num_levels_, &ptr, sizeof(num_levels_));
      copy_to_mem(&unused, &ptr, sizeof(unused));
      copy_to_mem(levels_, &ptr, sizeof(levels_[0]) * num_levels_);
      ptr += S().serialize(ptr, min_value_, 1);
      ptr += S().serialize(ptr, max_value_, 1);
    }
    ptr += S().serialize(ptr, &items_[levels_[0]], get_num_retained());
  }
  if (ptr != static_cast<char*>(data_ptr.get()) + size) throw std::logic_error("serialized size mismatch");
  return std::make_pair(std::move(data_ptr), size);
}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A> kll_sketch<T, C, S, A>::deserialize(std::istream& is) {
  uint8_t preamble_ints;
  is.read((char*)&preamble_ints, sizeof(preamble_ints));
  uint8_t serial_version;
  is.read((char*)&serial_version, sizeof(serial_version));
  uint8_t family_id;
  is.read((char*)&family_id, sizeof(family_id));
  uint8_t flags_byte;
  is.read((char*)&flags_byte, sizeof(flags_byte));
  uint16_t k;
  is.read((char*)&k, sizeof(k));
  uint8_t m;
  is.read((char*)&m, sizeof(m));
  uint8_t unused;
  is.read((char*)&unused, sizeof(unused));

  check_m(m);
  check_preamble_ints(preamble_ints, flags_byte);
  check_serial_version(serial_version);
  check_family_id(family_id);

  const bool is_empty(flags_byte & (1 << flags::IS_EMPTY));
  return is_empty ? kll_sketch<T, C, S, A>(k) : kll_sketch<T, C, S, A>(k, flags_byte, is);
}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A> kll_sketch<T, C, S, A>::deserialize(const void* bytes, size_t size) {
  const char* ptr = static_cast<const char*>(bytes);
  uint8_t preamble_ints;
  copy_from_mem(&ptr, &preamble_ints, sizeof(preamble_ints));
  uint8_t serial_version;
  copy_from_mem(&ptr, &serial_version, sizeof(serial_version));
  uint8_t family_id;
  copy_from_mem(&ptr, &family_id, sizeof(family_id));
  uint8_t flags_byte;
  copy_from_mem(&ptr, &flags_byte, sizeof(flags_byte));
  uint16_t k;
  copy_from_mem(&ptr, &k, sizeof(k));
  uint8_t m;
  copy_from_mem(&ptr, &m, sizeof(m));

  check_m(m);
  check_preamble_ints(preamble_ints, flags_byte);
  check_serial_version(serial_version);
  check_family_id(family_id);

  const bool is_empty(flags_byte & (1 << flags::IS_EMPTY));
  return is_empty ? kll_sketch<T, C, S, A>(k) : kll_sketch<T, C, S, A>(k, flags_byte, bytes, size);
}

/*
 * Gets the normalized rank error given k and pmf.
 * k - the configuration parameter
 * pmf - if true, returns the "double-sided" normalized rank error for the get_PMF() function.
 * Otherwise, it is the "single-sided" normalized rank error for all the other queries.
 * Constants were derived as the best fit to 99 percentile empirically measured max error in thousands of trials
 */
template<typename T, typename C, typename S, typename A>
double kll_sketch<T, C, S, A>::get_normalized_rank_error(uint16_t k, bool pmf) {
  return pmf
      ? 2.446 / pow(k, 0.9433)
      : 2.296 / pow(k, 0.9723);
}

// for deserialization
// the common part of the preamble was read and compatibility checks were done
template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::kll_sketch(uint16_t k, uint8_t flags_byte, std::istream& is) {
  k_ = k;
  m_ = DEFAULT_M;
  const bool is_single_item(flags_byte & (1 << flags::IS_SINGLE_ITEM)); // used in serial version 2
  if (is_single_item) {
    n_ = 1;
    min_k_ = k_;
    num_levels_ = 1;
  } else {
    is.read((char*)&n_, sizeof(n_));
    is.read((char*)&min_k_, sizeof(min_k_));
    is.read((char*)&num_levels_, sizeof(num_levels_));
    uint8_t unused;
    is.read((char*)&unused, sizeof(unused));
  }
  levels_ = AllocU32().allocate(num_levels_ + 1);
  levels_size_ = num_levels_ + 1;
  const uint32_t capacity(kll_helper::compute_total_capacity(k_, m_, num_levels_));
  if (is_single_item) {
    levels_[0] = capacity - 1;
  } else {
    // the last integer in levels_ is not serialized because it can be derived
    is.read((char*)levels_, sizeof(levels_[0]) * num_levels_);
  }
  levels_[num_levels_] = capacity;
  min_value_ = A().allocate(1);
  max_value_ = A().allocate(1);
  if (!is_single_item) {
    S().deserialize(is, min_value_, 1);
    S().deserialize(is, max_value_, 1);
  }
  items_ = A().allocate(capacity);
  items_size_ = capacity;
  const auto num_items = levels_[num_levels_] - levels_[0];
  S().deserialize(is, &items_[levels_[0]], num_items);
  if (is_single_item) {
    new (min_value_) T(items_[levels_[0]]);
    new (max_value_) T(items_[levels_[0]]);
  }
  is_level_zero_sorted_ = (flags_byte & (1 << flags::IS_LEVEL_ZERO_SORTED)) > 0;
}

// for deserialization
// the common part of the preamble was read and compatibility checks were done
template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::kll_sketch(uint16_t k, uint8_t flags_byte, const void* bytes, size_t size) {
  k_ = k;
  m_ = DEFAULT_M;
  const bool is_single_item(flags_byte & (1 << flags::IS_SINGLE_ITEM)); // used in serial version 2
  const char* ptr = static_cast<const char*>(bytes) + DATA_START_SINGLE_ITEM;
  if (is_single_item) {
    n_ = 1;
    min_k_ = k_;
    num_levels_ = 1;
  } else {
    copy_from_mem(&ptr, &n_, sizeof(n_));
    copy_from_mem(&ptr, &min_k_, sizeof(min_k_));
    copy_from_mem(&ptr, &num_levels_, sizeof(num_levels_));
    ptr++; // skip unused byte
  }
  levels_ = AllocU32().allocate(num_levels_ + 1);
  levels_size_ = num_levels_ + 1;
  const uint32_t capacity(kll_helper::compute_total_capacity(k_, m_, num_levels_));
  if (is_single_item) {
    levels_[0] = capacity - 1;
  } else {
    // the last integer in levels_ is not serialized because it can be derived
    copy_from_mem(&ptr, levels_, sizeof(levels_[0]) * num_levels_);
  }
  levels_[num_levels_] = capacity;
  min_value_ = A().allocate(1);
  max_value_ = A().allocate(1);
  if (!is_single_item) {
    ptr += S().deserialize(ptr, min_value_, 1);
    ptr += S().deserialize(ptr, max_value_, 1);
  }
  items_ = A().allocate(capacity);
  items_size_ = capacity;
  const auto num_items(levels_[num_levels_] - levels_[0]);
  ptr += S().deserialize(ptr, &items_[levels_[0]], num_items);
  if (is_single_item) {
    new (min_value_) T(items_[levels_[0]]);
    new (max_value_) T(items_[levels_[0]]);
  }
  is_level_zero_sorted_ = (flags_byte & (1 << flags::IS_LEVEL_ZERO_SORTED)) > 0;
  if (ptr != static_cast<const char*>(bytes) + size) throw std::logic_error("deserialized size mismatch");
}

// The following code is only valid in the special case of exactly reaching capacity while updating.
// It cannot be used while merging, while reducing k, or anything else.
template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::compress_while_updating(void) {
  const uint8_t level = find_level_to_compact();

  // It is important to add the new top level right here. Be aware that this operation
  // grows the buffer and shifts the data and also the boundaries of the data and grows the
  // levels array and increments num_levels_
  if (level == (num_levels_ - 1)) {
    add_empty_top_level_to_completely_full_sketch();
  }

  const uint32_t raw_beg = levels_[level];
  const uint32_t raw_lim = levels_[level + 1];
  // +2 is OK because we already added a new top level if necessary
  const uint32_t pop_above = levels_[level + 2] - raw_lim;
  const uint32_t raw_pop = raw_lim - raw_beg;
  const bool odd_pop = kll_helper::is_odd(raw_pop);
  const uint32_t adj_beg = odd_pop ? raw_beg + 1 : raw_beg;
  const uint32_t adj_pop = odd_pop ? raw_pop - 1 : raw_pop;
  const uint32_t half_adj_pop = adj_pop / 2;
  const uint32_t destroy_beg = levels_[0];

  // level zero might not be sorted, so we must sort it if we wish to compact it
  // sort_level_zero() is not used here because of the adjustment for odd number of items
  if ((level == 0) and !is_level_zero_sorted_) {
    std::sort(&items_[adj_beg], &items_[adj_beg + adj_pop], C());
  }
  if (pop_above == 0) {
    kll_helper::randomly_halve_up(items_, adj_beg, adj_pop);
  } else {
    kll_helper::randomly_halve_down(items_, adj_beg, adj_pop);
    kll_helper::merge_sorted_arrays<T, C>(items_, adj_beg, half_adj_pop, raw_lim, pop_above, adj_beg + half_adj_pop);
  }
  levels_[level + 1] -= half_adj_pop; // adjust boundaries of the level above
  if (odd_pop) {
    levels_[level] = levels_[level + 1] - 1; // the current level now contains one item
    if (levels_[level] != raw_beg) items_[levels_[level]] = std::move(items_[raw_beg]); // namely this leftover guy
  } else {
    levels_[level] = levels_[level + 1]; // the current level is now empty
  }

  // verify that we freed up half_adj_pop array slots just below the current level
  assert (levels_[level] == (raw_beg + half_adj_pop));

  // finally, we need to shift up the data in the levels below
  // so that the freed-up space can be used by level zero
  if (level > 0) {
    const uint32_t amount = raw_beg - levels_[0];
    std::move_backward(&items_[levels_[0]], &items_[levels_[0] + amount], &items_[levels_[0] + half_adj_pop + amount]);
    for (uint8_t lvl = 0; lvl < level; lvl++) levels_[lvl] += half_adj_pop;
  }
  for (uint32_t i = 0; i < half_adj_pop; i++) items_[i + destroy_beg].~T();
}

template<typename T, typename C, typename S, typename A>
uint8_t kll_sketch<T, C, S, A>::find_level_to_compact() const {
  uint8_t level = 0;
  while (true) {
    assert (level < num_levels_);
    const uint32_t pop = levels_[level + 1] - levels_[level];
    const uint32_t cap = kll_helper::level_capacity(k_, num_levels_, level, m_);
    if (pop >= cap) {
      return level;
    }
    level++;
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::add_empty_top_level_to_completely_full_sketch() {
  const uint32_t cur_total_cap = levels_[num_levels_];

  // make sure that we are following a certain growth scheme
  assert (levels_[0] == 0);
  assert (items_size_ == cur_total_cap);

  // note that merging MIGHT over-grow levels_, in which case we might not have to grow it here
  const uint8_t new_levels_size = num_levels_ + 2;
  if (levels_size_ < new_levels_size) {
    uint32_t* new_levels = AllocU32().allocate(new_levels_size);
    std::copy(&levels_[0], &levels_[levels_size_], new_levels);
    AllocU32().deallocate(levels_, levels_size_);
    levels_ = new_levels;
    levels_size_ = new_levels_size;
  }

  const uint32_t delta_cap = kll_helper::level_capacity(k_, num_levels_ + 1, 0, m_);
  const uint32_t new_total_cap = cur_total_cap + delta_cap;

  // move (and shift) the current data into the new buffer
  T* new_buf = A().allocate(new_total_cap);
  kll_helper::move_construct<T>(items_, 0, cur_total_cap, new_buf, delta_cap, true);
  A().deallocate(items_, items_size_);
  items_ = new_buf;
  items_size_ = new_total_cap;

  // this loop includes the old "extra" index at the top
  for (uint8_t i = 0; i <= num_levels_; i++) {
    levels_[i] += delta_cap;
  }

  assert (levels_[num_levels_] == new_total_cap);

  num_levels_++;
  levels_[num_levels_] = new_total_cap; // initialize the new "extra" index at the top
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::sort_level_zero() {
  if (!is_level_zero_sorted_) {
    std::sort(&items_[levels_[0]], &items_[levels_[1]], C());
    is_level_zero_sorted_ = true;
  }
}

template<typename T, typename C, typename S, typename A>
std::unique_ptr<kll_quantile_calculator<T, C, A>, std::function<void(kll_quantile_calculator<T, C, A>*)>> kll_sketch<T, C, S, A>::get_quantile_calculator() {
  sort_level_zero();
  typedef typename std::allocator_traits<A>::template rebind_alloc<kll_quantile_calculator<T, C, A>> AllocCalc;
  std::unique_ptr<kll_quantile_calculator<T, C, A>, std::function<void(kll_quantile_calculator<T, C, A>*)>> quantile_calculator(
    new (AllocCalc().allocate(1)) kll_quantile_calculator<T, C, A>(items_, levels_, num_levels_, n_),
    [](kll_quantile_calculator<T, C, A>* ptr){ ptr->~kll_quantile_calculator<T, C, A>(); AllocCalc().deallocate(ptr, 1); }
  );
  return std::move(quantile_calculator);
}

template<typename T, typename C, typename S, typename A>
std::unique_ptr<double[], std::function<void(double*)>> kll_sketch<T, C, S, A>::get_PMF_or_CDF(const T* split_points, uint32_t size, bool is_CDF) const {
  if (is_empty()) return nullptr;
  kll_helper::validate_values<T, C>(split_points, size);
  typedef typename std::allocator_traits<A>::template rebind_alloc<double> AllocD;
  const size_t array_size = size + 1;
  std::unique_ptr<double[], std::function<void(double*)>> buckets(AllocD().allocate(size + 1), [array_size](double* ptr){ AllocD().deallocate(ptr, array_size); });
  std::fill(&buckets.get()[0], &buckets.get()[array_size], 0);
  uint8_t level(0);
  uint64_t weight(1);
  while (level < num_levels_) {
    const auto from_index = levels_[level];
    const auto to_index = levels_[level + 1]; // exclusive
    if ((level == 0) and !is_level_zero_sorted_) {
      increment_buckets_unsorted_level(from_index, to_index, weight, split_points, size, buckets.get());
    } else {
      increment_buckets_sorted_level(from_index, to_index, weight, split_points, size, buckets.get());
    }
    level++;
    weight *= 2;
  }
  // normalize and, if CDF, convert to cumulative
  if (is_CDF) {
    double subtotal = 0;
    for (uint32_t i = 0; i <= size; i++) {
      subtotal += buckets[i];
      buckets[i] = subtotal / n_;
    }
  } else {
    for (uint32_t i = 0; i <= size; i++) {
      buckets[i] /= n_;
    }
  }
  return buckets;
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::increment_buckets_unsorted_level(uint32_t from_index, uint32_t to_index, uint64_t weight,
    const T* split_points, uint32_t size, double* buckets) const
{
  for (uint32_t i = from_index; i < to_index; i++) {
    uint32_t j;
    for (j = 0; j < size; j++) {
      if (C()(items_[i], split_points[j])) {
        break;
      }
    }
    buckets[j] += weight;
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::increment_buckets_sorted_level(uint32_t from_index, uint32_t to_index, uint64_t weight,
    const T* split_points, uint32_t size, double* buckets) const
{
  uint32_t i = from_index;
  uint32_t j = 0;
  while ((i <  to_index) and (j < size)) {
    if (C()(items_[i], split_points[j])) {
      buckets[j] += weight; // this sample goes into this bucket
      i++; // move on to next sample and see whether it also goes into this bucket
    } else {
      j++; // no more samples for this bucket
    }
  }
  // now either i == to_index (we are out of samples), or
  // j == size (we are out of buckets, but there are more samples remaining)
  // we only need to do something in the latter case
  if (j == size) {
    buckets[j] += weight * (to_index - i);
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::merge_higher_levels(const kll_sketch& other, uint64_t final_n) {
  const uint32_t tmp_num_items = get_num_retained() + other.get_num_retained_above_level_zero();
  auto tmp_items_deleter = [tmp_num_items](T* ptr) { A().deallocate(ptr, tmp_num_items); }; // no destructor needed
  const std::unique_ptr<T, decltype(tmp_items_deleter)> workbuf(A().allocate(tmp_num_items), tmp_items_deleter);
  const uint8_t ub = kll_helper::ub_on_num_levels(final_n);
  const size_t work_levels_size = ub + 2; // ub+1 does not work
  auto tmp_levels_deleter = [work_levels_size](uint32_t* ptr) { AllocU32().deallocate(ptr, work_levels_size); };
  const std::unique_ptr<uint32_t[], decltype(tmp_levels_deleter)> worklevels(AllocU32().allocate(work_levels_size), tmp_levels_deleter);
  const std::unique_ptr<uint32_t[], decltype(tmp_levels_deleter)> outlevels(AllocU32().allocate(work_levels_size), tmp_levels_deleter);

  const uint8_t provisional_num_levels = std::max(num_levels_, other.num_levels_);

  populate_work_arrays(other, workbuf.get(), worklevels.get(), provisional_num_levels);

  const kll_helper::compress_result result = kll_helper::general_compress<T, C>(k_, m_, provisional_num_levels, workbuf.get(),
      worklevels.get(), outlevels.get(), is_level_zero_sorted_);

  // ub can sometimes be much bigger
  if (result.final_num_levels > ub) throw std::logic_error("merge error");

  // now we need to transfer the results back into "this" sketch
  if (result.final_capacity != items_size_) {
    A().deallocate(items_, items_size_);
    items_size_ = result.final_capacity;
    items_ = A().allocate(items_size_);
  }
  const uint32_t free_space_at_bottom = result.final_capacity - result.final_num_items;
  kll_helper::move_construct<T>(workbuf.get(), outlevels[0], outlevels[0] + result.final_num_items, items_, free_space_at_bottom, true);

  if (levels_size_ < (result.final_num_levels + 1)) {
    AllocU32().deallocate(levels_, levels_size_);
    levels_size_ = result.final_num_levels + 1;
    levels_ = AllocU32().allocate(levels_size_);
  }
  const uint32_t offset = free_space_at_bottom - outlevels[0];
  for (uint8_t lvl = 0; lvl < levels_size_; lvl++) { // includes the "extra" index
    levels_[lvl] = outlevels[lvl] + offset;
  }
  num_levels_ = result.final_num_levels;
}

// this leaves items_ uninitialized (all objects moved out and destroyed)
template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::populate_work_arrays(const kll_sketch& other, T* workbuf, uint32_t* worklevels, uint8_t provisional_num_levels) {
  worklevels[0] = 0;

  // the level zero data from "other" was already inserted into "this"
  kll_helper::move_construct<T>(items_, levels_[0], levels_[1], workbuf, 0, true);
  worklevels[1] = safe_level_size(0);

  for (uint8_t lvl = 1; lvl < provisional_num_levels; lvl++) {
    const uint32_t self_pop = safe_level_size(lvl);
    const uint32_t other_pop = other.safe_level_size(lvl);
    worklevels[lvl + 1] = worklevels[lvl] + self_pop + other_pop;

    if ((self_pop > 0) and (other_pop == 0)) {
      kll_helper::move_construct<T>(items_, levels_[lvl], levels_[lvl] + self_pop, workbuf, worklevels[lvl], true);
    } else if ((self_pop == 0) and (other_pop > 0)) {
      kll_helper::copy_construct<T>(other.items_, other.levels_[lvl], other.levels_[lvl] + other_pop, workbuf, worklevels[lvl]);
    } else if ((self_pop > 0) and (other_pop > 0)) {
      kll_helper::merge_sorted_arrays<T, C>(items_, levels_[lvl], self_pop, other.items_, other.levels_[lvl], other_pop, workbuf, worklevels[lvl]);
    }
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::assert_correct_total_weight() const {
  const uint64_t total(kll_helper::sum_the_sample_weights(num_levels_, levels_));
  if (total != n_) {
    throw std::logic_error("Total weight does not match N");
  }
}

template<typename T, typename C, typename S, typename A>
uint32_t kll_sketch<T, C, S, A>::safe_level_size(uint8_t level) const {
  if (level >= num_levels_) return 0;
  return levels_[level + 1] - levels_[level];
}

template<typename T, typename C, typename S, typename A>
uint32_t kll_sketch<T, C, S, A>::get_num_retained_above_level_zero() const {
  if (num_levels_ == 1) return 0;
  return levels_[num_levels_] - levels_[1];
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::check_m(uint8_t m) {
  if (m != DEFAULT_M) {
    throw std::invalid_argument("Possible corruption: M must be " + std::to_string(DEFAULT_M)
        + ": " + std::to_string(m));
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::check_preamble_ints(uint8_t preamble_ints, uint8_t flags_byte) {
  const bool is_empty(flags_byte & (1 << flags::IS_EMPTY));
  const bool is_single_item(flags_byte & (1 << flags::IS_SINGLE_ITEM));
  if (is_empty or is_single_item) {
    if (preamble_ints != PREAMBLE_INTS_SHORT) {
      throw std::invalid_argument("Possible corruption: preamble ints must be "
          + std::to_string(PREAMBLE_INTS_SHORT) + " for an empty or single item sketch: " + std::to_string(preamble_ints));
    }
  } else {
    if (preamble_ints != PREAMBLE_INTS_FULL) {
      throw std::invalid_argument("Possible corruption: preamble ints must be "
          + std::to_string(PREAMBLE_INTS_FULL) + " for a sketch with more than one item: " + std::to_string(preamble_ints));
    }
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::check_serial_version(uint8_t serial_version) {
  if (serial_version != SERIAL_VERSION_1 and serial_version != SERIAL_VERSION_2) {
    throw std::invalid_argument("Possible corruption: serial version mismatch: expected "
        + std::to_string(SERIAL_VERSION_1) + " or " + std::to_string(SERIAL_VERSION_2)
        + ", got " + std::to_string(serial_version));
  }
}

template<typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::check_family_id(uint8_t family_id) {
  if (family_id != FAMILY) {
    throw std::invalid_argument("Possible corruption: family mismatch: expected "
        + std::to_string(FAMILY) + ", got " + std::to_string(family_id));
  }
}

template <typename T, typename C, typename S, typename A>
void kll_sketch<T, C, S, A>::to_stream(std::ostream& os, bool print_levels, bool print_items) const {
  os << "### KLL sketch summary:" << std::endl;
  os << "   K              : " << k_ << std::endl;
  os << "   min K          : " << min_k_ << std::endl;
  os << "   M              : " << (unsigned int) m_ << std::endl;
  os << "   N              : " << n_ << std::endl;
  os << "   Epsilon        : " << std::setprecision(3) << get_normalized_rank_error(false) * 100 << "%" << std::endl;
  os << "   Epsilon PMF    : " << get_normalized_rank_error(true) * 100 << "%" << std::endl;
  os << "   Empty          : " << (is_empty() ? "true" : "false") << std::endl;
  os << "   Estimation mode: " << (is_estimation_mode() ? "true" : "false") << std::endl;
  os << "   Levels         : " << (unsigned int) num_levels_ << std::endl;
  os << "   Sorted         : " << (is_level_zero_sorted_ ? "true" : "false") << std::endl;
  os << "   Capacity items : " << items_size_ << std::endl;
  os << "   Retained items : " << get_num_retained() << std::endl;
  os << "   Storage bytes  : " << get_serialized_size_bytes() << std::endl;
  if (!is_empty()) {
    os << "   Min value      : " << *min_value_ << std::endl;
    os << "   Max value      : " << *max_value_ << std::endl;
  }
  os << "### End sketch summary" << std::endl;

  // for debugging
  const bool with_levels(false);
  const bool with_data(false);

  if (with_levels) {
    os << "### KLL sketch levels:" << std::endl;
    os << "   index: nominal capacity, actual size" << std::endl;
    for (uint8_t i = 0; i < num_levels_; i++) {
      os << "   " << (unsigned int) i << ": " << kll_helper::level_capacity(k_, num_levels_, i, m_) << ", " << safe_level_size(i) << std::endl;
    }
    os << "### End sketch levels" << std::endl;
  }

  if (with_data) {
    os << "### KLL sketch data:" << std::endl;
    uint8_t level(0);
    while (level < num_levels_) {
      const uint32_t from_index = levels_[level];
      const uint32_t to_index = levels_[level + 1]; // exclusive
      if (from_index < to_index) {
        os << " level " << (unsigned int) level << ":" << std::endl;
      }
      for (uint32_t i = from_index; i < to_index; i++) {
        os << "   " << items_[i] << std::endl;
      }
      level++;
    }
    os << "### End sketch data" << std::endl;
  }
}

template <typename T, typename C, typename S, typename A>
typename kll_sketch<T, C, S, A>::const_iterator kll_sketch<T, C, S, A>::begin() const {
  return kll_sketch<T, C, S, A>::const_iterator(items_, levels_, num_levels_);
}

template <typename T, typename C, typename S, typename A>
typename kll_sketch<T, C, S, A>::const_iterator kll_sketch<T, C, S, A>::end() const {
  return kll_sketch<T, C, S, A>::const_iterator(nullptr, nullptr, num_levels_);
}

// kll_sketch::const_iterator implementation

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::const_iterator::const_iterator(const T* items, const uint32_t* levels, const uint8_t num_levels):
items(items), levels(levels), num_levels(num_levels), index(levels == nullptr ? 0 : levels[0]), level(levels == nullptr ? num_levels : 0), weight(1)
{}

template<typename T, typename C, typename S, typename A>
kll_sketch<T, C, S, A>::const_iterator::const_iterator(const const_iterator& other):
items(other.items), levels(other.levels), num_levels(other.num_levels), index(other.index), level(other.level), weight(other.weight)
{}

template<typename T, typename C, typename S, typename A>
typename kll_sketch<T, C, S, A>::const_iterator& kll_sketch<T, C, S, A>::const_iterator::operator++() {
  ++index;
  if (index == levels[level + 1]) { // go to the next non-empty level
    do {
      ++level;
      weight *= 2;
    } while (level < num_levels and levels[level] == levels[level + 1]);
  }
  return *this;
}

template<typename T, typename C, typename S, typename A>
typename kll_sketch<T, C, S, A>::const_iterator& kll_sketch<T, C, S, A>::const_iterator::operator++(int) {
  const_iterator tmp(*this);
  operator++();
  return tmp;
}

template<typename T, typename C, typename S, typename A>
bool kll_sketch<T, C, S, A>::const_iterator::operator==(const const_iterator& other) const {
  if (level != other.level) return false;
  if (level == num_levels) return true; // end
  return index == other.index;
}

template<typename T, typename C, typename S, typename A>
bool kll_sketch<T, C, S, A>::const_iterator::operator!=(const const_iterator& other) const {
  return !operator==(other);
}

template<typename T, typename C, typename S, typename A>
const std::pair<const T&, const uint64_t> kll_sketch<T, C, S, A>::const_iterator::operator*() const {
  return std::pair<const T&, const uint64_t>(items[index], weight);
}

} /* namespace datasketches */

#endif
