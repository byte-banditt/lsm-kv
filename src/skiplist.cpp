#include "lsm/skiplist.h"
#include <array>

namespace lsm {
SkipList::SkipList():head_(new Node("",std::nullopt,12)) {}
SkipList::~SkipList() { Node* p=head_; while(p) { Node* n=p->next[0]; delete p; p=n; } }
int SkipList::Height() { int h=1; do { rng_^=rng_<<13; rng_^=rng_>>17; rng_^=rng_<<5;
  if ((rng_&3u)!=0 || h==12) break;
  ++h; } while(true); return h; }
void SkipList::Put(std::string key,std::optional<std::string> value) {
  std::array<Node*,12> prev{}; Node* p=head_;
  for(int i=levels_-1;i>=0;--i) { while(p->next[i] && p->next[i]->key<key) p=p->next[i]; prev[i]=p; }
  p=p->next[0]; if(p && p->key==key) {
    bytes_-=p->value ? p->value->size() : 0;
    p->value=std::move(value); bytes_+=p->value ? p->value->size() : 0; return;
  }
  int h=Height(); if(h>levels_) { for(int i=levels_;i<h;++i) prev[i]=head_; levels_=h; }
  Node* n=new Node(std::move(key),std::move(value),h);
  bytes_+=n->key.size()+(n->value ? n->value->size():0)+16;
  for(int i=0;i<h;++i) { n->next[i]=prev[i]->next[i]; prev[i]->next[i]=n; }
}
bool SkipList::Find(const std::string& key,std::optional<std::string>& value) const {
  Node* p=head_; for(int i=levels_-1;i>=0;--i) while(p->next[i] && p->next[i]->key<key) p=p->next[i];
  p=p->next[0]; if(!p || p->key!=key) return false; value=p->value; return true;
}
std::vector<Entry> SkipList::Entries() const { std::vector<Entry> out;
  for(Node* p=head_->next[0];p;p=p->next[0]) out.emplace_back(p->key,p->value);
  return out; }
bool SkipList::Empty() const { return !head_->next[0]; }
}
