/**
 * @file traffic_MODULE_base.hpp
 * @author Masaya Kataoka (masaya.kataoka@tier4.jp)
 * @brief base class for traffic module
 * @version 0.1
 * @date 2021-04-01
 *
 * @copyright Copyright(c) Tier IV.Inc {2015-2021}
 *
 */

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

#ifndef TRAFFIC_SIMULATOR__TRAFFIC__TRAFFIC_MODULE_BASE_HPP_
#define TRAFFIC_SIMULATOR__TRAFFIC__TRAFFIC_MODULE_BASE_HPP_

namespace traffic_simulator
{
namespace traffic
{
class TrafficModuleBase
{
public:
  TrafficModuleBase() {}
  virtual void execute() = 0;
};
}  // namespace traffic
}  // namespace traffic_simulator

#endif  // TRAFFIC_SIMULATOR__TRAFFIC__TRAFFIC_MODULE_BASE_HPP_
