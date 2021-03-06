// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Concatenates two FSTs.

#include <memory>
#include <string>

#include <fst/script/concat.h>

int main(int argc, char **argv) {
  namespace s = fst::script;
  using fst::script::FstClass;
  using fst::script::MutableFstClass;

  string usage = "Concatenates two FSTs.\n\n  Usage: ";
  usage += argv[0];
  usage += " in1.fst in2.fst [out.fst]\n";

  std::set_new_handler(FailedNewHandler);
  SET_FLAGS(usage.c_str(), &argc, &argv, true);
  if (argc < 3 || argc > 4) {
    ShowUsage();
    return 1;
  }

  string in1_name = strcmp(argv[1], "-") == 0 ? "" : argv[1];
  string in2_name = strcmp(argv[2], "-") == 0 ? "" : argv[2];
  string out_fname = argc > 3 ? argv[3] : "";

  if (in1_name.empty() && in2_name.empty()) {
    LOG(ERROR) << argv[0] << ": Can't take both inputs from standard input.";
    return 1;
  }

  std::unique_ptr<MutableFstClass> fst1(MutableFstClass::Read(in1_name, true));
  if (!fst1) return 1;

  std::unique_ptr<FstClass> fst2(FstClass::Read(in2_name));
  if (!fst2) return 1;

  s::Concat(fst1.get(), *fst2);
  fst1->Write(out_fname);

  return 0;
}
