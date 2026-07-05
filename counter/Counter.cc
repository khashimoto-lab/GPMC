#include "include/gpmc/Counter.h"

#include "counter/CounterImpl.h"
#include "include/gpmc/Preprocessor.h"
#include "include/gpmc/semirings/Integer.h"

namespace gpmc {

// Integer is the natural default: unweighted counting still needs some
// algebra to accumulate model counts in, and arbitrary-precision integers are
// the obvious one. Kept out of Counter.h (a template default can't express
// this without pulling GMP into every includer) and set here instead, where
// the library already carries heavier dependencies (Glucose, etc.).
Counter::Counter() : impl_(std::make_unique<CounterImpl>()) {
    useSemiring<Integer>();
}
Counter::~Counter() = default;

void Counter::setup(CNF& cnf, bool normalize) {
    impl_->config          = config;
    impl_->selector_config = selector_config;
    impl_->setup(cnf, normalize);
}

void Counter::setup(CNF& cnf, const PreprocessingResult& prep_result) {
    impl_->config          = config;
    impl_->selector_config = selector_config;
    impl_->setup(cnf, prep_result);
}

SS   Counter::count()                { return impl_->count(); }
void Counter::printStats() const     { impl_->printStats(); }
const SetupReport& Counter::setupReport() const { return impl_->setupReport(); }
void Counter::set_stop_flag(volatile std::sig_atomic_t* flag) { impl_->set_stop_flag(flag); }
bool Counter::interrupted() const    { return impl_->interrupted(); }
bool Counter::okay() const           { return impl_->okay(); }

}
