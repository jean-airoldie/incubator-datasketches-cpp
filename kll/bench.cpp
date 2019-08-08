#include <ostream>
#include <chrono>

#include <kll_sketch.hpp>
#include <kll_helper.hpp>

using namespace datasketches;
using namespace std;

int main() {
    kll_sketch<float> sketch;
    const int n(1000000);

    auto start = chrono::steady_clock::now();
    for (int i = 0; i < n; i++) {
        sketch.update(i);
    }
    auto end = chrono::steady_clock::now();
    auto diff = end - start;
    std::cout << 1000000 << " update took :"
        << chrono::duration <double, milli> (diff).count() << " ms" << std::endl;

    auto sketch_copy = kll_sketch<float>(sketch);

    start = chrono::steady_clock::now();
    sketch.merge(sketch_copy);
    end = chrono::steady_clock::now();
    diff = end - start;

    std::cout << " merge took :"
        << chrono::duration <double, nano> (diff).count() << " ns" << std::endl;
}
