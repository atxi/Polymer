#include <polymer/asset/parsed_block_model.h>

#include <lib/json.h>
#include <polymer/memory.h>
#include <polymer/render/render.h>

#include <assert.h>
#include <stdio.h>

using namespace polymer::world;
using namespace polymer::render;

namespace polymer {
namespace asset {

template <typename T, size_t size>
struct JsonVectorParser {
  json_object_element_s* element;
  json_array_element_s* array_element;

  JsonVectorParser(json_object_element_s* element) : element(element) {
    array_element = json_value_as_array(element->value)->start;
  }

  T Next() {
    T result;

    for (size_t i = 0; i < size; ++i) {
      result[i] = (float)atof(json_value_as_number(array_element->value)->number) / 16.0f;
      array_element = array_element->next;
    }

    return result;
  }

  bool HasNext() {
    return array_element != nullptr && array_element->next != nullptr;
  }
};

using JsonVector2Parser = JsonVectorParser<Vector2f, 2>;
using JsonVector3Parser = JsonVectorParser<Vector3f, 3>;

inline static String GetFilenameBase(const char* filename) {
  size_t size = 0;

  while (true) {
    char c = filename[size];

    if (c == 0 || c == '.') {
      break;
    }

    ++size;
  }

  return String(filename, size);
}

inline static s32 ParseFaceName(const String& str) {
  const char* facename = str.data;
  s32 face_index = 0;

  if (str.size <= 0) return 0;

  if (str.data[0] == 'd' || str.data[0] == 'b') face_index = 0;
  if (str.data[0] == 'u' || str.data[0] == 't') face_index = 1;
  if (str.data[0] == 'n') face_index = 2;
  if (str.data[0] == 's') face_index = 3;
  if (str.data[0] == 'w') face_index = 4;
  if (str.data[0] == 'e') face_index = 5;

  return face_index;
}

String ParsedBlockModel::GetParentName(json_object_s* root) const {
  json_object_element_s* root_element = root->start;
  String result;

  // Loop through every root element until "parent" is found.
  while (root_element) {
    String element_name(root_element->name->string, root_element->name->string_size);

    if (poly_strcmp(element_name, POLY_STR("parent")) == 0) {
      json_string_s* parent_name = json_value_as_string(root_element->value);
      result = String(parent_name->string, parent_name->string_size);
      break;
    }

    root_element = root_element->next;
  }

  return result;
}

void ParsedBlockModel::ParseTextures(MemoryArena& trans_arena, json_object_s* root) {
  json_object_element_s* root_element = root->start;

  // Loop through every root element until "textures" is found.
  while (root_element) {
    String element_name(root_element->name->string, root_element->name->string_size);

    if (poly_strcmp(element_name, POLY_STR("textures")) == 0) {
      json_object_s* texture_object = json_value_as_object(root_element->value);
      json_object_element_s* texture_element = texture_object->start;

      // Go through each texture variable in the object and store it in this->texture_names
      while (texture_element) {
        String texture_variable_name(texture_element->name->string, texture_element->name->string_size);

        json_string_s* value_string = json_value_as_string(texture_element->value);
        String texture_variable_value(value_string->string, value_string->string_size);

        ParsedTextureName* texture_name_storage = memory_arena_push_type(&trans_arena, ParsedTextureName);

        assert(texture_variable_name.size < polymer_array_count(texture_name_storage->name));
        assert(texture_variable_value.size < polymer_array_count(texture_name_storage->value));

        memcpy(texture_name_storage->name, texture_variable_name.data, texture_variable_name.size);
        memcpy(texture_name_storage->value, texture_variable_value.data, texture_variable_value.size);

        texture_name_storage->name[texture_variable_name.size] = 0;
        texture_name_storage->value[texture_variable_value.size] = 0;

        texture_name_storage->next = this->texture_names;
        this->texture_names = texture_name_storage;

        texture_element = texture_element->next;
      }

      break;
    }

    root_element = root_element->next;
  }
}

void ParsedBlockModel::ParseElements(json_object_s* root) {
  json_object_element_s* root_element = root->start;

  this->element_count = 0;

  if (parent) {
    for (size_t i = 0; i < parent->element_count; ++i) {
      elements[element_count++] = parent->elements[i];

      assert(element_count < polymer_array_count(elements));
    }
  }

  json_array_element_s* element_array_element = nullptr;

  while (root_element) {
    if (strcmp(root_element->name->string, "elements") == 0) {
      json_array_s* element_array = json_value_as_array(root_element->value);

      element_array_element = element_array->start;
      break;
    }

    root_element = root_element->next;
  }

  if (!element_array_element) return;

  while (element_array_element) {
    json_object_s* element_obj = json_value_as_object(element_array_element->value);

    ParsedBlockElement* element = elements + element_count;

    element->shade = true;
    element->rotation.angle = 0;
    element->rotation.rescale = false;

    json_object_element_s* element_property = element_obj->start;
    while (element_property) {
      String property_name(element_property->name->string, element_property->name->string_size);

      if (poly_strcmp(property_name, POLY_STR("from")) == 0) {
        JsonVector3Parser parser(element_property);

        if (parser.HasNext()) {
          element->from = parser.Next();
        }
      } else if (poly_strcmp(property_name, POLY_STR("to")) == 0) {
        JsonVector3Parser parser(element_property);

        if (parser.HasNext()) {
          element->to = parser.Next();
        }
      } else if (poly_strcmp(property_name, POLY_STR("shade")) == 0) {
        element->shade = json_value_is_true(element_property->value);
      } else if (poly_strcmp(property_name, POLY_STR("rotation")) == 0) {
        json_object_element_s* rotation_obj_element = json_value_as_object(element_property->value)->start;

        while (rotation_obj_element) {
          String rotation_element_name(rotation_obj_element->name->string, rotation_obj_element->name->string_size);

          if (poly_strcmp(rotation_element_name, POLY_STR("rescale")) == 0) {
            element->rotation.rescale = json_value_is_true(rotation_obj_element->value);
          } else if (poly_strcmp(rotation_element_name, POLY_STR("origin")) == 0) {
            JsonVector3Parser parser(rotation_obj_element);
            Vector3f origin;

            if (parser.HasNext()) {
              origin = parser.Next();
            }

            element->rotation.origin = origin;
          } else if (poly_strcmp(rotation_element_name, POLY_STR("angle")) == 0) {
            element->rotation.angle = strtol(json_value_as_number(rotation_obj_element->value)->number, nullptr, 10);
          } else if (poly_strcmp(rotation_element_name, POLY_STR("axis")) == 0) {
            assert(rotation_obj_element->value->type == json_type_string);
            json_string_s* axis_str = json_value_as_string(rotation_obj_element->value);
            String axis(axis_str->string, axis_str->string_size);

            if (poly_strcmp(axis, POLY_STR("x")) == 0) {
              element->rotation.axis = Vector3f(1, 0, 0);
            } else if (poly_strcmp(axis, POLY_STR("y")) == 0) {
              element->rotation.axis = Vector3f(0, 1, 0);
            } else if (poly_strcmp(axis, POLY_STR("z")) == 0) {
              element->rotation.axis = Vector3f(0, 0, 1);
            }
          }

          rotation_obj_element = rotation_obj_element->next;
        }

      } else if (poly_strcmp(property_name, POLY_STR("faces")) == 0) {
        json_object_element_s* face_obj_element = json_value_as_object(element_property->value)->start;

        while (face_obj_element) {
          String facename(face_obj_element->name->string, face_obj_element->name->string_size);
          size_t face_index = ParseFaceName(facename);

          json_object_element_s* face_element = json_value_as_object(face_obj_element->value)->start;
          ParsedRenderableFace* face = element->faces + face_index;

          face->uv_from = Vector2f(0, 0);
          face->uv_to = Vector2f(1, 1);
          face->custom_uv = 0;
          face->render = true;
          face->tintindex = world::kHighestTintIndex;
          face->cullface = 6;
          face->render_layer = 0;
          face->rotation = 0;

          while (face_element) {
            String face_property(face_element->name->string, face_element->name->string_size);

            if (poly_strcmp(face_property, POLY_STR("texture")) == 0) {
              json_string_s* texture_str = json_value_as_string(face_element->value);
              String texture_name(texture_str->string, texture_str->string_size);

              face->texture_name_size = texture_name.size;
              memcpy(face->texture_name, texture_name.data, texture_name.size);
              face->texture_name[texture_name.size] = 0;
            } else if (poly_strcmp(face_property, POLY_STR("uv")) == 0) {
              JsonVector2Parser vec_parser(face_element);

              Vector2f uv_from, uv_to;

              if (vec_parser.HasNext()) {
                uv_from = vec_parser.Next();
              }

              if (vec_parser.HasNext()) {
                uv_to = vec_parser.Next();
              }

              face->custom_uv = 1;
              face->uv_from = uv_from;
              face->uv_to = uv_to;
            } else if (poly_strcmp(face_property, POLY_STR("tintindex")) == 0) {
              face->tintindex = (u32)strtol(json_value_as_number(face_element->value)->number, nullptr, 10);
            } else if (poly_strcmp(face_property, POLY_STR("cullface")) == 0) {
              json_string_s* texture_str = json_value_as_string(face_element->value);
              String face_str = poly_string(texture_str->string, texture_str->string_size);

              s32 face_index = ParseFaceName(face_str);

              face->cullface = face_index;
            } else if (poly_strcmp(face_property, POLY_STR("rotation")) == 0) {
              face->rotation = strtol(json_value_as_number(face_element->value)->number, nullptr, 10);
            }

            face_element = face_element->next;
          }

          face_obj_element = face_obj_element->next;
        }
      }

      element_property = element_property->next;
    }

    ++element_count;
    assert(element_count < polymer_array_count(elements));

    element_array_element = element_array_element->next;
  }
}

String ParsedBlockModel::ResolveTexture(const String& variable) {
  if (variable.size == 0 || variable.data[0] != '#') return String();

  ParsedTextureName* variable_check = texture_names;
  // Cut off the starting '#' for lookup.
  String lookup(variable.data + 1, variable.size - 1);

  String result;

  while (variable_check) {
    String variable_check_name(variable_check->name);

    if (poly_strcmp(variable_check_name, lookup) == 0) {
      if (variable_check->value[0] != '#') {
        // We found the exact one we're looking for.
        return String(variable_check->value);
      }

      String variable_value_name(variable_check->value + 1);

      // If the value is equal to the variable name then it is self referential.
      // This must be a parent node trying to resolve itself, so just return the variable name.
      if (poly_strcmp(lookup, variable_value_name) == 0) {
        return String(variable_check->value);
      }

      // The variable that we found is another variable lookup.
      result = ResolveTexture(String(variable_check->value));
      break;
    }

    variable_check = variable_check->next;
  }

  return result;
}

bool ParsedBlockModel::Parse(MemoryArena& trans_arena, const char* raw_filename, json_object_s* root) {
  if (this->parsed) return true;

  this->texture_names = nullptr;
  this->model.ambient_occlusion = 1;

  // Assign our texture_names to be equal to the parent so they can be resolved together
  if (parent) {
    this->texture_names = parent->texture_names;
    this->model.ambient_occlusion = parent->model.ambient_occlusion;
  }

  strcpy(this->filename, raw_filename);

  ParseTextures(trans_arena, root);
  ParseElements(root);

  json_object_element_s* root_element = root->start;
  while (root_element) {
    String element_name(root_element->name->string, root_element->name->string_size);

    if (poly_strcmp(element_name, POLY_STR("ambientocclusion")) == 0) {
      this->model.ambient_occlusion = json_value_is_true(root_element->value);
    }

    root_element = root_element->next;
  }

  this->parsed = true;

  return true;
}

} // namespace asset
} // namespace polymer
