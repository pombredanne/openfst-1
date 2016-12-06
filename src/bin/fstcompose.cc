// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Composes two FSTs.

#include <memory>
#include <string>

#include <fst/script/compose.h>
#include <fst/script/getters.h>

DEFINE_string(compose_filter, "auto",
              "Composition filter, one of: \"alt_sequence\", \"auto\", "
              "\"match\", \"null\", \"sequence\", \"trivial\"");
DEFINE_bool(connect, true, "Trim output");

int main(int argc, char **argv) {
  namespace s = fst::script;
  using fst::script::FstClass;
  using fst::script::MutableFstClass;
  using fst::script::VectorFstClass;

  string usage = "Composes two FSTs.\n\n  Usage: ";
  usage += argv[0];
  usage += " in1.fst in2.fst [out.fst]\n";

  std::set_new_handler(FailedNewHandler);
  SET_FLAGS(usage.c_str(), &argc, &argv, true);
  if (argc < 3 || argc > 4) {
    ShowUsage();
    return 1;
  }

  string in1_name = strcmp(argv[1], "-") != 0 ? argv[1] : "";
  string in2_name = (argc > 2 && (strcmp(argv[2], "-") != 0)) ? argv[2] : "";
  string out_name = argc > 3 ? argv[3] : "";

  if (in1_name.empty() && in2_name.empty()) {
    LOG(ERROR) << argv[0] << ": Can't take both inputs from standard input.";
    return 1;
  }

  std::unique_ptr<FstClass> ifst1(FstClass::Read(in1_name));
  if (!ifst1) return 1;

  std::unique_ptr<FstClass> ifst2(FstClass::Read(in2_name));
  if (!ifst2) return 1;

  if (ifst1->ArcType() != ifst2->ArcType()) {
    LOG(ERROR) << argv[0] << ": Input FSTs must have the same arc type.";
    return 1;
  }

  VectorFstClass ofst(ifst1->ArcType());

  fst::ComposeFilter compose_filter;
  if (!s::GetComposeFilter(FLAGS_compose_filter, &compose_filter)) {
    LOG(ERROR) << argv[0] << ": Unknown or unsupported compose filter type: "
               << FLAGS_compose_filter;
    return 1;
  }

  fst::ComposeOptions opts(FLAGS_connect, compose_filter);

  s::Compose(*ifst1, *ifst2, &ofst, opts);

  ofst.Write(out_name);

  return 0;
}