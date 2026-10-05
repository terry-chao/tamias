#include "ui/tac/platform/dialogs.h"

#include <stdexcept>
#include <utility>

namespace tac {

PromptForm& PromptForm::add_string(std::string id, std::string label, std::string value) {
  PromptField field;
  field.kind = PromptFieldKind::String;
  field.id = std::move(id);
  field.label = std::move(label);
  field.text = std::move(value);
  fields_.push_back(std::move(field));
  return *this;
}

PromptForm& PromptForm::add_number(std::string id, std::string label, double value,
                                   std::optional<double> min, std::optional<double> max) {
  PromptField field;
  field.kind = PromptFieldKind::Number;
  field.id = std::move(id);
  field.label = std::move(label);
  field.number = value;
  field.has_range = min.has_value() && max.has_value();
  field.min = min.value_or(0.0);
  field.max = max.value_or(0.0);
  fields_.push_back(std::move(field));
  return *this;
}

PromptForm& PromptForm::add_bool(std::string id, std::string label, bool value) {
  PromptField field;
  field.kind = PromptFieldKind::Bool;
  field.id = std::move(id);
  field.label = std::move(label);
  field.flag = value;
  fields_.push_back(std::move(field));
  return *this;
}

const PromptField& PromptForm::find(std::string_view id) const {
  for (const PromptField& field : fields_) {
    if (field.id == id) {
      return field;
    }
  }
  throw std::out_of_range("tac::PromptForm: unknown field id");
}

std::string PromptForm::string_value(std::string_view id) const { return find(id).text; }

double PromptForm::number_value(std::string_view id) const { return find(id).number; }

bool PromptForm::bool_value(std::string_view id) const { return find(id).flag; }

}  // namespace tac
