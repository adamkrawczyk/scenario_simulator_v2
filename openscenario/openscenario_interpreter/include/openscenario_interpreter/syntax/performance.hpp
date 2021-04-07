// Copyright 2015-2020 Tier IV, Inc. All rights reserved.
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

#ifndef OPENSCENARIO_INTERPRETER__SYNTAX__PERFORMANCE_HPP_
#define OPENSCENARIO_INTERPRETER__SYNTAX__PERFORMANCE_HPP_

#include <openscenario_interpreter/reader/attribute.hpp>
#include <openscenario_interpreter/reader/element.hpp>

namespace openscenario_interpreter
{
inline namespace syntax
{
/* ==== Performance ==========================================================
 *
 * <xsd:complexType name="Performance">
 *   <xsd:attribute name="maxSpeed" type="Double" use="required"/>
 *   <xsd:attribute name="maxAcceleration" type="Double" use="required"/>
 *   <xsd:attribute name="maxDeceleration" type="Double" use="required"/>
 * </xsd:complexType>
 *
 * ======================================================================== */
struct Performance
{
  const Double max_speed;
  const Double max_acceleration;
  const Double max_deceleration;

  Performance() = default;

  template <typename Node, typename Scope>
  explicit Performance(const Node & node, Scope & scope)
  : max_speed{readAttribute<Double>("maxSpeed", node, scope)},
    max_acceleration{readAttribute<Double>("maxAcceleration", node, scope)},
    max_deceleration{readAttribute<Double>("maxDeceleration", node, scope)}
  {
  }
};

template <typename... Ts>
std::basic_ostream<Ts...> & operator<<(std::basic_ostream<Ts...> & os, const Performance & rhs)
{
  return os << indent << blue << "<Performance"
            << " " << highlight("maxSpeed", rhs.max_speed) << " "
            << highlight("maxAcceleration", rhs.max_acceleration) << " "
            << highlight("maxDeceleration", rhs.max_deceleration) << blue << "/>" << reset;
}
}  // namespace syntax
}  // namespace openscenario_interpreter

#endif  // OPENSCENARIO_INTERPRETER__SYNTAX__PERFORMANCE_HPP_
