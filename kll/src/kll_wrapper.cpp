#include "kll_wrapper.hpp"

namespace datasketches_wrapper {

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
