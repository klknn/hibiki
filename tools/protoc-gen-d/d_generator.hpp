#pragma once

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>

#include <string>

namespace hibiki {
namespace compiler {

class DGenerator : public google::protobuf::compiler::CodeGenerator {
 public:
  DGenerator() = default;
  ~DGenerator() override = default;

  bool Generate(const google::protobuf::FileDescriptor* file,
                const std::string& parameter,
                google::protobuf::compiler::GeneratorContext* context,
                std::string* error) const override;

  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL;
  }
};

}  // namespace compiler
}  // namespace hibiki
