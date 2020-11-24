// Copyright 2015-2020 TierIV.inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef OPENSCENARIO_INTERPRETER__SYNTAX__OBJECT_CONTROLLER_HPP_
#define OPENSCENARIO_INTERPRETER__SYNTAX__OBJECT_CONTROLLER_HPP_

#include <openscenario_interpreter/reader/element.hpp>

#include <utility>

namespace openscenario_interpreter
{
inline namespace syntax
{
/* ==== ObjectController =====================================================
 *
 * <xsd:complexType name="ObjectController">
 *   <xsd:choice>
 *     <xsd:element name="CatalogReference" type="CatalogReference"/>
 *     <xsd:element name="Controller" type="Controller"/>
 *   </xsd:choice>
 * </xsd:complexType>
 *
 * ======================================================================== */
struct ObjectController
  : public Element
{
  template<typename Node, typename ... Ts>
  explicit ObjectController(const Node & node, Ts && ...)
  : Element(
      choice(
        node,
        std::make_pair("CatalogReference", UNSUPPORTED()),
        std::make_pair("Controller", UNSUPPORTED())))
  {}
};
}
}  // namespace openscenario_interpreter

#endif  // OPENSCENARIO_INTERPRETER__SYNTAX__OBJECT_CONTROLLER_HPP_