#include "bim/bim_model.h"

#include <algorithm>
#include <utility>

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
  storeys_.clear();
  active_storey_id_ = 0;
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

Storey& BimModel::insert_storey(Storey storey) {
  if (Storey* existing = find_storey(storey.id)) {
    *existing = std::move(storey);
    return *existing;
  }
  storeys_.push_back(std::move(storey));
  return storeys_.back();
}

void BimModel::remove_storey(std::uint64_t id) {
  storeys_.erase(std::remove_if(storeys_.begin(), storeys_.end(),
                                [id](const Storey& storey) { return storey.id == id; }),
                 storeys_.end());
  if (active_storey_id_ == id) {
    active_storey_id_ = 0;
  }
}

Storey* BimModel::find_storey(std::uint64_t id) {
  for (Storey& storey : storeys_) {
    if (storey.id == id) {
      return &storey;
    }
  }
  return nullptr;
}

const Storey* BimModel::find_storey(std::uint64_t id) const {
  for (const Storey& storey : storeys_) {
    if (storey.id == id) {
      return &storey;
    }
  }
  return nullptr;
}

void BimModel::set_active_storey_id(std::uint64_t id) {
  active_storey_id_ = id == 0 || find_storey(id) != nullptr ? id : 0;
}

double BimModel::storey_elevation(std::uint64_t id) const {
  const Storey* storey = find_storey(id);
  return storey != nullptr ? storey->elevation : 0.0;
}

}  // namespace tamias
