// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/schema.h"

#include <algorithm>
#include <vector>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "components/json_schema/json_schema_constants.h"
#include "components/json_schema/json_schema_validator.h"
#include "components/policy/core/common/schema_internal.h"

namespace policy {

using internal::PropertiesNode;
using internal::PropertyNode;
using internal::SchemaData;
using internal::SchemaNode;

namespace {

const int kInvalid = -1;

bool SchemaTypeToValueType(const std::string& type_string,
                           base::Value::Type* type) {
  // Note: "any" is not an accepted type.
  static const struct {
    const char* schema_type;
    base::Value::Type value_type;
  } kSchemaToValueTypeMap[] = {
    { json_schema_constants::kArray,        base::Value::TYPE_LIST       },
    { json_schema_constants::kBoolean,      base::Value::TYPE_BOOLEAN    },
    { json_schema_constants::kInteger,      base::Value::TYPE_INTEGER    },
    { json_schema_constants::kNull,         base::Value::TYPE_NULL       },
    { json_schema_constants::kNumber,       base::Value::TYPE_DOUBLE     },
    { json_schema_constants::kObject,       base::Value::TYPE_DICTIONARY },
    { json_schema_constants::kString,       base::Value::TYPE_STRING     },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kSchemaToValueTypeMap); ++i) {
    if (kSchemaToValueTypeMap[i].schema_type == type_string) {
      *type = kSchemaToValueTypeMap[i].value_type;
      return true;
    }
  }
  return false;
}

}  // namespace

// Contains the internal data representation of a Schema. This can either wrap
// a SchemaData owned elsewhere (currently used to wrap the Chrome schema, which
// is generated at compile time), or it can own its own SchemaData.
class Schema::InternalStorage
    : public base::RefCountedThreadSafe<InternalStorage> {
 public:
  static scoped_refptr<const InternalStorage> Wrap(const SchemaData* data);

  static scoped_refptr<const InternalStorage> ParseSchema(
      const base::DictionaryValue& schema,
      std::string* error);

  const SchemaData* data() const { return &schema_data_; }

  const SchemaNode* root_node() const {
    return schema(0);
  }

  const SchemaNode* schema(int index) const {
    return schema_data_.schema_nodes + index;
  }

  const PropertiesNode* properties(int index) const {
    return schema_data_.properties_nodes + index;
  }

  const PropertyNode* property(int index) const {
    return schema_data_.property_nodes + index;
  }

 private:
  friend class base::RefCountedThreadSafe<InternalStorage>;

  InternalStorage();
  ~InternalStorage();

  // Parses the JSON schema in |schema| and returns the index of the
  // corresponding SchemaNode in |schema_nodes_|, which gets populated with any
  // necessary intermediate nodes. If |schema| is invalid then -1 is returned
  // and |error| is set to the error cause.
  int Parse(const base::DictionaryValue& schema, std::string* error);

  // Helper for Parse().
  int ParseDictionary(const base::DictionaryValue& schema, std::string* error);

  // Helper for Parse().
  int ParseList(const base::DictionaryValue& schema, std::string* error);

  SchemaData schema_data_;
  // TODO: compute the sizes of these arrays before filling them up to avoid
  // having to resize them.
  ScopedVector<std::string> strings_;
  std::vector<SchemaNode> schema_nodes_;
  std::vector<PropertyNode> property_nodes_;
  std::vector<PropertiesNode> properties_nodes_;

  DISALLOW_COPY_AND_ASSIGN(InternalStorage);
};

Schema::InternalStorage::InternalStorage() {}

Schema::InternalStorage::~InternalStorage() {}

// static
scoped_refptr<const Schema::InternalStorage> Schema::InternalStorage::Wrap(
    const SchemaData* data) {
  InternalStorage* storage = new InternalStorage();
  storage->schema_data_.schema_nodes = data->schema_nodes;
  storage->schema_data_.property_nodes = data->property_nodes;
  storage->schema_data_.properties_nodes = data->properties_nodes;
  return storage;
}

// static
scoped_refptr<const Schema::InternalStorage>
Schema::InternalStorage::ParseSchema(const base::DictionaryValue& schema,
                                     std::string* error) {
  scoped_refptr<InternalStorage> storage = new InternalStorage();
  if (storage->Parse(schema, error) == kInvalid)
    return NULL;
  SchemaData* data = &storage->schema_data_;
  data->schema_nodes = vector_as_array(&storage->schema_nodes_);
  data->property_nodes = vector_as_array(&storage->property_nodes_);
  data->properties_nodes = vector_as_array(&storage->properties_nodes_);
  return storage;
}

int Schema::InternalStorage::Parse(const base::DictionaryValue& schema,
                                   std::string* error) {
  std::string type_string;
  if (!schema.GetString(json_schema_constants::kType, &type_string)) {
    *error = "The schema type must be declared.";
    return kInvalid;
  }

  base::Value::Type type = base::Value::TYPE_NULL;
  if (!SchemaTypeToValueType(type_string, &type)) {
    *error = "Type not supported: " + type_string;
    return kInvalid;
  }

  if (type == base::Value::TYPE_DICTIONARY)
    return ParseDictionary(schema, error);
  if (type == base::Value::TYPE_LIST)
    return ParseList(schema, error);

  int index = static_cast<int>(schema_nodes_.size());
  schema_nodes_.push_back(SchemaNode());
  SchemaNode& node = schema_nodes_.back();
  node.type = type;
  node.extra = kInvalid;
  return index;
}

// static
int Schema::InternalStorage::ParseDictionary(
    const base::DictionaryValue& schema,
    std::string* error) {
  // Note: recursive calls to Parse() invalidate iterators and references into
  // the vectors.

  // Reserve an index for this dictionary at the front, so that the root node
  // is at index 0.
  int schema_index = static_cast<int>(schema_nodes_.size());
  schema_nodes_.push_back(SchemaNode());

  int extra = static_cast<int>(properties_nodes_.size());
  properties_nodes_.push_back(PropertiesNode());
  properties_nodes_[extra].begin = kInvalid;
  properties_nodes_[extra].end = kInvalid;
  properties_nodes_[extra].additional = kInvalid;

  const base::DictionaryValue* dict = NULL;
  if (schema.GetDictionary(json_schema_constants::kAdditionalProperties,
                           &dict)) {
    int additional = Parse(*dict, error);
    if (additional == kInvalid)
      return kInvalid;
    properties_nodes_[extra].additional = additional;
  }

  const base::DictionaryValue* properties = NULL;
  if (schema.GetDictionary(json_schema_constants::kProperties, &properties)) {
    int base_index = static_cast<int>(property_nodes_.size());
    // This reserves nodes for all of the |properties|, and makes sure they
    // are contiguous. Recursive calls to Parse() will append after these
    // elements.
    property_nodes_.resize(base_index + properties->size());

    int index = base_index;
    for (base::DictionaryValue::Iterator it(*properties);
         !it.IsAtEnd(); it.Advance(), ++index) {
      // This should have been verified by the JSONSchemaValidator.
      CHECK(it.value().GetAsDictionary(&dict));
      int extra = Parse(*dict, error);
      if (extra == kInvalid)
        return kInvalid;
      strings_.push_back(new std::string(it.key()));
      property_nodes_[index].key = strings_.back()->c_str();
      property_nodes_[index].schema = extra;
    }
    CHECK_EQ(static_cast<int>(properties->size()), index - base_index);
    properties_nodes_[extra].begin = base_index;
    properties_nodes_[extra].end = index;
  }

  schema_nodes_[schema_index].type = base::Value::TYPE_DICTIONARY;
  schema_nodes_[schema_index].extra = extra;
  return schema_index;
}

// static
int Schema::InternalStorage::ParseList(const base::DictionaryValue& schema,
                                       std::string* error) {
  const base::DictionaryValue* dict = NULL;
  if (!schema.GetDictionary(json_schema_constants::kItems, &dict)) {
    *error = "Arrays must declare a single schema for their items.";
    return kInvalid;
  }
  int extra = Parse(*dict, error);
  if (extra == kInvalid)
    return kInvalid;
  int index = static_cast<int>(schema_nodes_.size());
  schema_nodes_.push_back(SchemaNode());
  schema_nodes_[index].type = base::Value::TYPE_LIST;
  schema_nodes_[index].extra = extra;
  return index;
}

Schema::Iterator::Iterator(const scoped_refptr<const InternalStorage>& storage,
                           const PropertiesNode* node)
    : storage_(storage),
      it_(storage->property(node->begin)),
      end_(storage->property(node->end)) {}

Schema::Iterator::Iterator(const Iterator& iterator)
    : storage_(iterator.storage_),
      it_(iterator.it_),
      end_(iterator.end_) {}

Schema::Iterator::~Iterator() {}

Schema::Iterator& Schema::Iterator::operator=(const Iterator& iterator) {
  storage_ = iterator.storage_;
  it_ = iterator.it_;
  end_ = iterator.end_;
  return *this;
}

bool Schema::Iterator::IsAtEnd() const {
  return it_ == end_;
}

void Schema::Iterator::Advance() {
  ++it_;
}

const char* Schema::Iterator::key() const {
  return it_->key;
}

Schema Schema::Iterator::schema() const {
  return Schema(storage_, storage_->schema(it_->schema));
}

Schema::Schema() : node_(NULL) {}

Schema::Schema(const scoped_refptr<const InternalStorage>& storage,
               const SchemaNode* node)
    : storage_(storage), node_(node) {}

Schema::Schema(const Schema& schema)
    : storage_(schema.storage_), node_(schema.node_) {}

Schema::~Schema() {}

Schema& Schema::operator=(const Schema& schema) {
  storage_ = schema.storage_;
  node_ = schema.node_;
  return *this;
}

// static
Schema Schema::Wrap(const SchemaData* data) {
  scoped_refptr<const InternalStorage> storage = InternalStorage::Wrap(data);
  return Schema(storage, storage->root_node());
}

bool Schema::Validate(const base::Value& value) const {
  if (!valid()) {
    // Schema not found, invalid entry.
    return false;
  }

  if (!value.IsType(type()))
    return false;

  const base::DictionaryValue* dict = NULL;
  const base::ListValue* list = NULL;
  if (value.GetAsDictionary(&dict)) {
    for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd();
         it.Advance()) {
      if (!GetProperty(it.key()).Validate(it.value()))
        return false;
    }
  } else if (value.GetAsList(&list)) {
    for (base::ListValue::const_iterator it = list->begin();
         it != list->end(); ++it) {
      if (!*it || !GetItems().Validate(**it))
        return false;
    }
  }

  return true;
}

// static
Schema Schema::Parse(const std::string& content, std::string* error) {
  // Validate as a generic JSON schema.
  scoped_ptr<base::DictionaryValue> dict =
      JSONSchemaValidator::IsValidSchema(content, error);
  if (!dict)
    return Schema();

  // Validate the main type.
  std::string string_value;
  if (!dict->GetString(json_schema_constants::kType, &string_value) ||
      string_value != json_schema_constants::kObject) {
    *error =
        "The main schema must have a type attribute with \"object\" value.";
    return Schema();
  }

  // Checks for invalid attributes at the top-level.
  if (dict->HasKey(json_schema_constants::kAdditionalProperties) ||
      dict->HasKey(json_schema_constants::kPatternProperties)) {
    *error = "\"additionalProperties\" and \"patternProperties\" are not "
             "supported at the main schema.";
    return Schema();
  }

  scoped_refptr<const InternalStorage> storage =
      InternalStorage::ParseSchema(*dict, error);
  if (!storage)
    return Schema();
  return Schema(storage, storage->root_node());
}

base::Value::Type Schema::type() const {
  CHECK(valid());
  return node_->type;
}

Schema::Iterator Schema::GetPropertiesIterator() const {
  CHECK(valid());
  CHECK_EQ(base::Value::TYPE_DICTIONARY, type());
  return Iterator(storage_, storage_->properties(node_->extra));
}

namespace {

bool CompareKeys(const PropertyNode& node, const std::string& key) {
  return node.key < key;
}

}  // namespace

Schema Schema::GetKnownProperty(const std::string& key) const {
  CHECK(valid());
  CHECK_EQ(base::Value::TYPE_DICTIONARY, type());
  const PropertiesNode* node = storage_->properties(node_->extra);
  const PropertyNode* begin = storage_->property(node->begin);
  const PropertyNode* end = storage_->property(node->end);
  const PropertyNode* it = std::lower_bound(begin, end, key, CompareKeys);
  if (it != end && it->key == key)
    return Schema(storage_, storage_->schema(it->schema));
  return Schema();
}

Schema Schema::GetAdditionalProperties() const {
  CHECK(valid());
  CHECK_EQ(base::Value::TYPE_DICTIONARY, type());
  const PropertiesNode* node = storage_->properties(node_->extra);
  if (node->additional == kInvalid)
    return Schema();
  return Schema(storage_, storage_->schema(node->additional));
}

Schema Schema::GetProperty(const std::string& key) const {
  Schema schema = GetKnownProperty(key);
  return schema.valid() ? schema : GetAdditionalProperties();
}

Schema Schema::GetItems() const {
  CHECK(valid());
  CHECK_EQ(base::Value::TYPE_LIST, type());
  if (node_->extra == kInvalid)
    return Schema();
  return Schema(storage_, storage_->schema(node_->extra));
}

}  // namespace policy
