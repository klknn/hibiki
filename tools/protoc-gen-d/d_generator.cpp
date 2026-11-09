#include "tools/protoc-gen-d/d_generator.hpp"

#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hibiki {
namespace compiler {

namespace {

using google::protobuf::Descriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::EnumValueDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::FileDescriptor;
using google::protobuf::OneofDescriptor;
using google::protobuf::io::Printer;

bool IsDKeyword(std::string_view name) {
  static const std::unordered_set<std::string> kKeywords = {
      "abstract",  "alias",        "align",
      "asm",       "assert",       "auto",
      "body",      "bool",         "byte",
      "case",      "cast",         "catch",
      "cdouble",   "cent",         "cfloat",
      "char",      "class",        "const",
      "continue",  "creal",        "dchar",
      "debug",     "default",      "delegate",
      "delete",    "deprecated",   "do",
      "double",    "else",         "enum",
      "export",    "extern",       "false",
      "final",     "finally",      "float",
      "for",       "foreach",      "foreach_reverse",
      "function",  "goto",         "idouble",
      "if",        "ifloat",       "immutable",
      "import",    "in",           "inout",
      "int",       "interface",    "invariant",
      "ireal",     "is",           "lazy",
      "long",      "macro",        "mixin",
      "module",    "new",          "nothrow",
      "null",      "out",          "override",
      "package",   "pragma",       "private",
      "protected", "public",       "pure",
      "real",      "ref",          "return",
      "scope",     "shared",       "short",
      "static",    "struct",       "super",
      "switch",    "synchronized", "template",
      "this",      "throw",        "true",
      "try",       "typeid",       "typeof",
      "ubyte",     "ucent",        "uint",
      "ulong",     "union",        "unittest",
      "ushort",    "version",      "void",
      "wchar",     "while",        "with",
      "__FILE__",  "__LINE__",     "__gshared",
      "__traits",  "__vector",     "__parameters"};
  return kKeywords.find(std::string(name)) != kKeywords.end();
}

std::string SafeDIdentifier(std::string_view name) {
  if (IsDKeyword(name)) {
    return std::string(name) + "_";
  }
  return std::string(name);
}

std::string StripProtoExtension(std::string_view filename) {
  constexpr std::string_view ext = ".proto";
  if (filename.size() >= ext.size() &&
      filename.substr(filename.size() - ext.size()) == ext) {
    return std::string(filename.substr(0, filename.size() - ext.size()));
  }
  return std::string(filename);
}

std::string FileToModule(const FileDescriptor* file) {
  std::string base = StripProtoExtension(file->name());
  for (char& c : base) {
    if (c == '/' || c == '\\') {
      c = '.';
    }
  }
  return base;
}

std::string GetPrimitiveTypeName(FieldDescriptor::Type type) {
  switch (type) {
    case FieldDescriptor::TYPE_DOUBLE:
      return "double";
    case FieldDescriptor::TYPE_FLOAT:
      return "float";
    case FieldDescriptor::TYPE_INT64:
    case FieldDescriptor::TYPE_SINT64:
    case FieldDescriptor::TYPE_SFIXED64:
      return "long";
    case FieldDescriptor::TYPE_UINT64:
    case FieldDescriptor::TYPE_FIXED64:
      return "ulong";
    case FieldDescriptor::TYPE_INT32:
    case FieldDescriptor::TYPE_SINT32:
    case FieldDescriptor::TYPE_SFIXED32:
      return "int";
    case FieldDescriptor::TYPE_FIXED32:
    case FieldDescriptor::TYPE_UINT32:
      return "uint";
    case FieldDescriptor::TYPE_BOOL:
      return "bool";
    case FieldDescriptor::TYPE_STRING:
      return "string";
    case FieldDescriptor::TYPE_BYTES:
      return "ubyte[]";
    default:
      return "";
  }
}

std::string GetFieldBaseType(const FieldDescriptor* field) {
  std::string prim = GetPrimitiveTypeName(field->type());
  if (!prim.empty()) {
    return prim;
  }

  if (field->type() == FieldDescriptor::TYPE_ENUM) {
    const EnumDescriptor* enum_desc = field->enum_type();
    if (enum_desc->file() == field->file()) {
      if (enum_desc->containing_type() != nullptr &&
          enum_desc->containing_type() != field->containing_type()) {
        return SafeDIdentifier(enum_desc->containing_type()->name()) + "." +
               SafeDIdentifier(enum_desc->name());
      }
      return SafeDIdentifier(enum_desc->name());
    }
    std::string mod = FileToModule(enum_desc->file());
    if (enum_desc->containing_type() != nullptr) {
      return mod + "." + SafeDIdentifier(enum_desc->containing_type()->name()) +
             "." + SafeDIdentifier(enum_desc->name());
    }
    return mod + "." + SafeDIdentifier(enum_desc->name());
  }

  if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
    const Descriptor* msg_desc = field->message_type();
    if (msg_desc->file() == field->file()) {
      if (msg_desc->containing_type() != nullptr &&
          msg_desc->containing_type() != field->containing_type()) {
        return SafeDIdentifier(msg_desc->containing_type()->name()) + "." +
               SafeDIdentifier(msg_desc->name());
      }
      return SafeDIdentifier(msg_desc->name());
    }
    std::string mod = FileToModule(msg_desc->file());
    if (msg_desc->containing_type() != nullptr) {
      return mod + "." + SafeDIdentifier(msg_desc->containing_type()->name()) +
             "." + SafeDIdentifier(msg_desc->name());
    }
    return mod + "." + SafeDIdentifier(msg_desc->name());
  }

  return "void*";
}

std::string GetDefaultInitializer(const FieldDescriptor* field,
                                  const std::string& base_type) {
  switch (field->type()) {
    case FieldDescriptor::TYPE_FLOAT:
      return " = 0.0f";
    case FieldDescriptor::TYPE_DOUBLE:
      return " = 0.0";
    case FieldDescriptor::TYPE_INT32:
    case FieldDescriptor::TYPE_SINT32:
    case FieldDescriptor::TYPE_SFIXED32:
      return " = 0";
    case FieldDescriptor::TYPE_INT64:
    case FieldDescriptor::TYPE_SINT64:
    case FieldDescriptor::TYPE_SFIXED64:
      return " = 0";
    case FieldDescriptor::TYPE_UINT32:
    case FieldDescriptor::TYPE_FIXED32:
      return " = 0";
    case FieldDescriptor::TYPE_UINT64:
    case FieldDescriptor::TYPE_FIXED64:
      return " = 0";
    case FieldDescriptor::TYPE_BOOL:
      return " = false";
    case FieldDescriptor::TYPE_ENUM: {
      const EnumDescriptor* ed = field->enum_type();
      if (ed->value_count() > 0) {
        return " = " + base_type + "." + SafeDIdentifier(ed->value(0)->name());
      }
      return " = cast(" + base_type + ")0";
    }
    default:
      return "";
  }
}

void GenerateEnum(const EnumDescriptor* enum_desc, Printer* printer,
                  int indent) {
  std::string ind(indent * 4, ' ');
  printer->Print(ind.c_str());
  printer->Print("enum $name$ : int {\n", "name",
                 SafeDIdentifier(enum_desc->name()));

  std::string val_ind((indent + 1) * 4, ' ');
  for (int i = 0; i < enum_desc->value_count(); ++i) {
    const EnumValueDescriptor* val = enum_desc->value(i);
    printer->Print(val_ind.c_str());
    printer->Print("$name$ = $number$,\n", "name", SafeDIdentifier(val->name()),
                   "number", std::to_string(val->number()));
  }

  printer->Print(ind.c_str());
  printer->Print("}\n\n");
}

void GenerateField(const FieldDescriptor* field, Printer* printer, int indent) {
  std::string ind(indent * 4, ' ');
  std::string field_name = SafeDIdentifier(field->name());
  std::string base_type = GetFieldBaseType(field);

  std::string proto_field_attr =
      "@ProtoField(" + std::to_string(field->number());

  const OneofDescriptor* real_oneof = field->real_containing_oneof();
  bool is_oneof = (real_oneof != nullptr);
  bool is_proto3_optional =
      (field->containing_oneof() != nullptr && real_oneof == nullptr);

  if (is_oneof) {
    proto_field_attr += ", true, \"" + std::string(real_oneof->name()) + "\")";
  } else {
    proto_field_attr += ")";
  }

  std::string d_type;
  std::string default_val = "";

  if (field->is_repeated()) {
    d_type = base_type + "[]";
  } else if (is_oneof || is_proto3_optional) {
    d_type = "ProtoOptional!(" + base_type + ")";
  } else {
    d_type = base_type;
    default_val = GetDefaultInitializer(field, base_type);
  }

  printer->Print(ind.c_str());
  printer->Print("$attr$ $type$ $name$$init$;\n", "attr", proto_field_attr,
                 "type", d_type, "name", field_name, "init", default_val);
}

void GenerateMessage(const Descriptor* message, Printer* printer, int indent) {
  std::string ind(indent * 4, ' ');
  printer->Print(ind.c_str());
  printer->Print("struct $name$ {\n", "name", SafeDIdentifier(message->name()));

  // 1. Nested Enums
  for (int i = 0; i < message->enum_type_count(); ++i) {
    GenerateEnum(message->enum_type(i), printer, indent + 1);
  }

  // 2. Nested Messages
  for (int i = 0; i < message->nested_type_count(); ++i) {
    GenerateMessage(message->nested_type(i), printer, indent + 1);
  }

  // 3. Fields
  for (int i = 0; i < message->field_count(); ++i) {
    GenerateField(message->field(i), printer, indent + 1);
  }

  printer->Print(ind.c_str());
  printer->Print("}\n\n");
}

}  // namespace

bool DGenerator::Generate(const FileDescriptor* file,
                          const std::string& /* parameter */,
                          google::protobuf::compiler::GeneratorContext* context,
                          std::string* /* error */) const {
  std::string out_filename = StripProtoExtension(file->name()) + ".d";
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> output(
      context->Open(out_filename));
  Printer printer(output.get(), '$');

  printer.Print("// Generated by protoc-gen-d. DO NOT EDIT!\n");
  printer.Print("// source: $source$\n\n", "source", std::string(file->name()));

  std::string mod_name = FileToModule(file);
  printer.Print("module $module$;\n\n", "module", mod_name);

  printer.Print("import pb.dproto : ProtoField, ProtoOptional;\n");

  // Dependencies
  for (int i = 0; i < file->dependency_count(); ++i) {
    const FileDescriptor* dep = file->dependency(i);
    std::string dep_mod = FileToModule(dep);
    printer.Print("import $dep_module$;\n", "dep_module", dep_mod);
  }
  printer.Print("\n");

  // Top-level Enums
  for (int i = 0; i < file->enum_type_count(); ++i) {
    GenerateEnum(file->enum_type(i), &printer, 0);
  }

  // Top-level Messages
  for (int i = 0; i < file->message_type_count(); ++i) {
    GenerateMessage(file->message_type(i), &printer, 0);
  }

  return true;
}

}  // namespace compiler
}  // namespace hibiki

int main(int argc, char* argv[]) {
  hibiki::compiler::DGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
