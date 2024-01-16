#pragma once

#include "dune_geometry_set.hh"

#include "NOD_socket_declarations.hh"

namespace dune::nodes::decl {

class GeometryBuilder;

class Geometry : public SocketDeclaration {
 private:
  dune::Vector<dune::GeometryComponent::Type> supported_types_;
  bool only_realized_data_ = false;
  bool only_instances_ = false;

  friend GeometryBuilder;

 public:
  using Builder = GeometryBuilder;

  NodeSocket &build(NodeTree &ntree, Node &node) const override;
  bool matches(const NodeSocket &socket) const override;
  bool can_connect(const NodeSocket &socket) const override;

  Span<dune::GeometryComponent::Type> supported_types() const;
  bool only_realized_data() const;
  bool only_instances() const;
};

class GeometryBuilder : public SocketDeclarationBuilder<Geometry> {
 public:
  GeometryBuilder &supported_type(dune::GeometryComponent::Type supported_type);
  GeometryBuilder &supported_type(dune::Vector<bke::GeometryComponent::Type> supported_types);
  GeometryBuilder &only_realized_data(bool val = true);
  GeometryBuilder &only_instances(bool val = true);
};

}  // namespace dube::nodes::decl
