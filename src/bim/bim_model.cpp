#include "bim/bim_model.h"

#include <algorithm>

namespace tamias {

Relation& BimModel::add(Relation relation) {
  relation.id = next_id_++;
  relations_.push_back(relation);
  return relations_.back();
}

Relation& BimModel::insert(Relation relation) {
  if (relation.id == 0) {
    relation.id = next_id_++;
  } else {
    next_id_ = std::max(next_id_, relation.id + 1);
  }
  relations_.push_back(relation);
  return relations_.back();
}

void BimModel::remove(std::uint64_t id) {
  for (auto it = relations_.begin(); it != relations_.end(); ++it) {
    if (it->id == id) {
      relations_.erase(it);
      return;
    }
  }
}

void BimModel::remove_involving(std::uint64_t entity_id) {
  relations_.erase(std::remove_if(relations_.begin(), relations_.end(),
                                  [entity_id](const Relation& r) {
                                    return r.from == entity_id || r.to == entity_id;
                                  }),
                   relations_.end());
}

void BimModel::clear() {
  relations_.clear();
  next_id_ = 1;
}

void BimModel::set_next_id(std::uint64_t id) { next_id_ = std::max<std::uint64_t>(1, id); }

Relation* BimModel::find(std::uint64_t id) {
  for (auto& r : relations_) {
    if (r.id == id) {
      return &r;
    }
  }
  return nullptr;
}

const Relation* BimModel::find(std::uint64_t id) const {
  for (const auto& r : relations_) {
    if (r.id == id) {
      return &r;
    }
  }
  return nullptr;
}

Relation* BimModel::host_of(std::uint64_t guest_id) {
  for (auto& r : relations_) {
    if (r.kind == RelationKind::HostedOn && r.from == guest_id) {
      return &r;
    }
  }
  return nullptr;
}

const Relation* BimModel::host_of(std::uint64_t guest_id) const {
  for (const auto& r : relations_) {
    if (r.kind == RelationKind::HostedOn && r.from == guest_id) {
      return &r;
    }
  }
  return nullptr;
}

std::vector<Relation*> BimModel::dependents(std::uint64_t host_id) {
  std::vector<Relation*> out;
  for (auto& r : relations_) {
    if (r.kind == RelationKind::HostedOn && r.to == host_id) {
      out.push_back(&r);
    }
  }
  return out;
}

std::vector<const Relation*> BimModel::dependents(std::uint64_t host_id) const {
  std::vector<const Relation*> out;
  for (const auto& r : relations_) {
    if (r.kind == RelationKind::HostedOn && r.to == host_id) {
      out.push_back(&r);
    }
  }
  return out;
}

}  // namespace tamias
