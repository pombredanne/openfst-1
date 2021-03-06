// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Queue-dependent visitation of finite-state transducers. See also dfs-visit.h.

#ifndef FST_LIB_VISIT_H_
#define FST_LIB_VISIT_H_


#include <fst/arcfilter.h>
#include <fst/mutable-fst.h>


namespace fst {

// Visitor Interface - class determines actions taken during a visit.
// If any of the boolean member functions return false, the visit is
// aborted by first calling FinishState() on all unfinished (grey)
// states and then calling FinishVisit().
//
// Note this is more general than the visitor interface in
// dfs-visit.h but lacks some DFS-specific behavior.
//
// template <class Arc>
// class Visitor {
//  public:
//   typedef typename Arc::StateId StateId;
//
//   Visitor(T *return_data);
//   // Invoked before visit
//   void InitVisit(const Fst<Arc> &fst);
//   // Invoked when state discovered (2nd arg is visitation root)
//   bool InitState(StateId s, StateId root);
//   // Invoked when arc to white/undiscovered state examined
//   bool WhiteArc(StateId s, const Arc &a);
//   // Invoked when arc to grey/unfinished state examined
//   bool GreyArc(StateId s, const Arc &a);
//   // Invoked when arc to black/finished state examined
//   bool BlackArc(StateId s, const Arc &a);
//   // Invoked when state finished.
//   void FinishState(StateId s);
//   // Invoked after visit
//   void FinishVisit();
// };

// Performs queue-dependent visitation. Visitor class argument
// determines actions and contains any return data. ArcFilter
// determines arcs that are considered. If 'access_only' is true,
// performs visitation only to states accessible from the initial
// state.
//
// Note this is more general than DfsVisit() in dfs-visit.h but lacks
// some DFS-specific Visitor behavior.
template <class F, class V, class Q, class ArcFilter>
void Visit(const F &fst, V *visitor, Q *queue, ArcFilter filter,
           bool access_only = false) {
  typedef typename F::Arc Arc;
  typedef typename Arc::StateId StateId;
  typedef ArcIterator<F> AIterator;

  visitor->InitVisit(fst);

  StateId start = fst.Start();
  if (start == kNoStateId) {
    visitor->FinishVisit();
    return;
  }

  // An Fst state's visit color
  const unsigned kWhiteState = 0x01;  // Undiscovered
  const unsigned kGreyState = 0x02;   // Discovered & unfinished
  const unsigned kBlackState = 0x04;  // Finished

  // We destroy an iterator as soon as possible and mark it so
  const unsigned kArcIterDone = 0x08;  // Arc iterator done and destroyed

  std::vector<unsigned char> state_status;
  std::vector<AIterator *> arc_iterator;
  MemoryPool<AIterator> aiter_pool;  // Pool for arc iterators

  StateId nstates = start + 1;  // # of known states in general case
  bool expanded = false;
  if (fst.Properties(kExpanded, false)) {  // tests if expanded case, then
    nstates = CountStates(fst);            // uses ExpandedFst::NumStates().
    expanded = true;
  }

  state_status.resize(nstates, kWhiteState);
  arc_iterator.resize(nstates);
  StateIterator<Fst<Arc>> siter(fst);

  // Continues visit while true
  bool visit = true;

  // Iterates over trees in visit forest.
  for (StateId root = start; visit && root < nstates;) {
    visit = visitor->InitState(root, root);
    state_status[root] = kGreyState;
    queue->Enqueue(root);
    while (!queue->Empty()) {
      StateId s = queue->Head();
      if (s >= state_status.size()) {
        nstates = s + 1;
        state_status.resize(nstates, kWhiteState);
        arc_iterator.resize(nstates);
      }
      // Creates arc iterator if needed.
      if (arc_iterator[s] == nullptr && !(state_status[s] & kArcIterDone) &&
          visit) {
        arc_iterator[s] = new (&aiter_pool) AIterator(fst, s);
      }
      // Deletes arc iterator if done.
      AIterator *aiter = arc_iterator[s];
      if ((aiter && aiter->Done()) || !visit) {
        Destroy(aiter, &aiter_pool);
        arc_iterator[s] = nullptr;
        state_status[s] |= kArcIterDone;
      }
      // Dequeues state and marks black if done
      if (state_status[s] & kArcIterDone) {
        queue->Dequeue();
        visitor->FinishState(s);
        state_status[s] = kBlackState;
        continue;
      }

      const Arc &arc = aiter->Value();
      if (arc.nextstate >= state_status.size()) {
        nstates = arc.nextstate + 1;
        state_status.resize(nstates, kWhiteState);
        arc_iterator.resize(nstates);
      }
      // Visits respective arc types
      if (filter(arc)) {
        // Enqueues destination state and marks grey if white
        if (state_status[arc.nextstate] == kWhiteState) {
          visit = visitor->WhiteArc(s, arc);
          if (!visit) continue;
          visit = visitor->InitState(arc.nextstate, root);
          state_status[arc.nextstate] = kGreyState;
          queue->Enqueue(arc.nextstate);
        } else if (state_status[arc.nextstate] == kBlackState) {
          visit = visitor->BlackArc(s, arc);
        } else {
          visit = visitor->GreyArc(s, arc);
        }
      }
      aiter->Next();
      // Destroys an iterator ASAP for efficiency.
      if (aiter->Done()) {
        Destroy(aiter, &aiter_pool);
        arc_iterator[s] = nullptr;
        state_status[s] |= kArcIterDone;
      }
    }

    if (access_only) break;

    // Finds next tree root
    for (root = root == start ? 0 : root + 1;
         root < nstates && state_status[root] != kWhiteState; ++root) {
    }

    // Check for a state beyond the largest known state
    if (!expanded && root == nstates) {
      for (; !siter.Done(); siter.Next()) {
        if (siter.Value() == nstates) {
          ++nstates;
          state_status.push_back(kWhiteState);
          arc_iterator.push_back(nullptr);
          break;
        }
      }
    }
  }
  visitor->FinishVisit();
}

template <class Arc, class V, class Q>
inline void Visit(const Fst<Arc> &fst, V *visitor, Q *queue) {
  Visit(fst, visitor, queue, AnyArcFilter<Arc>());
}

// Copies input FST to mutable FST following queue order.
template <class A>
class CopyVisitor {
 public:
  typedef A Arc;
  typedef typename A::StateId StateId;

  explicit CopyVisitor(MutableFst<Arc> *ofst) : ifst_(nullptr), ofst_(ofst) {}

  void InitVisit(const Fst<A> &ifst) {
    ifst_ = &ifst;
    ofst_->DeleteStates();
    ofst_->SetStart(ifst_->Start());
  }

  bool InitState(StateId s, StateId) {
    while (ofst_->NumStates() <= s) ofst_->AddState();
    return true;
  }

  bool WhiteArc(StateId s, const Arc &arc) {
    ofst_->AddArc(s, arc);
    return true;
  }

  bool GreyArc(StateId s, const Arc &arc) {
    ofst_->AddArc(s, arc);
    return true;
  }

  bool BlackArc(StateId s, const Arc &arc) {
    ofst_->AddArc(s, arc);
    return true;
  }

  void FinishState(StateId s) { ofst_->SetFinal(s, ifst_->Final(s)); }

  void FinishVisit() {}

 private:
  const Fst<Arc> *ifst_;
  MutableFst<Arc> *ofst_;
};

// Visits input FST up to a state limit following queue order.
template <class A>
class PartialVisitor {
 public:
  typedef A Arc;
  typedef typename A::StateId StateId;

  explicit PartialVisitor(StateId maxvisit)
      : fst_(nullptr), maxvisit_(maxvisit) {}

  void InitVisit(const Fst<A> &ifst) {
    fst_ = &ifst;
    ninit_ = 0;
    nfinish_ = 0;
  }

  bool InitState(StateId s, StateId root) {
    ++ninit_;
    return ninit_ <= maxvisit_;
  }

  bool WhiteArc(StateId s, const Arc &arc) { return true; }
  bool GreyArc(StateId s, const Arc &arc) { return true; }
  bool BlackArc(StateId s, const Arc &arc) { return true; }

  void FinishState(StateId s) {
    fst_->Final(s);  // Visits super-final arc
    ++nfinish_;
  }

  void FinishVisit() {}
  StateId NumInitialized() { return ninit_; }
  StateId NumFinished() { return nfinish_; }

 private:
  const Fst<Arc> *fst_;
  StateId maxvisit_;
  StateId ninit_;
  StateId nfinish_;
};

// Copies input FST to mutable FST up to a state limit following queue order.
template <class A>
class PartialCopyVisitor : public CopyVisitor<A> {
 public:
  typedef A Arc;
  typedef typename A::StateId StateId;
  using CopyVisitor<A>::WhiteArc;

  PartialCopyVisitor(MutableFst<Arc> *ofst, StateId maxvisit,
                     bool copy_grey = true, bool copy_black = true)
      : CopyVisitor<A>(ofst), maxvisit_(maxvisit),
        copy_grey_(copy_grey), copy_black_(copy_black) {}

  void InitVisit(const Fst<A> &ifst) {
    CopyVisitor<A>::InitVisit(ifst);
    ninit_ = 0;
    nfinish_ = 0;
  }

  bool InitState(StateId s, StateId root) {
    CopyVisitor<A>::InitState(s, root);
    ++ninit_;
    return ninit_ <= maxvisit_;
  }

  bool GreyArc(StateId s, const Arc &arc) {
    if (copy_grey_) return CopyVisitor<A>::GreyArc(s, arc);
    return true;
  }

  bool BlackArc(StateId s, const Arc &arc) {
    if (copy_black_) return CopyVisitor<A>::BlackArc(s, arc);
    return true;
  }

  void FinishState(StateId s) {
    CopyVisitor<A>::FinishState(s);
    ++nfinish_;
  }

  void FinishVisit() {}
  StateId NumInitialized() { return ninit_; }
  StateId NumFinished() { return nfinish_; }

 private:
  StateId maxvisit_;
  StateId ninit_;
  StateId nfinish_;
  const bool copy_grey_;
  const bool copy_black_;
};

}  // namespace fst

#endif  // FST_LIB_VISIT_H_
