// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// This file defines the registration mechanism for new operations.
// These operations are designed to enable scripts to work with FST classes
// at a high level.
//
// If you have a new arc type and want these operations to work with FSTs
// with that arc type, see below for the registration steps
// you must take.
//
// These methods are only recommended for use in high-level scripting
// applications. Most users should use the lower-level templated versions
// corresponding to these.
//
// If you have a new arc type you'd like these operations to work with,
// use the REGISTER_FST_OPERATIONS macro defined in fstscript.h.
//
// If you have a custom operation you'd like to define, you need four
// components. In the following, assume you want to create a new operation
// with the signature
//
//    void Foo(const FstClass &ifst, MutableFstClass *ofst);
//
//  You need:
//
//  1) A way to bundle the args that your new Foo operation will take, as
//     a single struct. The template structs in arg-packs.h provide a handy
//     way to do this. In Foo's case, that might look like this:
//
//       typedef args::Package<const FstClass &,
//                             MutableFstClass *> FooArgs;
//
//     Note: this package of args is going to be passed by non-const pointer.
//
//  2) A function template that is able to perform Foo, given the args and
//     arc type. Yours might look like this:
//
//       template<class Arc>
//       void Foo(FooArgs *args) {
//          // Pull out the actual, arc-templated FSTs
//          const Fst<Arc> &ifst = args->arg1.GetFst<Arc>();
//          MutableFst<Arc> *ofst = args->arg2->GetMutableFst<Arc>();
//
//          // actually perform foo on ifst and ofst...
//       }
//
//  3) a client-facing function for your operation. This would look like
//     the following:
//
//     void Foo(const FstClass &ifst, MutableFstClass *ofst) {
//       // Check that the arc types of the FSTs match
//       if (!ArcTypesMatch(ifst, *ofst, "Foo")) return;
//       // package the args
//       FooArgs args(ifst, ofst);
//       // Finally, call the operation
//       Apply<Operation<FooArgs>>("Foo", ifst->ArcType(), &args);
//     }
//
//  The Apply<> function template takes care of the link between 2 and 3,
//  provided you also have:
//
//  4) A registration for your new operation, on the arc types you care about.
//     This can be provided easily by the REGISTER_FST_OPERATION macro in
//     operations.h:
//
//       REGISTER_FST_OPERATION(Foo, StdArc, FooArgs);
//       REGISTER_FST_OPERATION(Foo, MyArc, FooArgs);
//       // .. etc
//
//
//  That's it! Now when you call Foo(const FstClass &, MutableFstClass *),
//  it dispatches (in #3) via the Apply<> function to the correct
//  instantiation of the template function in #2.
//

#ifndef FST_SCRIPT_SCRIPT_IMPL_H_
#define FST_SCRIPT_SCRIPT_IMPL_H_

//
// This file contains general-purpose templates which are used in the
// implementation of the operations.
//

#include <string>
#include <utility>

#include <fst/generic-register.h>
#include <fst/script/arg-packs.h>
#include <fst/script/fst-class.h>

#include <fst/types.h>

namespace fst {
namespace script {

enum RandArcSelection {
  UNIFORM_ARC_SELECTOR,
  LOG_PROB_ARC_SELECTOR,
  FAST_LOG_PROB_ARC_SELECTOR
};

// A generic register for operations with various kinds of signatures.
// Needed since every function signature requires a new registration class.
// The std::pair<string, string> is understood to be the operation name and arc
// type; subclasses (or typedefs) need only provide the operation signature.

template <class OperationSignature>
class GenericOperationRegister
    : public GenericRegister<std::pair<string, string>, OperationSignature,
                             GenericOperationRegister<OperationSignature>> {
 public:
  void RegisterOperation(const string &operation_name, const string &arc_type,
                         OperationSignature op) {
    this->SetEntry(std::make_pair(operation_name, arc_type), op);
  }

  OperationSignature GetOperation(const string &operation_name,
                                  const string &arc_type) {
    return this->GetEntry(std::make_pair(operation_name, arc_type));
  }

 protected:
  string ConvertKeyToSoFilename(
      const std::pair<string, string> &key) const override {
    // Just use the old-style FST for now.
    string legal_type(key.second);  // the arc type
    ConvertToLegalCSymbol(&legal_type);

    return legal_type + "-arc.so";
  }
};

// Operation package - everything you need to register a new type of operation.
// The ArgPack should be the type that's passed into each wrapped function -
// for instance, it might be a struct containing all the args.
// It's always passed by pointer, so const members should be used to enforce
// constness where it's needed. Return values should be implemented as a
// member of ArgPack as well.

template <class ArgPack>
struct Operation {
  typedef ArgPack Args;
  typedef void (*OpType)(ArgPack *args);

  // The register (hash) type
  typedef GenericOperationRegister<OpType> Register;

  // The register-er type
  typedef GenericRegisterer<Register> Registerer;
};

// Macro for registering new types of operations.

#define REGISTER_FST_OPERATION(Op, Arc, ArgPack)               \
  static fst::script::Operation<ArgPack>::Registerer       \
      arc_dispatched_operation_##ArgPack##Op##Arc##_registerer \
      (std::make_pair(#Op, Arc::Type()), Op<Arc>)

// Template function to apply an operation by name.

template <class OpReg>
void Apply(const string &op_name, const string &arc_type,
           typename OpReg::Args *args) {
  typename OpReg::Register *reg = OpReg::Register::GetRegister();
  typename OpReg::OpType op = reg->GetOperation(op_name, arc_type);
  if (!op) {
    FSTERROR() << "No operation found for " << op_name << " on "
               << "arc type " << arc_type;
    return;
  }
  op(args);
}

// Helper that logs to ERROR if the arc types of m and n don't match. The
// op_name is used to construct the error message.
template <class M, class N>
bool ArcTypesMatch(const M &m, const N &n, const string &op_name) {
  if (m.ArcType() != n.ArcType()) {
    FSTERROR() << "Arguments with non-matching arc types passed to "
               << op_name << ":\t" << m.ArcType() << " and " << n.ArcType();
    return false;
  }
  return true;
}

}  // namespace script
}  // namespace fst

#endif  // FST_SCRIPT_SCRIPT_IMPL_H_
