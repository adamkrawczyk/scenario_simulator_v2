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

#ifndef OPENSCENARIO_INTERPRETER__PROCEDURE_HPP_
#define OPENSCENARIO_INTERPRETER__PROCEDURE_HPP_

#include <simulation_api/api/api.hpp>
#include <openscenario_interpreter/error.hpp>

#include <limits>
#include <memory>
#include <utility>

namespace openscenario_interpreter
{
extern scenario_simulator::API & connection;

// static struct Connector
// {
//   Connector();
//   ~Connector();
// } connector;

template<typename ... Ts>
decltype(auto) connect(Ts && ... xs)
{
  new (&connection) scenario_simulator::API(std::forward<decltype(xs)>(xs)...);
  return connection;
}

template<typename ... Ts>
decltype(auto) getEntityStatus(Ts && ... xs) try {
  return connection.getEntityStatus(std::forward<decltype(xs)>(xs)...);
} catch (const simulation_api::SimulationRuntimeError & error) {
  std::stringstream ss {};
  ss << error.what() << ".\n";
  ss << "Possible causes:\n";
  ss << "  (1) The position of the corresponding entity is not specified by Teleport Action";
  throw SemanticError(ss.str());
}

template<typename ... Ts>
decltype(auto) isReachedPosition(Ts && ... xs)
{
  return connection.reachPosition(std::forward<decltype(xs)>(xs)...);
}

// template<typename ... Ts>
// decltype(auto) getRelativeDistance(Ts && ... xs)
// {
//   return connection.getRelativeDistance(std::forward<decltype(xs)>(xs)...);
// }

template<typename ... Ts>
decltype(auto) getRelativePose(Ts && ... xs) try {
  return connection.getRelativePose(std::forward<decltype(xs)>(xs)...);
} catch (const simulation_api::SimulationRuntimeError &) {
  geometry_msgs::msg::Pose result {};
  result.position.x = std::numeric_limits<double>::quiet_NaN();
  result.position.y = std::numeric_limits<double>::quiet_NaN();
  result.position.z = std::numeric_limits<double>::quiet_NaN();
  result.orientation.x = 0;
  result.orientation.y = 0;
  result.orientation.z = 0;
  result.orientation.w = 1;
  return result;
}

// template <typename... Ts>
// auto getDistanceAlongRoute(Ts&&... xs)
// {
//   if (const auto result {
//   connection->entity->getLongitudinalDistance(std::forward<decltype(xs)>(xs)...) })
//   {
//     return *result;
//   }
//   else
//   {
//     using value_type = typename std::decay<decltype(result)>::type::value_type;
//     return std::numeric_limits<value_type>::infinity();
//   }
// }

template<typename ... Ts>
decltype(auto) setController(Ts && ... xs)
{
  return connection.setDriverModel(std::forward<decltype(xs)>(xs)...);
}

#define STRIP_OPTIONAL(IDENTIFIER, ALTERNATE) \
  template<typename ... Ts> \
  auto IDENTIFIER(Ts && ... xs) \
  { \
    const auto result = connection.IDENTIFIER(std::forward<decltype(xs)>(xs)...); \
    if (result) { \
      return result.get(); \
    } else { \
      using value_type = typename std::decay<decltype(result)>::type::value_type; \
      return ALTERNATE; \
    } \
  } static_assert(true, "")

STRIP_OPTIONAL(getBoundingBoxDistance, static_cast<value_type>(0));
STRIP_OPTIONAL(getStandStillDuration, static_cast<value_type>(0));
STRIP_OPTIONAL(getTimeHeadway, std::numeric_limits<value_type>::quiet_NaN());

#undef STRIP_OPTIONAL

#define FORWARD_TO_SIMULATION_API(IDENTIFIER) \
  template<typename ... Ts> \
  decltype(auto) IDENTIFIER(Ts && ... xs) \
  { \
    return connection.IDENTIFIER(std::forward<decltype(xs)>(xs)...); \
  } static_assert(true, "")

FORWARD_TO_SIMULATION_API(attachDetectionSensor);
FORWARD_TO_SIMULATION_API(attachLidarSensor);
FORWARD_TO_SIMULATION_API(checkCollision);
FORWARD_TO_SIMULATION_API(despawn);
FORWARD_TO_SIMULATION_API(getCurrentTime);
FORWARD_TO_SIMULATION_API(initialize);
FORWARD_TO_SIMULATION_API(isInLanelet);
FORWARD_TO_SIMULATION_API(requestAcquirePosition);
FORWARD_TO_SIMULATION_API(requestLaneChange);
FORWARD_TO_SIMULATION_API(setEntityStatus);
FORWARD_TO_SIMULATION_API(setTargetSpeed);
FORWARD_TO_SIMULATION_API(spawn);
FORWARD_TO_SIMULATION_API(toMapPose);
FORWARD_TO_SIMULATION_API(updateFrame);

#undef DEFINE_FORWARD
}  // namespace openscenario_interpreter

#endif  // OPENSCENARIO_INTERPRETER__PROCEDURE_HPP_
