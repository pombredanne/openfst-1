// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.

#include <fst/fst.h>
#include <fst/compact-fst.h>

using fst::FstRegisterer;
using fst::CompactWeightedStringFst;
using fst::LogArc;
using fst::StdArc;

static FstRegisterer<
    CompactWeightedStringFst<StdArc, uint8>>
    CompactWeightedStringFst_StdArc_uint8_registerer;
static FstRegisterer<
    CompactWeightedStringFst<LogArc, uint8>>
    CompactWeightedStringFst_LogArc_uint8_registerer;
