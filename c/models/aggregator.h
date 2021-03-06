//------------------------------------------------------------------------------
// Copyright 2018 H2O.ai
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//------------------------------------------------------------------------------
#include <random>
#include <iostream>
#include "frame/py_frame.h"
#include "models/dt_ftrl.h"
#include "models/column_convertor.h"
#include "models/utils.h"
#include "python/_all.h"
#include "python/obj.h"
#include "utils/parallel.h"
#include "utils/shared_mutex.h"
#include "datatablemodule.h"
#include "options.h"
#include "rowindex.h"

// Number of steps for the aggregator progress bar
#define PBSTEPS 100

// Define templated types for Aggregator
template <typename T>
using ccptr = typename std::unique_ptr<ColumnConvertor<T>>;
template <typename T>
using ccptrvec = typename std::vector<ccptr<T>>;


/*
*  Aggregator base class.
*/
class AggregatorBase {
  public :
    virtual void aggregate(DataTable*, dtptr&, dtptr&) = 0;
    virtual ~AggregatorBase();
};


/*
*  Aggregator main template class, where `T` is a type used for all the distance
*  calculations. Templated with either `float` or `double`, aggregator
*  converges to roughly the same number of exemplars, and members distribution.
*  At the same time, using `float` can reduce memory usage.
*/
template <typename T>
class Aggregator : public AggregatorBase {
  public:
    struct exemplar {
      size_t id;
      tptr<T> coords;
    };
    using exptr = std::unique_ptr<exemplar>;
    Aggregator(size_t, size_t, size_t, size_t, size_t, size_t,
               unsigned int, py::oobj, unsigned int);
    void aggregate(DataTable*, dtptr&, dtptr&) override;
    static constexpr T epsilon = std::numeric_limits<T>::epsilon();
    static void set_norm_coeffs(T&, T&, T, T, size_t);

  private:
    // Input parameters and datatable
    const DataTable* dt;
    size_t min_rows;
    size_t n_bins;
    size_t nx_bins;
    size_t ny_bins;
    size_t nd_max_bins;
    size_t max_dimensions;
    unsigned int seed;
    unsigned int nthreads;
    py::oobj progress_fn;

    // Output exemplar and member datatables
    dtptr dt_exemplars;
    dtptr dt_members;

    // Continuous column convertors and datatable with categorical columns
    ccptrvec<T> contconvs;
    dtptr dt_cat;

    // Final aggregation method
    void aggregate_exemplars(bool);

    // Grouping methods, `0d` means no grouping is done
    void group_0d();
    void group_1d();
    void group_1d_continuous();
    void group_1d_categorical();
    void group_2d();
    void group_2d_continuous();
    void group_2d_categorical();
    template<typename U0, typename U1>
    void group_2d_categorical_str();
    void group_2d_mixed();
    template<typename U0>
    void group_2d_mixed_str();
    void group_nd();

    // Random sampling and modular quasi-random generator
    bool sample_exemplars(size_t, size_t);

    // Helper methods
    size_t get_nthreads(size_t nrows);
    void normalize_row(tptr<T>&, size_t);
    void project_row(tptr<T>&, size_t, tptr<T>&);
    tptr<T> generate_pmatrix(size_t ncols);
    T calculate_distance(tptr<T>&, tptr<T>&, size_t, T, bool early_exit = true);
    void adjust_delta(T&, std::vector<exptr>&, std::vector<size_t>&, size_t);
    void adjust_members(std::vector<size_t>&);
    size_t calculate_map(std::vector<size_t>&, size_t);
    void progress(float, int status_code=0);
};


/*
*  Constructor, initialize all the input parameters.
*/
template <typename T>
Aggregator<T>::Aggregator(size_t min_rows_in, size_t n_bins_in,
                          size_t nx_bins_in, size_t ny_bins_in,
                          size_t nd_max_bins_in, size_t max_dimensions_in,
                          unsigned int seed_in, py::oobj progress_fn_in,
                          unsigned int nthreads_in) :
  dt(nullptr),
  min_rows(min_rows_in),
  n_bins(n_bins_in),
  nx_bins(nx_bins_in),
  ny_bins(ny_bins_in),
  nd_max_bins(nd_max_bins_in),
  max_dimensions(max_dimensions_in),
  seed(seed_in),
  nthreads(nthreads_in),
  progress_fn(progress_fn_in)
{
}


/*
*  Main Aggregator method, convert all the numeric columns to `T`,
*  do the corresponding grouping and final exemplar aggregation:
*  - `dt` is an input datatable to aggregate;
*  - `dt_exemplars` is the result of aggregation;
*  - `dt_members` will store a relation between each of the original `dt` rows
*     and the exemplars gathered in `dt_exemplars`.
*/
template <typename T>
void Aggregator<T>::aggregate(DataTable* dt_in,
                              dtptr& dt_exemplars_in,
                              dtptr& dt_members_in)
{
  progress(0.0, 0);
  dt = dt_in;
  bool was_sampled = false;

  Column* col0 = Column::new_data_column(SType::INT32, dt->nrows);
  dt_members = dtptr(new DataTable({col0}, {"exemplar_id"}));

  if (dt->nrows >= min_rows) {
    colvec catcols;
    size_t ncols, max_bins;
    contconvs.reserve(dt->ncols);
    ccptr<T> contconv;

    // Number of possible `N/A` bins for a particular aggregator.
    size_t n_na_bins = 0;

    // Create a column convertor for each numeric columns,
    // and create a vector of categoricals.
    for (size_t i = 0; i < dt->ncols; ++i) {
      bool is_continuous = true;
      Column* col = dt->columns[i];
      switch (col->stype()) {
        case SType::BOOL:    contconv = ccptr<T>(new ColumnConvertorReal<int8_t, T, BoolColumn>(col)); break;
        case SType::INT8:    contconv = ccptr<T>(new ColumnConvertorReal<int8_t, T, IntColumn<int8_t>>(col)); break;
        case SType::INT16:   contconv = ccptr<T>(new ColumnConvertorReal<int16_t, T, IntColumn<int16_t>>(col)); break;
        case SType::INT32:   contconv = ccptr<T>(new ColumnConvertorReal<int32_t, T, IntColumn<int32_t>>(col)); break;
        case SType::INT64:   contconv = ccptr<T>(new ColumnConvertorReal<int64_t, T, IntColumn<int64_t>>(col)); break;
        case SType::FLOAT32: contconv = ccptr<T>(new ColumnConvertorReal<float, T, RealColumn<float>>(col)); break;
        case SType::FLOAT64: contconv = ccptr<T>(new ColumnConvertorReal<double, T, RealColumn<double>>(col)); break;
        default:             if (dt->ncols < 3) {
                               is_continuous = false;
                               catcols.push_back(dt->columns[i]->shallowcopy());
                             }
      }
      if (is_continuous && contconv != nullptr) {
        contconvs.push_back(std::move(contconv));
      }
    }

    dt_cat = dtptr(new DataTable(std::move(catcols)));
    ncols = contconvs.size() + dt_cat->ncols;

    // Depending on number of columns call a corresponding aggregating method.
    // If `dt` has too few rows, do not aggregate it, instead, just sort it by
    // the first column calling `group_0d()`.
    switch (ncols) {
      case 0:  group_0d();
               max_bins = nd_max_bins;
               break;
      case 1:  group_1d();
               max_bins = n_bins;
               n_na_bins = 1;
               break;
      case 2:  group_2d();
               max_bins = nx_bins * ny_bins;
               n_na_bins = 3;
               break;
      default: group_nd();
               max_bins = nd_max_bins;
    }
    // Sample members if we gathered too many exempalrs.
    was_sampled = sample_exemplars(max_bins, n_na_bins);
  } else {
    group_0d();
  }

  // Do not aggregate `dt` in-place, instead, make a shallow copy
  // and apply rowindex based on the `exemplar_id`s gathered in `dt_members`.
  dt_exemplars = dtptr(dt->copy());
  aggregate_exemplars(was_sampled);
  dt_exemplars_in = std::move(dt_exemplars);
  dt_members_in = std::move(dt_members);

  // We do not need a pointer to the original datatable anymore,
  // also clear vector of column convertors and datatable with categoricals
  dt = nullptr;
  contconvs.clear();
  dt_cat = nullptr;
  progress(1.0, 1);
}


/*
*  Check how many exemplars we have got, if there is more than `max_bins+1`
*  (e.g. too many distinct categorical values) do random sampling.
*/
template <typename T>
bool Aggregator<T>::sample_exemplars(size_t max_bins, size_t n_na_bins)
{
  bool was_sampled = false;

  // Sorting `dt_members` to calculate total number of exemplars.
  std::vector<sort_spec> spec = {sort_spec(0)};
  auto res = dt_members->group(spec);
  RowIndex ri_members = std::move(res.first);
  Groupby gb_members = std::move(res.second);

  // Do random sampling if there is too many exemplars, `n_na_bins` accounts
  // for the additional N/A bins that may appear during grouping.
  if (gb_members.ngroups() > max_bins + n_na_bins) {
    const int32_t* offsets = gb_members.offsets_r();
    auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());

    // First, set all `exemplar_id`s to `N/A`.
    for (size_t i = 0; i < dt_members->nrows; ++i) {
      d_members[i] = GETNA<int32_t>();
    }

    // Second, randomly select `max_bins` groups.
    if (!seed) {
      std::random_device rd;
      seed = rd();
    }
    srand(seed);
    size_t k = 0;
    while (k < max_bins) {
      int32_t i = rand() % static_cast<int32_t>(gb_members.ngroups());
      size_t off_i = static_cast<size_t>(offsets[i]);
      if (ISNA<int32_t>(d_members[ri_members[off_i]])) {
        size_t off_i1 = static_cast<size_t>(offsets[i + 1]);
        for (size_t j = off_i; j < off_i1; ++j) {
          d_members[ri_members[j]] = static_cast<int32_t>(k);
        }
        k++;
      }
    }
    dt_members->columns[0]->get_stats()->reset();
    was_sampled = true;
  }

  return was_sampled;
}


/*
*  Sort/group the members frame and set up the first member
*  in each group as an exemplar with the corresponding `members_count`,
*  that is essentially a number of members within the group.
*  If members were randomly sampled, those who got `exemplar_id == NA`
*  are ending up in the zero group, that is ignored and not included
*  in the aggregated frame.
*/
template <typename T>
void Aggregator<T>::aggregate_exemplars(bool was_sampled) {
  // Setting up offsets and members row index.
  std::vector<sort_spec> spec = {sort_spec(0)};
  auto res = dt_members->group(spec);
  RowIndex ri_members = std::move(res.first);
  Groupby gb_members = std::move(res.second);

  const int32_t* offsets = gb_members.offsets_r();
  size_t n_exemplars = gb_members.ngroups() - was_sampled;
  arr32_t exemplar_indices(n_exemplars);

  // Setting up a table for counts
  Column* col = Column::new_data_column(SType::INT32, n_exemplars);
  dtptr dt_counts = dtptr(new DataTable({col}, {"members_count"}));
  auto d_counts = static_cast<int32_t*>(col->data_w());
  std::memset(d_counts, 0, n_exemplars * sizeof(int32_t));

  // Setting up exemplar indices and counts
  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  for (size_t i = was_sampled; i < gb_members.ngroups(); ++i) {
    size_t i_sampled = i - was_sampled;
    size_t off_i = static_cast<size_t>(offsets[i]);
    exemplar_indices[i_sampled] = static_cast<int32_t>(ri_members[off_i]);
    d_counts[i_sampled] = offsets[i+1] - offsets[i];
  }

  // Replacing group ids with the actual `exemplar_id`s, because
  // - for 1D and 2D binnings some bins may be empty;
  // - for ND we could do id to re-mapping.
  #pragma omp parallel for schedule(static)
  for (size_t i = was_sampled; i < gb_members.ngroups(); ++i) {
    size_t i_sampled = i - was_sampled;
    for (size_t j = 0; j < static_cast<size_t>(d_counts[i_sampled]); ++j) {
      size_t member_shift = static_cast<size_t>(offsets[i]) + j;
      d_members[ri_members[member_shift]] = static_cast<int32_t>(i_sampled);
    }
  }
  dt_members->columns[0]->get_stats()->reset();

  // Applying exemplars row index and binding exemplars with the counts.
  RowIndex ri_exemplars = RowIndex(std::move(exemplar_indices));
  dt_exemplars->apply_rowindex(ri_exemplars);
  std::vector<DataTable*> dts = { dt_counts.release() };
  dt_exemplars->cbind(dts);
}


/*
*  Do no grouping, i.e. all rows become exemplars sorted by the first column.
*/
template <typename T>
void Aggregator<T>::group_0d() {
  if (dt->ncols > 0) {
    std::vector<sort_spec> spec = {sort_spec(0, false, false, true)};
    auto res = dt->group(spec);
    RowIndex ri_exemplars = std::move(res.first);

    auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
    ri_exemplars.iterate(0, dt->nrows, 1,
      [&](size_t i, size_t j) {
        d_members[j] = static_cast<int32_t>(i);
      });
  }
}


/*
*  Call an appropriate function for 1D grouping.
*/
template <typename T>
void Aggregator<T>::group_1d() {
  if (contconvs.size()) {
    group_1d_continuous();
  } else {
    group_1d_categorical();
  }
}


/*
*  Call an appropriate function for 2D grouping.
*  Dealing with NA's:
*    - (value, NA) goes to bin -1;
*    - (NA, value) goes to bin -2;
*    - (NA, NA)    goes to bin -3.
*  Rows having no NA's end up in the corresponding positive bins,
*  so that we are not mixing NA and not NA members. After calling
*  `aggregate_exemplars(...)` bins will be renumbered starting from 0,
*  with NA bins (if ones exist) being gathered at the very beginning
*  of the exemplar data frame.
*/
template <typename T>
void Aggregator<T>::group_2d() {
  size_t ncont = contconvs.size();

  switch (ncont) {
    case 0:  group_2d_categorical(); break;
    case 1:  group_2d_mixed(); break;
    case 2:  group_2d_continuous(); break;
    default: throw ValueError() << "Got datatable with too many columns "
                                << "for 2D aggregation:  " << ncont;
  }
}


/*
*  Do 1D grouping for a continuous column, i.e. 1D binning.
*/
template <typename T>
void Aggregator<T>::group_1d_continuous() {
  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  T norm_factor, norm_shift;
  set_norm_coeffs(norm_factor, norm_shift, (*contconvs[0]).get_min(), (*contconvs[0]).get_max(), n_bins);

  #pragma omp parallel for schedule(static)
  for (size_t i = 0; i < (*contconvs[0]).get_nrows(); ++i) {
    T value = (*contconvs[0])[i];
    if (ISNA<T>(value)) {
      d_members[i] = GETNA<int32_t>();
    } else {
      d_members[i] = static_cast<int32_t>(norm_factor * value + norm_shift);
    }
  }
}


/*
*  Do 2D grouping for two continuous columns, i.e. 2D binning.
*/
template <typename T>
void Aggregator<T>::group_2d_continuous() {
  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());

  T normx_factor, normx_shift;
  T normy_factor, normy_shift;
  set_norm_coeffs(normx_factor, normx_shift, (*contconvs[0]).get_min(), (*contconvs[0]).get_max(), nx_bins);
  set_norm_coeffs(normy_factor, normy_shift, (*contconvs[1]).get_min(), (*contconvs[1]).get_max(), ny_bins);

  #pragma omp parallel for schedule(static)
  for (size_t i = 0; i < (*contconvs[0]).get_nrows(); ++i) {
    T value0 = (*contconvs[0])[i];
    T value1 = (*contconvs[1])[i];
    int32_t na_case = ISNA<T>(value0) + 2 * ISNA<T>(value1);
    if (na_case) {
      d_members[i] = -na_case;
    } else {
      d_members[i] = static_cast<int32_t>(normy_factor * value1 + normy_shift) *
                     static_cast<int32_t>(nx_bins) +
                     static_cast<int32_t>(normx_factor * value0 + normx_shift);
    }
  }
}


/*
*  Do 1D grouping for a categorical column, i.e. just a `group by` operation.
*/
template <typename T>
void Aggregator<T>::group_1d_categorical() {
  std::vector<sort_spec> spec = {sort_spec(0)};
  auto res = dt_cat->group(spec);
  RowIndex ri0 = std::move(res.first);
  Groupby grpby0 = std::move(res.second);

  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  const int32_t* offsets0 = grpby0.offsets_r();

  #pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < grpby0.ngroups(); ++i) {
    size_t off_i = static_cast<size_t>(offsets0[i]);
    size_t off_i1 = static_cast<size_t>(offsets0[i+1]);
    for (size_t j = off_i; j < off_i1; ++j) {
      d_members[ri0[j]] = static_cast<int32_t>(i);
    }
  }
}


/*
*  Detect string types for both categorical columns and do a corresponding call
*  to `group_2d_mixed_str`.
*/
template <typename T>
void Aggregator<T>::group_2d_categorical () {
  switch (dt_cat->columns[0]->stype()) {
    case SType::STR32:  switch (dt_cat->columns[1]->stype()) {
                          case SType::STR32:  group_2d_categorical_str<uint32_t, uint32_t>(); break;
                          case SType::STR64:  group_2d_categorical_str<uint32_t, uint64_t>(); break;
                          default:            throw ValueError() << "For 2D categorical aggregation, all column types"
                                                                 << "should be either STR32 or STR64";
                        }
                        break;

    case SType::STR64:  switch (dt_cat->columns[1]->stype()) {
                          case SType::STR32:  group_2d_categorical_str<uint64_t, uint32_t>(); break;
                          case SType::STR64:  group_2d_categorical_str<uint64_t, uint64_t>(); break;
                          default:            throw ValueError() << "For 2D categorical aggregation, all column types"
                                                                 << "should be either STR32 or STR64";
                        }
                        break;

    default:            throw ValueError() << "In 2D categorical aggregator column types"
                                           << "should be either STR32 or STR64";
  }
}


/*
*  Do 2D grouping for two categorical columns, i.e. two `group by` operations,
*  and combine their results.
*/
template <typename T>
template <typename U0, typename U1>
void Aggregator<T>::group_2d_categorical_str() {

  std::vector<sort_spec> spec = {sort_spec(0), sort_spec(1)};
  auto res = dt_cat->group(spec);
  RowIndex ri = std::move(res.first);
  Groupby grpby = std::move(res.second);

  auto c0 = static_cast<const StringColumn<U0>*>(dt_cat->columns[0]);
  auto c1 = static_cast<const StringColumn<U1>*>(dt_cat->columns[1]);
  const U0* d_c0 = c0->offsets();
  const U1* d_c1 = c1->offsets();

  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  const int32_t* offsets = grpby.offsets_r();

  #pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < grpby.ngroups(); ++i) {
    auto group_id = static_cast<int32_t>(i);
    size_t off_i = static_cast<size_t>(offsets[i]);
    size_t off_i1 = static_cast<size_t>(offsets[i+1]);
    for (size_t j = off_i; j < off_i1; ++j) {
      int32_t gi = static_cast<int32_t>(ri[j]);
      int32_t na_case = ISNA<U0>(d_c0[gi]) + 2 * ISNA<U1>(d_c1[gi]);
      if (na_case) {
        d_members[gi] = -na_case;
      } else {
        d_members[gi] = group_id;
      }
    }
  }
}


/*
*  Detect string type for a categorical column and do a corresponding call
*  to `group_2d_mixed_str`.
*/
template <typename T>
void Aggregator<T>::group_2d_mixed() {
  switch (dt_cat->columns[0]->stype()) {
    case SType::STR32:  group_2d_mixed_str<uint32_t>(); break;
    case SType::STR64:  group_2d_mixed_str<uint64_t>(); break;
    default:            throw ValueError() << "For 2D mixed aggretation, the categorical column "
                                           << "type should be either STR32 or STR64";
  }
}


/*
*  Do 2D grouping for one continuous and one categorical string column,
*  i.e. 1D binning for the continuous column and a `group by`
*  operation for the categorical one.
*/
template<typename T>
template<typename U0>
void Aggregator<T>::group_2d_mixed_str() {
  auto c_cat = static_cast<const StringColumn<U0>*>(dt_cat->columns[0]);
  const U0* d_cat = c_cat->offsets();

  std::vector<sort_spec> spec = {sort_spec(0)};
  auto res = dt_cat->group(spec);
  RowIndex ri_cat = std::move(res.first);
  Groupby grpby = std::move(res.second);

  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  const int32_t* offsets_cat = grpby.offsets_r();

  T normx_factor, normx_shift;
  set_norm_coeffs(normx_factor, normx_shift, (*contconvs[0]).get_min(), (*contconvs[0]).get_max(), nx_bins);

  #pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < grpby.ngroups(); ++i) {
    int32_t group_cat_id = static_cast<int32_t>(nx_bins * i);
    size_t off_i = static_cast<size_t>(offsets_cat[i]);
    size_t off_i1 = static_cast<size_t>(offsets_cat[i+1]);
    for (size_t j = off_i; j < off_i1; ++j) {
      size_t gi = ri_cat[j];
      int32_t na_case = ISNA<T>((*contconvs[0])[gi]) + 2 * ISNA<U0>(d_cat[gi]);
      if (na_case) {
        d_members[gi] = -na_case;
      } else {
        d_members[gi] = group_cat_id +
                        static_cast<int32_t>(normx_factor * (*contconvs[0])[gi] + normx_shift);
      }
    }
  }
}


/*
*  Do ND grouping in the general case. The initial `delta` (`delta = radius^2`)
*  is set to machine precision, so that we are gathering some initial exemplars.
*  When this `delta` starts getting us more exemplars than is set by `nd_max_bins`
*  do the following:
*  - find the mean distance between all the gathered exemplars;
*  - merge all the exemplars that are within half of this distance;
*  - adjust `delta` taking into account initial size of bubbles;
*  - store the merging info and use it in `adjust_members(...)`.
*
*  Another approach is to have a constant `delta` see `Develop` branch
*  https://github.com/h2oai/vis-data-server/blob/master/library/src/main/java/com/
*  h2o/data/Aggregator.java based on the estimates given at
*  https://mathoverflow.net/questions/308018/coverage-of-balls-on-random-points-in-
*  euclidean-space?answertab=active#tab-top i.e.
*
*  T radius2 = (d / 6.0) - 1.744 * sqrt(7.0 * d / 180.0);
*  T radius = (d > 4)? .5 * sqrt(radius2) : .5 / pow(100.0, 1.0 / d);
*  if (d > max_dimensions) {
*    radius /= 7.0;
*  }
*  T delta = radius * radius;
*
*  However, for some datasets this `delta` results in too many (e.g. thousands) or
*  too few (e.g. just one) exemplars.
*/
template <typename T>
void Aggregator<T>::group_nd() {
  OmpExceptionManager oem;
  dt::shared_bmutex shmutex;
  size_t ncols = contconvs.size();
  size_t nrows = (*contconvs[0]).get_nrows();
  size_t ndims = std::min(max_dimensions, ncols);
  std::vector<exptr> exemplars;
  std::vector<size_t> ids;
  std::vector<size_t> coprimes;
  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  tptr<T> pmatrix = nullptr;
  bool do_projection = ncols > max_dimensions;
  if (do_projection) pmatrix = generate_pmatrix(ncols);

  // Figuring out how many threads to use.
  size_t nth0 = get_nthreads(nrows);

  // Start with a very small `delta`, that is Euclidean distance squared.
  T delta = epsilon;
  // Exemplar counter, if doesn't match thread local value, it means
  // some new exemplars were added (and may be even `delta` was adjusted)
  // meanwhile, so restart is needed for the `test_member` procedure.
  size_t ecounter = 0;

  #pragma omp parallel num_threads(nth0)
  {
    size_t ith = static_cast<size_t>(omp_get_thread_num());
    size_t nth = static_cast<size_t>(omp_get_num_threads());
    size_t rstep = (nrows > nth * PBSTEPS)? nrows / (nth * PBSTEPS) : 1;
    T distance;
    auto member = tptr<T>(new T[ndims]);
    size_t ecounter_local;

    // Each thread gets its own seed
    std::default_random_engine generator(seed + static_cast<unsigned int>(ith));

    try {
      // Main loop over all the rows
      for (size_t i = ith; i < nrows; i += nth) {
        bool is_exemplar = true;
        do_projection? project_row(member, i, pmatrix) : normalize_row(member, i);

        test_member: {
          dt::shared_lock<dt::shared_bmutex> lock(shmutex, /* exclusive = */ false);
          ecounter_local = ecounter;
          size_t nexemplars = exemplars.size();
          size_t ncoprimes = coprimes.size();

          // Generate random exemplar and coprime vector indices.
          // When `nexemplars` is zero, this may be any `size_t` number,
          // however, since we do not do any member testing in this case,
          // this is not an issue.
          std::uniform_int_distribution<size_t> exemplars_dist(0, nexemplars - 1);
          std::uniform_int_distribution<size_t> coprimes_dist(0, ncoprimes - 1);
          size_t exemplar_index = exemplars_dist(generator);
          size_t coprime_index = coprimes_dist(generator);

          // Instead of traversing exemplars in the order they appear
          // in the `exemplars` vector, we use modular quasi-random
          // paths. This ensures we get more uniform member distribution
          // across the clusters. Since `coprimes[coprime_index]` and
          // `nexemplars` are coprimes, `j` will take all the integer values
          // in the range [0; nexemplars - 1], where
          // - `exemplar_index` determines at which exemplar we start testing;
          // - `coprime_index` is a "seed" to the modular generator.
          for (size_t k = 0; k < nexemplars; ++k) {
            size_t j = (k * coprimes[coprime_index] + exemplar_index) % nexemplars;
            // Note, this distance will depend on delta, because
            // `early_exit = true` by default
            distance = calculate_distance(member, exemplars[j]->coords, ndims, delta);
            if (distance < delta) {
              d_members[i] = static_cast<int32_t>(exemplars[j]->id);
              is_exemplar = false;
              break;
            }
          }
        }

        if (is_exemplar) {
          dt::shared_lock<dt::shared_bmutex> lock(shmutex, /* exclusive = */ true);
          if (ecounter_local == ecounter) {
            ecounter++;
            exptr e = exptr(new exemplar{ids.size(), std::move(member)});
            member = tptr<T>(new T[ndims]);
            ids.push_back(e->id);
            d_members[i] = static_cast<int32_t>(e->id);
            exemplars.push_back(std::move(e));
            if (exemplars.size() > nd_max_bins) {
              adjust_delta(delta, exemplars, ids, ndims);
            }
            calculate_coprimes(exemplars.size(), coprimes);
          } else {
            goto test_member;
          }
        }

        #pragma omp master
        if ((i / nth) % rstep == 0) progress(static_cast<float>(i+1) / nrows);
      } // End main loop over all the rows
    } catch (...) {
      oem.capture_exception();
    }
  }
  oem.rethrow_exception_if_any();
  adjust_members(ids);
}


/*
 *  Figure out how many threads we need to run ND groupping.
 */
 template <typename T>
size_t Aggregator<T>::get_nthreads(size_t nrows) {
  size_t nth;
  if (nthreads) {
    nth = nthreads;
  } else {
    nth = std::min(static_cast<size_t>(config::nthreads), nrows);
  }
  return nth;
}

/*
*  Adjust `delta` (i.e. `radius^2`) based on the mean distance between
*  the gathered exemplars and merge all the exemplars within that distance.
*  Here we will just use an additional index `k` to map triangular matrix
*  into 1D array of distances. However, one can also use mapping from `k` to `(i,j)`:
*  i = n - 2 - floor(sqrt(-8 * k + 4 * n * (n - 1) - 7) / 2.0 - 0.5);
*  j = k + i + 1 - n * (n - 1) / 2 + (n - i) * ((n - i) - 1) / 2;
*  and mapping from `(i,j)` to `k`:
*  k = (2 * n - i - 1 ) * i / 2 + j
*/
template <typename T>
void Aggregator<T>::adjust_delta(T& delta, std::vector<exptr>& exemplars,
                                 std::vector<size_t>& ids, size_t ndims) {
  size_t n = exemplars.size();
  size_t n_distances = (n * n - n) / 2;
  size_t k = 0;
  tptr<T> deltas(new T[n_distances]);
  T total_distance = 0.0;

  for (size_t i = 0; i < n - 1; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      T distance = calculate_distance(exemplars[i]->coords,
                                      exemplars[j]->coords,
                                      ndims,
                                      delta,
                                      false);
      total_distance += sqrt(distance);
      deltas[k] = distance;
      k++;
    }
  }

  // Use `delta_merge` for merging exemplars.
  T delta_merge = pow(static_cast<T>(0.5) * total_distance / n_distances, static_cast<T>(2));

  // When exemplars are merged, all members will be within their `delta`,
  // not `delta_merge`. For that, update delta by taking into account size
  // of initial bubble.
  delta += delta_merge + 2 * sqrt(delta * delta_merge);


  // Set exemplars that have to be merged to `nullptr`.
  k = 0;
  for (size_t i = 0; i < n - 1; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (deltas[k] < delta_merge && exemplars[i] != nullptr && exemplars[j] != nullptr) {
        ids[exemplars[j]->id] = exemplars[i]->id;
        exemplars[j] = nullptr;
      }
      k++;
    }
  }

  // Remove all the `nullptr` exemplars from the vector.
  exemplars.erase(remove(begin(exemplars),
                  end(exemplars),
                  nullptr),
                  end(exemplars));
}


/*
*  Based on the merging info adjust the members information,
*  i.e. set which exemplar they belong to.
*/
template <typename T>
void Aggregator<T>::adjust_members(std::vector<size_t>& ids) {

  auto d_members = static_cast<int32_t*>(dt_members->columns[0]->data_w());
  auto map = std::unique_ptr<size_t[]>(new size_t[ids.size()]);

  #pragma omp parallel for schedule(static)
  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] == i) {
      map[i] = i;
    } else {
      map[i] = calculate_map(ids, i);
    }
  }

  #pragma omp parallel for schedule(static)
  for (size_t i = 0; i < dt_members->nrows; ++i) {
    auto j = static_cast<size_t>(d_members[i]);
    d_members[i] = static_cast<int32_t>(map[j]);
  }

}


/*
*  For each exemplar find the one it was merged to.
*/
template <typename T>
size_t Aggregator<T>::calculate_map(std::vector<size_t>& ids, size_t id) {
  if (id == ids[id]) {
    return id;
  } else {
    return calculate_map(ids, ids[id]);
  }
}


/*
*  Calculate distance between two vectors. If `early_exit` is set to `true`,
*  stop when the distance reaches `delta`.
*/
template <typename T>
T Aggregator<T>::calculate_distance(tptr<T>& e1, tptr<T>& e2,
                                    size_t ndims, T delta,
                                    bool early_exit /*=true*/) {
  T sum = 0.0;
  int32_t n = 0;

  for (size_t i = 0; i < ndims; ++i) {
    if (ISNA<T>(e1[i]) || ISNA<T>(e2[i])) {
      continue;
    }
    ++n;
    sum += (e1[i] - e2[i]) * (e1[i] - e2[i]);
    if (early_exit && sum > delta) return sum; // i/n normalization here?
  }

  return sum * ndims / n;
}


/*
*  Normalize the row elements to [0,1).
*/
template <typename T>
void Aggregator<T>::normalize_row(tptr<T>& r, size_t row) {
  for (size_t i = 0; i < contconvs.size(); ++i) {
    T norm_factor, norm_shift;
    T value = (*contconvs[i])[row];
    set_norm_coeffs(norm_factor, norm_shift, (*contconvs[i]).get_min(), (*contconvs[i]).get_max(), 1);
    r[i] =  norm_factor * value + norm_shift;
  }
}


/*
*  Project a particular row on a subspace by using the projection matrix.
*/
template <typename T>
void Aggregator<T>::project_row(tptr<T>& r, size_t row, tptr<T>& pmatrix)
{
  std::memset(r.get(), 0, max_dimensions * sizeof(T));
  int32_t n = 0;
  for (size_t i = 0; i < contconvs.size(); ++i) {
    T value = (*contconvs[i])[row];
    if (!ISNA<T>(value)) {
      T norm_factor, norm_shift;
      set_norm_coeffs(norm_factor, norm_shift, (*contconvs[i]).get_min(), (*contconvs[i]).get_max(), 1);
      T norm_row = norm_factor * value + norm_shift;
      for (size_t j = 0; j < max_dimensions; ++j) {
        r[j] +=  pmatrix[i * max_dimensions + j] * norm_row;
      }
      ++n;
    }
  }
  for (size_t j = 0; j < max_dimensions; ++j) {
    r[j] /= n;
  }
}


/*
*  Generate projection matrix.
*/
template <typename T>
std::unique_ptr<T[]> Aggregator<T>::generate_pmatrix(size_t ncols) {
  std::default_random_engine generator;
  auto pmatrix = tptr<T>(new T[ncols * max_dimensions]);

  if (!seed) {
    std::random_device rd;
    seed = rd();
  }

  generator.seed(seed);
  std::normal_distribution<T> distribution(0.0, 1.0);

  #pragma omp parallel for schedule(static)
  for (size_t i = 0; i < ncols * max_dimensions; ++i) {
    pmatrix[i] = distribution(generator);
  }

  return pmatrix;
}


/*
*  To normalize a continuous column x to [0; 1] range we use
*  the following formula: x_i_new = (x_i - min) / (max - min),
*  where x_i is the value stored in the i-th row, max and min are the column
*  maximum and minimum, respectively. To save on arithmetics, this can be
*  easily converted to x_i_new = x_i * norm_factor + norm_shift,
*  where norm_factor = 1 / (max - min) and norm_shift = - min / (max - min).
*  When max = min, i.e. the column is constant, there is a singularity
*  that may lead to wrong distance calculations done by the Aggregator.
*  One of the approaches here is to handle the constant columns separately
*  and set their values to 0.5, i.e. norm_factor = 0 and norm_shift = 0.5.
*/
template <typename T>
void Aggregator<T>::set_norm_coeffs(T& norm_factor, T& norm_shift,
                                    T c_min, T c_max, size_t c_bins) {
  if (fabs(c_max - c_min) > epsilon) {
    norm_factor = c_bins * (1 - epsilon) / (c_max - c_min);
    norm_shift = -norm_factor * c_min;
  } else {
    norm_factor = static_cast<T>(0.0);
    norm_shift =  static_cast<T>(0.5) * c_bins;
  }
}


/*
*  Helper function to invoke the Python progress function if supplied,
*  otherwise just print the progress bar.
*/
template <typename T>
void Aggregator<T>::progress(float progress, int status_code /*= 0*/) {
  if (progress_fn.is_none()) {
    print_progress(progress, status_code);
  } else {
    progress_fn.call({py::ofloat(progress), py::oint(status_code)});
  }
}
