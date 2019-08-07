#ifndef WRAPPER_HPP_
#define WRAPPER_HPP_

#include "kll_sketch.hpp"

namespace datasketches_wrapper {

class KllSketch {
    public:
        explicit KllSketch();
        ~KllSketch();
        void update(double value);
        void merge(const KllSketch& other);
        bool is_empty() const;
        uint64_t size() const;
        double min_value() const;
        double max_value() const;
        double quantile(double fraction) const;
    private:
        datasketches::kll_sketch<double> inner;
};

} // END datasketches_wrapper
#endif
