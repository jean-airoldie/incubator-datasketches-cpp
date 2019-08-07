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

KllSketch::KllSketch() {
    this->inner = datasketches::kll_sketch<double>();
}

void KllSketch::update(double value) {
    this->inner.update(value);
}

void KllSketch::merge(const KllSketch& other) {
    this->inner.merge(other.inner);
}

bool KllSketch::is_empty() const {
    return this->inner.is_empty();
}

uint64_t KllSketch::size() const {
    return this->inner.get_n();
}

double KllSketch::min_value() const {
    return this->inner.get_min_value();
}

double KllSketch::max_value() const {
    return this->inner.get_max_value();
}

double KllSketch::quantile(double fraction) const {
    return this->inner.get_quantile(fraction);
}

} // END datasketches_wrapper
#endif
