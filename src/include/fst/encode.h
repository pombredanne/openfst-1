// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Class to encode and decode an FST.

#ifndef FST_LIB_ENCODE_H_
#define FST_LIB_ENCODE_H_

#include <climits>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fstream>

#include <fst/arc-map.h>
#include <fst/rmfinalepsilon.h>


namespace fst {

static const uint32 kEncodeLabels = 0x0001;
static const uint32 kEncodeWeights = 0x0002;
static const uint32 kEncodeFlags = 0x0003;  // All non-internal flags

static const uint32 kEncodeHasISymbols = 0x0004;  // For internal use
static const uint32 kEncodeHasOSymbols = 0x0008;  // For internal use

enum EncodeType { ENCODE = 1, DECODE = 2 };

// Identifies stream data as an encode table (and its endianity)
static const int32 kEncodeMagicNumber = 2129983209;

// The following class encapsulates implementation details for the
// encoding and decoding of label/weight tuples used for encoding
// and decoding of Fsts. The EncodeTable is bidirectional. I.E it
// stores both the Tuple of encode labels and weights to a unique
// label, and the reverse.
template <class A>
class EncodeTable {
 public:
  typedef typename A::Label Label;
  typedef typename A::Weight Weight;

  // Encoded data consists of arc input/output labels and arc weight
  struct Tuple {
    Tuple() {}
    Tuple(Label ilabel_, Label olabel_, Weight weight_)
        : ilabel(ilabel_), olabel(olabel_), weight(std::move(weight_)) {}
    Tuple(const Tuple &tuple)
        : ilabel(tuple.ilabel), olabel(tuple.olabel), weight(tuple.weight) {}

    Label ilabel;
    Label olabel;
    Weight weight;
  };

  // Comparison object for hashing EncodeTable Tuple(s).
  class TupleEqual {
   public:
    bool operator()(const Tuple *x, const Tuple *y) const {
      return (x->ilabel == y->ilabel && x->olabel == y->olabel &&
              x->weight == y->weight);
    }
  };

  // Hash function for EncodeTabe Tuples. Based on the encode flags
  // we either hash the labels, weights or combination of them.
  class TupleKey {
   public:
    TupleKey() : encode_flags_(kEncodeLabels | kEncodeWeights) {}

    TupleKey(const TupleKey &key) : encode_flags_(key.encode_flags_) {}

    explicit TupleKey(uint32 encode_flags) : encode_flags_(encode_flags) {}

    size_t operator()(const Tuple *x) const {
      size_t hash = x->ilabel;
      const int lshift = 5;
      const int rshift = CHAR_BIT * sizeof(size_t) - 5;
      if (encode_flags_ & kEncodeLabels) {
        hash = hash << lshift ^ hash >> rshift ^ x->olabel;
      }
      if (encode_flags_ & kEncodeWeights) {
        hash = hash << lshift ^ hash >> rshift ^ x->weight.Hash();
      }
      return hash;
    }

   private:
    int32 encode_flags_;
  };

  typedef std::unordered_map<const Tuple *, Label, TupleKey, TupleEqual>
      EncodeHash;

  explicit EncodeTable(uint32 encode_flags)
      : flags_(encode_flags), encode_hash_(1024, TupleKey(encode_flags)) {}

  // Given an arc encode either input/ouptut labels or input/costs or both
  Label Encode(const A &arc) {
    std::unique_ptr<Tuple> tuple(
        new Tuple(arc.ilabel, flags_ & kEncodeLabels ? arc.olabel : 0,
                  flags_ & kEncodeWeights ? arc.weight : Weight::One()));
    auto insert_result = encode_hash_.insert(
        std::make_pair(tuple.get(), encode_tuples_.size() + 1));
    if (insert_result.second) {
      encode_tuples_.push_back(std::move(tuple));
    }
    return insert_result.first->second;
  }

  // Given an arc, look up its encoded label. Returns kNoLabel if not found.
  Label GetLabel(const A &arc) const {
    const Tuple tuple(arc.ilabel, flags_ & kEncodeLabels ? arc.olabel : 0,
                      flags_ & kEncodeWeights ? arc.weight : Weight::One());
    typename EncodeHash::const_iterator it = encode_hash_.find(&tuple);
    if (it == encode_hash_.end()) {
      return kNoLabel;
    } else {
      return it->second;
    }
  }

  // Given an encode arc Label decode back to input/output labels and costs
  const Tuple *Decode(Label key) const {
    if (key < 1 || key > encode_tuples_.size()) {
      LOG(ERROR) << "EncodeTable::Decode: Unknown decode key: " << key;
      return nullptr;
    }
    return encode_tuples_[key - 1].get();
  }

  size_t Size() const { return encode_tuples_.size(); }

  bool Write(std::ostream &strm, const string &source) const;

  static EncodeTable<A> *Read(std::istream &strm, const string &source);

  uint32 Flags() const { return flags_ & kEncodeFlags; }

  const SymbolTable *InputSymbols() const { return isymbols_.get(); }

  const SymbolTable *OutputSymbols() const { return osymbols_.get(); }

  void SetInputSymbols(const SymbolTable *syms) {
    if (syms) {
      isymbols_.reset(syms->Copy());
      flags_ |= kEncodeHasISymbols;
    } else {
      isymbols_.reset();
      flags_ &= ~kEncodeHasISymbols;
    }
  }

  void SetOutputSymbols(const SymbolTable *syms) {
    if (syms) {
      osymbols_.reset(syms->Copy());
      flags_ |= kEncodeHasOSymbols;
    } else {
      osymbols_.reset();
      flags_ &= ~kEncodeHasOSymbols;
    }
  }

 private:
  uint32 flags_;
  std::vector<std::unique_ptr<Tuple>> encode_tuples_;
  EncodeHash encode_hash_;
  std::unique_ptr<SymbolTable> isymbols_;  // Pre-encoded ilabel symbol table
  std::unique_ptr<SymbolTable> osymbols_;  // Pre-encoded olabel symbol table

  EncodeTable(const EncodeTable &) = delete;
  EncodeTable &operator=(const EncodeTable &) = delete;
};

template <class A>
inline bool EncodeTable<A>::Write(std::ostream &strm,
                                  const string &source) const {
  WriteType(strm, kEncodeMagicNumber);
  WriteType(strm, flags_);
  int64 size = encode_tuples_.size();
  WriteType(strm, size);
  for (size_t i = 0; i < size; ++i) {
    WriteType(strm, encode_tuples_[i]->ilabel);
    WriteType(strm, encode_tuples_[i]->olabel);
    encode_tuples_[i]->weight.Write(strm);
  }

  if (flags_ & kEncodeHasISymbols) isymbols_->Write(strm);

  if (flags_ & kEncodeHasOSymbols) osymbols_->Write(strm);

  strm.flush();
  if (!strm) {
    LOG(ERROR) << "EncodeTable::Write: Write failed: " << source;
    return false;
  }
  return true;
}

template <class A>
inline EncodeTable<A> *EncodeTable<A>::Read(std::istream &strm,
                                            const string &source) {
  int32 magic_number = 0;
  ReadType(strm, &magic_number);
  if (magic_number != kEncodeMagicNumber) {
    LOG(ERROR) << "EncodeTable::Read: Bad encode table header: " << source;
    return nullptr;
  }
  uint32 flags;
  ReadType(strm, &flags);
  EncodeTable<A> *table = new EncodeTable<A>(flags);

  int64 size;
  ReadType(strm, &size);
  if (!strm) {
    LOG(ERROR) << "EncodeTable::Read: Read failed: " << source;
    return nullptr;
  }

  for (size_t i = 0; i < size; ++i) {
    Tuple *tuple = new Tuple();
    ReadType(strm, &tuple->ilabel);
    ReadType(strm, &tuple->olabel);
    tuple->weight.Read(strm);
    if (!strm) {
      LOG(ERROR) << "EncodeTable::Read: Read failed: " << source;
      return nullptr;
    }
    table->encode_tuples_.emplace_back(tuple);
    table->encode_hash_[table->encode_tuples_.back().get()] =
        table->encode_tuples_.size();
  }

  if (flags & kEncodeHasISymbols) {
    table->isymbols_.reset(SymbolTable::Read(strm, source));
  }

  if (flags & kEncodeHasOSymbols) {
    table->osymbols_.reset(SymbolTable::Read(strm, source));
  }

  return table;
}

// A mapper to encode/decode weighted transducers. Encoding of an
// Fst is useful for performing classical determinization or minimization
// on a weighted transducer by treating it as an unweighted acceptor over
// encoded labels.
//
// The Encode mapper stores the encoding in a local hash table (EncodeTable)
// This table is shared (and reference counted) between the encoder and
// decoder. A decoder has read only access to the EncodeTable.
//
// The EncodeMapper allows on the fly encoding of the machine. As the
// EncodeTable is generated the same table may by used to decode the machine
// on the fly. For example in the following sequence of operations
//
//  Encode -> Determinize -> Decode
//
// we will use the encoding table generated during the encode step in the
// decode, even though the encoding is not complete.
//
template <class A>
class EncodeMapper {
  typedef typename A::Weight Weight;
  typedef typename A::Label Label;

 public:
  EncodeMapper(uint32 flags, EncodeType type)
      : flags_(flags),
        type_(type),
        table_(std::make_shared<EncodeTable<A>>(flags)),
        error_(false) {}

  EncodeMapper(const EncodeMapper &mapper)
      : flags_(mapper.flags_),
        type_(mapper.type_),
        table_(mapper.table_),
        error_(false) {}

  // Copy constructor but setting the type, typically to DECODE
  EncodeMapper(const EncodeMapper &mapper, EncodeType type)
      : flags_(mapper.flags_),
        type_(type),
        table_(mapper.table_),
        error_(mapper.error_) {}

  A operator()(const A &arc);

  MapFinalAction FinalAction() const {
    return (type_ == ENCODE && (flags_ & kEncodeWeights))
               ? MAP_REQUIRE_SUPERFINAL
               : MAP_NO_SUPERFINAL;
  }

  MapSymbolsAction InputSymbolsAction() const { return MAP_CLEAR_SYMBOLS; }

  MapSymbolsAction OutputSymbolsAction() const { return MAP_CLEAR_SYMBOLS; }

  uint64 Properties(uint64 inprops) {
    uint64 outprops = inprops;
    if (error_) outprops |= kError;

    uint64 mask = kFstProperties;
    if (flags_ & kEncodeLabels) {
      mask &= kILabelInvariantProperties & kOLabelInvariantProperties;
    }
    if (flags_ & kEncodeWeights) {
      mask &= kILabelInvariantProperties & kWeightInvariantProperties &
              (type_ == ENCODE ? kAddSuperFinalProperties
                               : kRmSuperFinalProperties);
    }

    return outprops & mask;
  }

  uint32 Flags() const { return flags_; }
  EncodeType Type() const { return type_; }
  const EncodeTable<A> &table() const { return *table_; }

  bool Write(std::ostream &strm, const string &source) const {
    return table_->Write(strm, source);
  }

  bool Write(const string &filename) const {
    std::ofstream strm(filename.c_str(),
                             std::ios_base::out | std::ios_base::binary);
    if (!strm) {
      LOG(ERROR) << "EncodeMap: Can't open file: " << filename;
      return false;
    }
    return Write(strm, filename);
  }

  static EncodeMapper<A> *Read(std::istream &strm, const string &source,
                               EncodeType type = ENCODE) {
    EncodeTable<A> *table = EncodeTable<A>::Read(strm, source);
    return table ? new EncodeMapper(table->Flags(), type, table) : nullptr;
  }

  static EncodeMapper<A> *Read(const string &filename,
                               EncodeType type = ENCODE) {
    std::ifstream strm(filename.c_str(),
                            std::ios_base::in | std::ios_base::binary);
    if (!strm) {
      LOG(ERROR) << "EncodeMap: Can't open file: " << filename;
      return nullptr;
    }
    return Read(strm, filename, type);
  }

  const SymbolTable *InputSymbols() const { return table_->InputSymbols(); }

  const SymbolTable *OutputSymbols() const { return table_->OutputSymbols(); }

  void SetInputSymbols(const SymbolTable *syms) {
    table_->SetInputSymbols(syms);
  }

  void SetOutputSymbols(const SymbolTable *syms) {
    table_->SetOutputSymbols(syms);
  }

 private:
  uint32 flags_;
  EncodeType type_;
  std::shared_ptr<EncodeTable<A>> table_;
  bool error_;

  explicit EncodeMapper(uint32 flags, EncodeType type, EncodeTable<A> *table)
      : flags_(flags), type_(type), table_(table), error_(false) {}

  EncodeMapper &operator=(const EncodeMapper &) = delete;
};

template <class A>
inline A EncodeMapper<A>::operator()(const A &arc) {
  if (type_ == ENCODE) {  // labels and/or weights to single label
    if ((arc.nextstate == kNoStateId && !(flags_ & kEncodeWeights)) ||
        (arc.nextstate == kNoStateId && (flags_ & kEncodeWeights) &&
         arc.weight == Weight::Zero())) {
      return arc;
    } else {
      Label label = table_->Encode(arc);
      return A(label, flags_ & kEncodeLabels ? label : arc.olabel,
               flags_ & kEncodeWeights ? Weight::One() : arc.weight,
               arc.nextstate);
    }
  } else {  // type_ == DECODE
    if (arc.nextstate == kNoStateId) {
      return arc;
    } else {
      if (arc.ilabel == 0) return arc;
      if (flags_ & kEncodeLabels && arc.ilabel != arc.olabel) {
        FSTERROR() << "EncodeMapper: Label-encoded arc has different "
                      "input and output labels";
        error_ = true;
      }
      if (flags_ & kEncodeWeights && arc.weight != Weight::One()) {
        FSTERROR() << "EncodeMapper: Weight-encoded arc has non-trivial weight";
        error_ = true;
      }
      const typename EncodeTable<A>::Tuple *tuple = table_->Decode(arc.ilabel);
      if (!tuple) {
        FSTERROR() << "EncodeMapper: Decode failed";
        error_ = true;
        return A(kNoLabel, kNoLabel, Weight::NoWeight(), arc.nextstate);
      } else {
        return A(tuple->ilabel,
                 flags_ & kEncodeLabels ? tuple->olabel : arc.olabel,
                 flags_ & kEncodeWeights ? tuple->weight : arc.weight,
                 arc.nextstate);
      }
    }
  }
}

// Complexity: O(nstates + narcs)
template <class A>
inline void Encode(MutableFst<A> *fst, EncodeMapper<A> *mapper) {
  mapper->SetInputSymbols(fst->InputSymbols());
  mapper->SetOutputSymbols(fst->OutputSymbols());
  ArcMap(fst, mapper);
}

template <class A>
inline void Decode(MutableFst<A> *fst, const EncodeMapper<A> &mapper) {
  ArcMap(fst, EncodeMapper<A>(mapper, DECODE));
  RmFinalEpsilon(fst);
  fst->SetInputSymbols(mapper.InputSymbols());
  fst->SetOutputSymbols(mapper.OutputSymbols());
}

// On the fly label and/or weight encoding of input Fst
//
// Complexity:
// - Constructor: O(1)
// - Traversal: O(nstates_visited + narcs_visited), assuming constant
//   time to visit an input state or arc.
template <class A>
class EncodeFst : public ArcMapFst<A, A, EncodeMapper<A>> {
 public:
  typedef A Arc;
  typedef EncodeMapper<A> C;
  typedef ArcMapFstImpl<A, A, EncodeMapper<A>> Impl;

  EncodeFst(const Fst<A> &fst, EncodeMapper<A> *encoder)
      : ArcMapFst<A, A, C>(fst, encoder, ArcMapFstOptions()) {
    encoder->SetInputSymbols(fst.InputSymbols());
    encoder->SetOutputSymbols(fst.OutputSymbols());
  }

  EncodeFst(const Fst<A> &fst, const EncodeMapper<A> &encoder)
      : ArcMapFst<A, A, C>(fst, encoder, ArcMapFstOptions()) {}

  // See Fst<>::Copy() for doc.
  EncodeFst(const EncodeFst<A> &fst, bool copy = false)
      : ArcMapFst<A, A, C>(fst, copy) {}

  // Get a copy of this EncodeFst. See Fst<>::Copy() for further doc.
  EncodeFst<A> *Copy(bool safe = false) const override {
    if (safe) {
      FSTERROR() << "EncodeFst::Copy(true): Not allowed";
      GetImpl()->SetProperties(kError, kError);
    }
    return new EncodeFst(*this);
  }

 private:
  using ImplToFst<Impl>::GetImpl;
  using ImplToFst<Impl>::GetMutableImpl;
};

// On the fly label and/or weight encoding of input Fst
//
// Complexity:
// - Constructor: O(1)
// - Traversal: O(nstates_visited + narcs_visited), assuming constant
//   time to visit an input state or arc.
template <class A>
class DecodeFst : public ArcMapFst<A, A, EncodeMapper<A>> {
 public:
  typedef A Arc;
  typedef EncodeMapper<A> C;
  typedef ArcMapFstImpl<A, A, EncodeMapper<A>> Impl;
  using ImplToFst<Impl>::GetImpl;

  DecodeFst(const Fst<A> &fst, const EncodeMapper<A> &encoder)
      : ArcMapFst<A, A, C>(fst, EncodeMapper<A>(encoder, DECODE),
                           ArcMapFstOptions()) {
    GetMutableImpl()->SetInputSymbols(encoder.InputSymbols());
    GetMutableImpl()->SetOutputSymbols(encoder.OutputSymbols());
  }

  // See Fst<>::Copy() for doc.
  DecodeFst(const DecodeFst<A> &fst, bool safe = false)
      : ArcMapFst<A, A, C>(fst, safe) {}

  // Get a copy of this DecodeFst. See Fst<>::Copy() for further doc.
  DecodeFst<A> *Copy(bool safe = false) const override {
    return new DecodeFst(*this, safe);
  }

 private:
  using ImplToFst<Impl>::GetMutableImpl;
};

// Specialization for EncodeFst.
template <class A>
class StateIterator<EncodeFst<A>>
    : public StateIterator<ArcMapFst<A, A, EncodeMapper<A>> > {
 public:
  explicit StateIterator(const EncodeFst<A> &fst)
      : StateIterator<ArcMapFst<A, A, EncodeMapper<A>> >(fst) {}
};

// Specialization for EncodeFst.
template <class A>
class ArcIterator<EncodeFst<A>>
    : public ArcIterator<ArcMapFst<A, A, EncodeMapper<A>> > {
 public:
  ArcIterator(const EncodeFst<A> &fst, typename A::StateId s)
      : ArcIterator<ArcMapFst<A, A, EncodeMapper<A>> >(fst, s) {}
};

// Specialization for DecodeFst.
template <class A>
class StateIterator<DecodeFst<A>>
    : public StateIterator<ArcMapFst<A, A, EncodeMapper<A>> > {
 public:
  explicit StateIterator(const DecodeFst<A> &fst)
      : StateIterator<ArcMapFst<A, A, EncodeMapper<A>> >(fst) {}
};

// Specialization for DecodeFst.
template <class A>
class ArcIterator<DecodeFst<A>>
    : public ArcIterator<ArcMapFst<A, A, EncodeMapper<A>> > {
 public:
  ArcIterator(const DecodeFst<A> &fst, typename A::StateId s)
      : ArcIterator<ArcMapFst<A, A, EncodeMapper<A>> >(fst, s) {}
};

// Useful aliases when using StdArc.
typedef EncodeFst<StdArc> StdEncodeFst;

typedef DecodeFst<StdArc> StdDecodeFst;

}  // namespace fst

#endif  // FST_LIB_ENCODE_H_
